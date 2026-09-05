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

private:
    float m_sampleRate = 48000.f;
    float m_envelope = 0.f;
    std::atomic<float> m_threshold{kDefaultThreshold};
};
