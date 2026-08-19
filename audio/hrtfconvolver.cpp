#include "hrtfconvolver.h"

#include <algorithm>
#include <cstring>

void HrtfConvolver::configure(const float *irLeft, const float *irRight, int irLength, int blockSize)
{
    reset();

    if (!irLeft || !irRight || irLength <= 0) {
        return;
    }

    m_blockSize = blockSize > 0 ? blockSize : 256;
    m_irLength = irLength;
    m_irLeft.assign(static_cast<size_t>(irLength), 0.f);
    m_irRight.assign(static_cast<size_t>(irLength), 0.f);
    m_history.assign(static_cast<size_t>(irLength), 0.f);

    for (int i = 0; i < irLength; ++i) {
        m_irLeft[static_cast<size_t>(i)] = irLeft[i];
        m_irRight[static_cast<size_t>(i)] = irRight[i];
    }
}

void HrtfConvolver::reset()
{
    m_historyIndex = 0;
    std::fill(m_history.begin(), m_history.end(), 0.f);
}

void HrtfConvolver::processAccumulate(const float *monoInput, float *stereoOutput, int frameCount)
{
    if (!monoInput || !stereoOutput || frameCount <= 0 || m_irLength <= 0) {
        return;
    }

    for (int frame = 0; frame < frameCount; ++frame) {
        m_history[static_cast<size_t>(m_historyIndex)] = monoInput[frame];

        float sumLeft = 0.f;
        float sumRight = 0.f;
        int sampleIndex = m_historyIndex;
        for (int tap = 0; tap < m_irLength; ++tap) {
            const float sample = m_history[static_cast<size_t>(sampleIndex)];
            sumLeft += sample * m_irLeft[static_cast<size_t>(tap)];
            sumRight += sample * m_irRight[static_cast<size_t>(tap)];
            if (--sampleIndex < 0) {
                sampleIndex = m_irLength - 1;
            }
        }

        stereoOutput[static_cast<size_t>(frame * 2)] += sumLeft;
        stereoOutput[static_cast<size_t>(frame * 2 + 1)] += sumRight;

        m_historyIndex = (m_historyIndex + 1) % m_irLength;
    }
}
