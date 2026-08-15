#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <vector>

class AudioRingBuffer
{
public:
    void configure(int channelCount, size_t capacityFrames)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_channelCount = std::max(1, channelCount);
        m_capacityFrames = std::max<size_t>(capacityFrames, 4096);
        m_buffer.assign(m_capacityFrames * static_cast<size_t>(m_channelCount), 0.f);
        m_readIndex = 0;
        m_writeIndex = 0;
        m_storedFrames = 0;
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_readIndex = 0;
        m_writeIndex = 0;
        m_storedFrames = 0;
    }

    void write(const float *interleaved, int frameCount)
    {
        if (!interleaved || frameCount <= 0 || m_channelCount <= 0) {
            return;
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        const size_t channels = static_cast<size_t>(m_channelCount);
        const size_t incomingFrames = static_cast<size_t>(frameCount);

        if (incomingFrames >= m_capacityFrames) {
            const size_t skipFrames = incomingFrames - m_capacityFrames;
            interleaved += skipFrames * channels;
            frameCount = static_cast<int>(m_capacityFrames);
            m_readIndex = 0;
            m_writeIndex = 0;
            m_storedFrames = 0;
        } else {
            const size_t totalAfterWrite = m_storedFrames + incomingFrames;
            if (totalAfterWrite > m_capacityFrames) {
                const size_t dropFrames = totalAfterWrite - m_capacityFrames;
                m_readIndex = (m_readIndex + dropFrames) % m_capacityFrames;
                m_storedFrames -= dropFrames;
            }
        }

        int framesRemaining = frameCount;
        const float *source = interleaved;
        while (framesRemaining > 0) {
            const size_t spaceUntilWrap = m_capacityFrames - m_writeIndex;
            const int framesThisSegment =
                static_cast<int>(std::min<size_t>(static_cast<size_t>(framesRemaining), spaceUntilWrap));
            const size_t sampleCount = static_cast<size_t>(framesThisSegment) * channels;

            std::memcpy(m_buffer.data() + m_writeIndex * channels,
                        source,
                        sampleCount * sizeof(float));

            m_writeIndex = (m_writeIndex + static_cast<size_t>(framesThisSegment)) % m_capacityFrames;
            m_storedFrames += static_cast<size_t>(framesThisSegment);
            source += sampleCount;
            framesRemaining -= framesThisSegment;
        }
    }

    int readAdd(float *dest, int frameCount, int destChannelCount)
    {
        if (!dest || frameCount <= 0 || destChannelCount <= 0 || m_channelCount <= 0) {
            return 0;
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        const int framesToRead = static_cast<int>(std::min<size_t>(m_storedFrames, static_cast<size_t>(frameCount)));
        const size_t srcChannels = static_cast<size_t>(m_channelCount);
        const size_t dstChannels = static_cast<size_t>(destChannelCount);

        int framesRemaining = framesToRead;
        int destFrameOffset = 0;

        while (framesRemaining > 0) {
            const size_t framesUntilWrap = m_capacityFrames - m_readIndex;
            const int framesThisSegment =
                static_cast<int>(std::min<size_t>(static_cast<size_t>(framesRemaining), framesUntilWrap));
            const float *segment = m_buffer.data() + m_readIndex * srcChannels;

            if (srcChannels == dstChannels) {
                const size_t sampleCount = static_cast<size_t>(framesThisSegment) * srcChannels;
                float *destSegment = dest + static_cast<size_t>(destFrameOffset) * dstChannels;
                for (size_t i = 0; i < sampleCount; ++i) {
                    destSegment[i] += segment[i];
                }
            } else {
                for (int frame = 0; frame < framesThisSegment; ++frame) {
                    const size_t src = static_cast<size_t>(frame) * srcChannels;
                    const size_t dst = static_cast<size_t>(destFrameOffset + frame) * dstChannels;
                    for (size_t ch = 0; ch < dstChannels; ++ch) {
                        const float sample = ch < srcChannels ? segment[src + ch] : 0.f;
                        dest[dst + ch] += sample;
                    }
                }
            }

            m_readIndex = (m_readIndex + static_cast<size_t>(framesThisSegment)) % m_capacityFrames;
            destFrameOffset += framesThisSegment;
            framesRemaining -= framesThisSegment;
        }

        m_storedFrames -= static_cast<size_t>(framesToRead);
        return framesToRead;
    }

    size_t availableFrames() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_storedFrames;
    }

private:
    mutable std::mutex m_mutex;
    int m_channelCount = 2;
    size_t m_capacityFrames = 8192;
    size_t m_readIndex = 0;
    size_t m_writeIndex = 0;
    size_t m_storedFrames = 0;
    std::vector<float> m_buffer;
};
