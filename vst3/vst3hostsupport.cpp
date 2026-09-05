#include "vst3hostsupport.h"

#include "pluginterfaces/vst/ivstmessage.h"
#include "ui/appconstants.h"

#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <string>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace CurvioVst3 {
namespace {

class HostAttributeList : public IAttributeList
{
public:
    tresult PLUGIN_API queryInterface(const TUID iid, void **obj) override
    {
        QUERY_INTERFACE(iid, obj, FUnknown::iid, IAttributeList)
        QUERY_INTERFACE(iid, obj, IAttributeList::iid, IAttributeList)
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    tresult PLUGIN_API setInt(AttrID, int64) override { return kResultFalse; }
    tresult PLUGIN_API getInt(AttrID, int64 &) override { return kResultFalse; }
    tresult PLUGIN_API setFloat(AttrID, double) override { return kResultFalse; }
    tresult PLUGIN_API getFloat(AttrID, double &) override { return kResultFalse; }
    tresult PLUGIN_API setString(AttrID, const TChar *) override { return kResultFalse; }
    tresult PLUGIN_API getString(AttrID, TChar *, uint32) override { return kResultFalse; }
    tresult PLUGIN_API setBinary(AttrID, const void *, uint32) override { return kResultFalse; }
    tresult PLUGIN_API getBinary(AttrID, const void *&, uint32 &) override { return kResultFalse; }
};

class HostMessage : public IMessage
{
public:
    tresult PLUGIN_API queryInterface(const TUID iid, void **obj) override
    {
        QUERY_INTERFACE(iid, obj, FUnknown::iid, IMessage)
        QUERY_INTERFACE(iid, obj, IMessage::iid, IMessage)
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return ++m_ref; }
    uint32 PLUGIN_API release() override
    {
        const uint32 ref = --m_ref;
        if (ref == 0) {
            delete this;
        }
        return ref;
    }
    FIDString PLUGIN_API getMessageID() override { return m_id.c_str(); }
    void PLUGIN_API setMessageID(FIDString id) override { m_id = id ? id : ""; }
    IAttributeList *PLUGIN_API getAttributes() override { return &m_attrs; }

private:
    std::atomic<uint32> m_ref{1};
    std::string m_id;
    HostAttributeList m_attrs;
};

class HostApplication : public IHostApplication
{
public:
    tresult PLUGIN_API queryInterface(const TUID iid, void **obj) override
    {
        QUERY_INTERFACE(iid, obj, FUnknown::iid, IHostApplication)
        QUERY_INTERFACE(iid, obj, IHostApplication::iid, IHostApplication)
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    tresult PLUGIN_API getName(String128 name) override
    {
        copyToString128(QString::fromLatin1(AppConstants::kAppDisplayName), name);
        return kResultOk;
    }
    tresult PLUGIN_API createInstance(TUID cid, TUID iid, void **obj) override
    {
        if (!obj) {
            return kInvalidArgument;
        }
        *obj = nullptr;
        if (FUnknownPrivate::iidEqual(cid, IMessage_iid) && FUnknownPrivate::iidEqual(iid, IMessage_iid)) {
            *obj = new HostMessage();
            return kResultOk;
        }
        return kNotImplemented;
    }
};

} // namespace

IHostApplication *hostApplication()
{
    static HostApplication host;
    return &host;
}

MemoryStream::MemoryStream(QByteArray data)
    : m_data(std::move(data))
{
}

tresult PLUGIN_API MemoryStream::queryInterface(const TUID iid, void **obj)
{
    QUERY_INTERFACE(iid, obj, FUnknown::iid, IBStream)
    QUERY_INTERFACE(iid, obj, IBStream::iid, IBStream)
    *obj = nullptr;
    return kNoInterface;
}

uint32 PLUGIN_API MemoryStream::addRef()
{
    return ++m_ref;
}

uint32 PLUGIN_API MemoryStream::release()
{
    const uint32 ref = --m_ref;
    if (ref == 0) {
        delete this;
    }
    return ref;
}

tresult PLUGIN_API MemoryStream::read(void *buffer, int32 numBytes, int32 *numBytesRead)
{
    if (!buffer || numBytes < 0) {
        return kInvalidArgument;
    }
    const int32 available = static_cast<int32>(m_data.size()) - m_pos;
    const int32 take = std::min(numBytes, std::max(0, available));
    if (take > 0) {
        std::memcpy(buffer, m_data.constData() + m_pos, static_cast<size_t>(take));
        m_pos += take;
    }
    if (numBytesRead) {
        *numBytesRead = take;
    }
    return kResultOk;
}

tresult PLUGIN_API MemoryStream::write(void *buffer, int32 numBytes, int32 *numBytesWritten)
{
    if (!buffer || numBytes < 0) {
        return kInvalidArgument;
    }
    if (m_pos + numBytes > m_data.size()) {
        m_data.resize(m_pos + numBytes);
    }
    std::memcpy(m_data.data() + m_pos, buffer, static_cast<size_t>(numBytes));
    m_pos += numBytes;
    if (numBytesWritten) {
        *numBytesWritten = numBytes;
    }
    return kResultOk;
}

tresult PLUGIN_API MemoryStream::seek(int64 pos, int32 mode, int64 *result)
{
    int64 next = m_pos;
    if (mode == kIBSeekSet) {
        next = pos;
    } else if (mode == kIBSeekCur) {
        next += pos;
    } else if (mode == kIBSeekEnd) {
        next = static_cast<int64>(m_data.size()) + pos;
    } else {
        return kInvalidArgument;
    }
    m_pos = static_cast<int32>(std::max<int64>(0, next));
    if (result) {
        *result = m_pos;
    }
    return kResultOk;
}

tresult PLUGIN_API MemoryStream::tell(int64 *pos)
{
    if (!pos) {
        return kInvalidArgument;
    }
    *pos = m_pos;
    return kResultOk;
}

tresult PLUGIN_API ComponentHandler::queryInterface(const TUID iid, void **obj)
{
    QUERY_INTERFACE(iid, obj, FUnknown::iid, IComponentHandler)
    QUERY_INTERFACE(iid, obj, IComponentHandler::iid, IComponentHandler)
    *obj = nullptr;
    return kNoInterface;
}

uint32 PLUGIN_API ComponentHandler::addRef()
{
    return 1;
}

uint32 PLUGIN_API ComponentHandler::release()
{
    return 1;
}

tresult PLUGIN_API ComponentHandler::beginEdit(ParamID)
{
    return kResultOk;
}

tresult PLUGIN_API ComponentHandler::performEdit(ParamID id, ParamValue valueNormalized)
{
    std::lock_guard<std::mutex> lock(mutex);
    edits.push_back({id, valueNormalized});
    return kResultOk;
}

tresult PLUGIN_API ComponentHandler::endEdit(ParamID)
{
    return kResultOk;
}

tresult PLUGIN_API ComponentHandler::restartComponent(int32)
{
    return kResultOk;
}

tresult PLUGIN_API ParamValueQueue::queryInterface(const TUID iid, void **obj)
{
    QUERY_INTERFACE(iid, obj, FUnknown::iid, IParamValueQueue)
    QUERY_INTERFACE(iid, obj, IParamValueQueue::iid, IParamValueQueue)
    *obj = nullptr;
    return kNoInterface;
}

uint32 PLUGIN_API ParamValueQueue::addRef()
{
    return 1;
}

uint32 PLUGIN_API ParamValueQueue::release()
{
    return 1;
}

ParamID PLUGIN_API ParamValueQueue::getParameterId()
{
    return id;
}

int32 PLUGIN_API ParamValueQueue::getPointCount()
{
    return 1;
}

tresult PLUGIN_API ParamValueQueue::getPoint(int32 index, int32 &sampleOffset, ParamValue &valueOut)
{
    if (index != 0) {
        return kResultFalse;
    }
    sampleOffset = 0;
    valueOut = value;
    return kResultOk;
}

tresult PLUGIN_API ParamValueQueue::addPoint(int32, ParamValue, int32 &)
{
    return kResultFalse;
}

tresult PLUGIN_API ParameterChanges::queryInterface(const TUID iid, void **obj)
{
    QUERY_INTERFACE(iid, obj, FUnknown::iid, IParameterChanges)
    QUERY_INTERFACE(iid, obj, IParameterChanges::iid, IParameterChanges)
    *obj = nullptr;
    return kNoInterface;
}

uint32 PLUGIN_API ParameterChanges::addRef()
{
    return 1;
}

uint32 PLUGIN_API ParameterChanges::release()
{
    return 1;
}

int32 PLUGIN_API ParameterChanges::getParameterCount()
{
    return count;
}

IParamValueQueue *PLUGIN_API ParameterChanges::getParameterData(int32 index)
{
    if (index < 0 || index >= count) {
        return nullptr;
    }
    return &queues[static_cast<size_t>(index)];
}

IParamValueQueue *PLUGIN_API ParameterChanges::addParameterData(const ParamID &, int32 &)
{
    return nullptr;
}

QString tuidToString(const TUID tuid)
{
    const FUID fuid = FUID::fromTUID(tuid);
    FUID::String text = {};
    fuid.toString(text);
    return QString::fromLatin1(text);
}

bool tuidFromString(const QString &text, TUID tuid)
{
    FUID fuid;
    const QByteArray latin = text.toLatin1();
    if (!fuid.fromString(latin.constData())) {
        return false;
    }
    fuid.toTUID(tuid);
    return true;
}

void copyToString128(const QString &text, String128 out)
{
    std::memset(out, 0, sizeof(String128));
    const auto utf16 = text.toStdU16String();
    const int n = std::min(127, static_cast<int>(utf16.size()));
    for (int i = 0; i < n; ++i) {
        out[i] = static_cast<TChar>(utf16[static_cast<size_t>(i)]);
    }
}

QString fromString128(const String128 text)
{
    return QString::fromUtf16(reinterpret_cast<const char16_t *>(text));
}

QString moduleBinaryPath(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists()) {
        return {};
    }
    if (info.isFile()) {
        return QDir::toNativeSeparators(info.absoluteFilePath());
    }
    const QDir contents(QDir(info.absoluteFilePath()).filePath(QStringLiteral("Contents/x86_64-win")));
    const QStringList names =
        contents.entryList(QStringList{QStringLiteral("*.vst3"), QStringLiteral("*.dll")}, QDir::Files);
    if (names.isEmpty()) {
        return {};
    }
    return QDir::toNativeSeparators(contents.absoluteFilePath(names.first()));
}

} // namespace CurvioVst3
