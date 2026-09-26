#include "audioengine.h"

#include "audiothreadutils.h"
#include "audiosessionvolume.h"
#include "eqaudiosession.h"
#include "vst3/vst3plugin.h"
#include "log.h"
#include "mixlimiter.h"
#include "processloopbackcapture.h"
#include "sinkmutemanager.h"
#include "ui/appconstants.h"
#include "wasapirenderer.h"

#include <QMetaObject>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace {

struct SessionMixState {
    bool prefilled = false;
    uint64_t generation = 0;
    std::array<float, 8> lastFrame{};
    int lastChannels = 0;
    bool haveLastFrame = false;
};

struct MixSource {
    std::shared_ptr<SpscRingBuffer> ring;
    uint64_t generation = 0;
    bool mixPaused = false;
};

} // namespace

AudioEngine::AudioEngine(QObject *parent)
    : QObject(parent)
    , m_renderer(std::make_unique<WasapiRenderer>())
{
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr)) {
        m_comInitialized = true;
    } else if (hr == RPC_E_CHANGED_MODE) {
        AudioLog::warn(QStringLiteral("AudioEngine"), QStringLiteral("COM already initialized in a different mode"));
    } else {
        AudioLog::logHresult(QStringLiteral("AudioEngine"), QStringLiteral("CoInitializeEx"), hr);
    }

    LARGE_INTEGER qpcFrequency = {};
    if (QueryPerformanceFrequency(&qpcFrequency) && qpcFrequency.QuadPart > 0) {
        m_qpcFrequency = static_cast<uint64_t>(qpcFrequency.QuadPart);
    }
}

AudioEngine::~AudioEngine()
{
    stop();
    if (m_comInitialized) {
        CoUninitialize();
    }
}

void AudioEngine::requestRebuild()
{
    bool expected = false;
    if (!m_rebuildRequested.compare_exchange_strong(expected, true)) {
        return;
    }
    if (!m_ioSettings.autoRecovery) {
        QMetaObject::invokeMethod(this, [this]() { stopAfterInvalidation(); }, Qt::QueuedConnection);
        return;
    }
    QMetaObject::invokeMethod(this, [this]() { rebuildAfterInvalidation(); }, Qt::QueuedConnection);
}

void AudioEngine::stopAfterInvalidation()
{
    m_rebuildRequested.store(false, std::memory_order_release);
    AudioLog::warn(QStringLiteral("AudioEngine"),
                   QStringLiteral("Output device lost; auto-recovery is off, stopping EQ"));
    emit errorOccurred(QStringLiteral("EQ output device lost"));
    stop();
}

void AudioEngine::publishRenderClock()
{
    if (!m_renderer) {
        return;
    }
    uint64_t frames = 0;
    uint64_t qpc = 0;
    if (m_renderer->readClock(&frames, &qpc)) {
        m_renderClockFrames.store(frames, std::memory_order_release);
        m_renderClockQpc.store(qpc, std::memory_order_release);
    }
}

void AudioEngine::rebuildAfterInvalidation()
{
    std::lock_guard<std::mutex> lifecycle(m_lifecycleMutex);
    m_rebuildRequested.store(false, std::memory_order_release);

    AudioLog::warn(QStringLiteral("AudioEngine"),
                   QStringLiteral("Output device invalidated; flushing rings and recovering"));
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        for (const auto &session : m_sessions) {
            if (session) {
                session->flushOutputRing();
            }
        }
    }
    closeRenderer();
    emit statusChanged(QStringLiteral("EQ output device lost; recovering"));
    emit deviceInvalidated();
}

void AudioEngine::reportMixerWriteFailed()
{
    emit errorOccurred(QStringLiteral("WASAPI write failed"));
}

void AudioEngine::maybeLogMixerDiagnostics()
{
    const unsigned timeouts = m_mixerWaitTimeouts.exchange(0, std::memory_order_relaxed);
    const unsigned underruns = m_mixerRingUnderruns.exchange(0, std::memory_order_relaxed);
    const unsigned padding = m_mixerPaddingFull.exchange(0, std::memory_order_relaxed);
    if (timeouts == 0 && underruns == 0 && padding == 0) {
        return;
    }
    AudioLog::warn(QStringLiteral("AudioEngine"),
                   QStringLiteral("I/O diag: mixer waitTimeouts=%1 ringUnderruns=%2 paddingFull=%3")
                       .arg(timeouts)
                       .arg(underruns)
                       .arg(padding));
}

