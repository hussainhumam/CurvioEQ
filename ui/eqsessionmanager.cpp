#include "eqsessionmanager.h"

#include "audio/audioengine.h"
#include "audio/log.h"
#include "audio/processloopbackcapture.h"
#include "ui/appconstants.h"
#include "ui/audiodeviceresolver.h"
#include "ui/eqcolorpalette.h"

#include <algorithm>

EqSessionManager::EqSessionManager(AudioEngine *engine, SettingsStore *store, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_store(store)
    , m_gainDebounceTimer(new QTimer(this))
    , m_routingWatchdogTimer(new QTimer(this))
{
    m_gainDebounceTimer->setSingleShot(true);
    m_gainDebounceTimer->setInterval(AppConstants::kGainUpdateDebounceMs);
    connect(m_gainDebounceTimer, &QTimer::timeout, this, [this]() {
        if (m_pendingGainPid != 0) {
            pushLiveGainsForProcess(m_pendingGainPid);
            m_pendingGainPid = 0;
        }
    });

    m_routingWatchdogTimer->setInterval(2500);
    connect(m_routingWatchdogTimer, &QTimer::timeout, this, [this]() {
        if (m_engine && m_engine->isRunning()) {
            m_engine->maintainActiveSessionRouting();
        }
    });
    m_routingWatchdogTimer->start();
}

void EqSessionManager::setEqStateReader(std::function<EqState()> reader)
{
    m_eqStateReader = std::move(reader);
}

void EqSessionManager::setSurroundStateReader(std::function<VirtualSurroundSettings()> reader)
{
    m_surroundStateReader = std::move(reader);
}

void EqSessionManager::setDynamicsStateReader(std::function<DynamicRangeSettings()> reader)
{
    m_dynamicsStateReader = std::move(reader);
}

void EqSessionManager::setAudioChainOrderReader(std::function<AudioChainOrder()> reader)
{
    m_audioChainOrderReader = std::move(reader);
}

void EqSessionManager::setDisplayNameProvider(std::function<QString(unsigned long)> provider)
{
    m_displayNameProvider = std::move(provider);
}

bool EqSessionManager::isAnyRunning() const
{
    return m_engine && m_engine->isRunning();
}

bool EqSessionManager::isRunning(unsigned long processId) const
{
    return m_engine && m_engine->isSessionActive(processId);
}

QHash<unsigned long, QColor> EqSessionManager::activeSessionColors() const
{
    QHash<unsigned long, QColor> colors;
    for (auto it = m_snapshots.constBegin(); it != m_snapshots.constEnd(); ++it) {
        if (it.value().active) {
            colors.insert(it.key(), it.value().labelColor);
        }
    }
    return colors;
}

QVector<unsigned long> EqSessionManager::activeProcessIdsForLabelColor(const QColor &labelColor) const
{
    QVector<unsigned long> processIds;
    if (!labelColor.isValid()) {
        return processIds;
    }

    for (auto it = m_snapshots.constBegin(); it != m_snapshots.constEnd(); ++it) {
        if (it.value().active && it.value().labelColor == labelColor) {
            processIds.push_back(it.key());
        }
    }
    return processIds;
}

QVector<ConfiguredEqSession> EqSessionManager::configuredTraySessions() const
{
    QVector<ConfiguredEqSession> sessions;

    for (auto it = m_snapshots.constBegin(); it != m_snapshots.constEnd(); ++it) {
        const EqSessionSnapshot &snapshot = it.value();
        const unsigned long processId = it.key();

        if (!snapshot.hasStoredGains || !snapshot.labelColor.isValid()) {
            continue;
        }

        if (!ProcessLoopbackCapture::isProcessRunning(processId)) {
            continue;
        }

        ConfiguredEqSession entry;
        entry.processId = processId;
        entry.active = isRunning(processId);
        entry.labelColor = snapshot.labelColor;
        entry.displayName = m_displayNameProvider ? m_displayNameProvider(processId) : QString();
        if (entry.displayName.isEmpty()) {
            entry.displayName = QStringLiteral("App (PID %1)").arg(processId);
        }
        sessions.push_back(entry);
    }

    std::sort(sessions.begin(), sessions.end(), [](const ConfiguredEqSession &left, const ConfiguredEqSession &right) {
        return QString::localeAwareCompare(left.displayName, right.displayName) < 0;
    });

    return sessions;
}

bool EqSessionManager::enableForProcess(unsigned long processId)
{
    if (!m_engine || processId == 0) {
        return false;
    }

    if (m_engine->isSessionActive(processId)) {
        return true;
    }

    const QColor labelColor = allocateLabelColor(processId);
    if (!labelColor.isValid()) {
        emit errorOccurred(QStringLiteral("Enable EQ"),
                           QStringLiteral("All label colors are in use. Disable EQ on another app first."));
        return false;
    }

    EqSessionSnapshot snapshot = m_snapshots.value(processId);
    if (!snapshot.hasStoredGains && m_eqStateReader) {
        snapshot.eq = m_eqStateReader();
    }
    if (m_surroundStateReader) {
        snapshot.virtualSurround = m_surroundStateReader();
    }
    if (m_dynamicsStateReader) {
        snapshot.dynamicRange = m_dynamicsStateReader();
    }
    if (m_audioChainOrderReader) {
        snapshot.audioChainOrder = normalizeAudioChainOrder(m_audioChainOrderReader());
    }

    QString sinkDeviceId;
    QString sinkDeviceName;
    QString outputDeviceId;
    QString outputDeviceName;
    QString errorTitle;
    QString errorMessage;
    if (!resolveDevices(&sinkDeviceId,
                        &sinkDeviceName,
                        &outputDeviceId,
                        &outputDeviceName,
                        &errorTitle,
                        &errorMessage)) {
        emit logMessage(QStringLiteral("WARN"), errorMessage);
        emit errorOccurred(errorTitle, errorMessage);
        if (errorTitle == QStringLiteral("Routing sink") || errorTitle == QStringLiteral("EQ output device")) {
            emit settingsRequested();
        }
        return false;
    }

    const bool muteRoutingSink = m_store ? m_store->settings().muteRoutingSink : true;

    if (!m_engine->startSession(processId,
                                snapshot.eq,
                                snapshot.virtualSurround,
                                snapshot.dynamicRange,
                                snapshot.audioChainOrder,
                                outputDeviceId,
                                sinkDeviceId,
                                muteRoutingSink,
                                &errorMessage)) {
        emit errorOccurred(QStringLiteral("EQ failed to start"), errorMessage);
        return false;
    }

    snapshot.processId = processId;
    snapshot.labelColor = labelColor;
    snapshot.active = true;
    snapshot.hasStoredGains = true;
    m_snapshots.insert(processId, snapshot);

    const QString appName = m_displayNameProvider ? m_displayNameProvider(processId) : QString();
    QString logLine = QStringLiteral("EQ active for %1 (PID %2)")
                          .arg(appName.isEmpty() ? QStringLiteral("app") : appName)
                          .arg(processId);
    if (muteRoutingSink) {
        logLine += QStringLiteral("; original audio muted on routing sink");
    }
    emit logMessage(QStringLiteral("INFO"), logLine);
    emit eqStateChanged();
    emit controlStateChanged();
    return true;
}

void EqSessionManager::disableForProcess(unsigned long processId)
{
    if (!m_engine || processId == 0) {
        return;
    }

    if (m_pendingGainPid == processId) {
        m_gainDebounceTimer->stop();
        m_pendingGainPid = 0;
    }

    if (m_eqStateReader) {
        saveDraftForProcess(processId, m_eqStateReader(),
                            m_surroundStateReader ? m_surroundStateReader() : VirtualSurroundSettings{},
                            m_dynamicsStateReader ? m_dynamicsStateReader() : DynamicRangeSettings{},
                            m_audioChainOrderReader ? m_audioChainOrderReader() : defaultAudioChainOrder());
    }

    m_engine->stopSession(processId);

    EqSessionSnapshot snapshot = m_snapshots.value(processId);
    snapshot.active = false;
    m_snapshots.insert(processId, snapshot);

    emit logMessage(QStringLiteral("INFO"), QStringLiteral("EQ disabled for PID %1").arg(processId));
    emit eqStateChanged();
    emit controlStateChanged();
}

void EqSessionManager::disableAll()
{
    if (!m_engine) {
        return;
    }

    m_engine->stop();
    for (auto it = m_snapshots.begin(); it != m_snapshots.end(); ++it) {
        it.value().active = false;
    }

    emit logMessage(QStringLiteral("INFO"), QStringLiteral("All EQ sessions disabled"));
    emit eqStateChanged();
    emit controlStateChanged();
}

bool EqSessionManager::canRestoreProcess(unsigned long processId) const
{
    if (processId == 0 || isRunning(processId)) {
        return false;
    }

    const EqSessionSnapshot snapshot = m_snapshots.value(processId);
    return snapshot.hasStoredGains && snapshot.labelColor.isValid();
}

bool EqSessionManager::restoreForProcess(unsigned long processId)
{
    const EqSessionSnapshot snapshot = m_snapshots.value(processId);
    if (!canRestoreProcess(processId)) {
        return false;
    }

    return enableForProcess(processId);
}

void EqSessionManager::saveDraftForProcess(unsigned long processId,
                                           const EqState &eqState,
                                           const VirtualSurroundSettings &virtualSurround,
                                           const DynamicRangeSettings &dynamicRange,
                                           const AudioChainOrder &audioChainOrder)
{
    if (processId == 0) {
        return;
    }

    EqSessionSnapshot snapshot = m_snapshots.value(processId);
    snapshot.processId = processId;
    snapshot.eq = eqState;
    snapshot.virtualSurround = virtualSurround;
    snapshot.dynamicRange = dynamicRange;
    snapshot.audioChainOrder = normalizeAudioChainOrder(audioChainOrder);
    snapshot.hasStoredGains = true;
    m_snapshots.insert(processId, snapshot);

    if (snapshot.active) {
        m_engine->setSessionEqState(processId, eqState);
        m_engine->setSessionVirtualSurround(processId, virtualSurround);
        m_engine->setSessionDynamicRange(processId, dynamicRange);
        m_engine->setSessionAudioChainOrder(processId, snapshot.audioChainOrder);
    }
}

void EqSessionManager::applySnapshotToUi(unsigned long processId,
                                           const std::function<void(const EqState &)> &applyEq,
                                           const std::function<void(const VirtualSurroundSettings &)> &applySurround,
                                           const std::function<void(const DynamicRangeSettings &)> &applyDynamics,
                                           const std::function<void(const AudioChainOrder &)> &applyAudioChain) const
{
    const EqSessionSnapshot snapshot = m_snapshots.value(processId);
    if (snapshot.hasStoredGains || snapshot.active) {
        applyEq(snapshot.eq);
        applySurround(snapshot.virtualSurround);
        applyDynamics(snapshot.dynamicRange);
        applyAudioChain(normalizeAudioChainOrder(snapshot.audioChainOrder));
        return;
    }

    applyEq(EqState{});
    applySurround(VirtualSurroundSettings{});
    applyDynamics(DynamicRangeSettings{});
    applyAudioChain(m_store ? normalizeAudioChainOrder(m_store->settings().audioChainOrder)
                            : defaultAudioChainOrder());
}

void EqSessionManager::pushLiveGainsForProcess(unsigned long processId)
{
    if (!m_eqStateReader || processId == 0) {
        return;
    }
    if (m_pendingGainPid == processId) {
        m_gainDebounceTimer->stop();
        m_pendingGainPid = 0;
    }

    const EqState eqState = m_eqStateReader();
    const VirtualSurroundSettings virtualSurround =
        m_surroundStateReader ? m_surroundStateReader() : VirtualSurroundSettings{};
    const DynamicRangeSettings dynamicRange =
        m_dynamicsStateReader ? m_dynamicsStateReader() : DynamicRangeSettings{};
    const AudioChainOrder audioChainOrder =
        m_audioChainOrderReader ? m_audioChainOrderReader() : defaultAudioChainOrder();
    for (unsigned long pid : linkedProcessIds(processId)) {
        saveDraftForProcess(pid, eqState, virtualSurround, dynamicRange, audioChainOrder);
    }
}

void EqSessionManager::scheduleLiveGainsForProcess(unsigned long processId)
{
    if (!m_eqStateReader || processId == 0) {
        return;
    }
    m_pendingGainPid = processId;
    m_gainDebounceTimer->start();
}

void EqSessionManager::pushLiveSurroundForProcess(unsigned long processId)
{
    if (!m_surroundStateReader || processId == 0) {
        return;
    }
    if (!m_eqStateReader) {
        return;
    }

    const EqState eqState = m_eqStateReader();
    const VirtualSurroundSettings virtualSurround = m_surroundStateReader();
    const DynamicRangeSettings dynamicRange =
        m_dynamicsStateReader ? m_dynamicsStateReader() : DynamicRangeSettings{};
    const AudioChainOrder audioChainOrder =
        m_audioChainOrderReader ? m_audioChainOrderReader() : defaultAudioChainOrder();
    for (unsigned long pid : linkedProcessIds(processId)) {
        saveDraftForProcess(pid, eqState, virtualSurround, dynamicRange, audioChainOrder);
    }
}

void EqSessionManager::pushLiveDynamicsForProcess(unsigned long processId)
{
    if (!m_dynamicsStateReader || processId == 0) {
        return;
    }
    if (!m_eqStateReader) {
        return;
    }

    const EqState eqState = m_eqStateReader();
    const VirtualSurroundSettings virtualSurround =
        m_surroundStateReader ? m_surroundStateReader() : VirtualSurroundSettings{};
    const DynamicRangeSettings dynamicRange = m_dynamicsStateReader();
    const AudioChainOrder audioChainOrder =
        m_audioChainOrderReader ? m_audioChainOrderReader() : defaultAudioChainOrder();
    for (unsigned long pid : linkedProcessIds(processId)) {
        saveDraftForProcess(pid, eqState, virtualSurround, dynamicRange, audioChainOrder);
    }
}

void EqSessionManager::pushLiveAudioChainForProcess(unsigned long processId)
{
    if (!m_audioChainOrderReader || processId == 0) {
        return;
    }
    if (!m_eqStateReader) {
        return;
    }

    const EqState eqState = m_eqStateReader();
    const VirtualSurroundSettings virtualSurround =
        m_surroundStateReader ? m_surroundStateReader() : VirtualSurroundSettings{};
    const DynamicRangeSettings dynamicRange =
        m_dynamicsStateReader ? m_dynamicsStateReader() : DynamicRangeSettings{};
    const AudioChainOrder audioChainOrder = m_audioChainOrderReader();
    for (unsigned long pid : linkedProcessIds(processId)) {
        saveDraftForProcess(pid, eqState, virtualSurround, dynamicRange, audioChainOrder);
    }
}

QVector<unsigned long> EqSessionManager::linkedProcessIds(unsigned long processId) const
{
    QVector<unsigned long> processIds;
    if (processId == 0) {
        return processIds;
    }

    const QColor labelColor = m_snapshots.value(processId).labelColor;
    if (labelColor.isValid()) {
        processIds = activeProcessIdsForLabelColor(labelColor);
    }
    if (!processIds.contains(processId)) {
        processIds.push_back(processId);
    }
    return processIds;
}

void EqSessionManager::onSessionStopped(unsigned long processId)
{
    if (processId == 0) {
        return;
    }

    EqSessionSnapshot snapshot = m_snapshots.value(processId);
    snapshot.active = false;
    if (!ProcessLoopbackCapture::isProcessRunning(processId)) {
        snapshot.labelColor = QColor();
    }
    m_snapshots.insert(processId, snapshot);
    emit eqStateChanged();
    emit controlStateChanged();
}

QColor EqSessionManager::allocateLabelColor(unsigned long processId) const
{
    auto colorInUse = [this](const QColor &color) {
        if (!color.isValid()) {
            return false;
        }
        for (auto it = m_snapshots.constBegin(); it != m_snapshots.constEnd(); ++it) {
            if (it.value().active && it.value().labelColor == color) {
                return true;
            }
        }
        return false;
    };

    const EqSessionSnapshot snapshot = m_snapshots.value(processId);
    if (snapshot.labelColor.isValid() && !colorInUse(snapshot.labelColor)) {
        return snapshot.labelColor;
    }

    for (int i = 0; i < EqColorPalette::kPresetColorCount; ++i) {
        const QColor color = EqColorPalette::presetColorAt(i);
        if (!colorInUse(color)) {
            return color;
        }
    }

    return QColor();
}

bool EqSessionManager::resolveDevices(QString *sinkId,
                                      QString *sinkName,
                                      QString *outputId,
                                      QString *outputName,
                                      QString *errorTitle,
                                      QString *errorMessage)
{
    if (!m_store) {
        return false;
    }

    const AppSettings settings = m_store->settings();

    const ResolvedDevice output = AudioDeviceResolver::resolveEqOutput(settings);
    if (!output.ok) {
        if (errorTitle) {
            *errorTitle = QStringLiteral("EQ output device");
        }
        if (errorMessage) {
            *errorMessage = QStringLiteral("Choose an EQ output device in Settings.");
        }
        return false;
    }

    const ResolvedDevice sink = AudioDeviceResolver::resolveRoutingSink(settings);
    if (!sink.ok) {
        if (errorTitle) {
            *errorTitle = QStringLiteral("Routing sink");
        }
        if (errorMessage) {
            *errorMessage = QStringLiteral("Choose a routing sink in Settings (for example VB-Cable).");
        }
        return false;
    }

    if (sink.id == output.id) {
        if (errorTitle) {
            *errorTitle = QStringLiteral("Audio devices");
        }
        if (errorMessage) {
            *errorMessage = QStringLiteral("Routing sink and EQ output must be different devices.");
        }
        return false;
    }

    if (sinkId) {
        *sinkId = sink.id;
    }
    if (sinkName) {
        *sinkName = sink.name;
    }
    if (outputId) {
        *outputId = output.id;
    }
    if (outputName) {
        *outputName = output.name;
    }
    return true;
}
