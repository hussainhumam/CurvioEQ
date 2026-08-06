#pragma once

#include <QString>
#include <audioclient.h>
#include <wtypes.h>

class ProcessLoopbackCapture
{
public:
    ProcessLoopbackCapture() = default;
    ~ProcessLoopbackCapture();

    ProcessLoopbackCapture(const ProcessLoopbackCapture &) = delete;
    ProcessLoopbackCapture &operator=(const ProcessLoopbackCapture &) = delete;

    bool open(unsigned long processId, QString *errorMessage);
    void close();

    bool isOpen() const { return m_audioClient != nullptr; }

    float sampleRate() const { return m_sampleRate; }
    int channelCount() const { return m_channelCount; }
    int bytesPerFrame() const { return m_bytesPerFrame; }

    bool read(float *interleavedBuffer, int frameCount, int *framesRead, QString *errorMessage);

private:
    IAudioClient *m_audioClient = nullptr;
    IAudioCaptureClient *m_captureClient = nullptr;
    WAVEFORMATEX *m_format = nullptr;
    float m_sampleRate = 0.f;
    int m_channelCount = 0;
    int m_bytesPerFrame = 0;
};
