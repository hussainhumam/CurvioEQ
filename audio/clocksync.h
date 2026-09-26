#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

class ClockSync
{
public:
    void configure(size_t ringCapacityFrames, size_t targetFillFrames, size_t highFillFrames)
    {
        m_ringCapacityFrames = std::max<size_t>(ringCapacityFrames, 16);
        m_targetFillFrames = std::clamp(targetFillFrames, size_t{1}, m_ringCapacityFrames);
        m_highFillFrames = std::clamp(highFillFrames, m_targetFillFrames, m_ringCapacityFrames);
        m_pllRatio = 1.0;
        m_haveCapture = false;
        m_haveRender = false;
    }

    int trimmedWriteFrames(int incomingFrames, size_t availableFrames) const
    {
        if (incomingFrames <= 0) {
            return 0;
        }
        if (availableFrames <= m_highFillFrames) {
            return incomingFrames;
        }

        const size_t overflow = availableFrames - m_highFillFrames;
        const int maxDrop = std::max(1, incomingFrames / 8);
        const int drop = static_cast<int>(std::min(overflow, static_cast<size_t>(maxDrop)));
        return std::max(0, incomingFrames - drop);
    }

    void observeCapture(uint64_t deviceFrames, uint64_t qpc, uint64_t qpcFrequency)
    {
        if (qpcFrequency == 0 || qpc == 0) {
            return;
        }
        if (m_haveCapture && qpc > m_captureQpc && deviceFrames > m_captureFrames) {
            const double dt = static_cast<double>(qpc - m_captureQpc) / static_cast<double>(qpcFrequency);
            if (dt > 0.002 && dt < 1.0) {
                m_captureHz = static_cast<double>(deviceFrames - m_captureFrames) / dt;
                m_haveCaptureRate = m_captureHz > 1000.0;
            }
        }
        m_captureFrames = deviceFrames;
        m_captureQpc = qpc;
        m_haveCapture = true;
        updatePll();
    }

    void observeRender(uint64_t deviceFrames, uint64_t qpc, uint64_t qpcFrequency)
    {
        if (qpcFrequency == 0 || qpc == 0) {
            return;
        }
        if (m_haveRender && qpc > m_renderQpc && deviceFrames > m_renderFrames) {
            const double dt = static_cast<double>(qpc - m_renderQpc) / static_cast<double>(qpcFrequency);
            if (dt > 0.002 && dt < 1.0) {
                m_renderHz = static_cast<double>(deviceFrames - m_renderFrames) / dt;
                m_haveRenderRate = m_renderHz > 1000.0;
            }
        }
        m_renderFrames = deviceFrames;
        m_renderQpc = qpc;
        m_haveRender = true;
        updatePll();
    }

    double rateRatio(size_t availableFrames) const
    {
        if (!m_pllEnabled) {
            return 1.0;
        }
        const double fillRatio = fillRateRatio(availableFrames);
        const double combined = fillRatio * m_pllRatio;
        constexpr double kMin = 0.997;
        constexpr double kMax = 1.003;
        return std::clamp(combined, kMin, kMax);
    }

    size_t targetFillFrames() const { return m_targetFillFrames; }
    size_t highFillFrames() const { return m_highFillFrames; }
    void setPllEnabled(bool enabled)
    {
        m_pllEnabled = enabled;
        if (!enabled) {
            m_pllRatio = 1.0;
        }
    }

private:
    double fillRateRatio(size_t availableFrames) const
    {
        const double target = static_cast<double>(m_targetFillFrames);
        const double fill = static_cast<double>(availableFrames);
        const double deadband = std::max(target * 0.1, 16.0);
        if (std::fabs(fill - target) < deadband) {
            return 1.0;
        }

        const double capacity = static_cast<double>(m_ringCapacityFrames);
        const double error = (fill - target) / capacity;
        constexpr double kMaxAdj = 0.001;
        constexpr double kGain = 0.01;
        return 1.0 - std::clamp(error * kGain, -kMaxAdj, kMaxAdj);
    }

    void updatePll()
    {
        if (!m_pllEnabled || !m_haveCaptureRate || !m_haveRenderRate || m_renderHz < 1.0) {
            return;
        }
        const double err = (m_captureHz / m_renderHz) - 1.0;
        m_pllRatio = std::clamp(m_pllRatio - err * 0.02, 0.997, 1.003);
    }

    size_t m_ringCapacityFrames = 2048;
    size_t m_targetFillFrames = 512;
    size_t m_highFillFrames = 1024;
    double m_pllRatio = 1.0;
    uint64_t m_captureFrames = 0;
    uint64_t m_captureQpc = 0;
    uint64_t m_renderFrames = 0;
    uint64_t m_renderQpc = 0;
    double m_captureHz = 0.0;
    double m_renderHz = 0.0;
    bool m_haveCapture = false;
    bool m_haveRender = false;
    bool m_haveCaptureRate = false;
    bool m_haveRenderRate = false;
    bool m_pllEnabled = true;
};