void AudioEngine::setSpectrumCapture(SpectrumCapture *capture)
{
    m_spectrumCapture = capture;
}

void AudioEngine::setSpectrumProcessId(unsigned long processId)
{
    m_spectrumProcessId.store(processId);
}

bool AudioEngine::isRunning() const
{
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    return !m_sessions.empty();
}

bool AudioEngine::isSessionActive(unsigned long processId) const
{
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    for (const auto &session : m_sessions) {
        if (session && session->processId() == processId && session->isRunning()) {
            return true;
        }
    }
    return false;
}

QVector<unsigned long> AudioEngine::activeProcessIds() const
{
    QVector<unsigned long> ids;
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    ids.reserve(static_cast<int>(m_sessions.size()));
    for (const auto &session : m_sessions) {
        if (session && session->isRunning()) {
            ids.push_back(session->processId());
        }
    }
    return ids;
}

bool AudioEngine::ensureRendererOpen(const QString &eqOutputDeviceId, QString *errorMessage)
{
    const int requestedBuffer = m_ioSettings.requestedBufferFrames();
    if (m_renderer->isOpen() && m_eqOutputDeviceId == eqOutputDeviceId
        && std::fabs(m_renderer->sampleRate() - m_requestedSampleRate) < 0.5f
        && m_openedBufferFrames == requestedBuffer
        && m_openedOutputFormat == m_ioSettings.outputFormat
        && m_openedShareMode == m_ioSettings.shareMode) {
        m_mixChannelCount = m_ioSettings.mixChannelCountForDevice(m_renderer->channelCount());
        return true;
    }

    if (m_renderer->isOpen()) {
        closeRenderer();
    }

    QString openError;
    if (!m_renderer->open(eqOutputDeviceId, m_ioSettings, &openError)) {
        if (errorMessage) {
            *errorMessage = openError;
        }
        return false;
    }

    m_eqOutputDeviceId = eqOutputDeviceId;
    m_openedBufferFrames = requestedBuffer;
    m_openedOutputFormat = m_ioSettings.outputFormat;
    m_openedShareMode = m_ioSettings.shareMode;
    m_mixChannelCount = m_ioSettings.mixChannelCountForDevice(m_renderer->channelCount());

    const UINT32 period = std::max<UINT32>(1, m_renderer->preferredFrameCount());
    const int effectiveRing = AppConstants::effectiveRingFrames(requestedBuffer, static_cast<int>(period));
    m_targetRingFillFrames = currentTargetRingFill(static_cast<int>(period));
    const double periodMs = m_renderer->sampleRate() > 0.f
                                ? (static_cast<double>(period) * 1000.0) / static_cast<double>(m_renderer->sampleRate())
                                : 0.0;
    emit statusChanged(QStringLiteral("I/O diag: render %1 Hz, period=%2 frames (%3 ms), clientBuffer=%4, preroll=%5, ring=%6 (target fill %7)")
                           .arg(m_renderer->sampleRate())
                           .arg(period)
                           .arg(periodMs, 0, 'f', 1)
                           .arg(m_renderer->bufferFrameCount())
                           .arg(m_renderer->prerollFrameCount())
                           .arg(effectiveRing)
                           .arg(m_targetRingFillFrames));

    if (!m_mixerRunning.load()) {
        m_mixerStopRequested.store(false);
        m_mixerRunning.store(true);
        m_mixerThread = std::thread(&AudioEngine::mixerThreadMain, this);
    }

    return true;
}

void AudioEngine::closeRenderer()
{
    m_mixerStopRequested.store(true);
    if (m_renderer) {
        m_renderer->interruptWait();
    }
    if (m_mixerThread.joinable()) {
        m_mixerThread.join();
    }
    m_mixerRunning.store(false);

    if (m_renderer) {
        m_renderer->close();
    }
    m_eqOutputDeviceId.clear();
    m_openedBufferFrames = 0;
}

