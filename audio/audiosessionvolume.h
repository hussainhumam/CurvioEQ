#pragma once

#include <algorithm>
#include <cmath>

#include <QString>

class AudioSessionVolume
{
public:
    static constexpr int kMinPercent = 0;
    static constexpr int kUnityPercent = 100;
    static constexpr int kMaxPercent = 150;
    // 1.5x perceived loudness (+10 dB ≈ 2x loud) → ~+5.85 dB ≈ 1.96x amplitude.
    static constexpr float kMaxOutputGain = 1.957144f;

    static bool toggleMute(unsigned long processId, QString *errorMessage = nullptr);
    static bool getMasterVolume(unsigned long processId, float *level01, QString *errorMessage = nullptr);
    static bool setMasterVolume(unsigned long processId, float level01, QString *errorMessage = nullptr);

    static float outputGainForPercent(int percent)
    {
        if (percent <= kUnityPercent) {
            return 1.f;
        }
        const int clampedPercent = std::min(percent, kMaxPercent);
        const float t = static_cast<float>(clampedPercent - kUnityPercent)
                        / static_cast<float>(kMaxPercent - kUnityPercent);
        const float loudness = 1.f + 0.5f * t;
        const float extraDb = 10.f * std::log2(loudness);
        return std::min(std::pow(10.f, extraDb / 20.f), kMaxOutputGain);
    }
};
