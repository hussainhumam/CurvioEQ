#pragma once

#include <vector>

class Resampler
{
public:
    void configure(float inputRate, float outputRate, int channelCount);
    void setChannelCount(int channelCount);
    int process(const float *input, int inputFrames, float *output, int maxOutputFrames);

    int estimateOutputFrames(int inputFrames) const;

private:
    float sampleAtPhase(const float *input, int inputFrames, int channel, double phase) const;
    void updateHistory(const float *input, int inputFrames);

    float m_inputRate = 48000.f;
    float m_outputRate = 48000.f;
    int m_channelCount = 2;
    bool m_configured = false;
    double m_phase = 0.0;
    std::vector<float> m_history;
};
