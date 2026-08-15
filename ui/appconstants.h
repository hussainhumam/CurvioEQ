#pragma once

namespace AppConstants {

inline constexpr char kAppDisplayName[] = "CurvioEQ";
inline constexpr char kAppId[] = "CurvioEQ";

inline constexpr int kSessionRefreshIntervalActiveMs = 5000;
inline constexpr int kSpectrumRefreshIntervalMs = 33;
inline constexpr float kSpectrumAttackAlpha = 0.7f;
inline constexpr float kSpectrumReleaseAlpha = 0.4f;
inline constexpr float kSpectrumYHeadroom = 1.15f;
inline constexpr float kSpectrumYMinPeak = 0.05f;
inline constexpr int kGainUpdateDebounceMs = 75;
inline constexpr int kTrayMessageDurationMs = 5000;
inline constexpr int kMaxGainDb = 20;
inline constexpr int kSessionRingBufferFrames = 2048;
inline constexpr int kBuiltInGamingPresetSeparatorIndex = 9;

} // namespace AppConstants
