#include "apppaths.h"

#include "appconstants.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>

QString AppPaths::dataRoot()
{
    QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (root.isEmpty()) {
        root = QDir::homePath() + QStringLiteral("/AppData/Roaming/")
               + QString::fromLatin1(AppConstants::kAppId);
    }

    const QString settingsPath = QDir(root).filePath(QStringLiteral("settings.json"));
    if (!QFile::exists(settingsPath)) {
        const QString legacyRoot = QDir::homePath() + QStringLiteral("/AppData/Roaming/PerAppEQ");
        const QString legacySettings = QDir(legacyRoot).filePath(QStringLiteral("settings.json"));
        if (QFile::exists(legacySettings)) {
            QDir().mkpath(root);
            QFile::copy(legacySettings, settingsPath);
        }
    }

    return root;
}

QString AppPaths::soundModsRoot()
{
    return QDir(dataRoot()).filePath(QStringLiteral("soundmods"));
}
