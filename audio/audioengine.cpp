#include "audioengine.h"

#include "log.h"
#include "processloopbackcapture.h"
#include "wasapirenderer.h"
#include "audiopolicyrouter.h"
#include "ui/spectrumanalyzer.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <vector>
#include <algorithm>
#include <cmath>

namespace {

int resampleLinear(const float *input,
                   int inputFrames,
                   int channelCount,
                   float inputRate,
                   float outputRate,
                   std::vector<float> *output)
{
    if (!input || inputFrames <= 0 || channelCount <= 0 || !output || inputRate <= 0.f || outputRate <= 0.f) {
        return 0;
    }

    if (std::fabs(inputRate - outputRate) < 0.5f) {
        output->assign(input, input + static_cast<size_t>(inputFrames * channelCount));
        return inputFrames;
    }

    const double ratio = static_cast<double>(inputRate) / static_cast<double>(outputRate);
    const int outputFrames = std::max(1, static_cast<int>(std::floor(inputFrames / ratio)));
    output->assign(static_cast<size_t>(outputFrames * channelCount), 0.f);

    for (int outFrame = 0; outFrame < outputFrames; ++outFrame) {
        const double sourcePos = outFrame * ratio;
        const int index0 = static_cast<int>(sourcePos);
        const int index1 = std::min(index0 + 1, inputFrames - 1);
        const float fraction = static_cast<float>(sourcePos - static_cast<double>(index0));

        for (int channel = 0; channel < channelCount; ++channel) {
            const float sample0 = input[static_cast<size_t>(index0 * channelCount + channel)];
            const float sample1 = input[static_cast<size_t>(index1 * channelCount + channel)];
            (*output)[static_cast<size_t>(outFrame * channelCount + channel)] =
                sample0 + (sample1 - sample0) * fraction;
        }
    }

    return outputFrames;
}

} // namespace

AudioEngine::AudioEngine(QObject *parent)
    : QObject(parent)
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

bool AudioEngine::start(unsigned long processId,
                        const std::array<float, EqProcessor::kBandCount> &gainsDb,
                        const QString &eqOutputDeviceId,
                        const QString &eqOutputDeviceName,
                        const QString &sinkDeviceId,
                        const QString &sinkDeviceName,
                        QString *errorMessage)
{
    const QString tag = QStringLiteral("AudioEngine");
    stop();

    if (processId == 0) {
        const QString message = QStringLiteral("Invalid process id");
        AudioLog::error(tag, message);
        if (errorMessage) {
            *errorMessage = message;
        }
        emit errorOccurred(message);
        return false;
    }

    if (eqOutputDeviceId.isEmpty()) {
        const QString message = QStringLiteral("No EQ output device for the selected app");
        AudioLog::error(tag, message);
        if (errorMessage) {
            *errorMessage = message;
        }
        emit errorOccurred(message);
        return false;
    }

    AudioLog::info(tag, QStringLiteral("Starting EQ for pid=%1").arg(processId));
    m_eqProcessor.setGains(gainsDb);

    m_eqOutputDeviceId = eqOutputDeviceId;
    m_eqOutputDeviceName = eqOutputDeviceName;
    m_sinkDeviceId = sinkDeviceId;
    m_sinkDeviceName = sinkDeviceName;
    m_activeProcessId = processId;
    m_routingApplied = false;

    if (!sinkDeviceId.isEmpty() && sinkDeviceId != eqOutputDeviceId) {
        QString routeError;
        if (!AudioPolicyRouter::routeProcessToDevice(processId, sinkDeviceId, &routeError)) {
            const QString message =
                QStringLiteral("Could not route app audio to sink: %1").arg(routeError);
            AudioLog::error(tag, message);
            if (errorMessage) {
                *errorMessage = message;
            }
            emit errorOccurred(message);
            m_activeProcessId = 0;
            m_eqOutputDeviceId.clear();
            m_eqOutputDeviceName.clear();
            m_sinkDeviceId.clear();
            m_sinkDeviceName.clear();
            return false;
        }
        m_routingApplied = true;
        const QString sinkLabel = sinkDeviceName.isEmpty() ? sinkDeviceId : sinkDeviceName;
        emit statusChanged(QStringLiteral("Routing original audio to %1").arg(sinkLabel));
    }

    m_stopRequested.store(false);
    m_running.store(true);

    m_thread = std::thread(&AudioEngine::audioThreadMain, this, processId);
    emit statusChanged(QStringLiteral("EQ active for PID %1").arg(processId));
    return true;
}

