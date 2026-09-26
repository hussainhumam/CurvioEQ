#pragma once

#include "ui/appconstants.h"

#include <algorithm>

enum class ProcessingPrecision {
    Float32 = 0,
    Float64 = 1,
};

enum class ResampleQuality {
    Fast = 0,
    Balanced = 1,
    Maximum = 2,
};

enum class ChannelLayout {
    Auto = 0,
    Stereo = 1,
    Surround51 = 2,
    Surround71 = 3,
};

enum class OutputFormat {
    Auto = 0,
    Float32 = 1,
    Pcm16 = 2,
};

enum class ThreadPriority {
    RealtimeAudio = 0,
    Normal = 1,
};

enum class ShareMode {
    PreferShared = 0,
    Exclusive = 1,
};

enum class BufferSizePreset {
    Low = 0,
    Balanced = 1,
    Safe = 2,
    Custom = 3,
};

struct EngineIoSettings {
    static constexpr int kBufferPresetLow = 16;
    static constexpr int kBufferPresetBalanced = 64;
    static constexpr int kBufferPresetSafe = 512;
    static constexpr int kMaxSafetyExtraFrames = 8192;

    int sampleRate = AppConstants::kDefaultSampleRate;
    int bufferFrames = AppConstants::kDefaultBufferFrames;
    ProcessingPrecision precision = ProcessingPrecision::Float32;
    ResampleQuality resampleQuality = ResampleQuality::Balanced;
    ChannelLayout channelLayout = ChannelLayout::Auto;
    OutputFormat outputFormat = OutputFormat::Auto;
    bool driftCorrection = false;
    bool safetyBufferAuto = true;
    int safetyBufferFrames = 0;
    ThreadPriority threadPriority = ThreadPriority::RealtimeAudio;
    bool cpuAffinityAuto = true;
    int cpuAffinityCore = 0;
    ShareMode shareMode = ShareMode::PreferShared;
    bool autoRecovery = true;

    static BufferSizePreset bufferPresetFromFrames(int frames)
    {
        if (frames <= 0) {
            return BufferSizePreset::Low;
        }
        if (frames == kBufferPresetLow) {
            return BufferSizePreset::Low;
        }
        if (frames == kBufferPresetBalanced) {
            return BufferSizePreset::Balanced;
        }
        if (frames == kBufferPresetSafe) {
            return BufferSizePreset::Safe;
        }
        return BufferSizePreset::Custom;
    }

    static int framesForBufferPreset(BufferSizePreset preset, int customFrames)
    {
        switch (preset) {
        case BufferSizePreset::Low:
            return kBufferPresetLow;
        case BufferSizePreset::Balanced:
            return kBufferPresetBalanced;
        case BufferSizePreset::Safe:
            return kBufferPresetSafe;
        case BufferSizePreset::Custom:
            return AppConstants::clampBufferFrames(customFrames);
        }
        return AppConstants::kDefaultBufferFrames;
    }

    int requestedBufferFrames() const
    {
        return AppConstants::clampBufferFrames(bufferFrames);
    }

    int safetyExtraFrames() const
    {
        if (safetyBufferAuto) {
            return 0;
        }
        return std::clamp(safetyBufferFrames, 0, kMaxSafetyExtraFrames);
    }

    int mixChannelCountForDevice(int deviceChannels) const
    {
        switch (channelLayout) {
        case ChannelLayout::Stereo:
            return 2;
        case ChannelLayout::Surround51:
            return 6;
        case ChannelLayout::Surround71:
            return 8;
        case ChannelLayout::Auto:
        default:
            return deviceChannels > 0 ? deviceChannels : 2;
        }
    }

    int affinityCoreOrNone() const
    {
        return cpuAffinityAuto ? -1 : std::max(0, cpuAffinityCore);
    }

    bool useDoublePrecision() const { return precision == ProcessingPrecision::Float64; }
    bool useRealtimeAudio() const { return threadPriority == ThreadPriority::RealtimeAudio; }
    bool preferExclusive() const { return shareMode == ShareMode::Exclusive; }

    friend bool operator==(const EngineIoSettings &left, const EngineIoSettings &right)
    {
        return left.sampleRate == right.sampleRate
            && left.bufferFrames == right.bufferFrames
            && left.precision == right.precision
            && left.resampleQuality == right.resampleQuality
            && left.channelLayout == right.channelLayout
            && left.outputFormat == right.outputFormat
            && left.driftCorrection == right.driftCorrection
            && left.safetyBufferAuto == right.safetyBufferAuto
            && left.safetyBufferFrames == right.safetyBufferFrames
            && left.threadPriority == right.threadPriority
            && left.cpuAffinityAuto == right.cpuAffinityAuto
            && left.cpuAffinityCore == right.cpuAffinityCore
            && left.shareMode == right.shareMode
            && left.autoRecovery == right.autoRecovery;
    }

    friend bool operator!=(const EngineIoSettings &left, const EngineIoSettings &right)
    {
        return !(left == right);
    }
};

inline int applySafetyFill(int targetFrames, int extraFrames, int capacityFrames)
{
    int target = targetFrames + std::max(0, extraFrames);
    if (target < 1) {
        target = 1;
    }
    if (capacityFrames > 1 && target >= capacityFrames) {
        target = capacityFrames - 1;
    }
    return target;
}
