#pragma once

#include <array>
#include <atomic>
#include <vector>

class SpectrumCeilingLimiter
{
public:
    static constexpr int kFftSize = 2048;
    static constexpr int kHop = 512;
    static constexpr int kBandCount = 48;
    static constexpr float kBypassThreshold = 0.999f;

    SpectrumCeilingLimiter() = default;
    explicit SpectrumCeilingLimiter(float linearPeak);

    void setSampleRate(float sampleRate);
    void setThreshold(float linearPeak);
    float threshold() const;
    void reset();
    void process(float *interleaved, int frameCount, int channelCount);

private:
    static constexpr int kMaxChannels = 8;

    void ensureLayout(int channelCount);
    void rebuildBands();
    void processHop();

    float m_sampleRate = 48000.f;
    int m_channelCount = 0;
    int m_hopFill = 0;
    int m_outRead = 0;
    int m_outCount = 0;
    std::atomic<float> m_threshold{1.f};

    std::vector<float> m_window;
    std::array<int, kBandCount> m_barBin0{};
    std::array<int, kBandCount> m_barBin1{};
    std::array<float, kBandCount> m_envelope{};

    std::array<std::vector<float>, kMaxChannels> m_input;
    std::array<std::vector<float>, kMaxChannels> m_ola;
    std::array<std::vector<float>, kMaxChannels> m_real;
    std::array<std::vector<float>, kMaxChannels> m_imag;
    std::array<std::vector<float>, kMaxChannels> m_hop;
    std::array<std::vector<float>, kMaxChannels> m_outRing;
};
