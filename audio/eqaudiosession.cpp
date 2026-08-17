#include "eqaudiosession.h"

#include "audiopolicyrouter.h"
#include "audiothreadutils.h"
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
        const size_t inputBase = static_cast<size_t>(frame * inputChannels);
        const size_t outputBase = static_cast<size_t>(frame * outputChannels);

        if (inputChannels == 2 && outputChannels > 2) {
            (*output)[outputBase] = input[inputBase];
            if (outputChannels > 1) {
                (*output)[outputBase + 1] = input[inputBase + 1];
            }
            continue;
        }

        const int channelsToCopy = std::min(inputChannels, outputChannels);
        for (int channel = 0; channel < channelsToCopy; ++channel) {
            (*output)[outputBase + static_cast<size_t>(channel)] = input[inputBase + static_cast<size_t>(channel)];
        }
    }
}

} // namespace

EqAudioSession::EqAudioSession()
    : m_ringBuffer(std::make_shared<SpscRingBuffer>())
{
    m_pipeline.addProcessor(&m_eqProcessor);
}

EqAudioSession::~EqAudioSession()
{
    stop();
}

bool EqAudioSession::start(SessionStartConfig config, QString *errorMessage)
{
    stop();

    if (config.processId == 0) {
        const QString message = QStringLiteral("Invalid process id");
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    }

    m_processId = config.processId;
    m_mixSampleRate = config.mixSampleRate;
    m_mixChannelCount = std::max(1, config.mixChannelCount);
    m_spectrumCapture = config.spectrumCapture;
    m_spectrumProcessId = config.spectrumProcessId;
    m_onThreadFinished = std::move(config.onThreadFinished);
    m_routingApplied = false;

    m_eqProcessor.setGains(config.gainsDb);
    m_surroundProcessor.setEnabled(config.surroundEnabled);
    m_surroundProcessor.setChannelLevels(config.surroundLevels);
    m_ringBuffer->configure(m_mixChannelCount, AppConstants::kSessionRingBufferFrames);
    m_clockSync.configure(AppConstants::kSessionRingBufferFrames,
                          AppConstants::kTargetRingFillFrames,
                          AppConstants::kHighRingFillFrames);

    if (!config.sinkDeviceId.isEmpty()) {
        QString routeError;
        if (!AudioPolicyRouter::routeProcessToDevice(config.processId, config.sinkDeviceId, &routeError)) {
            const QString message = QStringLiteral("Could not route app audio to sink: %1").arg(routeError);
            if (errorMessage) {
                *errorMessage = message;
            }
            m_processId = 0;
            return false;
        }
        m_routingApplied = true;
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

    if (m_ringBuffer && m_ringBuffer.use_count() == 1) {
        m_ringBuffer->clear();
    }
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

void EqAudioSession::processCaptureChunk(CaptureBuffers *buffers, int framesRead)
{
    if (!buffers || framesRead <= 0 || !m_ringBuffer) {
        return;
    }

    const int channelCount = buffers->captureChannelCount;
    const bool feedSpectrumThisChunk =
        buffers->feedSpectrum && m_spectrumProcessId && m_spectrumProcessId->load() == m_processId;

    if (feedSpectrumThisChunk) {
        std::memcpy(buffers->eqInput.data(),
                    buffers->capture.data(),
                    static_cast<size_t>(framesRead * channelCount) * sizeof(float));
    }

    m_pipeline.process(buffers->capture.data(), framesRead, channelCount);

    if (feedSpectrumThisChunk) {
        m_spectrumCapture->pushBeforeAndAfter(buffers->eqInput.data(),
                                              buffers->capture.data(),
                                              framesRead,
                                              channelCount);
    }

    const float *writeBuffer = buffers->capture.data();
    int writeChannelCount = channelCount;
    int framesToWrite = framesRead;

    if (m_surroundProcessor.isEnabled()) {
        m_surroundProcessor.process(buffers->capture.data(), buffers->surround.data(), framesRead);
        writeBuffer = buffers->surround.data();
        writeChannelCount = SurroundProcessor::kChannelCount;
    }

    if (buffers->needsResample && writeChannelCount != buffers->lastResamplerChannels) {
        m_resampler.setChannelCount(writeChannelCount);
        buffers->lastResamplerChannels = writeChannelCount;
    }

    if (buffers->needsResample) {
        framesToWrite = m_resampler.process(writeBuffer,
                                            framesRead,
                                            buffers->resampled.data(),
                                            buffers->maxResampleOutputFrames);
        writeBuffer = buffers->resampled.data();
    }

    if (writeChannelCount != m_mixChannelCount) {
        upmixChannels(writeBuffer, framesToWrite, writeChannelCount, m_mixChannelCount, &buffers->mixFormat);
        writeBuffer = buffers->mixFormat.data();
    }

    if (m_clockSync.shouldSkipWrite(m_ringBuffer->availableFrames())) {
        return;
    }

    m_ringBuffer->write(writeBuffer, framesToWrite);
}

void EqAudioSession::threadMain()
{
    AudioThreadUtils::enableFlushToZero();

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
    if (!capture.open(m_processId, m_mixSampleRate, &errorMessage)) {
        AudioLog::error(tag, errorMessage);
        capture.close();
        finishThread(processId);
        if (comInitializedOnThread) {
            CoUninitialize();
        }
        return;
    }

    CaptureBuffers buffers;
    buffers.captureChannelCount = capture.channelCount();
    buffers.captureRate = capture.sampleRate();
    buffers.needsResample = std::fabs(buffers.captureRate - m_mixSampleRate) >= 0.5f;
    buffers.lastResamplerChannels = buffers.captureChannelCount;

    m_pipeline.setSampleRate(buffers.captureRate);
    m_resampler.configure(buffers.captureRate, m_mixSampleRate, buffers.captureChannelCount);

    constexpr int kFrameChunk = 512;
    buffers.capture.assign(static_cast<size_t>(kFrameChunk * buffers.captureChannelCount), 0.f);
    if (m_spectrumCapture && m_spectrumProcessId) {
        buffers.eqInput.assign(static_cast<size_t>(kFrameChunk * buffers.captureChannelCount), 0.f);
    }
    buffers.surround.assign(static_cast<size_t>(kFrameChunk * SurroundProcessor::kChannelCount), 0.f);

    buffers.maxResampleOutputFrames =
        buffers.needsResample ? m_resampler.estimateOutputFrames(kFrameChunk) + 1 : kFrameChunk;
    const int maxPipelineChannels = std::max({buffers.captureChannelCount,
                                              SurroundProcessor::kChannelCount,
                                              m_mixChannelCount});
    buffers.resampled.assign(static_cast<size_t>(buffers.maxResampleOutputFrames * maxPipelineChannels), 0.f);
    buffers.mixFormat.assign(static_cast<size_t>(buffers.maxResampleOutputFrames * m_mixChannelCount), 0.f);

    buffers.feedSpectrum = m_spectrumCapture && m_spectrumProcessId;
    if (buffers.feedSpectrum) {
        m_spectrumCapture->setSampleRate(static_cast<int>(buffers.captureRate));
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
        if (!capture.read(buffers.capture.data(), kFrameChunk, &framesRead, &errorMessage)) {
            AudioLog::error(tag, errorMessage);
            break;
        }

        if (framesRead == 0) {
            Sleep(1);
            continue;
        }

        processCaptureChunk(&buffers, framesRead);
    }

    capture.close();
    finishThread(processId);

    if (comInitializedOnThread) {
        CoUninitialize();
    }
}
