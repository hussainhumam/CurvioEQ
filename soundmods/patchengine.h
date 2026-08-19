#pragma once

#include "soundmodtypes.h"

#include <QString>
#include <QVector>

struct PatchApplyResult {
    bool success = false;
    QString message;
    QStringList failedFiles;
    int patchedCount = 0;
    int restoredCount = 0;
};

class PatchEngine
{
public:
    PatchApplyResult apply(const GameIdentity &game,
                           const QVector<SoundAssetEntry> &assets,
                           const QString &scanRoot) const;
    PatchApplyResult restoreAll(const GameIdentity &game,
                                const QVector<SoundAssetEntry> &assets,
                                const QString &scanRoot) const;
    PatchApplyResult restoreSelected(const GameIdentity &game,
                                      const QVector<SoundAssetEntry> &assets,
                                      const QString &scanRoot,
                                      const QStringList &relativePaths) const;
};
