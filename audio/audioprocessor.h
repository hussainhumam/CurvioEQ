#pragma once

class AudioProcessor
{
public:
    virtual ~AudioProcessor() = default;

    virtual void setSampleRate(float sampleRate) = 0;
    virtual void reset() = 0;
    virtual void process(float *interleaved, int frameCount, int channelCount) = 0;
};
