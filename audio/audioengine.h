#pragma once

#include "eqprocessor.h"
#include "surroundprocessor.h"

#include <QObject>
#include <QString>
#include <array>
#include <atomic>
#include <thread>

class ProcessLoopbackCapture;
class WasapiRenderer;
class SpectrumCapture;

class AudioEngine : public QObject
{
    Q_OBJECT

public:
    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine() override;

    bool isRunning() const { return m_running.load(); }

    void setSpectrumCapture(SpectrumCapture *capture);

    bool start(unsigned long processId,
               const std::array<float, EqProcessor::kBandCount> &gainsDb,
               const QString &eqOutputDeviceId,
               const QString &eqOutputDeviceName,
               const QString &sinkDeviceId,
               const QString &sinkDeviceName,
               QString *errorMessage);
    void stop();
    void setGains(const std::array<float, EqProcessor::kBandCount> &gainsDb);
    void setSurroundEnabled(bool enabled);
    void setSurroundChannelLevels(const std::array<int, SurroundProcessor::kChannelCount> &levels);

signals:
    void statusChanged(const QString &message);
    void errorOccurred(const QString &message);

private:
    void audioThreadMain(unsigned long processId);

    EqProcessor m_eqProcessor;
    SurroundProcessor m_surroundProcessor;
    SpectrumCapture *m_spectrumCapture = nullptr;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    std::thread m_thread;
    unsigned long m_activeProcessId = 0;
    QString m_eqOutputDeviceId;
    QString m_eqOutputDeviceName;
    QString m_sinkDeviceId;
    QString m_sinkDeviceName;
    bool m_routingApplied = false;
    bool m_comInitialized = false;
};
