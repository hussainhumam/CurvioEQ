#pragma once

#include "audio/eqprocessor.h"
#include "audio/surroundprocessor.h"
#include "ui/settingsstore.h"

#include <QObject>
#include <QString>

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
    bool valid = false;
};

class EqSessionController : public QObject
{
    Q_OBJECT

public:
    EqSessionController(AudioEngine *engine, SettingsStore *store, QObject *parent = nullptr);

    void setGainReader(std::function<std::array<float, EqProcessor::kBandCount>()> reader);
    void setSurroundStateReader(std::function<std::pair<bool, std::array<int, SurroundProcessor::kChannelCount>>()> reader);
    void setSelectedProcessIdProvider(std::function<unsigned long()> provider);
    void setDisplayNameProvider(std::function<QString(unsigned long)> provider);

    bool isRunning() const;
    unsigned long activePid() const;
    const EqSessionSnapshot &snapshot() const;

    bool enableEq();
    void disableEq();
    void invalidateSnapshot();
    void resetBandGains();
    void notifyEngineStopped();

signals:
    void eqStateChanged(bool running, unsigned long pid, const QString &appName);
    void logMessage(const QString &level, const QString &message);
    void errorOccurred(const QString &title, const QString &message);
    void settingsRequested();
    void controlStateChanged();

private:
    bool resumeFromSnapshot();
    bool startForSelectedApp();
    void saveSnapshot();
    void applySurroundFromReader();
    bool resolveDevices(QString *sinkId,
                        QString *sinkName,
                        QString *outputId,
                        QString *outputName,
                        QString *errorTitle,
                        QString *errorMessage);

    AudioEngine *m_engine = nullptr;
    SettingsStore *m_store = nullptr;
    EqSessionSnapshot m_snapshot;
    unsigned long m_activePid = 0;
    std::function<std::array<float, EqProcessor::kBandCount>()> m_gainReader;
    std::function<std::pair<bool, std::array<int, SurroundProcessor::kChannelCount>>()> m_surroundStateReader;
    std::function<unsigned long()> m_selectedProcessIdProvider;
    std::function<QString(unsigned long)> m_displayNameProvider;
};
