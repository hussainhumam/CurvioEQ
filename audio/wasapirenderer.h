#pragma once

#include "engineiosettings.h"

#include <QString>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <audioclient.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

class WasapiRenderer
{
public:
    WasapiRenderer() = default;
    ~WasapiRenderer();

    WasapiRenderer(const WasapiRenderer &) = delete;
    WasapiRenderer &operator=(const WasapiRenderer &) = delete;

    bool open(const QString &deviceId, const EngineIoSettings &settings, QString *errorMessage);
    void close();
    void interruptWait();

    bool isOpen() const { return m_audioClient != nullptr; }

    float sampleRate() const { return m_sampleRate; }
    int channelCount() const { return m_channelCount; }
    UINT32 preferredFrameCount() const { return m_periodFrameCount; }
    UINT32 bufferFrameCount() const { return m_bufferFrameCount; }
    UINT32 prerollFrameCount() const { return m_prerollFrameCount; }
    UINT32 lastPaddingFrames() const { return m_lastPaddingFrames; }
    bool lastWaitTimedOut() const { return m_lastWaitTimedOut; }
    bool deviceLost() const { return m_deviceLost; }
    uint64_t generation() const { return m_generation.load(std::memory_order_acquire); }
    UINT32 availableWriteFrames() const;
    bool readClock(uint64_t *frames, uint64_t *qpc) const;

    bool waitForNextPeriod(DWORD timeoutMs);
    bool write(const float *interleavedBuffer, int frameCount, int inputChannelCount, QString *errorMessage);
    bool writePrepared(const float *interleavedBuffer, int frameCount, int inputChannelCount);

private:
    void upmixToDeviceFormat(const float *input, int frameCount, int inputChannelCount);
    void buildLogicalChannelMap();
    bool prerollSilence(QString *errorMessage);
    bool copyFramesToDevice(const float *source, int framesToWrite, QString *errorMessage);

    IAudioClient *m_audioClient = nullptr;
    IAudioRenderClient *m_renderClient = nullptr;
    IAudioClock *m_audioClock = nullptr;
    WAVEFORMATEX *m_format = nullptr;
    HANDLE m_bufferEvent = nullptr;
    float m_sampleRate = 0.f;
    int m_channelCount = 0;
    UINT32 m_bufferFrameCount = 0;
    UINT32 m_periodFrameCount = 480;
    UINT32 m_prerollFrameCount = 0;
    bool m_formatIsFloat = true;
    bool m_eventDriven = false;
    mutable UINT32 m_lastPaddingFrames = 0;
    bool m_lastWaitTimedOut = false;
    mutable bool m_deviceLost = false;
    std::atomic<uint64_t> m_generation{0};
    std::vector<float> m_upmixBuffer;
    std::array<int, 8> m_logicalToDevice{};
    bool m_hasLogicalChannelMap = false;
};
