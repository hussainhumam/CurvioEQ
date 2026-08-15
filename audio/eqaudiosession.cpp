#include "eqaudiosession.h"

#include "audiopolicyrouter.h"
#include "log.h"
#include "processloopbackcapture.h"
#include "ui/appconstants.h"
#include "ui/spectrumanalyzer.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace {

void ensureVectorCapacity(std::vector<float> *output, size_t sampleCount)
{
    if (output->size() < sampleCount) {
        output->resize(sampleCount);
    }
}

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

    const size_t inputSampleCount = static_cast<size_t>(inputFrames * channelCount);
    if (std::fabs(inputRate - outputRate) < 0.5f) {
        ensureVectorCapacity(output, inputSampleCount);
        std::memcpy(output->data(), input, inputSampleCount * sizeof(float));
        return inputFrames;
    }

    const double ratio = static_cast<double>(inputRate) / static_cast<double>(outputRate);
    const int outputFrames = std::max(1, static_cast<int>(std::floor(inputFrames / ratio)));
    const size_t outputSampleCount = static_cast<size_t>(outputFrames * channelCount);
    ensureVectorCapacity(output, outputSampleCount);
    std::fill(output->begin(), output->begin() + static_cast<ptrdiff_t>(outputSampleCount), 0.f);

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

void upmixChannels(const float *input,
                   int frameCount,
                   int inputChannels,
                   int outputChannels,
                   std::vector<float> *output)
{
    if (!input || frameCount <= 0 || inputChannels <= 0 || outputChannels <= 0) {
        return;
    }

    const size_t outputSampleCount = static_cast<size_t>(frameCount * outputChannels);
    ensureVectorCapacity(output, outputSampleCount);
    std::fill(output->begin(), output->begin() + static_cast<ptrdiff_t>(outputSampleCount), 0.f);

    for (int frame = 0; frame < frameCount; ++frame) {
        for (int outCh = 0; outCh < outputChannels; ++outCh) {
            const int inCh = std::min(outCh, inputChannels - 1);
            (*output)[static_cast<size_t>(frame * outputChannels + outCh)] =
                input[static_cast<size_t>(frame * inputChannels + inCh)];
        }
    }
}

} // namespace

EqAudioSession::~EqAudioSession()
{
    stop();
}

bool EqAudioSession::start(unsigned long processId,
                           const std::array<float, EqProcessor::kBandCount> &gainsDb,
                           bool surroundEnabled,
                           const std::array<int, SurroundProcessor::kChannelCount> &surroundLevels,
                           float mixSampleRate,
                           int mixChannelCount,
                           const QString &sinkDeviceId,
                           const QString &sinkDeviceName,
                           SpectrumCapture *spectrumCapture,
                           std::atomic<unsigned long> *spectrumProcessId,
                           std::function<void(unsigned long)> onThreadFinished,
                           QString *errorMessage)
{
    stop();

    if (processId == 0) {
        const QString message = QStringLiteral("Invalid process id");
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    }

    m_processId = processId;
    m_mixSampleRate = mixSampleRate;
    m_mixChannelCount = std::max(1, mixChannelCount);
    m_sinkDeviceId = sinkDeviceId;
    m_spectrumCapture = spectrumCapture;
    m_spectrumProcessId = spectrumProcessId;
    m_onThreadFinished = std::move(onThreadFinished);
    m_routingApplied = false;

    m_eqProcessor.setGains(gainsDb);
    m_surroundProcessor.setEnabled(surroundEnabled);
    m_surroundProcessor.setChannelLevels(surroundLevels);
    m_ringBuffer.configure(m_mixChannelCount, AppConstants::kSessionRingBufferFrames);

    if (!sinkDeviceId.isEmpty()) {
        QString routeError;
        if (!AudioPolicyRouter::routeProcessToDevice(processId, sinkDeviceId, &routeError)) {
            const QString message = QStringLiteral("Could not route app audio to sink: %1").arg(routeError);
            if (errorMessage) {
                *errorMessage = message;
            }
            m_processId = 0;
            return false;
        }
        m_routingApplied = true;
        Q_UNUSED(sinkDeviceName);
    }

    m_stopRequested.store(false);
    m_running.store(true);
    m_thread = std::thread(&EqAudioSession::threadMain, this);
    return true;
}

void EqAudioSession::clearRoutingIfApplied()
{
    if (!m_routingApplied || m_processId == 0) {
        return;
    }

    QString clearError;
    AudioPolicyRouter::clearProcessRouting(m_processId, &clearError);
    m_routingApplied = false;
}

void EqAudioSession::finishThread(unsigned long processId)
{
    clearRoutingIfApplied();
    m_running.store(false);

    if (m_onThreadFinished) {
        m_onThreadFinished(processId);
    }
}

