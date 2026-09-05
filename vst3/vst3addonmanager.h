#pragma once

#include "vst3addonstore.h"
#include "vst3plugin.h"

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVector>
#include <QWidget>
#include <array>
#include <memory>

class AudioEngine;

class Vst3AddonManager : public QObject
{
    Q_OBJECT

public:
    explicit Vst3AddonManager(AudioEngine *engine, QObject *parent = nullptr);

    void setExtraFolders(const QStringList &folders);
    QStringList extraFolders() const { return m_extraFolders; }

    QVector<Vst3PluginInfo> installedPlugins();
    Vst3AppAddons addonsForExe(const QString &exePath) const;
    std::shared_ptr<Vst3Plugin> plugin(unsigned long processId, int slot) const;

    bool addPlugin(unsigned long processId,
                   const QString &exePath,
                   const Vst3PluginInfo &info,
                   QString *errorMessage);
    void removePlugin(unsigned long processId, const QString &exePath, int slot);
    void setChain(unsigned long processId, const QString &exePath, const AudioChainOrder &chain);
    bool openEditor(unsigned long processId, int slot, QWidget *parent);
    void attachToSession(unsigned long processId, const QString &exePath, float sampleRate);

signals:
    void logMessage(const QString &level, const QString &message);
    void addonsChanged();

private:
    QStringList scanFolders() const;
    void pushToEngine(unsigned long processId, const Vst3AppAddons &addons);
    void persist(unsigned long processId, Vst3AppAddons addons);

    AudioEngine *m_engine = nullptr;
    Vst3AddonStore m_store;
    QStringList m_extraFolders;
    QVector<Vst3PluginInfo> m_cachedScan;
    bool m_scanDirty = true;
    QHash<unsigned long, std::array<std::shared_ptr<Vst3Plugin>, kAudioChainAddonCount>> m_live;
};
