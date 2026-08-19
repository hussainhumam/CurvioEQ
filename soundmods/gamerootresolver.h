#pragma once

#include <QString>

class GameRootResolver
{
public:
    static QString resolveScanRoot(const QString &executablePath, const QString &preferredRoot = {});
};