void AudioEngine::stop()
{
    if (!m_running.load() && !m_thread.joinable()) {
        return;
    }

    AudioLog::info(QStringLiteral("AudioEngine"), QStringLiteral("Stopping EQ"));
    m_stopRequested.store(true);

    if (m_thread.joinable()) {
        m_thread.join();
    }

    if (m_routingApplied && m_activeProcessId != 0) {
        QString clearError;
        if (!AudioPolicyRouter::clearProcessRouting(m_activeProcessId, &clearError)) {
            emit statusChanged(QStringLiteral("WARN: Failed to restore app routing: %1").arg(clearError));
        } else {
            emit statusChanged(
                QStringLiteral("Restored PID %1 to default output device").arg(m_activeProcessId));
        }
        m_routingApplied = false;
    }

    m_activeProcessId = 0;
    m_eqOutputDeviceId.clear();
    m_eqOutputDeviceName.clear();
    m_sinkDeviceId.clear();
    m_sinkDeviceName.clear();
    m_routingApplied = false;
    m_running.store(false);
    emit statusChanged(QStringLiteral("EQ stopped"));
}

void AudioEngine::setGains(const std::array<float, EqProcessor::kBandCount> &gainsDb)
{
    m_eqProcessor.setGains(gainsDb);
}

void AudioEngine::setSurroundEnabled(bool enabled)
{
    m_surroundProcessor.setEnabled(enabled);
}

void AudioEngine::setSurroundChannelLevels(const std::array<int, SurroundProcessor::kChannelCount> &levels)
{
    m_surroundProcessor.setChannelLevels(levels);
}

