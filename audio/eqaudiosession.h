#pragma once

#include "audiopipeline.h"
#include "clocksync.h"
#include "eqprocessor.h"
#include "resampler.h"
#include "spscringbuffer.h"
#include "surroundprocessor.h"

#include <QString>

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

class SpectrumCapture;

struct SessionStartConfig {
    unsigned long processId = 0;
    std::array<float, EqProcessor::kBandCount> gainsDb{};
    bool surroundEnabled = false;
    std::array<int, SurroundProcessor::kChannelCount> surroundLevels{50, 50, 50, 50, 50, 50, 50, 50};
    float mixSampleRate = 48000.f;
    int mixChannelCount = 2;
    QString sinkDeviceId;
    SpectrumCapture *spectrumCapture = nullptr;
    std::atomic<unsigned long> *spectrumProcessId = nullptr;
    std::function<void(unsigned long)> onThreadFinished;
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

    void setGains(const std::array<float, EqProcessor::kBandCount> &gainsDb);
    void setSurroundEnabled(bool enabled);
    void setSurroundChannelLevels(const std::array<int, SurroundProcessor::kChannelCount> &levels);

private:
    struct CaptureBuffers {
        int captureChannelCount = 2;
        float captureRate = 48000.f;
        bool needsResample = false;
        int maxResampleOutputFrames = 512;
        std::vector<float> capture;
        std::vector<float> eqInput;
        std::vector<float> surround;
        std::vector<float> resampled;
        std::vector<float> mixFormat;
        bool feedSpectrum = false;
        int lastResamplerChannels = 2;
    };

    void threadMain();
    void processCaptureChunk(CaptureBuffers *buffers, int framesRead);
    void clearRoutingIfApplied();
    void finishThread(unsigned long processId);

    unsigned long m_processId = 0;
    float m_mixSampleRate = 48000.f;
    int m_mixChannelCount = 2;
    bool m_routingApplied = false;

    EqProcessor m_eqProcessor;
    AudioPipeline m_pipeline;
    SurroundProcessor m_surroundProcessor;
    Resampler m_resampler;
    ClockSync m_clockSync;
    std::shared_ptr<SpscRingBuffer> m_ringBuffer;

    SpectrumCapture *m_spectrumCapture = nullptr;
    std::atomic<unsigned long> *m_spectrumProcessId = nullptr;

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    std::function<void(unsigned long)> m_onThreadFinished;
    std::thread m_thread;
};
