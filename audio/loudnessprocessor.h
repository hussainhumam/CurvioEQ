#pragma once

#include <atomic>

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

        float processSample(float input);
        void reset();
    };

    void updateFilters();
    void setHighPassCoeffs(BiquadState *filter, float frequency);
    void setHighShelfCoeffs(BiquadState *filter, float frequency, float gainDb);
    float processKWeightedSample(float input, BiquadState *highPass, BiquadState *highShelf);
    float computeGainDb(float measuredLoudnessDb, float targetLoudnessDb) const;
    static float softLimitSample(float sample);

    std::atomic<bool> m_enabled{false};
    std::atomic<int> m_amount{0};

    float m_sampleRate = 48000.f;
    float m_meanSquare = 0.f;
    float m_currentGainDb = 0.f;

    BiquadState m_highPassLeft;
    BiquadState m_highPassRight;
    BiquadState m_highShelfLeft;
    BiquadState m_highShelfRight;

    float m_loudnessCoeff = 0.f;
    float m_attackCoeff = 0.f;
    float m_releaseCoeff = 0.f;
};
