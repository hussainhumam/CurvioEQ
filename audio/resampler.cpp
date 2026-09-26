#include "resampler.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr int kMaxHistoryFrames = 16;
constexpr int kSincRadius = 8;
constexpr float kPi = 3.14159265358979323846f;

float hermite4(float y0, float y1, float y2, float y3, float t)
{
    const float c0 = y1;
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + c0;
}

float windowedSinc(float x)
{
    const float ax = std::fabs(x);
    if (ax < 1e-8f) {
        return 1.f;
    }
    if (ax >= static_cast<float>(kSincRadius)) {
        return 0.f;
    }
    const float sinc = std::sin(kPi * x) / (kPi * x);
    const float window = 0.5f + 0.5f * std::cos(kPi * x / static_cast<float>(kSincRadius));
    return sinc * window;
}

} // namespace

void Resampler::resizeHistory()
{
    m_history.assign(static_cast<size_t>(m_channelCount) * static_cast<size_t>(kMaxHistoryFrames), 0.f);
}

void Resampler::configure(float inputRate, float outputRate, int channelCount)
{
    m_inputRate = std::max(inputRate, 1.f);
    m_outputRate = std::max(outputRate, 1.f);
    m_channelCount = std::max(channelCount, 1);
    m_configured = true;
    m_rateRatio = 1.0;
    m_phase = 0.0;
    resizeHistory();
}

void Resampler::setQuality(ResampleQuality quality)
{
    m_quality = quality;
}

void Resampler::setRateRatio(double ratio)
{
    m_rateRatio = std::clamp(ratio, 0.999, 1.001);
}

double Resampler::effectiveOutputRate() const
{
    return static_cast<double>(m_outputRate) * m_rateRatio;
}

void Resampler::setChannelCount(int channelCount)
{
    const int channels = std::max(channelCount, 1);
    if (!m_configured || channels == m_channelCount) {
        return;
    }

    m_channelCount = channels;
    resizeHistory();
}

float Resampler::sampleAtIndex(const float *input, int inputFrames, int channel, int index) const
{
    if (index < 0) {
        const int historyIndex = kMaxHistoryFrames + index;
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
}

float Resampler::sampleAtPhase(const float *input, int inputFrames, int channel, double phase) const
{
    const int index1 = static_cast<int>(std::floor(phase));
    const float fraction = static_cast<float>(phase - static_cast<double>(index1));

    if (m_quality == ResampleQuality::Fast) {
        const float y1 = sampleAtIndex(input, inputFrames, channel, index1);
        const float y2 = sampleAtIndex(input, inputFrames, channel, index1 + 1);
        return y1 + (y2 - y1) * fraction;
    }

    if (m_quality == ResampleQuality::Maximum) {
        float sum = 0.f;
        float weightSum = 0.f;
        for (int tap = 1 - kSincRadius; tap <= kSincRadius; ++tap) {
            const float weight = windowedSinc(static_cast<float>(tap) - fraction);
            sum += sampleAtIndex(input, inputFrames, channel, index1 + tap) * weight;
            weightSum += weight;
        }
        if (weightSum > 1e-8f) {
            return sum / weightSum;
        }
        return sampleAtIndex(input, inputFrames, channel, index1);
    }

    const float y0 = sampleAtIndex(input, inputFrames, channel, index1 - 1);
    const float y1 = sampleAtIndex(input, inputFrames, channel, index1);
    const float y2 = sampleAtIndex(input, inputFrames, channel, index1 + 1);
    const float y3 = sampleAtIndex(input, inputFrames, channel, index1 + 2);
    return hermite4(y0, y1, y2, y3, fraction);
}

void Resampler::updateHistory(const float *input, int inputFrames)
{
    if (!input || inputFrames <= 0 || m_channelCount <= 0) {
        return;
    }

    const size_t channels = static_cast<size_t>(m_channelCount);
    for (int offset = kMaxHistoryFrames; offset >= 1; --offset) {
        const int sourceIndex = inputFrames - offset;
        const size_t historyOffset = static_cast<size_t>(kMaxHistoryFrames - offset) * channels;
        if (sourceIndex >= 0) {
            for (size_t channel = 0; channel < channels; ++channel) {
                m_history[historyOffset + channel] =
                    input[static_cast<size_t>(sourceIndex * m_channelCount + static_cast<int>(channel))];
            }
        } else {
            const size_t lastHistory = static_cast<size_t>(kMaxHistoryFrames - 1) * channels;
            for (size_t channel = 0; channel < channels; ++channel) {
                m_history[historyOffset + channel] = m_history[lastHistory + channel];
            }
        }
    }
}

int Resampler::estimateOutputFrames(int inputFrames) const
{
    if (!m_configured || inputFrames <= 0) {
        return 0;
    }

    const double outputRate = effectiveOutputRate();
    if (std::fabs(m_rateRatio - 1.0) < 1e-12 && std::fabs(m_inputRate - m_outputRate) < 0.5f) {
        return inputFrames;
    }

    const double step = static_cast<double>(m_inputRate) / outputRate;
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

    const double outputRate = effectiveOutputRate();
    if (std::fabs(m_rateRatio - 1.0) < 1e-12 && std::fabs(m_inputRate - m_outputRate) < 0.5f) {
        const int frames = std::min(inputFrames, maxOutputFrames);
        std::memcpy(output, input, static_cast<size_t>(frames * m_channelCount) * sizeof(float));
        m_phase = 0.0;
        updateHistory(input, inputFrames);
        return frames;
    }

    const double step = static_cast<double>(m_inputRate) / outputRate;
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
