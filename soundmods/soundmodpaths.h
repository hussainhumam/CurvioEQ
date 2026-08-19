#pragma once

#include "soundmodtypes.h"

#include <QString>

class SoundModPaths
{
public:
    static QString soundModsRoot();
    static QString gameRoot(const QString &gameId);
    static QString profilePath(const QString &gameId);
    static QString manifestPath(const QString &gameId);
    static QString backupRoot(const QString &gameId);
    static QString stagingRoot(const QString &gameId);
};
