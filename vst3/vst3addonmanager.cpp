#include "vst3addonmanager.h"

#include "audio/audioengine.h"

#include <array>

namespace {
QString defaultVst3Folder()
{
    return QStringLiteral("C:/Program Files/Common Files/VST3");
}
} // namespace

Vst3AddonManager::Vst3AddonManager(AudioEngine *engine, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
{
    m_store.load();
}

void Vst3AddonManager::setExtraFolders(const QStringList &folders)
{
    if (m_extraFolders == folders) {
        return;
    }
    m_extraFolders = folders;
    m_scanDirty = true;
}

QStringList Vst3AddonManager::scanFolders() const
{
    QStringList folders;
    folders.append(defaultVst3Folder());
    for (const QString &folder : m_extraFolders) {
        if (!folder.trimmed().isEmpty() && !folders.contains(folder)) {
            folders.append(folder);
        }
    }
    return folders;
}

QVector<Vst3PluginInfo> Vst3AddonManager::installedPlugins()
{
    if (m_scanDirty) {
        m_cachedScan = Vst3Plugin::scanFolders(scanFolders());
        m_scanDirty = false;
    }
    return m_cachedScan;
}

Vst3AppAddons Vst3AddonManager::addonsForExe(const QString &exePath) const
{
    return m_store.addonsForExe(exePath);
}

std::shared_ptr<Vst3Plugin> Vst3AddonManager::plugin(unsigned long processId, int slot) const
{
    if (slot < 0 || slot >= kAudioChainAddonCount) {
        return {};
    }
    return m_live.value(processId)[static_cast<size_t>(slot)];
}

void Vst3AddonManager::persist(unsigned long processId, Vst3AppAddons addons)
{
    const auto live = m_live.value(processId);
    for (int i = 0; i < kAudioChainAddonCount; ++i) {
        if (live[static_cast<size_t>(i)] && addons.pluginSlots[static_cast<size_t>(i)].occupied) {
            addons.pluginSlots[static_cast<size_t>(i)].state = live[static_cast<size_t>(i)]->saveState();
        }
    }
    m_store.setAddonsForExe(addons);
    m_store.save();
}

void Vst3AddonManager::pushToEngine(unsigned long processId, const Vst3AppAddons &addons)
{
    if (!m_engine || processId == 0) {
        return;
    }
    auto &live = m_live[processId];
    for (int slot = 0; slot < kAudioChainAddonCount; ++slot) {
        m_engine->setSessionAddon(processId, slot, live[static_cast<size_t>(slot)]);
    }
    m_engine->setSessionAudioChainOrder(processId, addons.chain);
}

bool Vst3AddonManager::addPlugin(unsigned long processId,
                                 const QString &exePath,
                                 const Vst3PluginInfo &info,
                                 QString *errorMessage)
{
    Vst3AppAddons addons = m_store.addonsForExe(exePath);
    addons.exePath = exePath;
    const int slot = addons.firstFreeSlot();
    if (slot < 0) {
        const QString error = QStringLiteral("This app already has 4 plugins");
        if (errorMessage) {
            *errorMessage = error;
        }
        emit logMessage(QStringLiteral("WARN"), error);
        return false;
    }

    auto plugin = std::make_shared<Vst3Plugin>();
    QString loadError;
    if (!plugin->load(info.path, info.uid, &loadError)) {
        if (errorMessage) {
            *errorMessage = loadError;
        }
        emit logMessage(QStringLiteral("WARN"), loadError);
        return false;
    }

    Vst3SlotState &state = addons.pluginSlots[static_cast<size_t>(slot)];
    state.occupied = true;
    state.uid = info.uid;
    state.name = plugin->name().isEmpty() ? info.name : plugin->name();
    state.vendor = info.vendor;
    state.path = info.path;
    state.state.clear();
    addons.chain = appendAddonToChain(addons.chain, slot);

    auto &live = m_live[processId];
    live[static_cast<size_t>(slot)] = plugin;
    persist(processId, addons);
    pushToEngine(processId, addons);
    emit addonsChanged();
    emit logMessage(QStringLiteral("INFO"), QStringLiteral("Added %1").arg(state.name));
    return true;
}

void Vst3AddonManager::removePlugin(unsigned long processId, const QString &exePath, int slot)
{
    if (slot < 0 || slot >= kAudioChainAddonCount) {
        return;
    }
    Vst3AppAddons addons = m_store.addonsForExe(exePath);
    addons.exePath = exePath;
    addons.pluginSlots[static_cast<size_t>(slot)] = {};
    addons.chain = removeAddonFromChain(addons.chain, slot);
    auto &live = m_live[processId];
    if (live[static_cast<size_t>(slot)]) {
        live[static_cast<size_t>(slot)]->closeEditor();
    }
    live[static_cast<size_t>(slot)].reset();
    persist(processId, addons);
    pushToEngine(processId, addons);
    emit addonsChanged();
}

void Vst3AddonManager::setChain(unsigned long processId, const QString &exePath, const AudioChainOrder &chain)
{
    Vst3AppAddons addons = m_store.addonsForExe(exePath);
    addons.exePath = exePath;
    addons.chain = normalizeAudioChainOrder(chain);
    persist(processId, addons);
    if (m_engine && processId != 0) {
        m_engine->setSessionAudioChainOrder(processId, addons.chain);
    }
    emit addonsChanged();
}

bool Vst3AddonManager::openEditor(unsigned long processId, int slot, QWidget *parent)
{
    auto plugin = this->plugin(processId, slot);
    if (!plugin) {
        return false;
    }
    return plugin->openEditor(parent);
}

void Vst3AddonManager::attachToSession(unsigned long processId, const QString &exePath, float sampleRate)
{
    Vst3AppAddons addons = m_store.addonsForExe(exePath);
    addons.exePath = exePath;
    auto &live = m_live[processId];
    for (int slot = 0; slot < kAudioChainAddonCount; ++slot) {
        const Vst3SlotState &state = addons.pluginSlots[static_cast<size_t>(slot)];
        if (!state.occupied) {
            live[static_cast<size_t>(slot)].reset();
            continue;
        }
        if (live[static_cast<size_t>(slot)] && live[static_cast<size_t>(slot)]->uid() == state.uid) {
            live[static_cast<size_t>(slot)]->setSampleRate(sampleRate, 512);
            continue;
        }
        auto plugin = std::make_shared<Vst3Plugin>();
        QString error;
        if (!plugin->load(state.path, state.uid, &error)) {
            emit logMessage(QStringLiteral("WARN"),
                            QStringLiteral("Could not restore %1: %2").arg(state.name, error));
            live[static_cast<size_t>(slot)].reset();
            continue;
        }
        plugin->restoreState(state.state);
        plugin->setSampleRate(sampleRate, 512);
        live[static_cast<size_t>(slot)] = plugin;
    }
    pushToEngine(processId, addons);
}
