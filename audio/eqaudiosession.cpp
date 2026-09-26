#include "eqaudiosession.h"

#include "audiopolicyrouter.h"
#include "audiothreadutils.h"
#include "audiosessionvolume.h"
#include "engineiosettings.h"
#include "log.h"
#include "processloopbackcapture.h"
#include "vst3/vst3plugin.h"
#include "ui/appconstants.h"
#include "ui/spectrumanalyzer.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <vector>

namespace {

constexpr int kFrameChunk = 512;

void upmixChannels(const float *input,
                   int frameCount,
                   int inputChannels,
                   int outputChannels,
                   std::vector<float> *output)
{
    if (!input || !output || frameCount <= 0 || inputChannels <= 0 || outputChannels <= 0) {
        return;
    }

    const int maxFrames = static_cast<int>(output->size() / static_cast<size_t>(outputChannels));
    const int frames = std::min(frameCount, maxFrames);
    if (frames <= 0) {
        return;
    }

    const size_t outputSampleCount = static_cast<size_t>(frames * outputChannels);
    std::fill(output->begin(), output->begin() + static_cast<ptrdiff_t>(outputSampleCount), 0.f);

    for (int frame = 0; frame < frames; ++frame) {
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
    , m_wakeEvent(CreateEventW(nullptr, FALSE, FALSE, nullptr))
{
    m_pipeline.addProcessor(&m_eqProcessor);
}

EqAudioSession::~EqAudioSession()
{
    stop();
    if (m_wakeEvent) {
        CloseHandle(static_cast<HANDLE>(m_wakeEvent));
        m_wakeEvent = nullptr;
    }
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

    if (config.processId == GetCurrentProcessId()) {
        const QString message = QStringLiteral("Cannot enable EQ on CurvioEQ itself");
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    }

    if (config.sinkDeviceId.isEmpty()) {
        const QString message = QStringLiteral("No routing sink device configured");
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    }

    m_processId = config.processId;
    m_mixSampleRate = config.mixSampleRate;
    m_mixChannelCount = std::max(1, config.mixChannelCount);
    m_enginePeriodFrames = config.enginePeriodFrames;
    m_ioSettings = config.io;
    m_bufferFrames = AppConstants::effectiveRingFrames(config.bufferFrames, m_enginePeriodFrames);
    m_captureChunkFrames = AppConstants::captureChunkFramesForPeriod(m_bufferFrames, m_enginePeriodFrames);
    m_targetFillFrames = applySafetyFill(
        AppConstants::ringTargetFillFramesForPeriod(m_bufferFrames, m_enginePeriodFrames),
        m_ioSettings.safetyExtraFrames(),
        m_bufferFrames);
    m_highFillFrames = AppConstants::ringHighFillFramesForPeriod(m_bufferFrames, m_enginePeriodFrames);
    m_spectrumCapture = config.spectrumCapture;
    m_spectrumProcessId = config.spectrumProcessId;
    m_renderClockFrames = config.renderClockFrames;
    m_renderClockQpc = config.renderClockQpc;
    m_qpcFrequency = config.qpcFrequency;
    m_prerollFrames = config.prerollFrames;
    m_onDeviceInvalidated = std::move(config.onDeviceInvalidated);
    m_onThreadFinished = std::move(config.onThreadFinished);
    m_routingApplied = false;
    m_mixPaused.store(false, std::memory_order_release);

    m_audioChainPacked.store(packAudioChainOrder(config.audioChainOrder), std::memory_order_release);
    m_eqProcessor.setEqState(config.eqState);
    m_virtualSurroundProcessor.setEnabled(config.virtualSurround.enabled);
    m_virtualSurroundProcessor.setPreset(config.virtualSurround.presetId);
    m_virtualSurroundProcessor.setStrength(config.virtualSurround.strength);
    m_virtualSurroundProcessor.setChannelLevels(config.virtualSurround.channelLevels);
    m_virtualSurroundProcessor.setSampleRate(config.mixSampleRate);
    m_dynamicsProcessor.setEnabled(config.dynamicRange.enabled);
    m_dynamicsProcessor.setAmount(config.dynamicRange.amount);
    m_dynamicsProcessor.setSampleRate(config.mixSampleRate);
    m_loudnessProcessor.setEnabled(config.dynamicRange.enabled);
    m_loudnessProcessor.setAmount(config.dynamicRange.loudnessAmount);
    m_loudnessProcessor.setSampleRate(config.mixSampleRate);
    m_outputLimiter.setSampleRate(config.mixSampleRate);
    m_ringBuffer->configure(m_mixChannelCount, m_bufferFrames);
    m_clockSync.configure(m_ringBuffer->capacityFrames(),
                          static_cast<size_t>(m_targetFillFrames),
                          static_cast<size_t>(m_highFillFrames));
    m_clockSync.setPllEnabled(m_ioSettings.driftCorrection);
    m_eqProcessor.setUseDoublePrecision(m_ioSettings.useDoublePrecision());
    m_dynamicsProcessor.setUseDoublePrecision(m_ioSettings.useDoublePrecision());
    m_loudnessProcessor.setUseDoublePrecision(m_ioSettings.useDoublePrecision());
    m_outputLimiter.setUseDoublePrecision(m_ioSettings.useDoublePrecision());

    m_sinkDeviceId = config.sinkDeviceId;
    QString routeError;
    if (!AudioPolicyRouter::routeProcessTreeToDevice(config.processId,
                                                     config.sinkDeviceId,
                                                     &m_routedProcessCount,
                                                     &routeError)) {
        const QString message = QStringLiteral("Could not route app audio to sink: %1").arg(routeError);
        if (errorMessage) {
            *errorMessage = message;
        }
        m_processId = 0;
        m_sinkDeviceId.clear();
        m_routedProcessCount = 0;
        return false;
    }

    if (!AudioPolicyRouter::verifyProcessTreeRouted(config.processId, config.sinkDeviceId)) {
        AudioPolicyRouter::routeProcessTreeToDevice(config.processId,
                                                    config.sinkDeviceId,
                                                    &m_routedProcessCount,
                                                    &routeError);
    }

    if (AudioPolicyRouter::persistedRenderDeviceId(config.processId) != config.sinkDeviceId) {
        const QString message =
            QStringLiteral("App audio routing did not stick. Close and reopen the app, then try EQ again.");
        if (errorMessage) {
            *errorMessage = message;
        }
        AudioPolicyRouter::clearProcessTreeRouting(config.processId);
        m_processId = 0;
        m_sinkDeviceId.clear();
        m_routedProcessCount = 0;
        return false;
    }

    AudioLog::info(QStringLiteral("EqAudioSession"),
                   QStringLiteral("Routed %1 process(es) to sink for pid=%2")
                       .arg(m_routedProcessCount)
                       .arg(config.processId));
    AudioLog::info(QStringLiteral("EqAudioSession"),
                   QStringLiteral("I/O diag: pid=%1 requestedRing=%2 actualRing=%3 period=%4 targetFill=%5 highFill=%6 chunk=%7")
                       .arg(config.processId)
                       .arg(config.bufferFrames)
                       .arg(m_ringBuffer->capacityFrames())
                       .arg(m_enginePeriodFrames)
                       .arg(m_targetFillFrames)
                       .arg(m_highFillFrames)
                       .arg(m_captureChunkFrames));
    m_routingApplied = true;

    m_stopRequested.store(false);
    m_running.store(true);
    m_thread = std::thread(&EqAudioSession::threadMain, this);
    return true;
}

void EqAudioSession::clearRoutingIfApplied()
{
    if (m_processId == 0) {
        return;
    }

    if (!m_routingApplied) {
        m_sinkDeviceId.clear();
        m_routedProcessCount = 0;
        return;
    }

    QString clearError;
    AudioPolicyRouter::clearProcessTreeRouting(m_processId, &clearError);
    m_routingApplied = false;
    m_sinkDeviceId.clear();
    m_routedProcessCount = 0;
}

void EqAudioSession::maintainRouting()
{
    if (m_processId == 0 || !m_routingApplied || m_sinkDeviceId.isEmpty()) {
        return;
    }

    if (AudioPolicyRouter::verifyProcessTreeRouted(m_processId, m_sinkDeviceId)) {
        if (m_mixPaused.exchange(false, std::memory_order_acq_rel)) {
            if (m_ringBuffer) {
                m_ringBuffer->clear();
                m_ringBuffer->bumpGeneration();
            }
            AudioLog::info(QStringLiteral("EqAudioSession"),
                           QStringLiteral("Resumed mix after routing recovered for pid=%1").arg(m_processId));
        }
        return;
    }

    if (!m_mixPaused.exchange(true, std::memory_order_acq_rel)) {
        if (m_ringBuffer) {
            m_ringBuffer->bumpGeneration();
        }
        AudioLog::warn(QStringLiteral("EqAudioSession"),
                       QStringLiteral("Paused mix; routing dropped for pid=%1").arg(m_processId));
    }

    int reroutedCount = 0;
    QString routeError;
    if (AudioPolicyRouter::routeProcessTreeToDevice(m_processId, m_sinkDeviceId, &reroutedCount, &routeError)) {
        m_routedProcessCount = reroutedCount;
        AudioLog::warn(QStringLiteral("EqAudioSession"),
                       QStringLiteral("Re-applied sink routing for pid=%1 (%2 process(es))")
                           .arg(m_processId)
                           .arg(reroutedCount));
        if (AudioPolicyRouter::verifyProcessTreeRouted(m_processId, m_sinkDeviceId)) {
            if (m_ringBuffer) {
                m_ringBuffer->clear();
                m_ringBuffer->bumpGeneration();
            }
            m_mixPaused.store(false, std::memory_order_release);
            AudioLog::info(QStringLiteral("EqAudioSession"),
                           QStringLiteral("Resumed mix after re-route for pid=%1").arg(m_processId));
        }
    } else if (!routeError.isEmpty()) {
        AudioLog::warn(QStringLiteral("EqAudioSession"),
                       QStringLiteral("Failed to re-apply sink routing for pid=%1: %2")
                           .arg(m_processId)
                           .arg(routeError));
    }
}

void EqAudioSession::flushOutputRing()
{
    if (m_ringBuffer) {
        m_ringBuffer->clear();
        m_ringBuffer->bumpGeneration();
    }
}

void EqAudioSession::wakeCaptureThread()
{
    if (m_wakeEvent) {
        SetEvent(static_cast<HANDLE>(m_wakeEvent));
    }
}

void EqAudioSession::finishThread(unsigned long processId, const QString &errorMessage)
{
    clearRoutingIfApplied();
    m_running.store(false);

    if (m_onThreadFinished) {
        m_onThreadFinished(processId, errorMessage);
    }
}

void EqAudioSession::stop()
{
    if (!m_running.load() && !m_thread.joinable()) {
        return;
    }

    m_stopRequested.store(true);
    wakeCaptureThread();
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

void EqAudioSession::setEqState(const EqState &eqState)
{
    m_eqProcessor.setEqState(eqState);
}

void EqAudioSession::setBalance(int balance)
{
    m_eqProcessor.setBalance(balance);
}

void EqAudioSession::setVirtualSurroundSettings(const VirtualSurroundSettings &settings)
{
    m_virtualSurroundProcessor.setEnabled(settings.enabled);
    m_virtualSurroundProcessor.setPreset(settings.presetId);
    m_virtualSurroundProcessor.setStrength(settings.strength);
    m_virtualSurroundProcessor.setChannelLevels(settings.channelLevels);
}

void EqAudioSession::setDynamicRangeSettings(const DynamicRangeSettings &settings)
{
    m_dynamicsProcessor.setEnabled(settings.enabled);
    m_dynamicsProcessor.setAmount(settings.amount);
    m_loudnessProcessor.setEnabled(settings.enabled);
    m_loudnessProcessor.setAmount(settings.loudnessAmount);
}

void EqAudioSession::setAudioChainOrder(const AudioChainOrder &order)
{
    m_audioChainPacked.store(packAudioChainOrder(order), std::memory_order_release);
}

void EqAudioSession::setOutputGain(float gain)
{
    const float clamped = std::clamp(gain, 1.f, AudioSessionVolume::kMaxOutputGain);
    m_outputGain.store(clamped, std::memory_order_release);
}

void EqAudioSession::setOutputLimiterThreshold(float linearPeak)
{
    m_outputLimiter.setThreshold(linearPeak);
}

void EqAudioSession::setAddon(int slot, std::shared_ptr<Vst3Plugin> plugin)
{
    if (slot < 0 || slot >= kAudioChainAddonCount) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_addonMutex);
    m_addons[static_cast<size_t>(slot)] = std::move(plugin);
    m_addonSnapshot[static_cast<size_t>(slot)] = m_addons[static_cast<size_t>(slot)];
    if (m_addons[static_cast<size_t>(slot)]) {
        const int blockSize = std::max(m_captureChunkFrames, 512);
        m_addons[static_cast<size_t>(slot)]->setSampleRate(m_mixSampleRate, blockSize);
        AudioLog::info(QStringLiteral("EqAudioSession"),
                       QStringLiteral("I/O diag: pid=%1 vst slot=%2 latency=%3 samples")
                           .arg(m_processId)
                           .arg(slot)
                           .arg(m_addons[static_cast<size_t>(slot)]->latencySamples()));
    }
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

    float *writeBuffer = buffers->capture.data();
    int writeChannelCount = channelCount;
    int framesToWrite = framesRead;

    const AudioChainOrder order = unpackAudioChainOrder(m_audioChainPacked.load(std::memory_order_acquire));
    for (int i = 0; i < order.count; ++i) {
        const AudioChainStage stage = order.stages[static_cast<size_t>(i)];
        switch (stage) {
        case AudioChainStage::Eq:
            m_eqProcessor.process(writeBuffer, framesRead, writeChannelCount);
            break;
        case AudioChainStage::VirtualSurround:
            if (m_virtualSurroundProcessor.isEnabled()) {
                m_virtualSurroundProcessor.process(writeBuffer, buffers->virtualSurround.data(), framesRead);
                writeBuffer = buffers->virtualSurround.data();
                writeChannelCount = 2;
            }
            break;
        case AudioChainStage::Dynamics:
            if (m_dynamicsProcessor.isEnabled()) {
                m_dynamicsProcessor.process(writeBuffer, framesRead, writeChannelCount);
            }
            break;
        case AudioChainStage::Loudness:
            if (m_loudnessProcessor.isEnabled()) {
                m_loudnessProcessor.process(writeBuffer, framesRead, writeChannelCount);
            }
            break;
        case AudioChainStage::Addon0:
        case AudioChainStage::Addon1:
        case AudioChainStage::Addon2:
        case AudioChainStage::Addon3: {
            const int slot = audioChainAddonIndex(stage);
            std::shared_ptr<Vst3Plugin> plugin = m_addonSnapshot[static_cast<size_t>(slot)];
            if (m_addonMutex.try_lock()) {
                plugin = m_addons[static_cast<size_t>(slot)];
                m_addonSnapshot[static_cast<size_t>(slot)] = plugin;
                m_addonMutex.unlock();
            }
            if (plugin) {
                plugin->process(writeBuffer, framesRead, writeChannelCount);
            }
            break;
        }
        }
    }

    m_outputLimiter.process(writeBuffer, framesRead, writeChannelCount);

    if (feedSpectrumThisChunk) {
        m_spectrumCapture->pushBeforeAndAfter(buffers->eqInput.data(),
                                              writeBuffer,
                                              framesRead,
                                              writeChannelCount);
    }

    const size_t fillBefore = m_ringBuffer->availableFrames();
    const double ratio = m_clockSync.rateRatio(fillBefore);
    m_resampler.setRateRatio(ratio);
    const bool applyResample = buffers->needsResample || std::fabs(ratio - 1.0) > 1e-9;

    if (applyResample && writeChannelCount != buffers->lastResamplerChannels) {
        m_resampler.setChannelCount(writeChannelCount);
        buffers->lastResamplerChannels = writeChannelCount;
    }

    if (applyResample) {
        framesToWrite = m_resampler.process(writeBuffer,
                                            framesRead,
                                            buffers->resampled.data(),
                                            buffers->maxResampleOutputFrames);
        writeBuffer = buffers->resampled.data();
    }

    if (writeChannelCount != m_mixChannelCount) {
        upmixChannels(writeBuffer, framesToWrite, writeChannelCount, m_mixChannelCount, &buffers->mixFormat);
        writeBuffer = buffers->mixFormat.data();
        writeChannelCount = m_mixChannelCount;
    }

    const float outputGain = m_outputGain.load(std::memory_order_acquire);
    if (outputGain > 1.001f) {
        const int sampleCount = framesToWrite * writeChannelCount;
        for (int sample = 0; sample < sampleCount; ++sample) {
            writeBuffer[sample] *= outputGain;
        }
    }

    if (m_mixPaused.load(std::memory_order_acquire)) {
        return;
    }

    const int trimmedFrames = m_clockSync.trimmedWriteFrames(framesToWrite, m_ringBuffer->availableFrames());
    if (trimmedFrames <= 0) {
        return;
    }

    m_ringBuffer->write(writeBuffer, trimmedFrames);
}

void EqAudioSession::logMeasuredLatency(float captureRate)
{
    const int period = m_enginePeriodFrames > 0 ? m_enginePeriodFrames : m_captureChunkFrames;
    const int ringFill = m_targetFillFrames;
    const int preroll = m_prerollFrames;
    const int hrtfIr = m_virtualSurroundProcessor.isEnabled() ? m_virtualSurroundProcessor.latencyFrames() : 0;
    const bool limiterOn = m_outputLimiter.threshold() < SpectrumCeilingLimiter::kBypassThreshold;
    const int limiterOla = limiterOn ? SpectrumCeilingLimiter::kHop : 0;
    int vstLatency = 0;
    {
        std::lock_guard<std::mutex> lock(m_addonMutex);
        for (const auto &plugin : m_addons) {
            if (plugin) {
                vstLatency += plugin->latencySamples();
            }
        }
    }
    const int totalFrames = period + ringFill + preroll + hrtfIr + limiterOla + vstLatency;
    const float rate = captureRate > 1.f ? captureRate : m_mixSampleRate;
    const double totalMs = rate > 0.f
                               ? (static_cast<double>(totalFrames) * 1000.0) / static_cast<double>(rate)
                               : 0.0;
    AudioLog::info(QStringLiteral("EqAudioSession"),
                   QStringLiteral("I/O diag: measured latency pid=%1 period=%2 ringFill=%3 preroll=%4 hrtfIr=%5 "
                                  "limiterOla=%6 vst=%7 total=%8 frames (%9 ms)")
                       .arg(m_processId)
                       .arg(period)
                       .arg(ringFill)
                       .arg(preroll)
                       .arg(hrtfIr)
                       .arg(limiterOla)
                       .arg(vstLatency)
                       .arg(totalFrames)
                       .arg(totalMs, 0, 'f', 1));
}

void EqAudioSession::threadMain()
{
    AudioThreadUtils::enableFlushToZero();
    AudioThreadUtils::applyCpuAffinity(m_ioSettings.affinityCoreOrNone());

    HANDLE taskHandle = nullptr;
    if (m_ioSettings.useRealtimeAudio()) {
        taskHandle = AudioThreadUtils::enableProAudioMmcss();
    }

    const QString tag = QStringLiteral("EqAudioSession");
    const unsigned long processId = m_processId;

    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool comInitializedOnThread = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        if (taskHandle) {
            AudioThreadUtils::disableMmcss(taskHandle);
        }
        finishThread(processId);
        return;
    }

    ProcessLoopbackCapture capture;
    QString errorMessage;
    if (!capture.open(m_processId, m_mixSampleRate, &errorMessage)) {
        AudioLog::error(tag, errorMessage);
        capture.close();
        finishThread(processId, errorMessage);
        if (comInitializedOnThread) {
            CoUninitialize();
        }
        if (taskHandle) {
            AudioThreadUtils::disableMmcss(taskHandle);
        }
        return;
    }

    CaptureBuffers buffers;
    buffers.captureChannelCount = capture.channelCount();
    buffers.captureRate = capture.sampleRate();
    buffers.needsResample = std::fabs(buffers.captureRate - m_mixSampleRate) >= 0.5f;
    buffers.lastResamplerChannels = buffers.captureChannelCount;
    AudioLog::info(tag,
                   QStringLiteral("I/O diag: capture=%1 Hz mix=%2 Hz resample=%3 chunk=%4")
                       .arg(buffers.captureRate)
                       .arg(m_mixSampleRate)
                       .arg(buffers.needsResample ? QStringLiteral("yes") : QStringLiteral("no"))
                       .arg(m_captureChunkFrames));

    m_pipeline.setSampleRate(buffers.captureRate);
    m_virtualSurroundProcessor.setSampleRate(buffers.captureRate);
    m_dynamicsProcessor.setSampleRate(buffers.captureRate);
    m_loudnessProcessor.setSampleRate(buffers.captureRate);
    m_outputLimiter.setSampleRate(buffers.captureRate);
    m_resampler.configure(buffers.captureRate, m_mixSampleRate, buffers.captureChannelCount);
    m_resampler.setQuality(m_ioSettings.resampleQuality);
    const int chunkFrames = m_captureChunkFrames > 0 ? m_captureChunkFrames : kFrameChunk;
    {
        std::lock_guard<std::mutex> lock(m_addonMutex);
        for (auto &plugin : m_addons) {
            if (plugin) {
                plugin->setSampleRate(buffers.captureRate, std::max(chunkFrames, 512));
            }
        }
        m_addonSnapshot = m_addons;
    }
    logMeasuredLatency(buffers.captureRate);

    const int maxPipelineChannels = std::max(buffers.captureChannelCount, m_mixChannelCount);
    buffers.capture.assign(static_cast<size_t>(chunkFrames * buffers.captureChannelCount), 0.f);
    if (m_spectrumCapture && m_spectrumProcessId) {
        buffers.eqInput.assign(static_cast<size_t>(chunkFrames * buffers.captureChannelCount), 0.f);
    }
    buffers.virtualSurround.assign(static_cast<size_t>(chunkFrames * 2), 0.f);

    buffers.maxResampleOutputFrames = m_resampler.estimateOutputFrames(chunkFrames) + chunkFrames / 20 + 8;
    buffers.resampled.assign(static_cast<size_t>(buffers.maxResampleOutputFrames * maxPipelineChannels), 0.f);
    buffers.mixFormat.assign(static_cast<size_t>(buffers.maxResampleOutputFrames * m_mixChannelCount), 0.f);

    buffers.feedSpectrum = m_spectrumCapture && m_spectrumProcessId;
    if (buffers.feedSpectrum) {
        m_spectrumCapture->setSampleRate(static_cast<int>(buffers.captureRate));
    }

    const DWORD waitTimeoutMs = buffers.captureRate > 0.f
                                    ? std::max<DWORD>(1, static_cast<DWORD>((static_cast<float>(chunkFrames) * 1000.f)
                                                                            / buffers.captureRate))
                                    : 10;
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
        if (!capture.read(buffers.capture.data(), chunkFrames, &framesRead, &errorMessage)) {
            if (capture.deviceLost()) {
                AudioLog::warn(tag, QStringLiteral("Capture device invalidated (pid=%1)").arg(m_processId));
                errorMessage.clear();
                if (m_onDeviceInvalidated) {
                    m_onDeviceInvalidated();
                }
                while (!m_stopRequested.load()) {
                    if (m_wakeEvent) {
                        WaitForSingleObject(static_cast<HANDLE>(m_wakeEvent), 20);
                    } else {
                        Sleep(20);
                    }
                }
                break;
            }
            AudioLog::error(tag, errorMessage);
            break;
        }

        if (framesRead == 0) {
            if (m_wakeEvent) {
                WaitForSingleObject(static_cast<HANDLE>(m_wakeEvent), waitTimeoutMs);
            } else {
                Sleep(waitTimeoutMs);
            }
            continue;
        }

        m_clockSync.observeCapture(capture.lastDevicePosition(), capture.lastQpcPosition(), m_qpcFrequency);
        if (m_renderClockFrames && m_renderClockQpc) {
            const uint64_t renderFrames = m_renderClockFrames->load(std::memory_order_acquire);
            const uint64_t renderQpc = m_renderClockQpc->load(std::memory_order_acquire);
            m_clockSync.observeRender(renderFrames, renderQpc, m_qpcFrequency);
        }

        if (m_mixPaused.load(std::memory_order_acquire)) {
            continue;
        }

        processCaptureChunk(&buffers, framesRead);
    }

    capture.close();
    finishThread(processId, errorMessage);

    if (comInitializedOnThread) {
        CoUninitialize();
    }
    if (taskHandle) {
        AudioThreadUtils::disableMmcss(taskHandle);
    }
}
