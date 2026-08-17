#include "audioengine.h"

#include "audiothreadutils.h"
#include "eqaudiosession.h"
#include "log.h"
#include "mixlimiter.h"
#include "processloopbackcapture.h"
#include "ui/appconstants.h"
#include "wasapirenderer.h"

#include <QMetaObject>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <avrt.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_map>
#include <vector>

namespace {

struct SessionMixState {
    bool prefilled = false;
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
}

AudioEngine::~AudioEngine()
{
    stop();
    if (m_comInitialized) {
        CoUninitialize();
    }
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

bool AudioEngine::ensureRendererOpen(const QString &eqOutputDeviceId,
                                     const QString &eqOutputDeviceName,
                                     QString *errorMessage)
{
    if (m_renderer->isOpen() && m_eqOutputDeviceId == eqOutputDeviceId) {
        return true;
    }

    if (m_renderer->isOpen()) {
        closeRenderer();
    }

    QString openError;
    if (!m_renderer->open(eqOutputDeviceId, 48000.f, 2, &openError)) {
        if (errorMessage) {
            *errorMessage = openError;
        }
        return false;
    }

    m_eqOutputDeviceId = eqOutputDeviceId;
    m_eqOutputDeviceName = eqOutputDeviceName;

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
    m_eqOutputDeviceName.clear();
}

bool AudioEngine::startSession(unsigned long processId,
                               const std::array<float, EqProcessor::kBandCount> &gainsDb,
                               bool surroundEnabled,
                               const std::array<int, SurroundProcessor::kChannelCount> &surroundLevels,
                               const QString &eqOutputDeviceId,
                               const QString &eqOutputDeviceName,
                               const QString &sinkDeviceId,
                               QString *errorMessage)
{
    const QString tag = QStringLiteral("AudioEngine");

    if (processId == 0) {
        const QString message = QStringLiteral("Invalid process id");
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

    if (!ensureRendererOpen(eqOutputDeviceId, eqOutputDeviceName, errorMessage)) {
        return false;
    }

    auto session = std::make_unique<EqAudioSession>();
    QString startError;
    SessionStartConfig startConfig;
    startConfig.processId = processId;
    startConfig.gainsDb = gainsDb;
    startConfig.surroundEnabled = surroundEnabled;
    startConfig.surroundLevels = surroundLevels;
    startConfig.mixSampleRate = m_renderer->sampleRate();
    startConfig.mixChannelCount = m_renderer->channelCount();
    startConfig.sinkDeviceId = sinkDeviceId;
    startConfig.spectrumCapture = m_spectrumCapture;
    startConfig.spectrumProcessId = &m_spectrumProcessId;
    startConfig.onThreadFinished = [this](unsigned long pid) {
        QMetaObject::invokeMethod(this,
                                  [this, pid]() { handleSessionThreadEnded(pid); },
                                  Qt::QueuedConnection);
    };
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

    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        m_sessions.push_back(std::move(session));
    }

    AudioLog::info(tag, QStringLiteral("Started EQ session for pid=%1").arg(processId));
    emit statusChanged(QStringLiteral("EQ active for PID %1").arg(processId));
    return true;
}

void AudioEngine::stopSession(unsigned long processId)
{
    std::unique_ptr<EqAudioSession> stoppedSession;
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        auto it = std::find_if(m_sessions.begin(), m_sessions.end(), [processId](const auto &session) {
            return session && session->processId() == processId;
        });
        if (it != m_sessions.end()) {
            stoppedSession = std::move(*it);
            m_sessions.erase(it);
        }
    }

    if (stoppedSession) {
        stoppedSession->stop();
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
    std::vector<std::unique_ptr<EqAudioSession>> sessions;
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        sessions = std::move(m_sessions);
        m_sessions.clear();
    }

    for (auto &session : sessions) {
        if (session) {
            const unsigned long pid = session->processId();
            session->stop();
            emit sessionStopped(pid);
        }
    }

    closeRenderer();
    emit statusChanged(QStringLiteral("EQ stopped"));
}

void AudioEngine::handleSessionThreadEnded(unsigned long processId)
{
    if (processId == 0) {
        return;
    }

    bool removed = false;
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        auto it = std::find_if(m_sessions.begin(), m_sessions.end(), [processId](const auto &session) {
            return session && session->processId() == processId;
        });
        if (it != m_sessions.end()) {
            m_sessions.erase(it);
            removed = true;
        }
    }

