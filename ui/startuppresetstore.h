#pragma once

#include <QHash>
#include <QString>

class StartupPresetStore
{
public:
    StartupPresetStore();

    bool load();
    bool save() const;

    QString presetIdForExe(const QString &exePath) const;
    bool hasBinding(const QString &exePath) const;
    bool setBinding(const QString &exePath, const QString &presetId);
    bool removeBinding(const QString &exePath);

    static QString normalizeExe(const QString &exePath);
    static QString storeFilePath();

private:
    QHash<QString, QString> m_presetIdByExe;
};
