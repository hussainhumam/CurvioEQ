#pragma once

#include "soundmodtypes.h"

class GameIdentityUtil
{
public:
    static GameIdentity fromProcess(unsigned long processId, const QString &displayName);
    static QString normalizePathKey(const QString &path);
};
