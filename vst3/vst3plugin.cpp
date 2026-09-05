#include "vst3plugin.h"

#include "vst3hostsupport.h"
#include "audio/log.h"

#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/vstspeaker.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <QCloseEvent>
#include <QDialog>
#include <QDirIterator>
#include <QShowEvent>
#include <algorithm>
#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;
using CurvioVst3::kMaxParamChanges;

namespace {
constexpr int kMaxProcessChannels = 8;
constexpr int kMaxProcessFrames = 512;
const QString kTag = QStringLiteral("Vst3");

using GetFactoryProc = IPluginFactory *(PLUGIN_API *)();

class EditorWindow : public QDialog, public IPlugFrame
{
public:
    EditorWindow(IPlugView *view, const QString &title, QWidget *parent)
        : QDialog(parent)
        , m_view(view)
    {
        setWindowFlag(Qt::Window);
        setWindowTitle(title);
    }

    tresult PLUGIN_API queryInterface(const TUID iid, void **obj) override
    {
        QUERY_INTERFACE(iid, obj, FUnknown::iid, IPlugFrame)
        QUERY_INTERFACE(iid, obj, IPlugFrame::iid, IPlugFrame)
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    tresult PLUGIN_API resizeView(IPlugView *view, ViewRect *newSize) override
    {
        if (!view || !newSize || view != m_view) {
            return kInvalidArgument;
        }
        setFixedSize(std::max(120, newSize->getWidth()), std::max(80, newSize->getHeight()));
        view->onSize(newSize);
        return kResultOk;
    }

    bool attach()
    {
        if (!m_view || m_view->isPlatformTypeSupported(kPlatformTypeHWND) != kResultTrue) {
            return false;
        }
        m_view->setFrame(this);
        ViewRect size;
        if (m_view->getSize(&size) == kResultOk) {
            setFixedSize(std::max(120, size.getWidth()), std::max(80, size.getHeight()));
        }
        winId();
        m_attached = m_view->attached(reinterpret_cast<void *>(reinterpret_cast<HWND>(winId())),
                                      kPlatformTypeHWND)
                     == kResultOk;
        return m_attached;
    }

    void detach()
    {
        if (m_view && m_attached) {
            m_view->removed();
            m_view->setFrame(nullptr);
            m_attached = false;
        }
    }

protected:
    void showEvent(QShowEvent *event) override
    {
        QDialog::showEvent(event);
        if (!m_attached) {
            attach();
        }
    }

    void closeEvent(QCloseEvent *event) override
    {
        detach();
        QDialog::closeEvent(event);
    }

private:
    IPlugView *m_view = nullptr;
    bool m_attached = false;
};

HMODULE loadModule(const QString &binaryPath)
{
    const std::wstring wide = binaryPath.toStdWString();
    HMODULE module = LoadLibraryW(wide.c_str());
    if (!module) {
        return nullptr;
    }
    if (auto initDll = reinterpret_cast<bool(PLUGIN_API *)()>(GetProcAddress(module, "InitDll"))) {
        initDll();
    }
    return module;
}

IPluginFactory *factoryFromModule(HMODULE module)
{
    auto getFactory = reinterpret_cast<GetFactoryProc>(GetProcAddress(module, "GetPluginFactory"));
    if (!getFactory) {
        return nullptr;
    }
    return getFactory();
}

void collectClasses(IPluginFactory *factory, const QString &path, QVector<Vst3PluginInfo> *out)
{
    if (!factory || !out) {
        return;
    }

    IPluginFactory2 *factory2 = nullptr;
    factory->queryInterface(IPluginFactory2::iid, reinterpret_cast<void **>(&factory2));

    const int32 count = factory->countClasses();
    for (int32 i = 0; i < count; ++i) {
        PClassInfo2 info2{};
        PClassInfo info{};
        QString name;
        QString vendor;
        QString category;
        QString sub;
        TUID cid{};

        if (factory2 && factory2->getClassInfo2(i, &info2) == kResultOk) {
            std::memcpy(cid, info2.cid, sizeof(TUID));
            name = QString::fromUtf8(info2.name);
            vendor = QString::fromUtf8(info2.vendor);
            category = QString::fromUtf8(info2.category);
            sub = QString::fromUtf8(info2.subCategories);
        } else if (factory->getClassInfo(i, &info) == kResultOk) {
            std::memcpy(cid, info.cid, sizeof(TUID));
            name = QString::fromUtf8(info.name);
            category = QString::fromUtf8(info.category);
        } else {
            continue;
        }

        if (category != QLatin1String(kVstAudioEffectClass)) {
            continue;
        }

        Vst3PluginInfo plugin;
        plugin.name = name;
        plugin.vendor = vendor;
        plugin.uid = CurvioVst3::tuidToString(cid);
        plugin.path = path;
        plugin.subCategories = sub;
        plugin.hasAudioInput = !sub.contains(QStringLiteral("Instrument"), Qt::CaseInsensitive)
                               || sub.contains(QStringLiteral("Fx"), Qt::CaseInsensitive);
        out->push_back(plugin);
    }

    if (factory2) {
        factory2->release();
    }
}

} // namespace

struct Vst3Plugin::Impl {
    HMODULE module = nullptr;
    IPluginFactory *factory = nullptr;
    IComponent *component = nullptr;
    IAudioProcessor *processor = nullptr;
    IEditController *controller = nullptr;
    IPlugView *view = nullptr;
    bool connected = false;
    bool processing = false;
    bool failed = false;
    bool hasAudioInput = true;
    float sampleRate = 48000.f;
    int maxBlock = kMaxProcessFrames;
    CurvioVst3::ComponentHandler handler;
    CurvioVst3::ParameterChanges inputChanges;
    std::array<float, kMaxProcessChannels * kMaxProcessFrames> planar{};
    std::array<float *, kMaxProcessChannels> channelPtrs{};
    AudioBusBuffers inBus{};
    AudioBusBuffers outBus{};
    EditorWindow *editor = nullptr;
    std::mutex mutex;

