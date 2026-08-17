#pragma once

#include "audioprocessor.h"

#include <vector>

class AudioPipeline
{
public:
    void addProcessor(AudioProcessor *processor);
    void setSampleRate(float sampleRate);
    void reset();
    void process(float *interleaved, int frameCount, int channelCount);

private:
    std::vector<AudioProcessor *> m_processors;
};
