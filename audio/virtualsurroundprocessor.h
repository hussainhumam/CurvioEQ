#pragma once

#include "hrtfconvolver.h"
#include "hrtfpresets.h"
#include "surroundprocessor.h"

#include <array>
#include <atomic>
#include <vector>

class VirtualSurroundProcessor
{
public:
    static constexpr int kSpeakerCount = SurroundProcessor::kChannelCount;
    static constexpr float kWetBusGain = 0.28f;

    VirtualSurroundProcessor();

    void setEnabled(bool enabled);
    bool isEnabled() const;

    void setPreset(int presetId);
    void setStrength(int strength);
    void setChannelLevels(const std::array<int, kSpeakerCount> &levels);
    void setSampleRate(float sampleRate);

    int latencyFrames() const;
    int presetId() const { return m_presetId.load(); }
    int strength() const { return m_strength.load(); }

    void process(const float *stereoIn, float *stereoOut, int frameCount);

private:
    struct BiquadState {
        float b0 = 1.f;
        float b1 = 0.f;
        float b2 = 0.f;
        float a1 = 0.f;
        float a2 = 0.f;
        float z1 = 0.f;
        float z2 = 0.f;

        float process(float input);
        void reset();
    };

    static float levelToMultiplier(int level);
    static float strengthToMix(int strength);
    void configureLfeLowPass();
    void configureCrossoverFilters();
    void ensureConfigured();
    void upmixFrame(float left, float right, std::array<float, kSpeakerCount> *speakers);
    void normalizeAndLimitWet(int frameCount);
    static void configureLowPass(BiquadState *filter, float sampleRate, float cutoffHz);
    static void configureHighPass(BiquadState *filter, float sampleRate, float cutoffHz);

    std::atomic<bool> m_enabled{false};
    std::atomic<int> m_presetId{static_cast<int>(HrtfPresetId::Default)};
    std::atomic<int> m_strength{75};
    std::array<std::atomic<int>, kSpeakerCount> m_levels{};

    float m_sampleRate = 48000.f;
    bool m_configured = false;
    int m_activePresetId = -1;
    float m_activeSampleRate = 0.f;

    BiquadState m_lfeLowPass;
    BiquadState m_spatialHighPassLeft;
    BiquadState m_spatialHighPassRight;
    BiquadState m_wetHighPassLeft;
    BiquadState m_wetHighPassRight;
    BiquadState m_wetHighPassLeft2;
    BiquadState m_wetHighPassRight2;
    std::array<HrtfConvolver, kSpeakerCount> m_convolvers{};
    std::vector<float> m_speakerScratch;
    std::vector<float> m_wetScratch;
};
