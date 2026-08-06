#include "eqsessioncontroller.h"

#include "audio/audioengine.h"
#include "audio/log.h"
#include "ui/audiodeviceresolver.h"

EqSessionController::EqSessionController(AudioEngine *engine, SettingsStore *store, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_store(store)
{
}

void EqSessionController::setGainReader(std::function<std::array<float, EqProcessor::kBandCount>()> reader)
{
    m_gainReader = std::move(reader);
}

void EqSessionController::setSurroundStateReader(
    std::function<std::pair<bool, std::array<int, SurroundProcessor::kChannelCount>>()> reader)
{
    m_surroundStateReader = std::move(reader);
}

void EqSessionController::setSelectedProcessIdProvider(std::function<unsigned long()> provider)
{
    m_selectedProcessIdProvider = std::move(provider);
}

void EqSessionController::setDisplayNameProvider(std::function<QString(unsigned long)> provider)
{
    m_displayNameProvider = std::move(provider);
}

bool EqSessionController::isRunning() const
{
    return m_engine && m_engine->isRunning();
}

unsigned long EqSessionController::activePid() const
{
    return m_activePid;
}

const EqSessionSnapshot &EqSessionController::snapshot() const
{
    return m_snapshot;
}

bool EqSessionController::enableEq()
{
    if (!m_engine || m_engine->isRunning()) {
        return false;
    }

    if (m_snapshot.valid) {
        return resumeFromSnapshot();
    }

    return startForSelectedApp();
}

void EqSessionController::disableEq()
{
    if (!m_engine || !m_engine->isRunning()) {
        return;
    }

    saveSnapshot();
    m_engine->stop();
    m_activePid = 0;

    emit logMessage(QStringLiteral("INFO"), QStringLiteral("EQ disabled"));
    emit eqStateChanged(false, 0, {});
    emit controlStateChanged();
}

void EqSessionController::invalidateSnapshot()
{
    m_snapshot = {};
    emit controlStateChanged();
}

void EqSessionController::resetBandGains()
{
    if (m_engine && m_engine->isRunning()) {
        m_engine->setGains({});
    }
    invalidateSnapshot();
}

void EqSessionController::notifyEngineStopped()
{
    m_activePid = 0;
    invalidateSnapshot();
    emit controlStateChanged();
}

void EqSessionController::applySurroundFromReader()
{
    if (!m_engine || !m_surroundStateReader) {
        return;
    }

    const auto surroundState = m_surroundStateReader();
    m_engine->setSurroundEnabled(surroundState.first);
    m_engine->setSurroundChannelLevels(surroundState.second);
}

bool EqSessionController::resumeFromSnapshot()
{
    applySurroundFromReader();

    QString errorMessage;
    if (!m_engine->start(m_snapshot.processId,
                         m_snapshot.gains,
                         m_snapshot.eqOutputDeviceId,
                         m_snapshot.eqOutputDeviceName,
                         m_snapshot.sinkDeviceId,
                         m_snapshot.sinkDeviceName,
                         &errorMessage)) {
        emit errorOccurred(QStringLiteral("EQ failed to resume"), errorMessage);
        return false;
    }

    m_activePid = m_snapshot.processId;
    const QString appName = m_displayNameProvider ? m_displayNameProvider(m_activePid) : QString();
    emit logMessage(QStringLiteral("INFO"), QStringLiteral("EQ resumed for PID %1").arg(m_activePid));
    emit eqStateChanged(true, m_activePid, appName);
    emit controlStateChanged();
    return true;
}

bool EqSessionController::startForSelectedApp()
{
    if (!m_gainReader || !m_selectedProcessIdProvider) {
        return false;
    }

    const unsigned long processId = m_selectedProcessIdProvider();
    if (processId == 0) {
        const QString message = QStringLiteral("Select a running app first");
        emit logMessage(QStringLiteral("WARN"), message);
        AudioLog::warn(QStringLiteral("EqSessionController"), message);
        return false;
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

    if (m_engine->isRunning() && m_activePid != processId) {
        m_engine->stop();
        m_activePid = 0;
    }

    const auto gains = m_gainReader();
    applySurroundFromReader();

    if (!m_engine->start(processId,
                         gains,
                         outputDeviceId,
                         outputDeviceName,
                         sinkDeviceId,
                         sinkDeviceName,
                         &errorMessage)) {
        emit errorOccurred(QStringLiteral("EQ failed to start"), errorMessage);
        return false;
    }

    m_activePid = processId;
    m_snapshot.processId = processId;
    m_snapshot.gains = gains;
    m_snapshot.eqOutputDeviceId = outputDeviceId;
    m_snapshot.eqOutputDeviceName = outputDeviceName;
    m_snapshot.sinkDeviceId = sinkDeviceId;
    m_snapshot.sinkDeviceName = sinkDeviceName;
    if (m_surroundStateReader) {
        const auto surroundState = m_surroundStateReader();
        m_snapshot.surroundEnabled = surroundState.first;
        m_snapshot.surroundChannelLevels = surroundState.second;
    }
    m_snapshot.valid = true;

    const QString appName = m_displayNameProvider ? m_displayNameProvider(m_activePid) : QString();
    emit logMessage(QStringLiteral("INFO"),
                    QStringLiteral("EQ active for PID %1 → %2").arg(processId).arg(outputDeviceName));
    emit eqStateChanged(true, m_activePid, appName);
    emit controlStateChanged();
    return true;
}

void EqSessionController::saveSnapshot()
{
    if (!m_gainReader) {
        return;
    }

    m_snapshot.processId = m_activePid;
    m_snapshot.gains = m_gainReader();
    if (m_surroundStateReader) {
        const auto surroundState = m_surroundStateReader();
        m_snapshot.surroundEnabled = surroundState.first;
        m_snapshot.surroundChannelLevels = surroundState.second;
    }
    m_snapshot.valid = m_snapshot.processId != 0
                       && !m_snapshot.eqOutputDeviceId.isEmpty()
                       && !m_snapshot.sinkDeviceId.isEmpty();
}

bool EqSessionController::resolveDevices(QString *sinkId,
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
