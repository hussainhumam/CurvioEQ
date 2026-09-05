#pragma once

struct DynamicRangeSettings {
    static constexpr int kAmountMin = -50;
    static constexpr int kAmountMax = 150;
    static constexpr int kAmountDefault = 35;

    static constexpr int kLoudnessMin = 0;
    static constexpr int kLoudnessMax = 900; // 0.01 LUFS steps from -23.00 to -14.00
    static constexpr int kLoudnessDefault = 0;
    static constexpr int kLoudnessLegacyMax = 100;
    static constexpr int kLoudnessV2Max = 600;
    static constexpr int kLoudnessScaleV2 = 2;
    static constexpr int kLoudnessScaleV3 = 3;
    static constexpr float kLoudnessQuietLufs = -23.f; // EBU R128
    static constexpr float kLoudnessLoudLufs = -14.f;  // streaming / YouTube

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

inline int migrateLegacyLoudnessAmount(int amount)
{
    if (amount <= DynamicRangeSettings::kLoudnessMin) {
        return DynamicRangeSettings::kLoudnessMin;
    }
    if (amount > DynamicRangeSettings::kLoudnessLegacyMax) {
        amount = DynamicRangeSettings::kLoudnessLegacyMax;
    }
    return (amount * DynamicRangeSettings::kLoudnessMax + DynamicRangeSettings::kLoudnessLegacyMax / 2)
           / DynamicRangeSettings::kLoudnessLegacyMax;
}

inline int migrateV2LoudnessAmount(int amount)
{
    if (amount <= DynamicRangeSettings::kLoudnessMin) {
        return DynamicRangeSettings::kLoudnessMin;
    }
    if (amount > DynamicRangeSettings::kLoudnessV2Max) {
        amount = DynamicRangeSettings::kLoudnessV2Max;
    }
    return (amount * DynamicRangeSettings::kLoudnessMax + DynamicRangeSettings::kLoudnessV2Max / 2)
           / DynamicRangeSettings::kLoudnessV2Max;
}

inline float loudnessAmountToTargetLufs(int amount)
{
    const int clamped = clampLoudnessAmount(amount);
    if (clamped <= DynamicRangeSettings::kLoudnessMin) {
        return DynamicRangeSettings::kLoudnessQuietLufs;
    }
    const float mix = static_cast<float>(clamped) / static_cast<float>(DynamicRangeSettings::kLoudnessMax);
    return DynamicRangeSettings::kLoudnessQuietLufs
           + mix * (DynamicRangeSettings::kLoudnessLoudLufs - DynamicRangeSettings::kLoudnessQuietLufs);
}
