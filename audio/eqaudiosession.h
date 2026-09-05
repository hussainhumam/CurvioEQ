#pragma once

#include "audiopipeline.h"
#include "clocksync.h"
#include "eqprocessor.h"
#include "eqstate.h"
#include "resampler.h"
#include "spscringbuffer.h"
#include "audiochainorder.h"
#include "dynamicsprocessor.h"
#include "dynamicrangesettings.h"
#include "loudnessprocessor.h"
#include "spectrumceilinglimiter.h"
#include "virtualsurroundprocessor.h"
#include "virtualsurroundsettings.h"

#include <QString>

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class SpectrumCapture;
class Vst3Plugin;

struct SessionStartConfig {
    unsigned long processId = 0;
    EqState eqState{};
    VirtualSurroundSettings virtualSurround{};
    DynamicRangeSettings dynamicRange{};
    AudioChainOrder audioChainOrder{};
    float mixSampleRate = 48000.f;
    int mixChannelCount = 2;
    QString sinkDeviceId;
    SpectrumCapture *spectrumCapture = nullptr;
    std::atomic<unsigned long> *spectrumProcessId = nullptr;
    std::function<void(unsigned long processId, const QString &errorMessage)> onThreadFinished;
};

class EqAudioSession
{
public:
    EqAudioSession();
    ~EqAudioSession();

    EqAudioSession(const EqAudioSession &) = delete;
    EqAudioSession &operator=(const EqAudioSession &) = delete;

    unsigned long processId() const { return m_processId; }
    bool isRunning() const { return m_running.load(); }
    std::shared_ptr<SpscRingBuffer> ringBuffer() const { return m_ringBuffer; }

    bool start(SessionStartConfig config, QString *errorMessage);
    void stop();

    void maintainRouting();

    void setEqState(const EqState &eqState);
    void setVirtualSurroundSettings(const VirtualSurroundSettings &settings);
    void setDynamicRangeSettings(const DynamicRangeSettings &settings);
    void setAudioChainOrder(const AudioChainOrder &order);
    void setOutputGain(float gain);
    void setOutputLimiterThreshold(float linearPeak);
    void setAddon(int slot, std::shared_ptr<Vst3Plugin> plugin);

    QString sinkDeviceId() const { return m_sinkDeviceId; }
    int routedProcessCount() const { return m_routedProcessCount; }

private:
    struct CaptureBuffers {
        int captureChannelCount = 2;
        float captureRate = 48000.f;
        bool needsResample = false;
        int maxResampleOutputFrames = 512;
        std::vector<float> capture;
        std::vector<float> eqInput;
        std::vector<float> virtualSurround;
        std::vector<float> resampled;
        std::vector<float> mixFormat;
        bool feedSpectrum = false;
        int lastResamplerChannels = 2;
    };

    void threadMain();
    void processCaptureChunk(CaptureBuffers *buffers, int framesRead);
    void clearRoutingIfApplied();
    void finishThread(unsigned long processId, const QString &errorMessage = QString());

    unsigned long m_processId = 0;
    float m_mixSampleRate = 48000.f;
    int m_mixChannelCount = 2;
    bool m_routingApplied = false;
    QString m_sinkDeviceId;
    int m_routedProcessCount = 0;

    EqProcessor m_eqProcessor;
    AudioPipeline m_pipeline;
    VirtualSurroundProcessor m_virtualSurroundProcessor;
    DynamicsProcessor m_dynamicsProcessor;
    LoudnessProcessor m_loudnessProcessor;
    SpectrumCeilingLimiter m_outputLimiter{1.f};
    Resampler m_resampler;
    ClockSync m_clockSync;
    std::shared_ptr<SpscRingBuffer> m_ringBuffer;

    SpectrumCapture *m_spectrumCapture = nullptr;
    std::atomic<unsigned long> *m_spectrumProcessId = nullptr;

    std::atomic<uint64_t> m_audioChainPacked{packAudioChainOrder(defaultAudioChainOrder())};
    std::array<std::shared_ptr<Vst3Plugin>, kAudioChainAddonCount> m_addons;
    std::mutex m_addonMutex;
    std::atomic<float> m_outputGain{1.f};
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    std::function<void(unsigned long processId, const QString &errorMessage)> m_onThreadFinished;
    std::thread m_thread;
};
