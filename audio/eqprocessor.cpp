#include "eqprocessor.h"

#include <algorithm>

namespace {
constexpr float kDefaultQ = 1.41f;
constexpr float kPi = 3.14159265358979323846f;
}

float EqProcessor::Biquad::processSample(float input)
{
    const float output = b0 * input + z1;
    z1 = b1 * input - a1 * output + z2;
    z2 = b2 * input - a2 * output;
    return output;
}

void EqProcessor::Biquad::reset()
{
    z1 = 0.f;
    z2 = 0.f;
}

EqProcessor::EqProcessor()
{
    for (auto &gain : m_gainsDb) {
        gain.store(0.f);
    }
    setSampleRate(48000.f);
}

void EqProcessor::setSampleRate(float sampleRate)
{
    m_sampleRate = std::max(sampleRate, 1.f);
    for (int band = 0; band < kBandCount; ++band) {
        updateCoefficients(band);
        m_biquadsLeft[static_cast<size_t>(band)].reset();
        m_biquadsRight[static_cast<size_t>(band)].reset();
    }
}

void EqProcessor::setBandGain(int band, float gainDb)
{
    if (band < 0 || band >= kBandCount) {
        return;
    }
    m_gainsDb[static_cast<size_t>(band)].store(gainDb);
    updateCoefficients(band);
}

void EqProcessor::setGains(const std::array<float, kBandCount> &gainsDb)
{
    for (int band = 0; band < kBandCount; ++band) {
        setBandGain(band, gainsDb[static_cast<size_t>(band)]);
    }
}

void EqProcessor::updateCoefficients(int band)
{
    const float gainDb = m_gainsDb[static_cast<size_t>(band)].load();
    const float frequency = kBandFreqs[static_cast<size_t>(band)];
    const float A = std::pow(10.f, gainDb / 40.f);
    const float omega = 2.f * kPi * frequency / m_sampleRate;
    const float sinOmega = std::sin(omega);
    const float cosOmega = std::cos(omega);
    const float alpha = sinOmega / (2.f * kDefaultQ);

    float b0 = 1.f + alpha * A;
    float b1 = -2.f * cosOmega;
    float b2 = 1.f - alpha * A;
    const float a0 = 1.f + alpha / A;
    const float a1 = -2.f * cosOmega;
    const float a2 = 1.f - alpha / A;

    b0 /= a0;
    b1 /= a0;
    b2 /= a0;

    for (Biquad *chain : {&m_biquadsLeft[static_cast<size_t>(band)], &m_biquadsRight[static_cast<size_t>(band)]}) {
        chain->b0 = b0;
        chain->b1 = b1;
        chain->b2 = b2;
        chain->a1 = a1 / a0;
        chain->a2 = a2 / a0;
    }
}

void EqProcessor::process(float *interleavedSamples, int frameCount, int channelCount)
{
    if (!interleavedSamples || frameCount <= 0 || channelCount <= 0) {
        return;
    }

    const int channelsToProcess = std::min(channelCount, kMaxChannels);

    for (int frame = 0; frame < frameCount; ++frame) {
        for (int channel = 0; channel < channelsToProcess; ++channel) {
            const int index = frame * channelCount + channel;
            float sample = interleavedSamples[index];
            auto &biquads = (channel == 0) ? m_biquadsLeft : m_biquadsRight;

            for (int band = 0; band < kBandCount; ++band) {
                sample = biquads[static_cast<size_t>(band)].processSample(sample);
            }

            interleavedSamples[index] = sample;
        }
    }
}