void AudioEngine::setEngineSettings(const EngineIoSettings &settings)
{
    m_ioSettings = settings;
    m_ioSettings.sampleRate = AppConstants::clampSampleRate(settings.sampleRate);
    m_ioSettings.bufferFrames = settings.requestedBufferFrames();
    m_requestedSampleRate = static_cast<float>(m_ioSettings.sampleRate);
    m_requestedBufferFrames = m_ioSettings.bufferFrames;
    if (m_renderer && m_renderer->isOpen()) {
        const int period = static_cast<int>(std::max<UINT32>(1, m_renderer->preferredFrameCount()));
        m_mixChannelCount = m_ioSettings.mixChannelCountForDevice(m_renderer->channelCount());
        m_targetRingFillFrames = currentTargetRingFill(period);
    } else {
        m_mixChannelCount = m_ioSettings.mixChannelCountForDevice(m_mixChannelCount);
        m_targetRingFillFrames = AppConstants::ringTargetFillFrames(
            m_requestedBufferFrames > 0 ? m_requestedBufferFrames : AppConstants::kDefaultBufferFrames);
        m_targetRingFillFrames = applySafetyFill(m_targetRingFillFrames,
                                                 m_ioSettings.safetyExtraFrames(),
                                                 std::max(m_targetRingFillFrames * 2, 32));
    }
}

int AudioEngine::currentMixChannelCount() const
{
    if (m_renderer && m_renderer->isOpen()) {
        return m_ioSettings.mixChannelCountForDevice(m_renderer->channelCount());
    }
    return std::max(1, m_mixChannelCount);
}

int AudioEngine::currentTargetRingFill(int periodFrames) const
{
    const int effectiveRing = AppConstants::effectiveRingFrames(m_ioSettings.requestedBufferFrames(), periodFrames);
    const int target = AppConstants::ringTargetFillFramesForPeriod(effectiveRing, periodFrames);
    return applySafetyFill(target, m_ioSettings.safetyExtraFrames(), effectiveRing);
}

