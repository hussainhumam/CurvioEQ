#pragma once

#include "hrtfpresets.h"

#include <QString>

class HrtfLoader
{
public:
    static bool loadPresetFromDirectory(const QString &directoryPath,
                                        HrtfPresetId presetId,
                                        float targetSampleRate,
                                        HrtfPresetData *outData,
                                        QString *errorMessage = nullptr);

    static QString presetDirectoryName(HrtfPresetId presetId);
    static QString bundledHrtfRoot(const QString &applicationDirPath);
};
