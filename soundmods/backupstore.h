#pragma once

#include "soundmodtypes.h"

#include <QString>
#include <QVector>

class BackupStore
{
public:
    bool ensureBackup(const QString &gameId, const QString &sourcePath, const QString &relativePath, QString *errorMessage);
    bool restoreFile(const QString &gameId, const QString &relativePath, const QString &targetPath, QString *errorMessage);
    bool hasBackup(const QString &gameId, const QString &relativePath) const;
    QString backupPathFor(const QString &gameId, const QString &relativePath) const;
};
