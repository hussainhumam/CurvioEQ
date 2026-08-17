#include "mixlimiter.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kThreshold = 0.97f;
constexpr float kKnee = 0.08f;
constexpr float kAttackSeconds = 0.002f;
constexpr float kReleaseSeconds = 0.05f;
}

void MixLimiter::setSampleRate(float sampleRate)
{
    m_sampleRate = std::max(sampleRate, 1.f);
    reset();
}

void MixLimiter::reset()
{
    m_envelope = 0.f;
}

void MixLimiter::process(float *interleaved, int frameCount, int channelCount)
{
    if (!interleaved || frameCount <= 0 || channelCount <= 0) {
        return;
    }

    const float releaseCoeff = std::exp(-1.f / (kReleaseSeconds * m_sampleRate));
    const float attackCoeff = std::exp(-1.f / (kAttackSeconds * m_sampleRate));

    for (int frame = 0; frame < frameCount; ++frame) {
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
        if (m_envelope > kThreshold - kKnee) {
            const float over = m_envelope - (kThreshold - kKnee);
            const float compressed = (kThreshold - kKnee) + over / (1.f + over / kKnee);
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
