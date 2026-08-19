#pragma once

struct DynamicRangeSettings {
    static constexpr int kAmountMin = -50;
    static constexpr int kAmountMax = 150;
    static constexpr int kAmountDefault = 35;

    static constexpr int kLoudnessMin = 0;
    static constexpr int kLoudnessMax = 100;
    static constexpr int kLoudnessDefault = 0;

    bool enabled = false;
    int amount = kAmountDefault;
    int loudnessAmount = kLoudnessDefault;
};

inline int clampDynamicRangeAmount(int amount)
{
    if (amount < DynamicRangeSettings::kAmountMin) {
        return DynamicRangeSettings::kAmountMin;
    }
    if (amount > DynamicRangeSettings::kAmountMax) {
        return DynamicRangeSettings::kAmountMax;
    }
    return amount;
}

inline int clampLoudnessAmount(int amount)
{
    if (amount < DynamicRangeSettings::kLoudnessMin) {
        return DynamicRangeSettings::kLoudnessMin;
    }
    if (amount > DynamicRangeSettings::kLoudnessMax) {
        return DynamicRangeSettings::kLoudnessMax;
    }
    return amount;
}