void EqAudioSession::stop()
{
    if (!m_running.load() && !m_thread.joinable()) {
        return;
    }

    m_stopRequested.store(true);
    if (m_thread.joinable()) {
        m_thread.join();
    }

    clearRoutingIfApplied();

    m_ringBuffer.clear();
    m_processId = 0;
    m_running.store(false);
    m_onThreadFinished = nullptr;
}

void EqAudioSession::setGains(const std::array<float, EqProcessor::kBandCount> &gainsDb)
{
    m_eqProcessor.setGains(gainsDb);
}

void EqAudioSession::setSurroundEnabled(bool enabled)
{
    m_surroundProcessor.setEnabled(enabled);
}

void EqAudioSession::setSurroundChannelLevels(const std::array<int, SurroundProcessor::kChannelCount> &levels)
{
    m_surroundProcessor.setChannelLevels(levels);
}

void EqAudioSession::threadMain()
{
    const QString tag = QStringLiteral("EqAudioSession");
    const unsigned long processId = m_processId;

    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool comInitializedOnThread = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        finishThread(processId);
        return;
    }

    ProcessLoopbackCapture capture;
    QString errorMessage;
    if (!capture.open(m_processId, &errorMessage)) {
        AudioLog::error(tag, errorMessage);
        capture.close();
        finishThread(processId);
        if (comInitializedOnThread) {
            CoUninitialize();
        }
        return;
    }

    m_eqProcessor.setSampleRate(capture.sampleRate());

    const int channelCount = capture.channelCount();
    const float captureRate = capture.sampleRate();
    const bool needsResample = std::fabs(captureRate - m_mixSampleRate) >= 0.5f;

    const int frameChunk = 512;
    std::vector<float> buffer(static_cast<size_t>(frameChunk * channelCount));
    std::vector<float> eqInputBuffer;
    std::vector<float> surroundBuffer(static_cast<size_t>(frameChunk * SurroundProcessor::kChannelCount), 0.f);

    const int maxResampleOutputFrames =
        needsResample ? std::max(1, static_cast<int>(std::floor(frameChunk * captureRate / m_mixSampleRate))) + 1
                      : frameChunk;
    const int maxPipelineChannels = std::max({channelCount,
                                              SurroundProcessor::kChannelCount,
                                              m_mixChannelCount});
    std::vector<float> resampledBuffer(
        static_cast<size_t>(maxResampleOutputFrames * maxPipelineChannels), 0.f);
    std::vector<float> mixFormatBuffer(
        static_cast<size_t>(maxResampleOutputFrames * m_mixChannelCount), 0.f);

    const bool feedSpectrum = m_spectrumCapture && m_spectrumProcessId;
    if (feedSpectrum) {
        m_spectrumCapture->setSampleRate(static_cast<int>(captureRate));
    }

    int processCheckCounter = 0;
    constexpr int kProcessCheckInterval = 100;

    while (!m_stopRequested.load()) {
        if (++processCheckCounter >= kProcessCheckInterval) {
            processCheckCounter = 0;
            if (!ProcessLoopbackCapture::isProcessRunning(m_processId)) {
                AudioLog::info(tag, QStringLiteral("Target process exited (pid=%1)").arg(m_processId));
                break;
            }
        }

        int framesRead = 0;
        if (!capture.read(buffer.data(), frameChunk, &framesRead, &errorMessage)) {
            AudioLog::error(tag, errorMessage);
            break;
        }

        if (framesRead == 0) {
            Sleep(5);
            continue;
        }

        const bool feedSpectrumThisChunk = feedSpectrum && m_spectrumProcessId->load() == m_processId;
        if (feedSpectrumThisChunk) {
            eqInputBuffer.resize(static_cast<size_t>(framesRead * channelCount));
            std::memcpy(eqInputBuffer.data(),
                        buffer.data(),
                        static_cast<size_t>(framesRead * channelCount) * sizeof(float));
        }

        m_eqProcessor.process(buffer.data(), framesRead, channelCount);

        if (feedSpectrumThisChunk) {
            m_spectrumCapture->pushBeforeAndAfter(eqInputBuffer.data(),
                                                  buffer.data(),
                                                  framesRead,
                                                  channelCount);
        }

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
                                           m_mixSampleRate,
                                           &resampledBuffer);
            writeBuffer = resampledBuffer.data();
        }

        if (writeChannelCount != m_mixChannelCount) {
            upmixChannels(writeBuffer, framesToWrite, writeChannelCount, m_mixChannelCount, &mixFormatBuffer);
            writeBuffer = mixFormatBuffer.data();
        }

        m_ringBuffer.write(writeBuffer, framesToWrite);
    }

    capture.close();
    finishThread(processId);

    if (comInitializedOnThread) {
        CoUninitialize();
    }
}