void AudioEngine::audioThreadMain(unsigned long processId)
{
    const QString tag = QStringLiteral("AudioEngine");

    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool comInitializedOnThread = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        const QString message = QStringLiteral("COM init on audio thread failed: %1")
                                  .arg(AudioLog::hresultToString(hr));
        AudioLog::error(tag, message);
        emit errorOccurred(message);
        m_running.store(false);
        return;
    }

    ProcessLoopbackCapture capture;
    WasapiRenderer renderer;
    QString errorMessage;

    if (!capture.open(processId, &errorMessage)) {
        AudioLog::error(tag, errorMessage);
        emit errorOccurred(errorMessage);
        m_running.store(false);
        if (comInitializedOnThread) {
            CoUninitialize();
        }
        return;
    }

    m_eqProcessor.setSampleRate(capture.sampleRate());

    const QString eqLabel = m_eqOutputDeviceName.isEmpty() ? m_eqOutputDeviceId : m_eqOutputDeviceName;
    emit statusChanged(QStringLiteral("EQ active on %1").arg(eqLabel));

    if (!renderer.open(m_eqOutputDeviceId, capture.sampleRate(), capture.channelCount(), &errorMessage)) {
        AudioLog::error(tag, errorMessage);
        emit errorOccurred(errorMessage);
        capture.close();
        m_running.store(false);
        if (comInitializedOnThread) {
            CoUninitialize();
        }
        return;
    }

    emit statusChanged(QStringLiteral("[LoopbackCapture] Capture opened: %1 Hz, %2 ch | [WasapiRenderer] Render opened: %3 Hz, %4 ch")
                           .arg(capture.sampleRate())
                           .arg(capture.channelCount())
                           .arg(renderer.sampleRate())
                           .arg(renderer.channelCount()));

    if (m_spectrumCapture) {
        m_spectrumCapture->reset();
        m_spectrumCapture->setSampleRate(static_cast<int>(capture.sampleRate()));
    }

    const int channelCount = capture.channelCount();
    const int frameChunk = static_cast<int>(std::max<UINT32>(renderer.preferredFrameCount(), 512));
    std::vector<float> buffer(static_cast<size_t>(frameChunk * channelCount));
    std::vector<float> surroundBuffer(static_cast<size_t>(frameChunk * SurroundProcessor::kChannelCount));
    std::vector<float> resampledBuffer;
    const float captureRate = capture.sampleRate();
    const float renderRate = renderer.sampleRate();
    const bool needsResample = std::fabs(captureRate - renderRate) >= 0.5f;

    AudioLog::info(tag, QStringLiteral("Audio loop running with chunk=%1 frames, resample=%2")
                             .arg(frameChunk)
                             .arg(needsResample));

    bool firstChunkLogged = false;
    int silentChunkCount = 0;
    bool silenceWarningEmitted = false;

    while (!m_stopRequested.load()) {
        int framesRead = 0;
        if (!capture.read(buffer.data(), frameChunk, &framesRead, &errorMessage)) {
            AudioLog::error(tag, errorMessage);
            emit errorOccurred(errorMessage);
            break;
        }

        if (framesRead == 0) {
            Sleep(5);
            continue;
        }

        float peakBefore = 0.f;
        float peakAfter = 0.f;
#ifndef NDEBUG
        for (int i = 0; i < framesRead * channelCount; ++i) {
            peakBefore = std::max(peakBefore, std::fabs(buffer[static_cast<size_t>(i)]));
        }
#endif

        if (m_spectrumCapture) {
            m_spectrumCapture->pushBefore(buffer.data(), framesRead, channelCount);
        }

        m_eqProcessor.process(buffer.data(), framesRead, channelCount);

        if (m_spectrumCapture) {
            m_spectrumCapture->pushAfter(buffer.data(), framesRead, channelCount);
        }

#ifndef NDEBUG
        for (int i = 0; i < framesRead * channelCount; ++i) {
            peakAfter = std::max(peakAfter, std::fabs(buffer[static_cast<size_t>(i)]));
        }
#endif

        if (!firstChunkLogged) {
            firstChunkLogged = true;
#ifndef NDEBUG
            emit statusChanged(QStringLiteral("Pipeline levels: peakBefore=%1 peakAfter=%2 capture=%3 Hz render=%4 Hz inCh=%5 outCh=%6")
                                   .arg(peakBefore, 0, 'f', 4)
                                   .arg(peakAfter, 0, 'f', 4)
                                   .arg(captureRate, 0, 'f', 0)
                                   .arg(renderRate, 0, 'f', 0)
                                   .arg(channelCount)
                                   .arg(renderer.channelCount()));
#else
            emit statusChanged(QStringLiteral("Pipeline running: capture=%1 Hz render=%2 Hz inCh=%3 outCh=%4")
                                   .arg(captureRate, 0, 'f', 0)
                                   .arg(renderRate, 0, 'f', 0)
                                   .arg(channelCount)
                                   .arg(renderer.channelCount()));
#endif
        }

#ifndef NDEBUG
        if (peakBefore < 0.001f) {
            ++silentChunkCount;
            if (!silenceWarningEmitted && silentChunkCount >= 5) {
                silenceWarningEmitted = true;
                emit statusChanged(QStringLiteral("WARN: Capture near silence — ensure target app is playing audio"));
            }
        } else {
            silentChunkCount = 0;
        }
#endif

        const float *writeBuffer = buffer.data();
        int writeChannelCount = channelCount;
        int framesToWrite = framesRead;

        if (m_surroundProcessor.isEnabled()) {
            m_surroundProcessor.process(buffer.data(), surroundBuffer.data(), framesRead);
            writeBuffer = surroundBuffer.data();
            writeChannelCount = SurroundProcessor::kChannelCount;
        }

        if (needsResample) {
            framesToWrite = resampleLinear(writeBuffer,
                                           framesRead,
                                           writeChannelCount,
                                           captureRate,
                                           renderRate,
                                           &resampledBuffer);
            writeBuffer = resampledBuffer.data();
        }

        if (framesToWrite <= 0) {
            continue;
        }

        if (!renderer.write(writeBuffer, framesToWrite, writeChannelCount, &errorMessage)) {
            AudioLog::error(tag, errorMessage);
            emit errorOccurred(errorMessage);
            break;
        }
    }

    capture.close();
    renderer.close();

    m_running.store(false);
    AudioLog::info(tag, QStringLiteral("Audio loop exited"));

    if (comInitializedOnThread) {
        CoUninitialize();
    }
}
