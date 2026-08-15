#pragma once

#include <QString>

class AudioEndpointVolume
{
public:
    static bool setMute(const QString &deviceId, bool muted, QString *errorMessage = nullptr);
    static bool toggleMute(const QString &deviceId, QString *errorMessage = nullptr);
};
