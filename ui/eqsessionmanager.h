#pragma once

#include "audio/eqprocessor.h"
#include "audio/surroundprocessor.h"
#include "ui/settingsstore.h"

#include <QColor>
#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>

#include <array>
#include <functional>

class AudioEngine;

struct EqSessionSnapshot {
    unsigned long processId = 0;
    std::array<float, EqProcessor::kBandCount> gains{};
    QString eqOutputDeviceId;
    QString eqOutputDeviceName;
    QString sinkDeviceId;
    QString sinkDeviceName;
    bool surroundEnabled = false;
    std::array<int, SurroundProcessor::kChannelCount> surroundChannelLevels{50, 50, 50, 50, 50, 50, 50, 50};
    QColor labelColor;
    bool active = false;
    bool hasStoredGains = false;
};

struct ConfiguredEqSession {
    unsigned long processId = 0;
    QString displayName;
    bool active = false;
    QColor labelColor;
};

class EqSessionManager : public QObject
{
    Q_OBJECT

public:
    EqSessionManager(AudioEngine *engine, SettingsStore *store, QObject *parent = nullptr);

    void setGainReader(std::function<std::array<float, EqProcessor::kBandCount>()> reader);
    void setSurroundStateReader(std::function<std::pair<bool, std::array<int, SurroundProcessor::kChannelCount>>()> reader);
    void setDisplayNameProvider(std::function<QString(unsigned long)> provider);

    bool isAnyRunning() const;
    bool isRunning(unsigned long processId) const;
    QVector<unsigned long> activeProcessIds() const;
    QHash<unsigned long, QColor> activeSessionColors() const;
    QVector<unsigned long> activeProcessIdsForLabelColor(const QColor &labelColor) const;
    QVector<ConfiguredEqSession> configuredTraySessions() const;

    const EqSessionSnapshot *findSnapshot(unsigned long processId) const;
    EqSessionSnapshot snapshotFor(unsigned long processId) const;

    bool enableForProcess(unsigned long processId, const QColor &labelColor);
    void disableForProcess(unsigned long processId);
    void disableAll();
    bool canRestoreProcess(unsigned long processId) const;
    bool restoreForProcess(unsigned long processId);

    void saveDraftForProcess(unsigned long processId,
                             const std::array<float, EqProcessor::kBandCount> &gains,
                             const std::pair<bool, std::array<int, SurroundProcessor::kChannelCount>> &surround);
    void applySnapshotToUi(unsigned long processId,
                           const std::function<void(const std::array<float, EqProcessor::kBandCount> &)> &applyGains,
                           const std::function<void(bool, const std::array<int, SurroundProcessor::kChannelCount> &)> &applySurround) const;

    void pushLiveGainsForProcess(unsigned long processId);
    void scheduleLiveGainsForProcess(unsigned long processId);
    void pushLiveSurroundForProcess(unsigned long processId);

    void onSessionStopped(unsigned long processId);

signals:
    void eqStateChanged();
    void logMessage(const QString &level, const QString &message);
    void errorOccurred(const QString &title, const QString &message);
    void settingsRequested();
    void controlStateChanged();

private:
    bool resolveDevices(QString *sinkId,
                        QString *sinkName,
                        QString *outputId,
                        QString *outputName,
                        QString *errorTitle,
                        QString *errorMessage);
    QVector<unsigned long> linkedProcessIds(unsigned long processId) const;

    AudioEngine *m_engine = nullptr;
    SettingsStore *m_store = nullptr;
    QHash<unsigned long, EqSessionSnapshot> m_snapshots;
    QTimer *m_gainDebounceTimer = nullptr;
    unsigned long m_pendingGainPid = 0;
    std::function<std::array<float, EqProcessor::kBandCount>()> m_gainReader;
    std::function<std::pair<bool, std::array<int, SurroundProcessor::kChannelCount>>()> m_surroundStateReader;
    std::function<QString(unsigned long)> m_displayNameProvider;
};
