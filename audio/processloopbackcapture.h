#pragma once

#include <QString>
#include <audioclient.h>
#include <cstdint>
#include <vector>
#include <wtypes.h>

class ProcessLoopbackCapture
{
public:
    ProcessLoopbackCapture() = default;
    ~ProcessLoopbackCapture();

    ProcessLoopbackCapture(const ProcessLoopbackCapture &) = delete;
    ProcessLoopbackCapture &operator=(const ProcessLoopbackCapture &) = delete;

    bool open(unsigned long processId, float preferredSampleRate = 0.f, QString *errorMessage = nullptr);
    void close();

    float sampleRate() const { return m_sampleRate; }
    int channelCount() const { return m_channelCount; }

    bool read(float *interleavedBuffer, int frameCount, int *framesRead, QString *errorMessage);

    uint64_t lastDevicePosition() const { return m_lastDevicePosition; }
    uint64_t lastQpcPosition() const { return m_lastQpcPosition; }
    bool deviceLost() const { return m_deviceLost; }

    static bool isProcessRunning(unsigned long processId);

private:
    bool appendPacketFrames(const BYTE *data,
                            UINT32 numFramesAvailable,
                            DWORD flags,
                            QString *errorMessage);
    bool copyPendingFrames(float *interleavedBuffer, int frameCount, int *totalFramesRead);

    IAudioClient *m_audioClient = nullptr;
    IAudioCaptureClient *m_captureClient = nullptr;
    WAVEFORMATEX *m_format = nullptr;
    float m_sampleRate = 0.f;
    int m_channelCount = 0;
    int m_bytesPerFrame = 0;
    std::vector<float> m_pendingFrames;
    size_t m_pendingRead = 0;
    uint64_t m_lastDevicePosition = 0;
    uint64_t m_lastQpcPosition = 0;
    uint64_t m_capturedFrameCounter = 0;
    bool m_deviceLost = false;
};
