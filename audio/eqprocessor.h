#pragma once

#include "audioprocessor.h"
#include "eqstate.h"

#include <array>
#include <atomic>
#include <cmath>

class EqProcessor : public AudioProcessor
{
public:
    static constexpr int kBandCount = EqState::kBandCount;
    static constexpr int kMaxParametricFilters = EqState::kMaxParametricFilters;
    static constexpr int kMaxChannels = 2;
    static constexpr std::array<float, kBandCount> kBandFreqs = EqState::kBandFreqs;

    EqProcessor();

    void setSampleRate(float sampleRate) override;
    void reset() override;
    void process(float *interleavedSamples, int frameCount, int channelCount) override;

    void setBandGain(int band, float gainDb);
    void setGains(const std::array<float, kBandCount> &gainsDb);
    void setEqState(const EqState &state);
    void setAdvancedMode(bool advanced);
    void setParametricFilters(const EqFilter *filters, int count);

private:
    struct BiquadCoeffs {
        float b0 = 1.f;
        float b1 = 0.f;
        float b2 = 0.f;
        float a1 = 0.f;
        float a2 = 0.f;
    };

    struct Biquad {
        std::array<BiquadCoeffs, 2> coeffs{};
        std::atomic<int> activeCoeffIndex{0};
        float z1 = 0.f;
        float z2 = 0.f;

        float processSample(float input);
        void reset();
        void publishCoeffs(const BiquadCoeffs &updated);
    };

    void updateSimpleBandCoefficients(int band, float gainDb);
    void updateParametricSlot(int slot, const EqFilter &filter);
    void publishBypass(int slot);
    void advanceGainRamps(int frameCount);
    void syncParametricFromTargets();

    float m_sampleRate = 48000.f;
    std::atomic<bool> m_advanced{false};

    std::array<std::atomic<float>, kBandCount> m_targetGainsDb{};
    std::array<float, kBandCount> m_currentGainsDb{};
    std::array<float, kBandCount> m_gainRampPerSample{};
    std::array<Biquad, kBandCount> m_simpleLeft{};
    std::array<Biquad, kBandCount> m_simpleRight{};

    std::atomic<int> m_parametricCount{0};
    std::array<std::atomic<int>, kMaxParametricFilters> m_targetType{};
    std::array<std::atomic<float>, kMaxParametricFilters> m_targetFreqHz{};
    std::array<std::atomic<float>, kMaxParametricFilters> m_targetParamGainDb{};
    std::array<std::atomic<float>, kMaxParametricFilters> m_targetQ{};
    std::array<float, kMaxParametricFilters> m_currentParamGainDb{};
    std::array<float, kMaxParametricFilters> m_paramGainRampPerSample{};
    std::array<EqFilter, kMaxParametricFilters> m_currentParamFilters{};
    std::array<Biquad, kMaxParametricFilters> m_paramLeft{};
    std::array<Biquad, kMaxParametricFilters> m_paramRight{};
};
