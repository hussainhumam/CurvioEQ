#include "backupstore.h"

#include "soundmodpaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

QString BackupStore::backupPathFor(const QString &gameId, const QString &relativePath) const
{
    return QDir(SoundModPaths::backupRoot(gameId)).filePath(relativePath);
}

bool BackupStore::hasBackup(const QString &gameId, const QString &relativePath) const
{
    return QFileInfo::exists(backupPathFor(gameId, relativePath));
}

bool BackupStore::ensureBackup(const QString &gameId,
                               const QString &sourcePath,
                               const QString &relativePath,
                               QString *errorMessage)
{
    const QString destination = backupPathFor(gameId, relativePath);
    if (QFileInfo::exists(destination)) {
        return true;
    }

    QDir().mkpath(QFileInfo(destination).absolutePath());
    if (QFile::copy(sourcePath, destination)) {
        return true;
    }

    if (errorMessage) {
        *errorMessage = QStringLiteral("Could not backup %1").arg(relativePath);
    }
    return false;
}

bool BackupStore::restoreFile(const QString &gameId,
                              const QString &relativePath,
                              const QString &targetPath,
                              QString *errorMessage)
{
    const QString backupPath = backupPathFor(gameId, relativePath);
    if (!QFileInfo::exists(backupPath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No backup for %1").arg(relativePath);
        }
        return false;
    }

    QFile::remove(targetPath);
    QDir().mkpath(QFileInfo(targetPath).absolutePath());
    if (QFile::copy(backupPath, targetPath)) {
        return true;
    }

    if (errorMessage) {
        *errorMessage = QStringLiteral("Could not restore %1").arg(relativePath);
    }
    return false;
}
