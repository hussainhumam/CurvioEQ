#pragma once

#include "audiochainorder.h"
#include "engineiosettings.h"
#include "eqprocessor.h"
#include "eqstate.h"
#include "virtualsurroundsettings.h"
#include "dynamicrangesettings.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

class EqAudioSession;
class Vst3Plugin;
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
                      const EqState &eqState,
                      const VirtualSurroundSettings &virtualSurround,
                      const DynamicRangeSettings &dynamicRange,
                      const AudioChainOrder &audioChainOrder,
                      const QString &eqOutputDeviceId,
                      const QString &sinkDeviceId,
                      bool muteRoutingSink,
                      QString *errorMessage);

    void stopSession(unsigned long processId);
    void stop();
    void pruneEndedSessions();
    void maintainActiveSessionRouting();

    void setSessionEqState(unsigned long processId, const EqState &eqState);
    void setSessionBalance(unsigned long processId, int balance);
    void setSessionVirtualSurround(unsigned long processId, const VirtualSurroundSettings &settings);
    void setSessionDynamicRange(unsigned long processId, const DynamicRangeSettings &settings);
    void setSessionAudioChainOrder(unsigned long processId, const AudioChainOrder &order);
    void setSessionOutputGain(unsigned long processId, float gain);
    void setOutputLimiterThreshold(float linearPeak);
    void setSessionAddon(unsigned long processId, int slot, std::shared_ptr<Vst3Plugin> plugin);

    void setEngineSettings(const EngineIoSettings &settings);

signals:
    void statusChanged(const QString &message);
    void errorOccurred(const QString &message);
    void sessionStopped(unsigned long processId);
    void deviceInvalidated();

private slots:
    void handleSessionThreadEnded(unsigned long processId, const QString &errorMessage = QString());
    void rebuildAfterInvalidation();
    void reportMixerWriteFailed();

private:
    void mixerThreadMain();
    bool ensureRendererOpen(const QString &eqOutputDeviceId, QString *errorMessage);
    void closeRenderer();
    void requestRebuild();
    void stopAfterInvalidation();
    void publishRenderClock();
    void maybeLogMixerDiagnostics();
    int currentMixChannelCount() const;
    int currentTargetRingFill(int periodFrames) const;

    SpectrumCapture *m_spectrumCapture = nullptr;
    std::atomic<unsigned long> m_spectrumProcessId{0};
    std::atomic<float> m_outputLimiterThreshold{1.f};

    std::unique_ptr<WasapiRenderer> m_renderer;
    QString m_eqOutputDeviceId;
    EngineIoSettings m_ioSettings;
    float m_requestedSampleRate = 48000.f;
    int m_requestedBufferFrames = 16;
    int m_openedBufferFrames = 16;
    OutputFormat m_openedOutputFormat = OutputFormat::Auto;
    ShareMode m_openedShareMode = ShareMode::PreferShared;
    int m_mixChannelCount = 2;
    int m_targetRingFillFrames = 512;

    mutable std::mutex m_sessionsMutex;
    std::mutex m_lifecycleMutex;
    std::vector<std::unique_ptr<EqAudioSession>> m_sessions;
    QHash<unsigned long, bool> m_sessionMuteRoutingSink;
    QHash<unsigned long, QString> m_sessionSinkDeviceIds;
    QHash<unsigned long, float> m_sessionOutputGains;

    std::atomic<bool> m_mixerRunning{false};
    std::atomic<bool> m_mixerStopRequested{false};
    std::atomic<bool> m_rebuildRequested{false};
    std::atomic<unsigned> m_mixerWaitTimeouts{0};
    std::atomic<unsigned> m_mixerRingUnderruns{0};
    std::atomic<unsigned> m_mixerPaddingFull{0};
    std::atomic<uint64_t> m_renderClockFrames{0};
    std::atomic<uint64_t> m_renderClockQpc{0};
    uint64_t m_qpcFrequency = 0;
    std::thread m_mixerThread;

    bool m_comInitialized = false;
};
