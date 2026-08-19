#include "soundmodpaths.h"

#include "ui/apppaths.h"

#include <QDir>

QString SoundModPaths::soundModsRoot()
{
    return QDir(AppPaths::dataRoot()).filePath(QStringLiteral("soundmods"));
}

QString SoundModPaths::gameRoot(const QString &gameId)
{
    return QDir(soundModsRoot()).filePath(gameId);
}

QString SoundModPaths::profilePath(const QString &gameId)
{
    return QDir(gameRoot(gameId)).filePath(QStringLiteral("profile.json"));
}

QString SoundModPaths::manifestPath(const QString &gameId)
{
    return QDir(gameRoot(gameId)).filePath(QStringLiteral("manifest.json"));
}

QString SoundModPaths::backupRoot(const QString &gameId)
{
    return QDir(gameRoot(gameId)).filePath(QStringLiteral("backup"));
}

QString SoundModPaths::stagingRoot(const QString &gameId)
{
    return QDir(gameRoot(gameId)).filePath(QStringLiteral("staging"));
}
