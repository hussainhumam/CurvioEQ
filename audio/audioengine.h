#pragma once

#include "eqprocessor.h"
#include "virtualsurroundsettings.h"
#include "dynamicrangesettings.h"

#include <QHash>
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
                      const VirtualSurroundSettings &virtualSurround,
                      const DynamicRangeSettings &dynamicRange,
                      const QString &eqOutputDeviceId,
                      const QString &eqOutputDeviceName,
                      const QString &sinkDeviceId,
                      bool muteRoutingSink,
                      QString *errorMessage);

    void stopSession(unsigned long processId);
    void stop();
    void pruneEndedSessions();
    void maintainActiveSessionRouting();

    void setSessionGains(unsigned long processId, const std::array<float, EqProcessor::kBandCount> &gainsDb);
    void setSessionVirtualSurround(unsigned long processId, const VirtualSurroundSettings &settings);
    void setSessionDynamicRange(unsigned long processId, const DynamicRangeSettings &settings);

signals:
    void statusChanged(const QString &message);
    void errorOccurred(const QString &message);
    void sessionStopped(unsigned long processId);

private slots:
    void handleSessionThreadEnded(unsigned long processId, const QString &errorMessage = QString());

private:
    void mixerThreadMain();
    bool ensureRendererOpen(const QString &eqOutputDeviceId, const QString &eqOutputDeviceName, QString *errorMessage);
    void closeRenderer();

    SpectrumCapture *m_spectrumCapture = nullptr;
    std::atomic<unsigned long> m_spectrumProcessId{0};

    std::unique_ptr<WasapiRenderer> m_renderer;
    QString m_eqOutputDeviceId;
    QString m_eqOutputDeviceName;

    mutable std::mutex m_sessionsMutex;
    std::vector<std::unique_ptr<EqAudioSession>> m_sessions;
    QHash<unsigned long, bool> m_sessionMuteRoutingSink;
    QHash<unsigned long, QString> m_sessionSinkDeviceIds;

    std::atomic<bool> m_mixerRunning{false};
    std::atomic<bool> m_mixerStopRequested{false};
    std::thread m_mixerThread;

    bool m_comInitialized = false;
};
