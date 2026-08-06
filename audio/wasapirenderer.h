#pragma once

#include <QString>
#include <audioclient.h>
#include <wtypes.h>

#include <vector>
#include <array>

class WasapiRenderer
{
public:
    WasapiRenderer() = default;
    ~WasapiRenderer();

    WasapiRenderer(const WasapiRenderer &) = delete;
    WasapiRenderer &operator=(const WasapiRenderer &) = delete;

    bool open(const QString &deviceId, float sampleRate, int channelCount, QString *errorMessage);
    void close();

    bool isOpen() const { return m_audioClient != nullptr; }

    float sampleRate() const { return m_sampleRate; }
    int channelCount() const { return m_channelCount; }
    UINT32 preferredFrameCount() const { return m_bufferFrameCount; }

    bool write(const float *interleavedBuffer, int frameCount, int inputChannelCount, QString *errorMessage);

private:
    void upmixToDeviceFormat(const float *input, int frameCount, int inputChannelCount);
    void buildLogicalChannelMap();

    IAudioClient *m_audioClient = nullptr;
    IAudioRenderClient *m_renderClient = nullptr;
    WAVEFORMATEX *m_format = nullptr;
    float m_sampleRate = 0.f;
    int m_channelCount = 0;
    UINT32 m_bufferFrameCount = 0;
    bool m_formatIsFloat = true;
    std::vector<float> m_upmixBuffer;
    std::array<int, 8> m_logicalToDevice{};
    bool m_hasLogicalChannelMap = false;
};
