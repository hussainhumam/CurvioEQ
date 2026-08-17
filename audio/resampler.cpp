#include "resampler.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

float hermite4(float y0, float y1, float y2, float y3, float t)
{
    const float c0 = y1;
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + c0;
}

} // namespace

void Resampler::configure(float inputRate, float outputRate, int channelCount)
{
    m_inputRate = std::max(inputRate, 1.f);
    m_outputRate = std::max(outputRate, 1.f);
    m_channelCount = std::max(channelCount, 1);
    m_configured = true;
    m_phase = 0.0;
    m_history.assign(static_cast<size_t>(m_channelCount) * 3, 0.f);
}

void Resampler::setChannelCount(int channelCount)
{
    const int channels = std::max(channelCount, 1);
    if (!m_configured || channels == m_channelCount) {
        return;
    }

    m_channelCount = channels;
    m_history.assign(static_cast<size_t>(m_channelCount) * 3, 0.f);
}

float Resampler::sampleAtPhase(const float *input, int inputFrames, int channel, double phase) const
{
    const int index1 = static_cast<int>(std::floor(phase));
    const float fraction = static_cast<float>(phase - static_cast<double>(index1));

    auto sampleAtIndex = [&](int index) -> float {
        if (index < 0) {
            const int historyIndex = 3 + index;
            if (historyIndex >= 0) {
                return m_history[static_cast<size_t>(historyIndex * m_channelCount + channel)];
            }
            return input ? input[channel] : 0.f;
        }

        if (!input || index >= inputFrames) {
            if (input && inputFrames > 0) {
                return input[static_cast<size_t>((inputFrames - 1) * m_channelCount + channel)];
            }
            return 0.f;
        }

        return input[static_cast<size_t>(index * m_channelCount + channel)];
    };

    const float y0 = sampleAtIndex(index1 - 1);
    const float y1 = sampleAtIndex(index1);
    const float y2 = sampleAtIndex(index1 + 1);
    const float y3 = sampleAtIndex(index1 + 2);
    return hermite4(y0, y1, y2, y3, fraction);
}

void Resampler::updateHistory(const float *input, int inputFrames)
{
    if (!input || inputFrames <= 0 || m_channelCount <= 0) {
        return;
    }

    const size_t channels = static_cast<size_t>(m_channelCount);
    for (int offset = 3; offset >= 1; --offset) {
        const int sourceIndex = inputFrames - offset;
        const size_t historyOffset = static_cast<size_t>(3 - offset) * channels;
        if (sourceIndex >= 0) {
            for (size_t channel = 0; channel < channels; ++channel) {
                m_history[historyOffset + channel] =
                    input[static_cast<size_t>(sourceIndex * m_channelCount + static_cast<int>(channel))];
            }
        } else {
            for (size_t channel = 0; channel < channels; ++channel) {
                m_history[historyOffset + channel] = m_history[static_cast<size_t>(2 * m_channelCount + channel)];
            }
        }
    }
}

int Resampler::estimateOutputFrames(int inputFrames) const
{
    if (!m_configured || inputFrames <= 0) {
        return 0;
    }

    if (std::fabs(m_inputRate - m_outputRate) < 0.5f) {
        return inputFrames;
    }

    const double step = static_cast<double>(m_inputRate) / static_cast<double>(m_outputRate);
    if (step <= 0.0) {
        return 0;
    }

    const double availableInput = static_cast<double>(inputFrames) - m_phase;
    if (availableInput <= 0.0) {
        return 0;
    }

    return std::max(1, static_cast<int>(std::floor(availableInput / step)));
}

int Resampler::process(const float *input, int inputFrames, float *output, int maxOutputFrames)
{
    if (!m_configured || !input || !output || inputFrames <= 0 || maxOutputFrames <= 0 || m_channelCount <= 0) {
        return 0;
    }

    if (std::fabs(m_inputRate - m_outputRate) < 0.5f) {
        const int frames = std::min(inputFrames, maxOutputFrames);
        std::memcpy(output, input, static_cast<size_t>(frames * m_channelCount) * sizeof(float));
        m_phase = 0.0;
        updateHistory(input, inputFrames);
        return frames;
    }

    const double step = static_cast<double>(m_inputRate) / static_cast<double>(m_outputRate);
    double phase = m_phase;
    int outputFrames = 0;

    while (outputFrames < maxOutputFrames && phase < static_cast<double>(inputFrames)) {
        for (int channel = 0; channel < m_channelCount; ++channel) {
            output[static_cast<size_t>(outputFrames * m_channelCount + channel)] =
                sampleAtPhase(input, inputFrames, channel, phase);
        }
        phase += step;
        ++outputFrames;
    }

    m_phase = phase - static_cast<double>(inputFrames);
    updateHistory(input, inputFrames);
    return outputFrames;
}
