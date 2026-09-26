#include "loudnessprocessor.h"

#include "dynamicrangesettings.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kPeakCeiling = 0.944f; // ~ -0.5 dBFS
constexpr float kMaxBoostDb = 12.f;
constexpr float kMaxCutDb = 6.f;
constexpr float kLoudnessWindowSeconds = 0.400f;
constexpr float kAttackSeconds = 0.300f;
constexpr float kReleaseSeconds = 2.000f;
constexpr float kMinMeanSquare = 1e-12f;
constexpr float kLufsOffsetDb = -0.691f;

// ITU-R BS.1770-4 / libebur128 analog prototypes
constexpr float kPreFilterF0 = 1681.974450955533f;
constexpr float kPreFilterGainDb = 3.999843853973347f;
constexpr float kPreFilterQ = 0.7071752369554196f;
constexpr float kRlbF0 = 38.13547087602444f;
constexpr float kRlbQ = 0.5003270373238773f;

float coeffForTime(float seconds, float sampleRate)
{
    if (seconds <= 0.f || sampleRate <= 0.f) {
        return 0.f;
    }
    return std::exp(-1.f / (seconds * sampleRate));
}

} // namespace

void LoudnessProcessor::setKWeightingPreFilter(BiquadState *filter)
{
    const float A = std::pow(10.f, kPreFilterGainDb / 40.f);
    const float omega = 2.f * kPi * kPreFilterF0 / m_sampleRate;
    const float sinOmega = std::sin(omega);
    const float cosOmega = std::cos(omega);
    const float alpha = sinOmega / (2.f * kPreFilterQ);

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

void LoudnessProcessor::setKWeightingRlbFilter(BiquadState *filter)
{
    const float omega = 2.f * kPi * kRlbF0 / m_sampleRate;
    const float sinOmega = std::sin(omega);
    const float cosOmega = std::cos(omega);
    const float alpha = sinOmega / (2.f * kRlbQ);

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

float LoudnessProcessor::BiquadState::processSample(float input)
{
    const float output = b0 * input + z1;
    z1 = b1 * input - a1 * output + z2;
    z2 = b2 * input - a2 * output;
    return output;
}

double LoudnessProcessor::BiquadState::processSampleD(double input)
{
    const double output = static_cast<double>(b0) * input + dz1;
    dz1 = static_cast<double>(b1) * input - static_cast<double>(a1) * output + dz2;
    dz2 = static_cast<double>(b2) * input - static_cast<double>(a2) * output;
    return output;
}

void LoudnessProcessor::BiquadState::reset()
{
    z1 = 0.f;
    z2 = 0.f;
    dz1 = 0.0;
    dz2 = 0.0;
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

void LoudnessProcessor::setUseDoublePrecision(bool enabled)
{
    m_useDouble.store(enabled, std::memory_order_release);
    reset();
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
    m_currentGainDb = 0.f;
    m_preFilterLeft.reset();
    m_preFilterRight.reset();
    m_rlbLeft.reset();
    m_rlbRight.reset();
    std::fill(m_momentaryRing.begin(), m_momentaryRing.end(), 0.f);
    m_momentaryIndex = 0;
    m_momentaryFilled = 0;
    m_momentarySum = 0.0;
}

float LoudnessProcessor::targetLoudnessDbForAmount(int amount)
{
    if (clampLoudnessAmount(amount) <= 0) {
        return DynamicRangeSettings::kLoudnessQuietLufs;
    }
    return loudnessAmountToTargetLufs(amount);
}

void LoudnessProcessor::updateFilters()
{
    setKWeightingPreFilter(&m_preFilterLeft);
    setKWeightingPreFilter(&m_preFilterRight);
    setKWeightingRlbFilter(&m_rlbLeft);
    setKWeightingRlbFilter(&m_rlbRight);

    const int windowSamples = std::max(1, static_cast<int>(std::lround(kLoudnessWindowSeconds * m_sampleRate)));
    m_momentaryRing.assign(static_cast<std::size_t>(windowSamples), 0.f);
    m_momentaryIndex = 0;
    m_momentaryFilled = 0;
    m_momentarySum = 0.0;

    m_attackCoeff = coeffForTime(kAttackSeconds, m_sampleRate);
    m_releaseCoeff = coeffForTime(kReleaseSeconds, m_sampleRate);
}

float LoudnessProcessor::processKWeightedSample(float input, BiquadState *preFilter, BiquadState *rlb)
{
    const float pre = preFilter->processSample(input);
    return rlb->processSample(pre);
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

    if (m_momentaryRing.empty()) {
        updateFilters();
    }

    const float targetLoudnessDb = targetLoudnessDbForAmount(amount);
    const std::size_t ringSize = m_momentaryRing.size();
    const bool useDouble = m_useDouble.load(std::memory_order_acquire);

    for (int frame = 0; frame < frameCount; ++frame) {
        double weightedPower = 0.0;
        for (int channel = 0; channel < channelCount; ++channel) {
            const std::size_t index = static_cast<std::size_t>(frame * channelCount + channel);
            BiquadState *preFilter = channel == 0 ? &m_preFilterLeft : &m_preFilterRight;
            BiquadState *rlb = channel == 0 ? &m_rlbLeft : &m_rlbRight;
            if (channelCount == 1) {
                preFilter = &m_preFilterLeft;
                rlb = &m_rlbLeft;
            }
            if (useDouble) {
                const double input = static_cast<double>(interleaved[index]);
                const double weighted = rlb->processSampleD(preFilter->processSampleD(input));
                weightedPower += weighted * weighted;
            } else {
                const float input = interleaved[index];
                const float weighted = processKWeightedSample(input, preFilter, rlb);
                weightedPower += static_cast<double>(weighted) * static_cast<double>(weighted);
            }
        }

        if (m_momentaryFilled == ringSize) {
            m_momentarySum -= static_cast<double>(m_momentaryRing[m_momentaryIndex]);
        } else {
            ++m_momentaryFilled;
        }
        m_momentaryRing[m_momentaryIndex] = static_cast<float>(weightedPower);
        m_momentarySum += weightedPower;
        m_momentaryIndex = (m_momentaryIndex + 1) % ringSize;

        const float meanWeightedPower =
            static_cast<float>(m_momentarySum / static_cast<double>(std::max<std::size_t>(m_momentaryFilled, 1)));
        const float measuredLoudnessDb =
            kLufsOffsetDb + 10.f * std::log10(std::max(meanWeightedPower, kMinMeanSquare));
        const float targetGainDb = computeGainDb(measuredLoudnessDb, targetLoudnessDb);

        if (targetGainDb > m_currentGainDb) {
            m_currentGainDb = m_attackCoeff * m_currentGainDb + (1.f - m_attackCoeff) * targetGainDb;
        } else {
            m_currentGainDb = m_releaseCoeff * m_currentGainDb + (1.f - m_releaseCoeff) * targetGainDb;
        }

        const float gain = std::pow(10.f, m_currentGainDb / 20.f);
        for (int channel = 0; channel < channelCount; ++channel) {
            const std::size_t index = static_cast<std::size_t>(frame * channelCount + channel);
            interleaved[index] = softLimitSample(interleaved[index] * gain);
        }
    }
}
