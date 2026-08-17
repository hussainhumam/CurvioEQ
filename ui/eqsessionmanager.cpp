#include "eqsessionmanager.h"

#include "audio/audioengine.h"
#include "audio/log.h"
#include "audio/processloopbackcapture.h"
#include "ui/appconstants.h"
#include "ui/audiodeviceresolver.h"

#include <algorithm>

EqSessionManager::EqSessionManager(AudioEngine *engine, SettingsStore *store, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_store(store)
    , m_gainDebounceTimer(new QTimer(this))
{
    m_gainDebounceTimer->setSingleShot(true);
    m_gainDebounceTimer->setInterval(AppConstants::kGainUpdateDebounceMs);
    connect(m_gainDebounceTimer, &QTimer::timeout, this, [this]() {
        if (m_pendingGainPid != 0) {
            pushLiveGainsForProcess(m_pendingGainPid);
            m_pendingGainPid = 0;
        }
    });
}

void EqSessionManager::setGainReader(std::function<std::array<float, EqProcessor::kBandCount>()> reader)
{
    m_gainReader = std::move(reader);
}

void EqSessionManager::setSurroundStateReader(
    std::function<std::pair<bool, std::array<int, SurroundProcessor::kChannelCount>>()> reader)
{
    m_surroundStateReader = std::move(reader);
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

QVector<unsigned long> EqSessionManager::activeProcessIds() const
{
    return m_engine ? m_engine->activeProcessIds() : QVector<unsigned long>{};
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

const EqSessionSnapshot *EqSessionManager::findSnapshot(unsigned long processId) const
{
    const auto it = m_snapshots.constFind(processId);
    return it == m_snapshots.constEnd() ? nullptr : &it.value();
}

EqSessionSnapshot EqSessionManager::snapshotFor(unsigned long processId) const
{
    return m_snapshots.value(processId);
}

bool EqSessionManager::enableForProcess(unsigned long processId, const QColor &labelColor)
{
    if (!m_engine || processId == 0 || !labelColor.isValid()) {
        return false;
    }

    if (m_engine->isSessionActive(processId)) {
        return true;
    }

    EqSessionSnapshot snapshot = m_snapshots.value(processId);
    if (!snapshot.hasStoredGains && m_gainReader) {
        snapshot.gains = m_gainReader();
    }
    if (m_surroundStateReader) {
        const auto surroundState = m_surroundStateReader();
        snapshot.surroundEnabled = surroundState.first;
        snapshot.surroundChannelLevels = surroundState.second;
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

    if (!m_engine->startSession(processId,
                                snapshot.gains,
                                snapshot.surroundEnabled,
                                snapshot.surroundChannelLevels,
                                outputDeviceId,
                                outputDeviceName,
                                sinkDeviceId,
                                &errorMessage)) {
        emit errorOccurred(QStringLiteral("EQ failed to start"), errorMessage);
        return false;
    }

    snapshot.processId = processId;
    snapshot.eqOutputDeviceId = outputDeviceId;
    snapshot.eqOutputDeviceName = outputDeviceName;
    snapshot.sinkDeviceId = sinkDeviceId;
    snapshot.sinkDeviceName = sinkDeviceName;
    snapshot.labelColor = labelColor;
    snapshot.active = true;
    snapshot.hasStoredGains = true;
    m_snapshots.insert(processId, snapshot);

    const QString appName = m_displayNameProvider ? m_displayNameProvider(processId) : QString();
    emit logMessage(QStringLiteral("INFO"),
                    QStringLiteral("EQ active for %1 (PID %2)")
                        .arg(appName.isEmpty() ? QStringLiteral("app") : appName)
                        .arg(processId));
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

    if (m_gainReader) {
        saveDraftForProcess(processId, m_gainReader(), m_surroundStateReader ? m_surroundStateReader() : std::make_pair(false, std::array<int, SurroundProcessor::kChannelCount>{}));
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

    return enableForProcess(processId, snapshot.labelColor);
}

void EqSessionManager::saveDraftForProcess(unsigned long processId,
                                           const std::array<float, EqProcessor::kBandCount> &gains,
                                           const std::pair<bool, std::array<int, SurroundProcessor::kChannelCount>> &surround)
{
    if (processId == 0) {
        return;
    }

    EqSessionSnapshot snapshot = m_snapshots.value(processId);
    snapshot.processId = processId;
    snapshot.gains = gains;
    snapshot.surroundEnabled = surround.first;
    snapshot.surroundChannelLevels = surround.second;
    snapshot.hasStoredGains = true;
    m_snapshots.insert(processId, snapshot);

    if (snapshot.active) {
        m_engine->setSessionGains(processId, gains);
        m_engine->setSessionSurroundEnabled(processId, surround.first);
        m_engine->setSessionSurroundChannelLevels(processId, surround.second);
    }
}

void EqSessionManager::applySnapshotToUi(unsigned long processId,
                                         const std::function<void(const std::array<float, EqProcessor::kBandCount> &)> &applyGains,
                                         const std::function<void(bool, const std::array<int, SurroundProcessor::kChannelCount> &)> &applySurround) const
{
    const EqSessionSnapshot snapshot = m_snapshots.value(processId);
    if (snapshot.hasStoredGains || snapshot.active) {
        applyGains(snapshot.gains);
        applySurround(snapshot.surroundEnabled, snapshot.surroundChannelLevels);
        return;
    }

    applyGains({});
    applySurround(false, {50, 50, 50, 50, 50, 50, 50, 50});
}

void EqSessionManager::pushLiveGainsForProcess(unsigned long processId)
{
    if (!m_gainReader || processId == 0) {
        return;
    }
    if (m_pendingGainPid == processId) {
        m_gainDebounceTimer->stop();
        m_pendingGainPid = 0;
    }

    const auto gains = m_gainReader();
    const auto surround = m_surroundStateReader
                              ? m_surroundStateReader()
                              : std::make_pair(false, std::array<int, SurroundProcessor::kChannelCount>{});
    for (unsigned long pid : linkedProcessIds(processId)) {
        saveDraftForProcess(pid, gains, surround);
    }
}

void EqSessionManager::scheduleLiveGainsForProcess(unsigned long processId)
{
    if (!m_gainReader || processId == 0) {
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
    if (!m_gainReader) {
        return;
    }

    const auto gains = m_gainReader();
    const auto surround = m_surroundStateReader();
    for (unsigned long pid : linkedProcessIds(processId)) {
        saveDraftForProcess(pid, gains, surround);
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
    const ResolvedDevice sink = AudioDeviceResolver::resolveRoutingSink(settings);
    if (!sink.ok) {
        if (errorTitle) {
            *errorTitle = QStringLiteral("Routing sink");
        }
        if (errorMessage) {
            *errorMessage = QStringLiteral("Choose a routing sink in Settings before enabling EQ.");
        }
        return false;
    }

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
