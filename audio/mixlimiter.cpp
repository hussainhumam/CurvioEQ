#include "mixlimiter.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kKnee = 0.08f;
constexpr float kAttackSeconds = 0.002f;
constexpr float kReleaseSeconds = 0.05f;
}

MixLimiter::MixLimiter(float linearPeak)
{
    setThreshold(linearPeak);
}

float MixLimiter::dbToLinear(float db)
{
    return std::pow(10.f, db / 20.f);
}

float MixLimiter::linearToDb(float linear)
{
    return 20.f * std::log10(std::max(linear, 1e-12f));
}

void MixLimiter::setSampleRate(float sampleRate)
{
    m_sampleRate = std::max(sampleRate, 1.f);
    reset();
}

void MixLimiter::setThreshold(float linearPeak)
{
    m_threshold.store(std::clamp(linearPeak, 0.001f, 1.f), std::memory_order_relaxed);
}

float MixLimiter::threshold() const
{
    return m_threshold.load(std::memory_order_relaxed);
}

void MixLimiter::reset()
{
    m_envelope = 0.f;
    m_envelopeD = 0.0;
}

void MixLimiter::setUseDoublePrecision(bool enabled)
{
    m_useDouble.store(enabled, std::memory_order_release);
    reset();
}

void MixLimiter::process(float *interleaved, int frameCount, int channelCount)
{
    if (!interleaved || frameCount <= 0 || channelCount <= 0) {
        return;
    }

    const float threshold = m_threshold.load(std::memory_order_relaxed);
    if (threshold >= kBypassThreshold) {
        return;
    }

    const float knee = std::min(kKnee, std::max(0.001f, threshold * 0.4f));
    const float kneeStart = threshold - knee;
    const float releaseCoeff = std::exp(-1.f / (kReleaseSeconds * m_sampleRate));
    const float attackCoeff = std::exp(-1.f / (kAttackSeconds * m_sampleRate));
    const bool useDouble = m_useDouble.load(std::memory_order_acquire);

    for (int frame = 0; frame < frameCount; ++frame) {
        if (useDouble) {
            double peak = 0.0;
            for (int channel = 0; channel < channelCount; ++channel) {
                peak = std::max(
                    peak,
                    std::fabs(static_cast<double>(
                        interleaved[static_cast<size_t>(frame * channelCount + channel)])));
            }
            const double attack = static_cast<double>(attackCoeff);
            const double release = static_cast<double>(releaseCoeff);
            if (peak > m_envelopeD) {
                m_envelopeD = attack * m_envelopeD + (1.0 - attack) * peak;
            } else {
                m_envelopeD = release * m_envelopeD + (1.0 - release) * peak;
            }

            double gain = 1.0;
            if (m_envelopeD > static_cast<double>(kneeStart)) {
                const double over = m_envelopeD - static_cast<double>(kneeStart);
                const double compressed = static_cast<double>(kneeStart) + over / (1.0 + over / static_cast<double>(knee));
                gain = compressed / std::max(m_envelopeD, 1e-9);
            }
            if (gain >= 0.999) {
                continue;
            }
            for (int channel = 0; channel < channelCount; ++channel) {
                const size_t index = static_cast<size_t>(frame * channelCount + channel);
                interleaved[index] = static_cast<float>(static_cast<double>(interleaved[index]) * gain);
            }
            continue;
        }
        float peak = 0.f;
        for (int channel = 0; channel < channelCount; ++channel) {
            peak = std::max(peak, std::fabs(interleaved[static_cast<size_t>(frame * channelCount + channel)]));
        }

        if (peak > m_envelope) {
            m_envelope = attackCoeff * m_envelope + (1.f - attackCoeff) * peak;
        } else {
            m_envelope = releaseCoeff * m_envelope + (1.f - releaseCoeff) * peak;
        }

        float gain = 1.f;
        if (m_envelope > kneeStart) {
            const float over = m_envelope - kneeStart;
            const float compressed = kneeStart + over / (1.f + over / knee);
            gain = compressed / std::max(m_envelope, 1e-9f);
        }

        if (gain >= 0.999f) {
            continue;
        }

        for (int channel = 0; channel < channelCount; ++channel) {
            const size_t index = static_cast<size_t>(frame * channelCount + channel);
            interleaved[index] *= gain;
        }
    }
}
