#pragma once

#include "engineiosettings.h"

#include <vector>

class Resampler
{
public:
    void configure(float inputRate, float outputRate, int channelCount);
    void setChannelCount(int channelCount);
    void setRateRatio(double ratio);
    void setQuality(ResampleQuality quality);
    int process(const float *input, int inputFrames, float *output, int maxOutputFrames);

    int estimateOutputFrames(int inputFrames) const;

private:
    float sampleAtPhase(const float *input, int inputFrames, int channel, double phase) const;
    float sampleAtIndex(const float *input, int inputFrames, int channel, int index) const;
    void updateHistory(const float *input, int inputFrames);
    double effectiveOutputRate() const;
    void resizeHistory();

    float m_inputRate = 48000.f;
    float m_outputRate = 48000.f;
    double m_rateRatio = 1.0;
    int m_channelCount = 2;
    bool m_configured = false;
    double m_phase = 0.0;
    ResampleQuality m_quality = ResampleQuality::Balanced;
    std::vector<float> m_history;
};
