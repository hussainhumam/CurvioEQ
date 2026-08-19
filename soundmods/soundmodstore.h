#pragma once

#include "soundmodtypes.h"

#include <QVector>

class SoundModStore
{
public:
    bool loadProfile(const QString &gameId, QVector<SoundModProfileEntry> *entries) const;
    bool saveProfile(const QString &gameId, const QVector<SoundModProfileEntry> &entries) const;
    bool loadManifest(const QString &gameId, QVector<SoundModManifestEntry> *entries) const;
    bool saveManifest(const QString &gameId, const QVector<SoundModManifestEntry> &entries) const;
    float gainForRelativePath(const QString &gameId, const QString &relativePath) const;
};
