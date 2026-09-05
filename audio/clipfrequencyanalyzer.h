#pragma once

#include <QObject>
#include <QString>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

class ClipFrequencyAnalyzer : public QObject
{
    Q_OBJECT

public:
    explicit ClipFrequencyAnalyzer(QObject *parent = nullptr);
    ~ClipFrequencyAnalyzer() override;

    ClipFrequencyAnalyzer(const ClipFrequencyAnalyzer &) = delete;
    ClipFrequencyAnalyzer &operator=(const ClipFrequencyAnalyzer &) = delete;

    bool isRecording() const { return m_recording.load(); }
    unsigned long recordingProcessId() const { return m_processId; }
    QString recordingDisplayName() const;

    void start(unsigned long processId, const QString &displayName);
    void stopAndAnalyze();

signals:
    void logMessage(const QString &level, const QString &message);
    void recordingChanged(bool recording, unsigned long processId, const QString &displayName);

private slots:
    void onCaptureFinished();

private:
    void captureThreadMain(unsigned long processId);
    void emitRecordingChanged();
    QString analyzeClip() const;

    std::thread m_thread;
    mutable std::mutex m_mutex;
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_recording{false};
    unsigned long m_processId = 0;
    QString m_displayName;
    std::vector<float> m_mono;
    float m_sampleRate = 48000.f;
    QString m_captureError;
};
