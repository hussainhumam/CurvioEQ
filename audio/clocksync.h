#pragma once

#include <algorithm>
#include <cstddef>

class ClockSync
{
public:
    void configure(size_t ringCapacityFrames, size_t targetFillFrames, size_t highFillFrames)
    {
        m_ringCapacityFrames = std::max<size_t>(ringCapacityFrames, 64);
        m_targetFillFrames = std::clamp(targetFillFrames, size_t{1}, m_ringCapacityFrames);
        m_highFillFrames = std::clamp(highFillFrames, m_targetFillFrames, m_ringCapacityFrames);
    }

    bool shouldSkipWrite(size_t availableFrames) const
    {
        return availableFrames > m_highFillFrames;
    }

private:
    size_t m_ringCapacityFrames = 2048;
    size_t m_targetFillFrames = 512;
    size_t m_highFillFrames = 1024;
};
