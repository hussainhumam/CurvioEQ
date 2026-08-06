#include "surroundprocessor.h"

#include <algorithm>
#include <cmath>

SurroundProcessor::SurroundProcessor()
{
    for (auto &level : m_levels) {
        level.store(50);
    }
}

void SurroundProcessor::setEnabled(bool enabled)
{
    m_enabled.store(enabled);
}

bool SurroundProcessor::isEnabled() const
{
    return m_enabled.load();
}

void SurroundProcessor::setChannelLevel(int channel, int level)
{
    if (channel < 0 || channel >= kChannelCount) {
        return;
    }
    m_levels[static_cast<size_t>(channel)].store(std::clamp(level, 0, 100));
}

void SurroundProcessor::setChannelLevels(const std::array<int, kChannelCount> &levels)
{
    for (int channel = 0; channel < kChannelCount; ++channel) {
        setChannelLevel(channel, levels[static_cast<size_t>(channel)]);
    }
}

float SurroundProcessor::levelToMultiplier(int level)
{
    return static_cast<float>(std::clamp(level, 0, 100)) / 50.f;
}

void SurroundProcessor::process(const float *stereoIn, float *out, int frameCount) const
{
    if (!stereoIn || !out || frameCount <= 0) {
        return;
    }

    const float gainFl = levelToMultiplier(m_levels[FrontLeft].load());
    const float gainFr = levelToMultiplier(m_levels[FrontRight].load());
    const float gainFc = levelToMultiplier(m_levels[FrontCenter].load());
    const float gainLfe = levelToMultiplier(m_levels[Lfe].load());
    const float gainBl = levelToMultiplier(m_levels[BackLeft].load());
    const float gainBr = levelToMultiplier(m_levels[BackRight].load());
    const float gainSl = levelToMultiplier(m_levels[SideLeft].load());
    const float gainSr = levelToMultiplier(m_levels[SideRight].load());

    for (int frame = 0; frame < frameCount; ++frame) {
        const float left = stereoIn[static_cast<size_t>(frame * 2)];
        const float right = stereoIn[static_cast<size_t>(frame * 2 + 1)];
        const float center = 0.70710678f * (left + right);

        float *frameOut = out + static_cast<size_t>(frame * kChannelCount);
        frameOut[FrontLeft] = left * gainFl;
        frameOut[FrontRight] = right * gainFr;
        frameOut[FrontCenter] = center * gainFc;
        frameOut[Lfe] = 0.5f * (left + right) * gainLfe;
        frameOut[BackLeft] = 0.5f * left * gainBl;
        frameOut[BackRight] = 0.5f * right * gainBr;
        frameOut[SideLeft] = 0.3f * left * gainSl;
        frameOut[SideRight] = 0.3f * right * gainSr;
    }
}
