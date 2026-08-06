#pragma once

#include <array>
#include <atomic>

class SurroundProcessor
{
public:
    static constexpr int kChannelCount = 8;

    enum Channel {
        FrontLeft = 0,
        FrontRight,
        FrontCenter,
        Lfe,
        BackLeft,
        BackRight,
        SideLeft,
        SideRight,
    };

    SurroundProcessor();

    void setEnabled(bool enabled);
    bool isEnabled() const;

    void setChannelLevel(int channel, int level);
    void setChannelLevels(const std::array<int, kChannelCount> &levels);

    void process(const float *stereoIn, float *out, int frameCount) const;

private:
    static float levelToMultiplier(int level);

    std::atomic<bool> m_enabled{false};
    std::array<std::atomic<int>, kChannelCount> m_levels{};
};