    if (!removed) {
        return;
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

void AudioEngine::setSessionGains(unsigned long processId, const std::array<float, EqProcessor::kBandCount> &gainsDb)
{
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    for (auto &session : m_sessions) {
        if (session && session->processId() == processId) {
            session->setGains(gainsDb);
            return;
        }
    }
}

void AudioEngine::setSessionSurroundEnabled(unsigned long processId, bool enabled)
{
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    for (auto &session : m_sessions) {
        if (session && session->processId() == processId) {
            session->setSurroundEnabled(enabled);
            return;
        }
    }
}

void AudioEngine::setSessionSurroundChannelLevels(unsigned long processId,
                                                  const std::array<int, SurroundProcessor::kChannelCount> &levels)
{
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    for (auto &session : m_sessions) {
        if (session && session->processId() == processId) {
            session->setSurroundChannelLevels(levels);
            return;
        }
    }
}

void AudioEngine::mixerThreadMain()
{
    AudioThreadUtils::enableFlushToZero();

    DWORD taskIndex = 0;
    HANDLE taskHandle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);

    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool comInitializedOnThread = SUCCEEDED(hr);

    MixLimiter mixLimiter;
    mixLimiter.setSampleRate(48000.f);

    std::vector<float> mixBuffer;
    std::vector<float> sessionScratch;
    int lastFrameCount = 0;
    int lastChannelCount = 0;
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

        const UINT32 availableFrames = m_renderer->availableWriteFrames();
        const UINT32 periodFrames = std::max<UINT32>(1, m_renderer->preferredFrameCount());
        const int frameCount = static_cast<int>(std::min(availableFrames, periodFrames));
        if (frameCount <= 0) {
            continue;
        }

        const int channelCount = m_renderer->channelCount();
        const size_t periodSamples = static_cast<size_t>(periodFrames * static_cast<UINT32>(channelCount));
        if (lastFrameCount != static_cast<int>(periodFrames) || lastChannelCount != channelCount) {
            mixBuffer.assign(periodSamples, 0.f);
            sessionScratch.assign(periodSamples, 0.f);
            mixLimiter.setSampleRate(static_cast<float>(m_renderer->sampleRate()));
            lastFrameCount = static_cast<int>(periodFrames);
            lastChannelCount = channelCount;
        } else {
            std::fill(mixBuffer.begin(), mixBuffer.begin() + static_cast<ptrdiff_t>(frameCount * channelCount), 0.f);
        }

        std::vector<std::shared_ptr<SpscRingBuffer>> activeRingBuffers;
        {
            std::lock_guard<std::mutex> lock(m_sessionsMutex);
            activeRingBuffers.reserve(m_sessions.size());
            for (const auto &session : m_sessions) {
                EqAudioSession *liveSession = session.get();
                if (liveSession && liveSession->isRunning()) {
                    if (std::shared_ptr<SpscRingBuffer> ring = liveSession->ringBuffer()) {
                        activeRingBuffers.push_back(std::move(ring));
                    }
                }
            }
        }

        int mixedSessionCount = 0;
        for (const std::shared_ptr<SpscRingBuffer> &ringBuffer : activeRingBuffers) {
            SessionMixState &mixState = sessionMixStates[ringBuffer];
            if (!mixState.prefilled
                && ringBuffer->availableFrames() < static_cast<size_t>(AppConstants::kTargetRingFillFrames)) {
                continue;
            }
            mixState.prefilled = true;

            std::fill(sessionScratch.begin(), sessionScratch.end(), 0.f);
            ringBuffer->readAdd(sessionScratch.data(), frameCount, channelCount);
            for (int i = 0; i < frameCount * channelCount; ++i) {
                mixBuffer[static_cast<size_t>(i)] += sessionScratch[static_cast<size_t>(i)];
            }
            ++mixedSessionCount;
        }

        for (auto it = sessionMixStates.begin(); it != sessionMixStates.end();) {
            const bool stillActive =
                std::find(activeRingBuffers.begin(), activeRingBuffers.end(), it->first) != activeRingBuffers.end();
            if (!stillActive) {
                it = sessionMixStates.erase(it);
            } else {
                ++it;
            }
        }

        if (mixedSessionCount > 1) {
            const float mixScale = 1.f / std::sqrt(static_cast<float>(mixedSessionCount));
            const int sampleCount = frameCount * channelCount;
            for (int i = 0; i < sampleCount; ++i) {
                mixBuffer[static_cast<size_t>(i)] *= mixScale;
            }
        }

        mixLimiter.process(mixBuffer.data(), frameCount, channelCount);

        QString writeError;
        if (!m_renderer->write(mixBuffer.data(), frameCount, channelCount, &writeError)) {
            emit errorOccurred(writeError);
            break;
        }
    }

    if (taskHandle) {
        AvRevertMmThreadCharacteristics(taskHandle);
    }

    if (comInitializedOnThread) {
        CoUninitialize();
    }
}
