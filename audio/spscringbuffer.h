#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <vector>

class SpscRingBuffer
{
public:
    void configure(int channelCount, size_t capacityFrames)
    {
        m_channelCount = std::max(1, channelCount);
        size_t capacity = std::max<size_t>(capacityFrames, 64);
        if ((capacity & (capacity - 1)) != 0) {
            size_t power = 64;
            while (power < capacity) {
                power <<= 1;
            }
            capacity = power;
        }

        m_capacityFrames = capacity;
        m_mask = capacity - 1;
        m_buffer.assign(capacity * static_cast<size_t>(m_channelCount), 0.f);
        m_writeIndex.store(0, std::memory_order_relaxed);
        m_readIndex.store(0, std::memory_order_relaxed);
    }

    void clear()
    {
        m_writeIndex.store(0, std::memory_order_relaxed);
        m_readIndex.store(0, std::memory_order_relaxed);
    }

    size_t availableFrames() const
    {
        const size_t writeIndex = m_writeIndex.load(std::memory_order_acquire);
        const size_t readIndex = m_readIndex.load(std::memory_order_acquire);
        return writeIndex - readIndex;
    }

    void write(const float *interleaved, int frameCount)
    {
        if (!interleaved || frameCount <= 0 || m_channelCount <= 0 || m_capacityFrames == 0) {
            return;
        }

        const size_t channels = static_cast<size_t>(m_channelCount);
        size_t writeIndex = m_writeIndex.load(std::memory_order_relaxed);
        const size_t readIndex = m_readIndex.load(std::memory_order_acquire);
        size_t storedFrames = writeIndex - readIndex;

        size_t incomingFrames = static_cast<size_t>(frameCount);
        const float *source = interleaved;

        if (incomingFrames >= m_capacityFrames) {
            const size_t skipFrames = incomingFrames - m_capacityFrames;
            source += skipFrames * channels;
            incomingFrames = m_capacityFrames;
            writeIndex = readIndex + m_capacityFrames;
            storedFrames = m_capacityFrames;
        } else if (storedFrames + incomingFrames > m_capacityFrames) {
            incomingFrames = m_capacityFrames - storedFrames;
            if (incomingFrames == 0) {
                return;
            }
        }

        size_t framesRemaining = incomingFrames;
        while (framesRemaining > 0) {
            const size_t writePos = writeIndex & m_mask;
            const size_t spaceUntilWrap = m_capacityFrames - writePos;
            const size_t framesThisSegment = std::min(framesRemaining, spaceUntilWrap);
            const size_t sampleCount = framesThisSegment * channels;

            std::memcpy(m_buffer.data() + writePos * channels,
                        source,
                        sampleCount * sizeof(float));

            writeIndex += framesThisSegment;
            source += sampleCount;
            framesRemaining -= framesThisSegment;
        }

        m_writeIndex.store(writeIndex, std::memory_order_release);
    }

    int readAdd(float *dest, int frameCount, int destChannelCount)
    {
        if (!dest || frameCount <= 0 || destChannelCount <= 0 || m_channelCount <= 0) {
            return 0;
        }

        const size_t srcChannels = static_cast<size_t>(m_channelCount);
        const size_t dstChannels = static_cast<size_t>(destChannelCount);

        size_t readIndex = m_readIndex.load(std::memory_order_relaxed);
        const size_t writeIndex = m_writeIndex.load(std::memory_order_acquire);
        const size_t storedFrames = writeIndex - readIndex;
        const int framesToRead = static_cast<int>(std::min(storedFrames, static_cast<size_t>(frameCount)));

        int framesRemaining = framesToRead;
        int destFrameOffset = 0;

        while (framesRemaining > 0) {
            const size_t readPos = readIndex & m_mask;
            const size_t framesUntilWrap = m_capacityFrames - readPos;
            const int framesThisSegment =
                static_cast<int>(std::min<size_t>(static_cast<size_t>(framesRemaining), framesUntilWrap));
            const float *segment = m_buffer.data() + readPos * srcChannels;

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

            readIndex += static_cast<size_t>(framesThisSegment);
            destFrameOffset += framesThisSegment;
            framesRemaining -= framesThisSegment;
        }

        m_readIndex.store(readIndex, std::memory_order_release);
        return framesToRead;
    }

private:
    int m_channelCount = 2;
    size_t m_capacityFrames = 2048;
    size_t m_mask = 2047;
    std::vector<float> m_buffer;
    alignas(64) std::atomic<size_t> m_writeIndex{0};
    alignas(64) std::atomic<size_t> m_readIndex{0};
};
