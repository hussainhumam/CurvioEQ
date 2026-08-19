#pragma once

#include "soundmodtypes.h"

#include <QString>
#include <vector>

class WavAudioUtil
{
public:
    struct AudioBuffer {
        std::vector<float> interleaved;
        int channels = 0;
        int sampleRate = 0;
    };

    static bool load(const QString &path, AudioBuffer *output, QString *errorMessage = nullptr);
    static bool save(const QString &path, const AudioBuffer &input, QString *errorMessage = nullptr);
    static void applyGainDb(AudioBuffer *buffer, float gainDb);
    static QString sha256File(const QString &path);
};
