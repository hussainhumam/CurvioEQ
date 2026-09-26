#pragma once

#include <atomic>

class MixLimiter
{
public:
    static constexpr float kDefaultThreshold = 0.97f;
    static constexpr float kBypassThreshold = 0.999f;

    MixLimiter() = default;
    explicit MixLimiter(float linearPeak);

    static float dbToLinear(float db);
    static float linearToDb(float linear);

    void setSampleRate(float sampleRate);
    void setThreshold(float linearPeak);
    float threshold() const;
    void reset();
    void process(float *interleaved, int frameCount, int channelCount);
    void setUseDoublePrecision(bool enabled);

private:
    float m_sampleRate = 48000.f;
    float m_envelope = 0.f;
    double m_envelopeD = 0.0;
    std::atomic<float> m_threshold{kDefaultThreshold};
    std::atomic<bool> m_useDouble{false};
};
