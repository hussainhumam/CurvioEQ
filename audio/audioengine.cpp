#include "audioengine.h"

#include "eqaudiosession.h"
#include "log.h"
#include "processloopbackcapture.h"
#include "wasapirenderer.h"

#include <QMetaObject>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <vector>

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
                               const QString &sinkDeviceName,
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
    if (!session->start(processId,
                        gainsDb,
                        surroundEnabled,
                        surroundLevels,
                        m_renderer->sampleRate(),
                        m_renderer->channelCount(),
                        sinkDeviceId,
                        sinkDeviceName,
                        m_spectrumCapture,
                        &m_spectrumProcessId,
                        [this](unsigned long pid) {
                            QMetaObject::invokeMethod(this,
                                                      [this, pid]() { handleSessionThreadEnded(pid); },
                                                      Qt::QueuedConnection);
                        },
                        &startError)) {
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

EqAudioSession *AudioEngine::findSession(unsigned long processId)
{
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    for (auto &session : m_sessions) {
        if (session && session->processId() == processId) {
            return session.get();
        }
    }
    return nullptr;
}

void AudioEngine::setSessionGains(unsigned long processId, const std::array<float, EqProcessor::kBandCount> &gainsDb)
{
    if (EqAudioSession *session = findSession(processId)) {
        session->setGains(gainsDb);
    }
}

void AudioEngine::setSessionSurroundEnabled(unsigned long processId, bool enabled)
{
    if (EqAudioSession *session = findSession(processId)) {
        session->setSurroundEnabled(enabled);
    }
}

void AudioEngine::setSessionSurroundChannelLevels(unsigned long processId,
                                                  const std::array<int, SurroundProcessor::kChannelCount> &levels)
{
    if (EqAudioSession *session = findSession(processId)) {
        session->setSurroundChannelLevels(levels);
    }
}

void AudioEngine::setGains(const std::array<float, EqProcessor::kBandCount> &gainsDb)
{
    const unsigned long pid = m_spectrumProcessId.load();
    if (pid != 0) {
        setSessionGains(pid, gainsDb);
        return;
    }
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    if (!m_sessions.empty() && m_sessions.front()) {
        m_sessions.front()->setGains(gainsDb);
    }
}

void AudioEngine::setSurroundEnabled(bool enabled)
{
    const unsigned long pid = m_spectrumProcessId.load();
    if (pid != 0) {
        setSessionSurroundEnabled(pid, enabled);
        return;
    }
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    if (!m_sessions.empty() && m_sessions.front()) {
        m_sessions.front()->setSurroundEnabled(enabled);
    }
}

void AudioEngine::setSurroundChannelLevels(const std::array<int, SurroundProcessor::kChannelCount> &levels)
{
    const unsigned long pid = m_spectrumProcessId.load();
    if (pid != 0) {
        setSessionSurroundChannelLevels(pid, levels);
        return;
    }
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    if (!m_sessions.empty() && m_sessions.front()) {
        m_sessions.front()->setSurroundChannelLevels(levels);
    }
}

void AudioEngine::mixerThreadMain()
{
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool comInitializedOnThread = SUCCEEDED(hr);

    std::vector<float> mixBuffer;
    std::vector<float> sessionScratch;
    int lastFrameCount = 0;
    int lastChannelCount = 0;

    while (!m_mixerStopRequested.load()) {
        if (!m_renderer || !m_renderer->isOpen()) {
            Sleep(10);
            continue;
        }

        const int frameCount = static_cast<int>(std::max<UINT32>(m_renderer->preferredFrameCount(), 512));
        const int channelCount = m_renderer->channelCount();
        const size_t bufferSamples = static_cast<size_t>(frameCount * channelCount);
        if (lastFrameCount != frameCount || lastChannelCount != channelCount) {
            mixBuffer.assign(bufferSamples, 0.f);
            sessionScratch.assign(bufferSamples, 0.f);
            lastFrameCount = frameCount;
            lastChannelCount = channelCount;
        } else {
            std::fill(mixBuffer.begin(), mixBuffer.end(), 0.f);
        }

        std::vector<AudioRingBuffer *> activeRingBuffers;
        {
            std::lock_guard<std::mutex> lock(m_sessionsMutex);
            activeRingBuffers.reserve(m_sessions.size());
            for (const auto &session : m_sessions) {
                EqAudioSession *liveSession = session.get();
                if (liveSession && liveSession->isRunning()) {
                    activeRingBuffers.push_back(liveSession->ringBuffer());
                }
            }
        }

        for (AudioRingBuffer *ringBuffer : activeRingBuffers) {
            std::fill(sessionScratch.begin(), sessionScratch.end(), 0.f);
            ringBuffer->readAdd(sessionScratch.data(), frameCount, channelCount);
            for (size_t i = 0; i < mixBuffer.size(); ++i) {
                mixBuffer[i] += sessionScratch[i];
            }
        }

        float peak = 0.f;
        for (float sample : mixBuffer) {
            peak = std::max(peak, std::fabs(sample));
        }

        if (peak > 1.f) {
            for (float &sample : mixBuffer) {
                sample = sample / (1.f + std::fabs(sample));
            }
        }

        QString writeError;
        if (!m_renderer->write(mixBuffer.data(), frameCount, channelCount, &writeError)) {
            emit errorOccurred(writeError);
            break;
        }
    }

    if (comInitializedOnThread) {
        CoUninitialize();
    }
}
