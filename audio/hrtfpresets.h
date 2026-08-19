#pragma once

#include "surroundprocessor.h"

#include <array>
#include <vector>

struct HrtfSpeakerImpulse {
    std::vector<float> left;
    std::vector<float> right;
};

struct HrtfPresetData {
    float sampleRate = 48000.f;
    int irLength = 0;
    std::array<HrtfSpeakerImpulse, SurroundProcessor::kChannelCount> speakers{};
};

enum class HrtfPresetId {
    Default = 0,
    Wide = 1,
    Close = 2,
    Count = 3,
};

class HrtfPresets
{
public:
    static constexpr int kIrLength = 128;
    static constexpr int kConvolverBlockSize = 256;

    static const HrtfPresetData &preset(HrtfPresetId presetId, float sampleRate);
    static const char *presetDisplayName(HrtfPresetId presetId);
    static void normalizeImpulse(HrtfSpeakerImpulse *impulse);
    static void highPassImpulse(HrtfSpeakerImpulse *impulse, float sampleRate, float cutoffHz);

private:
    static HrtfPresetData buildPreset(HrtfPresetId presetId, float sampleRate);
    static void generateSpeakerImpulse(float azimuthDegrees,
                                       float elevationDegrees,
                                       float itdScale,
                                       float ildScale,
                                       float sampleRate,
                                       HrtfSpeakerImpulse *impulse);
};
