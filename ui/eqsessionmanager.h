#pragma once

#include "audio/eqprocessor.h"
#include "audio/eqstate.h"
#include "audio/dynamicrangesettings.h"
#include "audio/virtualsurroundsettings.h"
#include "ui/settingsstore.h"

#include <QColor>
#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>

#include <functional>

class AudioEngine;

struct EqSessionSnapshot {
    unsigned long processId = 0;
    EqState eq{};
    QString eqOutputDeviceId;
    QString eqOutputDeviceName;
    QString sinkDeviceId;
    QString sinkDeviceName;
    VirtualSurroundSettings virtualSurround{};
    DynamicRangeSettings dynamicRange{};
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

    void setEqStateReader(std::function<EqState()> reader);
    void setSurroundStateReader(std::function<VirtualSurroundSettings()> reader);
    void setDynamicsStateReader(std::function<DynamicRangeSettings()> reader);
    void setDisplayNameProvider(std::function<QString(unsigned long)> provider);

    bool isAnyRunning() const;
    bool isRunning(unsigned long processId) const;
    QVector<unsigned long> activeProcessIds() const;
    QHash<unsigned long, QColor> activeSessionColors() const;
    QVector<unsigned long> activeProcessIdsForLabelColor(const QColor &labelColor) const;
    QVector<ConfiguredEqSession> configuredTraySessions() const;

    const EqSessionSnapshot *findSnapshot(unsigned long processId) const;
    EqSessionSnapshot snapshotFor(unsigned long processId) const;

    bool enableForProcess(unsigned long processId);
    void disableForProcess(unsigned long processId);
    void disableAll();
    bool canRestoreProcess(unsigned long processId) const;
    bool restoreForProcess(unsigned long processId);

    void saveDraftForProcess(unsigned long processId,
                             const EqState &eqState,
                             const VirtualSurroundSettings &virtualSurround,
                             const DynamicRangeSettings &dynamicRange);
    void applySnapshotToUi(unsigned long processId,
                           const std::function<void(const EqState &)> &applyEq,
                           const std::function<void(const VirtualSurroundSettings &)> &applySurround,
                           const std::function<void(const DynamicRangeSettings &)> &applyDynamics) const;

    void pushLiveGainsForProcess(unsigned long processId);
    void scheduleLiveGainsForProcess(unsigned long processId);
    void pushLiveSurroundForProcess(unsigned long processId);
    void pushLiveDynamicsForProcess(unsigned long processId);

    void onSessionStopped(unsigned long processId);

signals:
    void eqStateChanged();
    void logMessage(const QString &level, const QString &message);
    void errorOccurred(const QString &title, const QString &message);
    void settingsRequested();
    void controlStateChanged();

private:
    QColor allocateLabelColor(unsigned long processId) const;
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
    QTimer *m_routingWatchdogTimer = nullptr;
    unsigned long m_pendingGainPid = 0;
    std::function<EqState()> m_eqStateReader;
    std::function<VirtualSurroundSettings()> m_surroundStateReader;
    std::function<DynamicRangeSettings()> m_dynamicsStateReader;
    std::function<QString(unsigned long)> m_displayNameProvider;
};
