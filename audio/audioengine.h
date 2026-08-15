#pragma once

#include "eqprocessor.h"
#include "surroundprocessor.h"

#include <QObject>
#include <QString>
#include <QVector>

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

class EqAudioSession;
class WasapiRenderer;
class SpectrumCapture;

class AudioEngine : public QObject
{
    Q_OBJECT

public:
    static constexpr int kMaxSessions = 8;

    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine() override;

    bool isRunning() const;
    bool isSessionActive(unsigned long processId) const;
    QVector<unsigned long> activeProcessIds() const;

    void setSpectrumCapture(SpectrumCapture *capture);
    void setSpectrumProcessId(unsigned long processId);

    bool startSession(unsigned long processId,
                      const std::array<float, EqProcessor::kBandCount> &gainsDb,
                      bool surroundEnabled,
                      const std::array<int, SurroundProcessor::kChannelCount> &surroundLevels,
                      const QString &eqOutputDeviceId,
                      const QString &eqOutputDeviceName,
                      const QString &sinkDeviceId,
                      const QString &sinkDeviceName,
                      QString *errorMessage);

    void stopSession(unsigned long processId);
    void stop();
    void pruneEndedSessions();

    void setSessionGains(unsigned long processId, const std::array<float, EqProcessor::kBandCount> &gainsDb);
    void setSessionSurroundEnabled(unsigned long processId, bool enabled);
    void setSessionSurroundChannelLevels(unsigned long processId,
                                         const std::array<int, SurroundProcessor::kChannelCount> &levels);

    // Legacy single-session helpers (delegate to first/only active session)
    void setGains(const std::array<float, EqProcessor::kBandCount> &gainsDb);
    void setSurroundEnabled(bool enabled);
    void setSurroundChannelLevels(const std::array<int, SurroundProcessor::kChannelCount> &levels);

signals:
    void statusChanged(const QString &message);
    void errorOccurred(const QString &message);
    void sessionStopped(unsigned long processId);

private slots:
    void handleSessionThreadEnded(unsigned long processId);

private:
    void mixerThreadMain();
    bool ensureRendererOpen(const QString &eqOutputDeviceId, const QString &eqOutputDeviceName, QString *errorMessage);
    void closeRenderer();
    EqAudioSession *findSession(unsigned long processId);

    SpectrumCapture *m_spectrumCapture = nullptr;
    std::atomic<unsigned long> m_spectrumProcessId{0};

    std::unique_ptr<WasapiRenderer> m_renderer;
    QString m_eqOutputDeviceId;
    QString m_eqOutputDeviceName;

    mutable std::mutex m_sessionsMutex;
    std::vector<std::unique_ptr<EqAudioSession>> m_sessions;

    std::atomic<bool> m_mixerRunning{false};
    std::atomic<bool> m_mixerStopRequested{false};
    std::thread m_mixerThread;

    bool m_comInitialized = false;
};
