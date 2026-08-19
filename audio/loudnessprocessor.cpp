#include "loudnessprocessor.h"

#include "dynamicrangesettings.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTargetLoudnessMinDb = -24.f;
constexpr float kTargetLoudnessMaxDb = -14.f;
constexpr float kMaxBoostDb = 12.f;
constexpr float kMaxCutDb = 6.f;
constexpr float kPeakCeiling = 0.944f; // ~ -0.5 dBFS
constexpr float kLoudnessWindowSeconds = 0.400f;
constexpr float kAttackSeconds = 0.300f;
constexpr float kReleaseSeconds = 2.000f;
constexpr float kMinMeanSquare = 1e-12f;

float coeffForTime(float seconds, float sampleRate)
{
    if (seconds <= 0.f || sampleRate <= 0.f) {
        return 0.f;
    }
    return std::exp(-1.f / (seconds * sampleRate));
}

} // namespace

void LoudnessProcessor::setHighPassCoeffs(BiquadState *filter, float frequency)
{
    const float omega = 2.f * kPi * frequency / m_sampleRate;
    const float sinOmega = std::sin(omega);
    const float cosOmega = std::cos(omega);
    const float alpha = sinOmega / (2.f * 0.707f);

    const float b0 = (1.f + cosOmega) * 0.5f;
    const float b1 = -(1.f + cosOmega);
    const float b2 = (1.f + cosOmega) * 0.5f;
    const float a0 = 1.f + alpha;
    const float a1 = -2.f * cosOmega;
    const float a2 = 1.f - alpha;

    filter->b0 = b0 / a0;
    filter->b1 = b1 / a0;
    filter->b2 = b2 / a0;
    filter->a1 = a1 / a0;
    filter->a2 = a2 / a0;
}

void LoudnessProcessor::setHighShelfCoeffs(BiquadState *filter, float frequency, float gainDb)
{
    const float A = std::pow(10.f, gainDb / 40.f);
    const float omega = 2.f * kPi * frequency / m_sampleRate;
    const float sinOmega = std::sin(omega);
    const float cosOmega = std::cos(omega);
    const float alpha = sinOmega / (2.f * 0.707f);

    float b0 = A * ((A + 1.f) + (A - 1.f) * cosOmega + 2.f * std::sqrt(A) * alpha);
    float b1 = -2.f * A * ((A - 1.f) + (A + 1.f) * cosOmega);
    float b2 = A * ((A + 1.f) + (A - 1.f) * cosOmega - 2.f * std::sqrt(A) * alpha);
    const float a0 = (A + 1.f) - (A - 1.f) * cosOmega + 2.f * std::sqrt(A) * alpha;
    const float a1 = 2.f * ((A - 1.f) - (A + 1.f) * cosOmega);
    const float a2 = (A + 1.f) - (A - 1.f) * cosOmega - 2.f * std::sqrt(A) * alpha;

    filter->b0 = b0 / a0;
    filter->b1 = b1 / a0;
    filter->b2 = b2 / a0;
    filter->a1 = a1 / a0;
    filter->a2 = a2 / a0;
}

float LoudnessProcessor::BiquadState::processSample(float input)
{
    const float output = b0 * input + z1;
    z1 = b1 * input - a1 * output + z2;
    z2 = b2 * input - a2 * output;
    return output;
}

void LoudnessProcessor::BiquadState::reset()
{
    z1 = 0.f;
    z2 = 0.f;
}

LoudnessProcessor::LoudnessProcessor()
{
    updateFilters();
    reset();
}

void LoudnessProcessor::setEnabled(bool enabled)
{
    m_enabled.store(enabled);
}

void LoudnessProcessor::setAmount(int amount)
{
    m_amount.store(clampLoudnessAmount(amount));
}

void LoudnessProcessor::setSampleRate(float sampleRate)
{
    if (sampleRate <= 0.f) {
        return;
    }
    if (std::fabs(m_sampleRate - sampleRate) > 0.5f) {
        m_sampleRate = sampleRate;
        updateFilters();
        reset();
    }
}

void LoudnessProcessor::reset()
{
    m_meanSquare = kMinMeanSquare;
    m_currentGainDb = 0.f;
    m_highPassLeft.reset();
    m_highPassRight.reset();
    m_highShelfLeft.reset();
    m_highShelfRight.reset();
}