bool AudioEngine::startSession(unsigned long processId,
                               const EqState &eqState,
                               const VirtualSurroundSettings &virtualSurround,
                               const DynamicRangeSettings &dynamicRange,
                               const AudioChainOrder &audioChainOrder,
                               const QString &eqOutputDeviceId,
                               const QString &sinkDeviceId,
                               bool muteRoutingSink,
                               QString *errorMessage)
{
    std::lock_guard<std::mutex> lifecycle(m_lifecycleMutex);
    const QString tag = QStringLiteral("AudioEngine");

    if (processId == 0) {
        const QString message = QStringLiteral("Invalid process id");
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    }

    if (processId == GetCurrentProcessId()) {
        const QString message = QStringLiteral("Cannot enable EQ on CurvioEQ itself");
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    }

    if (eqOutputDeviceId.isEmpty()) {
        const QString message = QStringLiteral("No EQ output device");
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    }

    if (sinkDeviceId.isEmpty()) {
        const QString message = QStringLiteral("No routing sink device");
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        for (const auto &existing : m_sessions) {
            if (existing && existing->processId() == processId && existing->isRunning()) {
                return true;
            }
        }
        if (static_cast<int>(m_sessions.size()) >= kMaxSessions) {
            const QString message = QStringLiteral("Maximum number of EQ sessions reached (%1)").arg(kMaxSessions);
            if (errorMessage) {
                *errorMessage = message;
            }
            return false;
        }
    }

    if (!ensureRendererOpen(eqOutputDeviceId, errorMessage)) {
        return false;
    }

    auto session = std::make_unique<EqAudioSession>();
    QString startError;
    SessionStartConfig startConfig;
    startConfig.processId = processId;
    startConfig.eqState = eqState;
    startConfig.virtualSurround = virtualSurround;
    startConfig.dynamicRange = dynamicRange;
    startConfig.audioChainOrder = audioChainOrder;
    startConfig.mixSampleRate = m_renderer->sampleRate();
    startConfig.mixChannelCount = currentMixChannelCount();
    startConfig.enginePeriodFrames = static_cast<int>(std::max<UINT32>(1, m_renderer->preferredFrameCount()));
    startConfig.bufferFrames = m_ioSettings.requestedBufferFrames();
    startConfig.io = m_ioSettings;
    startConfig.sinkDeviceId = sinkDeviceId;
    startConfig.spectrumCapture = m_spectrumCapture;
    startConfig.spectrumProcessId = &m_spectrumProcessId;
    startConfig.renderClockFrames = &m_renderClockFrames;
    startConfig.renderClockQpc = &m_renderClockQpc;
    startConfig.qpcFrequency = m_qpcFrequency;
    startConfig.prerollFrames = static_cast<int>(m_renderer->prerollFrameCount());
    startConfig.onDeviceInvalidated = [this]() { requestRebuild(); };
    startConfig.onThreadFinished = [this](unsigned long pid, const QString &errorMessage) {
        QMetaObject::invokeMethod(this,
                                  [this, pid, errorMessage]() { handleSessionThreadEnded(pid, errorMessage); },
                                  Qt::QueuedConnection);
    };
    session->setOutputLimiterThreshold(m_outputLimiterThreshold.load());
    if (!session->start(std::move(startConfig), &startError)) {
        if (errorMessage) {
            *errorMessage = startError;
        }
        emit errorOccurred(startError);

        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        if (m_sessions.empty()) {
            closeRenderer();
        }
        return false;
    }

    if (!sinkDeviceId.isEmpty()) {
        QString muteError;
        if (!SinkMuteManager::instance().acquire(sinkDeviceId, muteRoutingSink, &muteError)) {
            session->stop();
            if (errorMessage) {
                *errorMessage = muteError;
            }
            emit errorOccurred(muteError);

            std::lock_guard<std::mutex> lock(m_sessionsMutex);
            if (m_sessions.empty()) {
                closeRenderer();
            }
            return false;
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        m_sessionMuteRoutingSink.insert(processId, muteRoutingSink);
        m_sessionSinkDeviceIds.insert(processId, sinkDeviceId);
        session->setOutputGain(m_sessionOutputGains.value(processId, 1.f));
        m_sessions.push_back(std::move(session));
    }

    AudioLog::info(tag, QStringLiteral("Started EQ session for pid=%1").arg(processId));
    emit statusChanged(QStringLiteral("EQ active for PID %1").arg(processId));
    return true;
}

void AudioEngine::stopSession(unsigned long processId)
{
    std::lock_guard<std::mutex> lifecycle(m_lifecycleMutex);
    std::unique_ptr<EqAudioSession> stoppedSession;
    QString sinkDeviceId;
    bool muteRoutingSink = true;
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        auto it = std::find_if(m_sessions.begin(), m_sessions.end(), [processId](const auto &session) {
            return session && session->processId() == processId;
        });
        if (it != m_sessions.end()) {
            stoppedSession = std::move(*it);
            sinkDeviceId = m_sessionSinkDeviceIds.value(processId);
            muteRoutingSink = m_sessionMuteRoutingSink.value(processId, true);
            m_sessions.erase(it);
            m_sessionMuteRoutingSink.remove(processId);
            m_sessionSinkDeviceIds.remove(processId);
        }
    }

    if (stoppedSession) {
        stoppedSession->stop();
        if (!sinkDeviceId.isEmpty()) {
            SinkMuteManager::instance().release(sinkDeviceId, muteRoutingSink);
        }
        emit statusChanged(QStringLiteral("EQ stopped for PID %1").arg(processId));
        emit sessionStopped(processId);
    }

    bool shouldCloseRenderer = false;
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        shouldCloseRenderer = m_sessions.empty();
    }

    if (shouldCloseRenderer) {
        closeRenderer();
        emit statusChanged(QStringLiteral("EQ stopped"));
    }
}

void AudioEngine::stop()
{
    std::lock_guard<std::mutex> lifecycle(m_lifecycleMutex);
    std::vector<std::unique_ptr<EqAudioSession>> sessions;
    QHash<unsigned long, bool> muteRoutingByPid;
    QHash<unsigned long, QString> sinkDeviceByPid;
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        sessions = std::move(m_sessions);
        muteRoutingByPid = m_sessionMuteRoutingSink;
        sinkDeviceByPid = m_sessionSinkDeviceIds;
        m_sessions.clear();
        m_sessionMuteRoutingSink.clear();
        m_sessionSinkDeviceIds.clear();
    }

    for (auto &session : sessions) {
        if (session) {
            const unsigned long pid = session->processId();
            const QString sinkDeviceId = sinkDeviceByPid.value(pid);
            const bool muteRoutingSink = muteRoutingByPid.value(pid, true);
            session->stop();
            if (!sinkDeviceId.isEmpty()) {
                SinkMuteManager::instance().release(sinkDeviceId, muteRoutingSink);
            }
            emit sessionStopped(pid);
        }
    }

    closeRenderer();
    emit statusChanged(QStringLiteral("EQ stopped"));
}

