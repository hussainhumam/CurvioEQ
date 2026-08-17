#include "audiopipeline.h"

void AudioPipeline::addProcessor(AudioProcessor *processor)
{
    if (processor) {
        m_processors.push_back(processor);
    }
}

void AudioPipeline::setSampleRate(float sampleRate)
{
    for (AudioProcessor *processor : m_processors) {
        processor->setSampleRate(sampleRate);
    }
}

void AudioPipeline::reset()
{
    for (AudioProcessor *processor : m_processors) {
        processor->reset();
    }
}

void AudioPipeline::process(float *interleaved, int frameCount, int channelCount)
{
    if (!interleaved || frameCount <= 0 || channelCount <= 0) {
        return;
    }

    for (AudioProcessor *processor : m_processors) {
        processor->process(interleaved, frameCount, channelCount);
    }
}
