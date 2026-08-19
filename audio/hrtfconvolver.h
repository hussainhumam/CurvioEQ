#pragma once

#include <vector>

class HrtfConvolver
{
public:
    void configure(const float *irLeft, const float *irRight, int irLength, int blockSize);
    void reset();

    int blockSize() const { return m_blockSize; }
    int latencyFrames() const { return m_irLength > 0 ? m_irLength - 1 : 0; }

    void processAccumulate(const float *monoInput, float *stereoOutput, int frameCount);

private:
    int m_blockSize = 256;
    int m_irLength = 0;
    int m_historyIndex = 0;
    std::vector<float> m_irLeft;
    std::vector<float> m_irRight;
    std::vector<float> m_history;
};
