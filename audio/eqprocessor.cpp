#include "eqprocessor.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kDefaultQ = 1.41f;
constexpr float kPi = 3.14159265358979323846f;
}

float EqProcessor::Biquad::processSample(float input)
{
    const int coeffIndex = activeCoeffIndex.load(std::memory_order_acquire);
    const BiquadCoeffs &c = coeffs[static_cast<size_t>(coeffIndex)];

    const float output = c.b0 * input + z1;
    z1 = c.b1 * input - c.a1 * output + z2;
    z2 = c.b2 * input - c.a2 * output;
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
    const float previous = m_gainsDb[static_cast<size_t>(band)].load();
    if (std::fabs(previous - gainDb) < 0.001f) {
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

    BiquadCoeffs updated;
    updated.b0 = b0;
    updated.b1 = b1;
    updated.b2 = b2;
    updated.a1 = a1 / a0;
    updated.a2 = a2 / a0;

    for (Biquad *chain : {&m_biquadsLeft[static_cast<size_t>(band)], &m_biquadsRight[static_cast<size_t>(band)]}) {
        const int inactiveIndex = 1 - chain->activeCoeffIndex.load(std::memory_order_relaxed);
        chain->coeffs[static_cast<size_t>(inactiveIndex)] = updated;
        chain->activeCoeffIndex.store(inactiveIndex, std::memory_order_release);
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