float LoudnessProcessor::targetLoudnessDbForAmount(int amount)
{
    const int clamped = clampLoudnessAmount(amount);
    if (clamped <= 0) {
        return kTargetLoudnessMinDb;
    }
    const float mix = static_cast<float>(clamped) / static_cast<float>(DynamicRangeSettings::kLoudnessMax);
    return kTargetLoudnessMinDb + mix * (kTargetLoudnessMaxDb - kTargetLoudnessMinDb);
}

void LoudnessProcessor::updateFilters()
{
    setHighPassCoeffs(&m_highPassLeft, 60.f);
    setHighPassCoeffs(&m_highPassRight, 60.f);
    setHighShelfCoeffs(&m_highShelfLeft, 4000.f, 4.f);
    setHighShelfCoeffs(&m_highShelfRight, 4000.f, 4.f);

    m_loudnessCoeff = coeffForTime(kLoudnessWindowSeconds, m_sampleRate);
    m_attackCoeff = coeffForTime(kAttackSeconds, m_sampleRate);
    m_releaseCoeff = coeffForTime(kReleaseSeconds, m_sampleRate);
}

float LoudnessProcessor::processKWeightedSample(float input, BiquadState *highPass, BiquadState *highShelf)
{
    const float hp = highPass->processSample(input);
    return highShelf->processSample(hp);
}

float LoudnessProcessor::computeGainDb(float measuredLoudnessDb, float targetLoudnessDb) const
{
    const int amount = m_amount.load();
    if (amount <= 0) {
        return 0.f;
    }

    const float correctionDb = targetLoudnessDb - measuredLoudnessDb;
    return std::clamp(correctionDb, -kMaxCutDb, kMaxBoostDb);
}

float LoudnessProcessor::softLimitSample(float sample)
{
    if (sample > kPeakCeiling) {
        sample = kPeakCeiling + (sample - kPeakCeiling) / (1.f + (sample - kPeakCeiling) * 8.f);
    }
    if (sample < -kPeakCeiling) {
        sample = -kPeakCeiling + (sample + kPeakCeiling) / (1.f - (sample + kPeakCeiling) * 8.f);
    }
    return std::clamp(sample, -0.99f, 0.99f);
}

void LoudnessProcessor::process(float *interleaved, int frameCount, int channelCount)
{
    if (!interleaved || frameCount <= 0 || channelCount <= 0 || !m_enabled.load()) {
        return;
    }

    const int amount = m_amount.load();
    if (amount <= 0) {
        return;
    }

    const float targetLoudnessDb = targetLoudnessDbForAmount(amount);

    for (int frame = 0; frame < frameCount; ++frame) {
        float sumSquares = 0.f;
        for (int channel = 0; channel < channelCount; ++channel) {
            const size_t index = static_cast<size_t>(frame * channelCount + channel);
            const float input = interleaved[index];
            BiquadState *highPass = channel == 0 ? &m_highPassLeft : &m_highPassRight;
            BiquadState *highShelf = channel == 0 ? &m_highShelfLeft : &m_highShelfRight;
            if (channelCount == 1) {
                highPass = &m_highPassLeft;
                highShelf = &m_highShelfLeft;
            }
            const float weighted = processKWeightedSample(input, highPass, highShelf);
            sumSquares += weighted * weighted;
        }

        const float instantMeanSquare = sumSquares / static_cast<float>(channelCount);
        m_meanSquare = m_loudnessCoeff * m_meanSquare + (1.f - m_loudnessCoeff) * instantMeanSquare;

        const float measuredLoudnessDb = 10.f * std::log10(std::max(m_meanSquare, kMinMeanSquare));
        const float targetGainDb = computeGainDb(measuredLoudnessDb, targetLoudnessDb);

        if (targetGainDb > m_currentGainDb) {
            m_currentGainDb = m_attackCoeff * m_currentGainDb + (1.f - m_attackCoeff) * targetGainDb;
        } else {
            m_currentGainDb = m_releaseCoeff * m_currentGainDb + (1.f - m_releaseCoeff) * targetGainDb;
        }

        const float gain = std::pow(10.f, m_currentGainDb / 20.f);
        for (int channel = 0; channel < channelCount; ++channel) {
            const size_t index = static_cast<size_t>(frame * channelCount + channel);
            interleaved[index] = softLimitSample(interleaved[index] * gain);
        }
    }
}
