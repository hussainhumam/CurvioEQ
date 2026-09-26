#pragma once

namespace AppConstants {

inline constexpr char kAppDisplayName[] = "CurvioEQ";
inline constexpr char kAppId[] = "CurvioEQ";
inline constexpr char kAppVersion[] = "1.3.2";
inline constexpr char kGitHubOwner[] = "hussainhumam";
inline constexpr char kGitHubRepo[] = "CurvioEQ";

inline constexpr int kSessionRefreshIntervalActiveMs = 500;
inline constexpr int kSpectrumRefreshIntervalMs = 33;
inline constexpr float kSpectrumAttackAlpha = 0.7f;
inline constexpr float kSpectrumReleaseAlpha = 0.4f;
inline constexpr float kSpectrumYHeadroom = 1.15f;
inline constexpr float kSpectrumYMinPeak = 0.05f;
inline constexpr float kSpectrumLimiterMinDb = -48.f;
inline constexpr float kSpectrumLimiterMaxDb = 0.f;
inline constexpr int kSpectrumLimiterGrabPx = 8;
inline constexpr int kGainUpdateDebounceMs = 75;
inline constexpr int kTrayMessageDurationMs = 5000;
inline constexpr int kMaxGainDb = 20;
inline constexpr int kMinBalance = -10;
inline constexpr int kMaxBalance = 10;
inline constexpr int kDefaultBalance = 0;
inline constexpr int kDefaultSampleRate = 48000;
inline constexpr int kMinSampleRate = 8000;
inline constexpr int kMaxSampleRate = 384000;
inline constexpr int kSampleRateChoices[] = {44100, 48000, 96000, 192000};
inline constexpr int kSampleRateChoiceCount =
    static_cast<int>(sizeof(kSampleRateChoices) / sizeof(kSampleRateChoices[0]));

inline constexpr int kDefaultBufferFrames = 16;
inline constexpr int kMinBufferFrames = 1;
inline constexpr int kMaxBufferFrames = 65536;
inline constexpr int kBufferFrameChoices[] = {16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384};
inline constexpr int kBufferFrameChoiceCount =
    static_cast<int>(sizeof(kBufferFrameChoices) / sizeof(kBufferFrameChoices[0]));
inline constexpr int kCustomAudioIoChoice = -1;
inline constexpr int kSessionRingBufferFrames = kDefaultBufferFrames;
inline constexpr int kTargetRingFillFrames = 512;
inline constexpr int kHighRingFillFrames = 1024;

inline int nearestChoice(const int *choices, int count, int value, int fallback)
{
    if (!choices || count <= 0) {
        return fallback;
    }
    int best = fallback;
    int bestDiff = value > fallback ? value - fallback : fallback - value;
    for (int i = 0; i < count; ++i) {
        const int diff = value > choices[i] ? value - choices[i] : choices[i] - value;
        if (diff < bestDiff) {
            best = choices[i];
            bestDiff = diff;
        }
    }
    return best;
}

inline bool isListedChoice(const int *choices, int count, int value)
{
    if (!choices) {
        return false;
    }
    for (int i = 0; i < count; ++i) {
        if (choices[i] == value) {
            return true;
        }
    }
    return false;
}

inline bool isListedSampleRate(int sampleRate)
{
    return isListedChoice(kSampleRateChoices, kSampleRateChoiceCount, sampleRate);
}

inline bool isListedBufferFrames(int bufferFrames)
{
    return isListedChoice(kBufferFrameChoices, kBufferFrameChoiceCount, bufferFrames);
}

inline int clampSampleRate(int sampleRate)
{
    if (sampleRate <= 0) {
        return kDefaultSampleRate;
    }
    if (sampleRate < kMinSampleRate) {
        return kMinSampleRate;
    }
    if (sampleRate > kMaxSampleRate) {
        return kMaxSampleRate;
    }
    return sampleRate;
}

inline int clampBufferFrames(int bufferFrames)
{
    if (bufferFrames <= 0) {
        return kDefaultBufferFrames;
    }
    if (bufferFrames < kMinBufferFrames) {
        return kMinBufferFrames;
    }
    if (bufferFrames > kMaxBufferFrames) {
        return kMaxBufferFrames;
    }
    return bufferFrames;
}

inline int snapSampleRate(int sampleRate)
{
    return clampSampleRate(sampleRate);
}

inline int snapBufferFrames(int bufferFrames)
{
    return clampBufferFrames(bufferFrames);
}

inline int requestedEnginePeriodFrames(int bufferFrames)
{
    const int snapped = clampBufferFrames(bufferFrames);
    if (snapped < 16) {
        return snapped;
    }
    const int quarter = snapped / 4;
    return quarter > 16 ? quarter : 16;
}

inline int ringTargetFillFrames(int bufferFrames)
{
    const int snapped = clampBufferFrames(bufferFrames);
    const int target = snapped / 4;
    return target < kMinBufferFrames ? kMinBufferFrames : target;
}

inline int ringHighFillFrames(int bufferFrames)
{
    const int snapped = clampBufferFrames(bufferFrames);
    const int target = ringTargetFillFrames(snapped);
    const int high = snapped / 2;
    return high > target ? high : target * 2;
}

inline int captureChunkFrames(int bufferFrames)
{
    const int target = ringTargetFillFrames(bufferFrames);
    if (target < kMinBufferFrames) {
        return kMinBufferFrames;
    }
    if (target > 512) {
        return 512;
    }
    return target;
}

inline int effectiveRingFrames(int bufferFrames, int periodFrames)
{
    const int period = periodFrames > 1 ? periodFrames : 1;
    const int minimum = period * 4;
    const int requested = clampBufferFrames(bufferFrames);
    return requested > minimum ? requested : minimum;
}

inline int ringTargetFillFramesForPeriod(int ringFrames, int periodFrames)
{
    const int ring = clampBufferFrames(ringFrames);
    const int period = periodFrames > 1 ? periodFrames : 1;
    const int twoPeriods = period * 2 > 1 ? period * 2 : 1;
    const int halfRing = ring / 2 > 1 ? ring / 2 : 1;
    return twoPeriods < halfRing ? twoPeriods : halfRing;
}

inline int ringHighFillFramesForPeriod(int ringFrames, int periodFrames)
{
    const int ring = clampBufferFrames(ringFrames);
    const int target = ringTargetFillFramesForPeriod(ring, periodFrames);
    const int period = periodFrames > 1 ? periodFrames : 1;
    const int fourPeriods = period * 4;
    const int fromTarget = target + period;
    const int periodHigh = fourPeriods > fromTarget ? fourPeriods : fromTarget;
    const int half = ring / 2 > target ? ring / 2 : target;
    const int high = periodHigh > half ? periodHigh : half;
    return high < ring ? high : ring;
}

inline int captureChunkFramesForPeriod(int bufferFrames, int periodFrames)
{
    if (periodFrames > 0) {
        return periodFrames > 1 ? periodFrames : 1;
    }
    int chunk = captureChunkFrames(bufferFrames);
    if (chunk > 512) {
        chunk = 512;
    }
    return chunk;
}
inline constexpr int kBuiltInGamingPresetSeparatorIndex = 9;

} // namespace AppConstants