    void closeEditor()
    {
        if (editor) {
            editor->detach();
            editor->deleteLater();
            editor = nullptr;
        }
        if (view) {
            view->release();
            view = nullptr;
        }
    }

    void setProcessing(bool on)
    {
        if (!processor || processing == on) {
            return;
        }
        processor->setProcessing(on ? 1 : 0);
        processing = on;
    }

    void teardown()
    {
        closeEditor();
        setProcessing(false);
        if (component && connected) {
            IConnectionPoint *compPoint = nullptr;
            IConnectionPoint *ctrlPoint = nullptr;
            component->queryInterface(IConnectionPoint::iid, reinterpret_cast<void **>(&compPoint));
            if (controller) {
                controller->queryInterface(IConnectionPoint::iid, reinterpret_cast<void **>(&ctrlPoint));
            }
            if (compPoint && ctrlPoint && compPoint != ctrlPoint) {
                compPoint->disconnect(ctrlPoint);
                ctrlPoint->disconnect(compPoint);
            }
            if (compPoint) {
                compPoint->release();
            }
            if (ctrlPoint) {
                ctrlPoint->release();
            }
            connected = false;
        }
        if (component) {
            component->setActive(false);
            component->terminate();
            component->release();
            component = nullptr;
        }
        processor = nullptr;
        if (controller) {
            controller->setComponentHandler(nullptr);
            controller->terminate();
            controller->release();
            controller = nullptr;
        }
        if (factory) {
            factory->release();
            factory = nullptr;
        }
        if (module) {
            if (auto exitDll = reinterpret_cast<bool(PLUGIN_API *)()>(GetProcAddress(module, "ExitDll"))) {
                exitDll();
            }
            FreeLibrary(module);
            module = nullptr;
        }
        failed = false;
    }
};

Vst3Plugin::Vst3Plugin()
    : m_impl(std::make_unique<Impl>())
{
}

Vst3Plugin::~Vst3Plugin()
{
    unload();
}

bool Vst3Plugin::isLoaded() const
{
    return m_impl && m_impl->component && m_impl->processor;
}

void Vst3Plugin::unload()
{
    if (!m_impl) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->teardown();
    m_name.clear();
    m_vendor.clear();
    m_uid.clear();
    m_path.clear();
}

bool Vst3Plugin::load(const QString &modulePath, const QString &uid, QString *errorMessage)
{
    unload();
    const QString binary = CurvioVst3::moduleBinaryPath(modulePath);
    if (binary.isEmpty()) {
        const QString message = QStringLiteral("VST3 module not found: %1").arg(modulePath);
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    }

    TUID cid{};
    if (!CurvioVst3::tuidFromString(uid, cid)) {
        const QString message = QStringLiteral("Invalid VST3 id");
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    }

    m_impl->module = loadModule(binary);
    if (!m_impl->module) {
        const QString message = QStringLiteral("Could not load VST3: %1").arg(binary);
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    }

    m_impl->factory = factoryFromModule(m_impl->module);
    if (!m_impl->factory) {
        const QString message = QStringLiteral("No VST3 factory in %1").arg(binary);
        if (errorMessage) {
            *errorMessage = message;
        }
        m_impl->teardown();
        return false;
    }

    IComponent *component = nullptr;
    if (m_impl->factory->createInstance(cid, IComponent_iid, reinterpret_cast<void **>(&component)) != kResultOk
        || !component) {
        const QString message = QStringLiteral("Could not create VST3 component");
        if (errorMessage) {
            *errorMessage = message;
        }
        m_impl->teardown();
        return false;
    }
    m_impl->component = component;

    if (component->initialize(CurvioVst3::hostApplication()) != kResultOk) {
        const QString message = QStringLiteral("VST3 initialize failed");
        if (errorMessage) {
            *errorMessage = message;
        }
        m_impl->teardown();
        return false;
    }

    if (component->queryInterface(IAudioProcessor::iid, reinterpret_cast<void **>(&m_impl->processor)) != kResultOk
        || !m_impl->processor) {
        const QString message = QStringLiteral("VST3 has no audio processor");
        if (errorMessage) {
            *errorMessage = message;
        }
        m_impl->teardown();
        return false;
    }

    if (component->queryInterface(IEditController::iid, reinterpret_cast<void **>(&m_impl->controller)) != kResultOk) {
        m_impl->controller = nullptr;
        TUID controllerCid{};
        if (component->getControllerClassId(controllerCid) == kResultOk) {
            IEditController *controller = nullptr;
            if (m_impl->factory->createInstance(controllerCid, IEditController_iid,
                                                reinterpret_cast<void **>(&controller))
                    == kResultOk
                && controller) {
                if (controller->initialize(CurvioVst3::hostApplication()) == kResultOk) {
                    m_impl->controller = controller;
                } else {
                    controller->release();
                }
            }
        }
    }

    if (m_impl->controller) {
        m_impl->controller->setComponentHandler(&m_impl->handler);
        IConnectionPoint *compPoint = nullptr;
        IConnectionPoint *ctrlPoint = nullptr;
        component->queryInterface(IConnectionPoint::iid, reinterpret_cast<void **>(&compPoint));
        m_impl->controller->queryInterface(IConnectionPoint::iid, reinterpret_cast<void **>(&ctrlPoint));
        if (compPoint && ctrlPoint && compPoint != ctrlPoint) {
            compPoint->connect(ctrlPoint);
            ctrlPoint->connect(compPoint);
            m_impl->connected = true;
        }
        if (compPoint) {
            compPoint->release();
        }
        if (ctrlPoint) {
            ctrlPoint->release();
        }
    }

    m_impl->hasAudioInput = component->getBusCount(kAudio, kInput) > 0;
    SpeakerArrangement inArr = m_impl->hasAudioInput ? SpeakerArr::kStereo : 0;
    SpeakerArrangement outArr = SpeakerArr::kStereo;
    m_impl->processor->setBusArrangements(m_impl->hasAudioInput ? &inArr : nullptr,
                                          m_impl->hasAudioInput ? 1 : 0,
                                          &outArr,
                                          1);

    const int32 inBusses = component->getBusCount(kAudio, kInput);
    const int32 outBusses = component->getBusCount(kAudio, kOutput);
    for (int32 i = 0; i < inBusses; ++i) {
        component->activateBus(kAudio, kInput, i, i == 0 ? 1 : 0);
    }
    for (int32 i = 0; i < outBusses; ++i) {
        component->activateBus(kAudio, kOutput, i, i == 0 ? 1 : 0);
    }

    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = kMaxProcessFrames;
    setup.sampleRate = m_impl->sampleRate;
    m_impl->processor->setupProcessing(setup);
    component->setActive(true);

    m_name = uid;
    m_uid = uid;
    m_path = modulePath;
    m_hasAudioInput = m_impl->hasAudioInput;

    QVector<Vst3PluginInfo> infos = scanFile(modulePath);
    for (const Vst3PluginInfo &info : infos) {
        if (info.uid == uid) {
            m_name = info.name;
            m_vendor = info.vendor;
            m_hasAudioInput = info.hasAudioInput && m_impl->hasAudioInput;
            break;
        }
    }

    if (!m_impl->hasAudioInput) {
        AudioLog::warn(kTag, QStringLiteral("%1 has no audio input; it may stay silent").arg(m_name));
    }

    return true;
}

void Vst3Plugin::setSampleRate(float sampleRate, int maxBlockSize)
{
    if (!m_impl || !m_impl->processor || !m_impl->component) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->setProcessing(false);
    m_impl->component->setActive(false);
    m_impl->sampleRate = sampleRate > 1.f ? sampleRate : 48000.f;
    m_impl->maxBlock = std::clamp(maxBlockSize, 32, kMaxProcessFrames);
    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = m_impl->maxBlock;
    setup.sampleRate = m_impl->sampleRate;
    m_impl->processor->setupProcessing(setup);
    m_impl->component->setActive(true);
}

bool Vst3Plugin::process(float *interleaved, int frameCount, int channelCount)
{
    if (!interleaved || frameCount <= 0 || channelCount <= 0 || !m_impl) {
        return true;
    }
    std::unique_lock<std::mutex> lock(m_impl->mutex, std::try_to_lock);
    if (!lock.owns_lock() || m_impl->failed || !m_impl->processor) {
        return true;
    }

    const int frames = std::min(frameCount, kMaxProcessFrames);
    const int channels = std::clamp(channelCount, 1, kMaxProcessChannels);

    {
        std::lock_guard<std::mutex> editLock(m_impl->handler.mutex);
        m_impl->inputChanges.count = 0;
        for (const CurvioVst3::PendingEdit &edit : m_impl->handler.edits) {
            if (m_impl->inputChanges.count >= kMaxParamChanges) {
                break;
            }
            auto &queue = m_impl->inputChanges.queues[static_cast<size_t>(m_impl->inputChanges.count++)];
            queue.id = edit.id;
            queue.value = edit.value;
        }
        m_impl->handler.edits.clear();
    }

    for (int ch = 0; ch < channels; ++ch) {
        m_impl->channelPtrs[static_cast<size_t>(ch)] =
            m_impl->planar.data() + static_cast<size_t>(ch * kMaxProcessFrames);
        for (int i = 0; i < frames; ++i) {
            m_impl->channelPtrs[static_cast<size_t>(ch)][i] =
                interleaved[static_cast<size_t>(i * channelCount + std::min(ch, channelCount - 1))];
        }
    }

    m_impl->inBus.numChannels = m_impl->hasAudioInput ? channels : 0;
    m_impl->inBus.silenceFlags = 0;
    m_impl->inBus.channelBuffers32 = m_impl->channelPtrs.data();
    m_impl->outBus.numChannels = channels;
    m_impl->outBus.silenceFlags = 0;
    m_impl->outBus.channelBuffers32 = m_impl->channelPtrs.data();

    ProcessData data{};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = frames;
    data.numInputs = m_impl->hasAudioInput ? 1 : 0;
    data.numOutputs = 1;
    data.inputs = m_impl->hasAudioInput ? &m_impl->inBus : nullptr;
    data.outputs = &m_impl->outBus;
    data.inputParameterChanges = m_impl->inputChanges.count > 0 ? &m_impl->inputChanges : nullptr;

    m_impl->setProcessing(true);
    if (m_impl->processor->process(data) != kResultOk) {
        m_impl->failed = true;
        AudioLog::warn(kTag, QStringLiteral("%1 process failed; bypassing").arg(m_name));
        return false;
    }

    for (int i = 0; i < frames; ++i) {
        for (int ch = 0; ch < channelCount; ++ch) {
            const int src = std::min(ch, channels - 1);
            interleaved[static_cast<size_t>(i * channelCount + ch)] =
                m_impl->channelPtrs[static_cast<size_t>(src)][i];
        }
    }
    return true;
}

QByteArray Vst3Plugin::saveState() const
{
    if (!m_impl || !m_impl->component) {
        return {};
    }
    auto *stream = new CurvioVst3::MemoryStream();
    m_impl->component->getState(stream);
    if (m_impl->controller) {
        m_impl->controller->getState(stream);
    }
    QByteArray data = stream->data();
    stream->release();
    return data;
}

bool Vst3Plugin::restoreState(const QByteArray &state)
{
    if (state.isEmpty() || !m_impl || !m_impl->component) {
        return false;
    }
    auto *stream = new CurvioVst3::MemoryStream(state);
    m_impl->component->setState(stream);
    stream->seek(0, IBStream::kIBSeekSet, nullptr);
    if (m_impl->controller) {
        m_impl->controller->setState(stream);
    }
    stream->release();
    return true;
}

bool Vst3Plugin::openEditor(QWidget *parent)
{
    if (!m_impl || !m_impl->controller) {
        return false;
    }
    if (m_impl->editor) {
        m_impl->editor->show();
        m_impl->editor->raise();
        m_impl->editor->activateWindow();
        return true;
    }
    m_impl->view = m_impl->controller->createView(ViewType::kEditor);
    if (!m_impl->view) {
        return false;
    }
    m_impl->editor = new EditorWindow(m_impl->view, m_name, parent);
    m_impl->editor->show();
    return true;
}

void Vst3Plugin::closeEditor()
{
    if (m_impl) {
        m_impl->closeEditor();
    }
}

bool Vst3Plugin::editorOpen() const
{
    return m_impl && m_impl->editor && m_impl->editor->isVisible();
}

QVector<Vst3PluginInfo> Vst3Plugin::scanFile(const QString &path)
{
    QVector<Vst3PluginInfo> result;
    const QString binary = CurvioVst3::moduleBinaryPath(path);
    if (binary.isEmpty()) {
        return result;
    }
    HMODULE module = loadModule(binary);
    if (!module) {
        return result;
    }
    IPluginFactory *factory = factoryFromModule(module);
    if (factory) {
        collectClasses(factory, QFileInfo(path).absoluteFilePath(), &result);
        factory->release();
    }
    if (auto exitDll = reinterpret_cast<bool(PLUGIN_API *)()>(GetProcAddress(module, "ExitDll"))) {
        exitDll();
    }
    FreeLibrary(module);
    return result;
}

QVector<Vst3PluginInfo> Vst3Plugin::scanFolders(const QStringList &folders)
{
    QVector<Vst3PluginInfo> result;
    QStringList seen;
    for (const QString &folder : folders) {
        QDirIterator it(folder, QStringList{QStringLiteral("*.vst3")}, QDir::Dirs | QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            const QFileInfo info(path);
            if (info.suffix().compare(QLatin1String("vst3"), Qt::CaseInsensitive) != 0) {
                continue;
            }
            if (info.isFile() && info.dir().dirName().compare(QLatin1String("x86_64-win"), Qt::CaseInsensitive) == 0) {
                continue;
            }
            const QString key = QDir::toNativeSeparators(info.absoluteFilePath()).toLower();
            if (seen.contains(key)) {
                continue;
            }
            seen.append(key);
            result += scanFile(info.absoluteFilePath());
        }
    }
    return result;
}
