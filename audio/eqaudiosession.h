#pragma once

#include "audioringbuffer.h"
#include "eqprocessor.h"
#include "surroundprocessor.h"

#include <QString>
#include <array>
#include <atomic>
#include <functional>
#include <thread>

class SpectrumCapture;

class EqAudioSession
{
public:
    EqAudioSession() = default;
    ~EqAudioSession();

    EqAudioSession(const EqAudioSession &) = delete;
    EqAudioSession &operator=(const EqAudioSession &) = delete;

    unsigned long processId() const { return m_processId; }
    bool isRunning() const { return m_running.load(); }
    AudioRingBuffer *ringBuffer() { return &m_ringBuffer; }

    bool start(unsigned long processId,
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
               QString *errorMessage);

    void stop();

    void setGains(const std::array<float, EqProcessor::kBandCount> &gainsDb);
    void setSurroundEnabled(bool enabled);
    void setSurroundChannelLevels(const std::array<int, SurroundProcessor::kChannelCount> &levels);

private:
    void threadMain();
    void clearRoutingIfApplied();
    void finishThread(unsigned long processId);

    unsigned long m_processId = 0;
    float m_mixSampleRate = 48000.f;
    int m_mixChannelCount = 2;
    QString m_sinkDeviceId;
    bool m_routingApplied = false;

    EqProcessor m_eqProcessor;
    SurroundProcessor m_surroundProcessor;
    AudioRingBuffer m_ringBuffer;

    SpectrumCapture *m_spectrumCapture = nullptr;
    std::atomic<unsigned long> *m_spectrumProcessId = nullptr;

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    std::function<void(unsigned long)> m_onThreadFinished;
    std::thread m_thread;
};
