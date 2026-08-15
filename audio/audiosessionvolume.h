#pragma once

#include <QString>

class AudioSessionVolume
{
public:
    static bool toggleMute(unsigned long processId, QString *errorMessage = nullptr);
    static bool setMute(unsigned long processId, bool muted, QString *errorMessage = nullptr);
};