void AudioEngine::maintainActiveSessionRouting()
{
    maybeLogMixerDiagnostics();
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    for (const auto &session : m_sessions) {
        if (session && session->isRunning()) {
            session->maintainRouting();
            const unsigned long pid = session->processId();
            const QString sinkDeviceId = m_sessionSinkDeviceIds.value(pid);
            const bool muteRoutingSink = m_sessionMuteRoutingSink.value(pid, true);
            if (muteRoutingSink && !sinkDeviceId.isEmpty()) {
                QString muteError;
                if (!SinkMuteManager::instance().ensure(sinkDeviceId, true, &muteError) && !muteError.isEmpty()) {
                    AudioLog::warn(QStringLiteral("AudioEngine"),
                                   QStringLiteral("Sink mute re-check failed for pid=%1: %2")
                                       .arg(pid)
                                       .arg(muteError));
                }
            }
        }
    }
}

void AudioEngine::handleSessionThreadEnded(unsigned long processId, const QString &errorMessage)
{
    std::lock_guard<std::mutex> lifecycle(m_lifecycleMutex);
    if (processId == 0) {
        return;
    }

    bool removed = false;
    QString sinkDeviceId;
    bool muteRoutingSink = true;
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        auto it = std::find_if(m_sessions.begin(), m_sessions.end(), [processId](const auto &session) {
            return session && session->processId() == processId;
        });
        if (it != m_sessions.end()) {
            sinkDeviceId = m_sessionSinkDeviceIds.value(processId);
            muteRoutingSink = m_sessionMuteRoutingSink.value(processId, true);
            m_sessions.erase(it);
            m_sessionMuteRoutingSink.remove(processId);
            m_sessionSinkDeviceIds.remove(processId);
            removed = true;
        }
    }

    if (!removed) {
        return;
    }

    if (!sinkDeviceId.isEmpty()) {
        SinkMuteManager::instance().release(sinkDeviceId, muteRoutingSink);
    }

    if (!errorMessage.isEmpty()) {
        AudioLog::error(QStringLiteral("AudioEngine"), errorMessage);
        emit errorOccurred(errorMessage);
    }

    AudioLog::info(QStringLiteral("AudioEngine"),
                   QStringLiteral("EQ session ended for pid=%1").arg(processId));
    emit statusChanged(QStringLiteral("EQ stopped for PID %1").arg(processId));
    emit sessionStopped(processId);

    bool shouldCloseRenderer = false;
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        shouldCloseRenderer = m_sessions.empty();
    }

    if (shouldCloseRenderer) {
        closeRenderer();
        emit statusChanged(QStringLiteral("EQ stopped"));
    }
}

void AudioEngine::pruneEndedSessions()
{
    QVector<unsigned long> processIdsToStop;
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        processIdsToStop.reserve(static_cast<int>(m_sessions.size()));
        for (const auto &session : m_sessions) {
            if (!session) {
                continue;
            }
            const unsigned long pid = session->processId();
            if (!session->isRunning() || !ProcessLoopbackCapture::isProcessRunning(pid)) {
                processIdsToStop.push_back(pid);
            }
        }
    }

    for (unsigned long pid : processIdsToStop) {
        stopSession(pid);
    }
}

void AudioEngine::setSessionEqState(unsigned long processId, const EqState &eqState)
{
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    for (auto &session : m_sessions) {
        if (session && session->processId() == processId) {
            session->setEqState(eqState);
            return;
        }
    }
}

void AudioEngine::setSessionBalance(unsigned long processId, int balance)
{
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    for (auto &session : m_sessions) {
        if (session && session->processId() == processId) {
            session->setBalance(balance);
            return;
        }
    }
}

void AudioEngine::setSessionVirtualSurround(unsigned long processId, const VirtualSurroundSettings &settings)
{
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    for (auto &session : m_sessions) {
        if (session && session->processId() == processId) {
            session->setVirtualSurroundSettings(settings);
            return;
        }
    }
}

void AudioEngine::setSessionDynamicRange(unsigned long processId, const DynamicRangeSettings &settings)
{
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    for (auto &session : m_sessions) {
        if (session && session->processId() == processId) {
            session->setDynamicRangeSettings(settings);
            return;
        }
    }
}

void AudioEngine::setSessionAudioChainOrder(unsigned long processId, const AudioChainOrder &order)
{
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    for (auto &session : m_sessions) {
        if (session && session->processId() == processId) {
            session->setAudioChainOrder(order);
            return;
        }
    }
}

