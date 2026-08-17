#pragma once

#include <QString>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <audioclient.h>

#include <array>
#include <vector>

class WasapiRenderer
{
public:
    WasapiRenderer() = default;
    ~WasapiRenderer();

    WasapiRenderer(const WasapiRenderer &) = delete;
    WasapiRenderer &operator=(const WasapiRenderer &) = delete;

    bool open(const QString &deviceId, float sampleRate, int channelCount, QString *errorMessage);
    void close();
    void interruptWait();

    bool isOpen() const { return m_audioClient != nullptr; }

    float sampleRate() const { return m_sampleRate; }
    int channelCount() const { return m_channelCount; }
    UINT32 preferredFrameCount() const { return m_periodFrameCount; }
    UINT32 availableWriteFrames() const;

    bool waitForNextPeriod(DWORD timeoutMs);
    bool write(const float *interleavedBuffer, int frameCount, int inputChannelCount, QString *errorMessage);

private:
    void upmixToDeviceFormat(const float *input, int frameCount, int inputChannelCount);
    void buildLogicalChannelMap();
    bool prerollSilence(QString *errorMessage);
    bool copyFramesToDevice(const float *source, int framesToWrite, QString *errorMessage);

    IAudioClient *m_audioClient = nullptr;
    IAudioRenderClient *m_renderClient = nullptr;
    WAVEFORMATEX *m_format = nullptr;
    HANDLE m_bufferEvent = nullptr;
    float m_sampleRate = 0.f;
    int m_channelCount = 0;
    UINT32 m_bufferFrameCount = 0;
    UINT32 m_periodFrameCount = 480;
    bool m_formatIsFloat = true;
    bool m_eventDriven = false;
    std::vector<float> m_upmixBuffer;
    std::array<int, 8> m_logicalToDevice{};
    bool m_hasLogicalChannelMap = false;
};
