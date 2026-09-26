#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

class LoudnessProcessor
{
public:
    LoudnessProcessor();

    void setEnabled(bool enabled);
    void setAmount(int amount);
    void setSampleRate(float sampleRate);

    bool isEnabled() const { return m_enabled.load(); }

    void reset();
    void process(float *interleaved, int frameCount, int channelCount);
    void setUseDoublePrecision(bool enabled);

    static float targetLoudnessDbForAmount(int amount);

private:
    struct BiquadState {
        float b0 = 1.f;
        float b1 = 0.f;
        float b2 = 0.f;
        float a1 = 0.f;
        float a2 = 0.f;
        float z1 = 0.f;
        float z2 = 0.f;
        double dz1 = 0.0;
        double dz2 = 0.0;

        float processSample(float input);
        double processSampleD(double input);
        void reset();
    };

    void updateFilters();
    void setKWeightingPreFilter(BiquadState *filter);
    void setKWeightingRlbFilter(BiquadState *filter);
    float processKWeightedSample(float input, BiquadState *preFilter, BiquadState *rlb);
    float computeGainDb(float measuredLoudnessDb, float targetLoudnessDb) const;
    static float softLimitSample(float sample);

    std::atomic<bool> m_enabled{false};
    std::atomic<int> m_amount{0};
    std::atomic<bool> m_useDouble{false};

    float m_sampleRate = 48000.f;
    float m_currentGainDb = 0.f;

    BiquadState m_preFilterLeft;
    BiquadState m_preFilterRight;
    BiquadState m_rlbLeft;
    BiquadState m_rlbRight;

    std::vector<float> m_momentaryRing;
    std::size_t m_momentaryIndex = 0;
    std::size_t m_momentaryFilled = 0;
    double m_momentarySum = 0.0;

    float m_attackCoeff = 0.f;
    float m_releaseCoeff = 0.f;
};