void AudioEngine::setSessionOutputGain(unsigned long processId, float gain)
{
    const float clamped = std::clamp(gain, 1.f, AudioSessionVolume::kMaxOutputGain);
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    m_sessionOutputGains.insert(processId, clamped);
    for (auto &session : m_sessions) {
        if (session && session->processId() == processId) {
            session->setOutputGain(clamped);
            return;
        }
    }
}

void AudioEngine::setOutputLimiterThreshold(float linearPeak)
{
    const float clamped = std::clamp(linearPeak, MixLimiter::dbToLinear(AppConstants::kSpectrumLimiterMinDb), 1.f);
    m_outputLimiterThreshold.store(clamped);
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    for (auto &session : m_sessions) {
        if (session) {
            session->setOutputLimiterThreshold(clamped);
        }
    }
}

void AudioEngine::setSessionAddon(unsigned long processId, int slot, std::shared_ptr<Vst3Plugin> plugin)
{
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    for (auto &session : m_sessions) {
        if (session && session->processId() == processId) {
            session->setAddon(slot, std::move(plugin));
            return;
        }
    }
}

void AudioEngine::mixerThreadMain()
{
    AudioThreadUtils::enableFlushToZero();
    AudioThreadUtils::applyCpuAffinity(m_ioSettings.affinityCoreOrNone());

    HANDLE taskHandle = nullptr;
    if (m_ioSettings.useRealtimeAudio()) {
        taskHandle = AudioThreadUtils::enableProAudioMmcss();
    }

    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool comInitializedOnThread = SUCCEEDED(hr);

    MixLimiter mixLimiter;
    mixLimiter.setSampleRate(m_requestedSampleRate);
    mixLimiter.setUseDoublePrecision(m_ioSettings.useDoublePrecision());

    std::vector<float> mixBuffer;
    std::vector<float> sessionScratch;
    std::vector<MixSource> activeSources;
    activeSources.reserve(static_cast<size_t>(kMaxSessions));
    int lastChannelCount = 0;
    UINT32 lastClientBuffer = 0;
    std::unordered_map<std::shared_ptr<SpscRingBuffer>, SessionMixState> sessionMixStates;

    while (!m_mixerStopRequested.load()) {
        if (!m_renderer || !m_renderer->isOpen()) {
            Sleep(10);
            continue;
        }

        if (!m_renderer->waitForNextPeriod(20)) {
            continue;
        }
        if (m_mixerStopRequested.load()) {
            break;
        }
        if (m_renderer->deviceLost()) {
            requestRebuild();
            break;
        }

        publishRenderClock();

        if (m_renderer->lastWaitTimedOut()) {
            m_mixerWaitTimeouts.fetch_add(1, std::memory_order_relaxed);
        }

        const UINT32 availableFrames = m_renderer->availableWriteFrames();
        if (m_renderer->deviceLost()) {
            requestRebuild();
            break;
        }
        const UINT32 periodFrames = std::max<UINT32>(1, m_renderer->preferredFrameCount());
        const UINT32 padding = m_renderer->lastPaddingFrames();
        const UINT32 clientBuffer = std::max<UINT32>(1, m_renderer->bufferFrameCount());
        const UINT32 catchUpLimit = periodFrames * 3;
        UINT32 framesToMix = periodFrames;
        if (padding == 0 || m_renderer->lastWaitTimedOut()) {
            framesToMix = std::min({availableFrames, catchUpLimit, clientBuffer});
        } else {
            framesToMix = std::min(availableFrames, periodFrames);
        }
        const int frameCount = static_cast<int>(framesToMix);
        if (frameCount <= 0) {
            m_mixerPaddingFull.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        const int mixChannelCount = currentMixChannelCount();
        const size_t maxSamples =
            static_cast<size_t>(clientBuffer * static_cast<UINT32>(std::max(1, mixChannelCount)));
        if (lastClientBuffer != clientBuffer || lastChannelCount != mixChannelCount) {
            mixBuffer.assign(maxSamples, 0.f);
            sessionScratch.assign(maxSamples, 0.f);
            mixLimiter.setSampleRate(static_cast<float>(m_renderer->sampleRate()));
            mixLimiter.setUseDoublePrecision(m_ioSettings.useDoublePrecision());
            lastClientBuffer = clientBuffer;
            lastChannelCount = mixChannelCount;
        } else {
            std::fill(mixBuffer.begin(),
                      mixBuffer.begin() + static_cast<ptrdiff_t>(frameCount * mixChannelCount),
                      0.f);
        }

        {
            std::unique_lock<std::mutex> lock(m_sessionsMutex, std::try_to_lock);
            if (lock.owns_lock()) {
                activeSources.clear();
                for (const auto &session : m_sessions) {
                    EqAudioSession *liveSession = session.get();
                    if (liveSession && liveSession->isRunning()) {
                        if (std::shared_ptr<SpscRingBuffer> ring = liveSession->ringBuffer()) {
                            MixSource source;
                            source.ring = std::move(ring);
                            source.generation = source.ring->generation();
                            source.mixPaused = liveSession->mixPaused();
                            activeSources.push_back(std::move(source));
                        }
                    }
                }
            }
        }

        int mixedSessionCount = 0;
        for (const MixSource &source : activeSources) {
            const std::shared_ptr<SpscRingBuffer> &ringBuffer = source.ring;
            SessionMixState &mixState = sessionMixStates[ringBuffer];
            if (source.mixPaused || ringBuffer->generation() != source.generation) {
                mixState.prefilled = false;
                mixState.generation = ringBuffer->generation();
                mixState.haveLastFrame = false;
                continue;
            }
            if (!mixState.prefilled
                && ringBuffer->availableFrames() < static_cast<size_t>(m_targetRingFillFrames)) {
                continue;
            }
            mixState.prefilled = true;
            mixState.generation = source.generation;

            std::fill(sessionScratch.begin(),
                      sessionScratch.begin() + static_cast<ptrdiff_t>(frameCount * mixChannelCount),
                      0.f);
            const int framesGot = ringBuffer->readAdd(sessionScratch.data(), frameCount, mixChannelCount);
            if (framesGot > 0 && mixChannelCount > 0) {
                const float *last = sessionScratch.data()
                    + static_cast<size_t>(framesGot - 1) * static_cast<size_t>(mixChannelCount);
                const int holdChannels = std::min(mixChannelCount, static_cast<int>(mixState.lastFrame.size()));
                for (int channel = 0; channel < holdChannels; ++channel) {
                    mixState.lastFrame[static_cast<size_t>(channel)] = last[channel];
                }
                mixState.lastChannels = mixChannelCount;
                mixState.haveLastFrame = true;
            }
            if (framesGot < frameCount) {
                m_mixerRingUnderruns.fetch_add(1, std::memory_order_relaxed);
                if (mixState.haveLastFrame) {
                    for (int frame = framesGot; frame < frameCount; ++frame) {
                        for (int channel = 0; channel < mixChannelCount; ++channel) {
                            const float sample = channel < mixState.lastChannels
                                ? mixState.lastFrame[static_cast<size_t>(channel)]
                                : 0.f;
                            sessionScratch[static_cast<size_t>(frame * mixChannelCount + channel)] = sample;
                        }
                    }
                }
            }
            for (int i = 0; i < frameCount * mixChannelCount; ++i) {
                mixBuffer[static_cast<size_t>(i)] += sessionScratch[static_cast<size_t>(i)];
            }
            ++mixedSessionCount;
        }

        for (auto it = sessionMixStates.begin(); it != sessionMixStates.end();) {
            const bool stillActive = std::any_of(activeSources.begin(), activeSources.end(),
                                                 [&](const MixSource &source) { return source.ring == it->first; });
            if (!stillActive) {
                it = sessionMixStates.erase(it);
            } else {
                ++it;
            }
        }

        if (mixedSessionCount > 1) {
            const float mixScale = 1.f / std::sqrt(static_cast<float>(mixedSessionCount));
            const int sampleCount = frameCount * mixChannelCount;
            for (int i = 0; i < sampleCount; ++i) {
                mixBuffer[static_cast<size_t>(i)] *= mixScale;
            }
        }

        mixLimiter.process(mixBuffer.data(), frameCount, mixChannelCount);

        if (!m_renderer->writePrepared(mixBuffer.data(), frameCount, mixChannelCount)) {
            if (m_renderer->deviceLost()) {
                requestRebuild();
                break;
            }
            QMetaObject::invokeMethod(this, [this]() { reportMixerWriteFailed(); }, Qt::QueuedConnection);
            break;
        }
    }

    if (taskHandle) {
        AudioThreadUtils::disableMmcss(taskHandle);
    }

    if (comInitializedOnThread) {
        CoUninitialize();
    }
}
