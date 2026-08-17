#pragma once

class MixLimiter
{
public:
    void setSampleRate(float sampleRate);
    void reset();
    void process(float *interleaved, int frameCount, int channelCount);

private:
    float m_sampleRate = 48000.f;
    float m_envelope = 0.f;
};
