#pragma once

#include <atomic>

class DynamicsProcessor
{
public:
    DynamicsProcessor();

    void setEnabled(bool enabled);
    void setAmount(int amount);
    void setSampleRate(float sampleRate);

    bool isEnabled() const { return m_enabled.load(); }

    void reset();
    void process(float *interleaved, int frameCount, int channelCount);

private:
    struct CompressorParams {
        float threshold = 1.f;
        float ratio = 1.f;
        float knee = 0.2f;
        float attackCoeff = 0.f;
        float releaseCoeff = 0.f;
        float blend = 0.f;
    };

    CompressorParams paramsForAmount(int amount);
    static float softKneeGain(float envelope, const CompressorParams &params);
    static float computeDetection(float peak, float rms);

    std::atomic<bool> m_enabled{false};
    std::atomic<int> m_amount{35};

    float m_sampleRate = 48000.f;
    float m_envelope = 0.f;
    float m_rmsState = 0.f;
};
