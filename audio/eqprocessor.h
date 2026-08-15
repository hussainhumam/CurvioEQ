#pragma once

#include <array>
#include <atomic>
#include <cmath>

class EqProcessor
{
public:
    static constexpr int kBandCount = 10;
    static constexpr int kMaxChannels = 2;

    static constexpr std::array<float, kBandCount> kBandFreqs = {
        20.f, 40.f, 160.f, 300.f, 600.f,
        1200.f, 2400.f, 5000.f, 10000.f, 20000.f
    };

    EqProcessor();

    void setSampleRate(float sampleRate);
    void setBandGain(int band, float gainDb);
    void setGains(const std::array<float, kBandCount> &gainsDb);

    void process(float *interleavedSamples, int frameCount, int channelCount);

private:
    struct BiquadCoeffs {
        float b0 = 1.f;
        float b1 = 0.f;
        float b2 = 0.f;
        float a1 = 0.f;
        float a2 = 0.f;
    };

    struct Biquad {
        std::array<BiquadCoeffs, 2> coeffs{};
        std::atomic<int> activeCoeffIndex{0};
        float z1 = 0.f;
        float z2 = 0.f;

        float processSample(float input);
        void reset();
    };

    void updateCoefficients(int band);

    float m_sampleRate = 48000.f;
    std::array<std::atomic<float>, kBandCount> m_gainsDb{};
    std::array<Biquad, kBandCount> m_biquadsLeft{};
    std::array<Biquad, kBandCount> m_biquadsRight{};
};
