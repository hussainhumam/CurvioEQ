#include "spectrumceilinglimiter.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kReleaseSeconds = 0.08f;
constexpr int kOutCap = 4096;
constexpr float kCola = 2.f;

void fftRadix2(std::vector<float> &real, std::vector<float> &imag)
{
    const size_t n = real.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(real[i], real[j]);
            std::swap(imag[i], imag[j]);
        }
    }

    for (size_t len = 2; len <= n; len <<= 1) {
        const float angle = -2.f * kPi / static_cast<float>(len);
        const float wlenReal = std::cos(angle);
        const float wlenImag = std::sin(angle);
        for (size_t i = 0; i < n; i += len) {
            float wReal = 1.f;
            float wImag = 0.f;
            for (size_t j = 0; j < len / 2; ++j) {
                const size_t u = i + j;
                const size_t v = i + j + len / 2;
                const float tReal = wReal * real[v] - wImag * imag[v];
                const float tImag = wReal * imag[v] + wImag * real[v];
                real[v] = real[u] - tReal;
                imag[v] = imag[u] - tImag;
                real[u] += tReal;
                imag[u] += tImag;

                const float nextWReal = wReal * wlenReal - wImag * wlenImag;
                wImag = wReal * wlenImag + wImag * wlenReal;
                wReal = nextWReal;
            }
        }
    }
}

void ifftRadix2(std::vector<float> &real, std::vector<float> &imag)
{
    const size_t n = real.size();
    for (size_t i = 0; i < n; ++i) {
        imag[i] = -imag[i];
    }
    fftRadix2(real, imag);
    const float scale = 1.f / static_cast<float>(n);
    for (size_t i = 0; i < n; ++i) {
        real[i] *= scale;
        imag[i] = -imag[i] * scale;
    }
}
} // namespace

SpectrumCeilingLimiter::SpectrumCeilingLimiter(float linearPeak)
{
    setThreshold(linearPeak);
}

void SpectrumCeilingLimiter::setSampleRate(float sampleRate)
{
    m_sampleRate = std::max(sampleRate, 1.f);
    rebuildBands();
    reset();
}

void SpectrumCeilingLimiter::setThreshold(float linearPeak)
{
    m_threshold.store(std::clamp(linearPeak, 0.001f, 1.f), std::memory_order_relaxed);
}

float SpectrumCeilingLimiter::threshold() const
{
    return m_threshold.load(std::memory_order_relaxed);
}

void SpectrumCeilingLimiter::reset()
{
    m_hopFill = 0;
    m_outRead = 0;
    m_outCount = 0;
    m_envelope.fill(1.f);
    for (int channel = 0; channel < kMaxChannels; ++channel) {
        std::fill(m_input[static_cast<size_t>(channel)].begin(), m_input[static_cast<size_t>(channel)].end(), 0.f);
        std::fill(m_ola[static_cast<size_t>(channel)].begin(), m_ola[static_cast<size_t>(channel)].end(), 0.f);
        std::fill(m_hop[static_cast<size_t>(channel)].begin(), m_hop[static_cast<size_t>(channel)].end(), 0.f);
        std::fill(m_outRing[static_cast<size_t>(channel)].begin(), m_outRing[static_cast<size_t>(channel)].end(), 0.f);
    }
}

void SpectrumCeilingLimiter::rebuildBands()
{
    m_window.resize(static_cast<size_t>(kFftSize));
    for (int i = 0; i < kFftSize; ++i) {
        m_window[static_cast<size_t>(i)] =
            0.5f * (1.f - std::cos(2.f * kPi * static_cast<float>(i) / static_cast<float>(kFftSize)));
    }

    const int halfBins = kFftSize / 2;
    const float minFreq = 20.f;
    const float maxFreq = m_sampleRate * 0.5f;
    const float logMin = std::log10(minFreq);
    const float logMax = std::log10(std::max(maxFreq, minFreq + 1.f));

    for (int bar = 0; bar < kBandCount; ++bar) {
        const float t0 = static_cast<float>(bar) / static_cast<float>(kBandCount);
        const float t1 = static_cast<float>(bar + 1) / static_cast<float>(kBandCount);
        const float freq0 = std::pow(10.f, logMin + (logMax - logMin) * t0);
        const float freq1 = std::pow(10.f, logMin + (logMax - logMin) * t1);
        int bin0 = std::max(1, static_cast<int>(freq0 * static_cast<float>(kFftSize) / m_sampleRate));
        int bin1 = std::min(halfBins - 1, static_cast<int>(freq1 * static_cast<float>(kFftSize) / m_sampleRate));
        if (bin1 < bin0) {
            bin1 = bin0;
        }
        m_barBin0[static_cast<size_t>(bar)] = bin0;
        m_barBin1[static_cast<size_t>(bar)] = bin1;
    }
}

void SpectrumCeilingLimiter::ensureLayout(int channelCount)
{
    const int channels = std::clamp(channelCount, 1, kMaxChannels);
    if (channels == m_channelCount && !m_input[0].empty()) {
        return;
    }
    m_channelCount = channels;
    for (int channel = 0; channel < kMaxChannels; ++channel) {
        m_input[static_cast<size_t>(channel)].assign(static_cast<size_t>(kFftSize), 0.f);
        m_ola[static_cast<size_t>(channel)].assign(static_cast<size_t>(kFftSize), 0.f);
        m_real[static_cast<size_t>(channel)].assign(static_cast<size_t>(kFftSize), 0.f);
        m_imag[static_cast<size_t>(channel)].assign(static_cast<size_t>(kFftSize), 0.f);
        m_hop[static_cast<size_t>(channel)].assign(static_cast<size_t>(kHop), 0.f);
        m_outRing[static_cast<size_t>(channel)].assign(static_cast<size_t>(kOutCap), 0.f);
    }
    if (m_window.size() != static_cast<size_t>(kFftSize)) {
        rebuildBands();
    }
    reset();
}

void SpectrumCeilingLimiter::processHop()
{
    const float magScale = 4.f / static_cast<float>(kFftSize);
    const float threshold = m_threshold.load(std::memory_order_relaxed);
    const float releaseCoeff = std::exp(-static_cast<float>(kHop) / (kReleaseSeconds * m_sampleRate));

    for (int channel = 0; channel < m_channelCount; ++channel) {
        auto &input = m_input[static_cast<size_t>(channel)];
        std::memmove(input.data(),
                     input.data() + kHop,
                     static_cast<size_t>(kFftSize - kHop) * sizeof(float));
        std::memcpy(input.data() + (kFftSize - kHop),
                    m_hop[static_cast<size_t>(channel)].data(),
                    static_cast<size_t>(kHop) * sizeof(float));

        auto &real = m_real[static_cast<size_t>(channel)];
        auto &imag = m_imag[static_cast<size_t>(channel)];
        for (int i = 0; i < kFftSize; ++i) {
            real[static_cast<size_t>(i)] = input[static_cast<size_t>(i)] * m_window[static_cast<size_t>(i)];
            imag[static_cast<size_t>(i)] = 0.f;
        }
        fftRadix2(real, imag);
    }

    std::array<float, kBandCount> gains{};
    for (int bar = 0; bar < kBandCount; ++bar) {
        float peak = 0.f;
        const int bin0 = m_barBin0[static_cast<size_t>(bar)];
        const int bin1 = m_barBin1[static_cast<size_t>(bar)];
        for (int channel = 0; channel < m_channelCount; ++channel) {
            const auto &real = m_real[static_cast<size_t>(channel)];
            const auto &imag = m_imag[static_cast<size_t>(channel)];
            for (int bin = bin0; bin <= bin1; ++bin) {
                const float mag = magScale
                    * std::sqrt(real[static_cast<size_t>(bin)] * real[static_cast<size_t>(bin)]
                                + imag[static_cast<size_t>(bin)] * imag[static_cast<size_t>(bin)]);
                peak = std::max(peak, mag);
            }
        }

        float target = 1.f;
        if (peak > threshold) {
            target = threshold / std::max(peak, 1e-9f);
        }

        float &env = m_envelope[static_cast<size_t>(bar)];
        if (target < env) {
            env = target;
        } else {
            env = releaseCoeff * env + (1.f - releaseCoeff) * target;
        }
        gains[static_cast<size_t>(bar)] = env;
    }

    std::array<float, kFftSize / 2> binGain{};
    binGain.fill(1.f);
    for (int bar = 0; bar < kBandCount; ++bar) {
        const float gain = gains[static_cast<size_t>(bar)];
        for (int bin = m_barBin0[static_cast<size_t>(bar)]; bin <= m_barBin1[static_cast<size_t>(bar)]; ++bin) {
            binGain[static_cast<size_t>(bin)] = std::min(binGain[static_cast<size_t>(bin)], gain);
        }
    }

    for (int channel = 0; channel < m_channelCount; ++channel) {
        auto &real = m_real[static_cast<size_t>(channel)];
        auto &imag = m_imag[static_cast<size_t>(channel)];
        for (int bin = 1; bin < kFftSize / 2; ++bin) {
            const float gain = binGain[static_cast<size_t>(bin)];
            if (gain >= 0.999f) {
                continue;
            }
            real[static_cast<size_t>(bin)] *= gain;
            imag[static_cast<size_t>(bin)] *= gain;
            real[static_cast<size_t>(kFftSize - bin)] *= gain;
            imag[static_cast<size_t>(kFftSize - bin)] *= gain;
        }
        ifftRadix2(real, imag);

        auto &ola = m_ola[static_cast<size_t>(channel)];
        for (int i = 0; i < kFftSize; ++i) {
            ola[static_cast<size_t>(i)] += real[static_cast<size_t>(i)];
        }

        auto &outRing = m_outRing[static_cast<size_t>(channel)];
        for (int i = 0; i < kHop; ++i) {
            if (m_outCount + i >= kOutCap) {
                break;
            }
            const int write = (m_outRead + m_outCount + i) % kOutCap;
            outRing[static_cast<size_t>(write)] = ola[static_cast<size_t>(i)] / kCola;
        }

        std::memmove(ola.data(), ola.data() + kHop, static_cast<size_t>(kFftSize - kHop) * sizeof(float));
        std::fill(ola.begin() + (kFftSize - kHop), ola.end(), 0.f);
    }

    m_outCount = std::min(kOutCap, m_outCount + kHop);
}

void SpectrumCeilingLimiter::process(float *interleaved, int frameCount, int channelCount)
{
    if (!interleaved || frameCount <= 0 || channelCount <= 0) {
        return;
    }

    const float threshold = m_threshold.load(std::memory_order_relaxed);
    if (threshold >= kBypassThreshold) {
        return;
    }

    const int channels = std::min(channelCount, kMaxChannels);
    ensureLayout(channels);

    for (int frame = 0; frame < frameCount; ++frame) {
        for (int channel = 0; channel < channels; ++channel) {
            m_hop[static_cast<size_t>(channel)][static_cast<size_t>(m_hopFill)] =
                interleaved[static_cast<size_t>(frame * channelCount + channel)];
        }
        ++m_hopFill;
        if (m_hopFill >= kHop) {
            processHop();
            m_hopFill = 0;
        }

        if (m_outCount > 0) {
            for (int channel = 0; channel < channels; ++channel) {
                interleaved[static_cast<size_t>(frame * channelCount + channel)] =
                    m_outRing[static_cast<size_t>(channel)][static_cast<size_t>(m_outRead)];
            }
            m_outRead = (m_outRead + 1) % kOutCap;
            --m_outCount;
        } else {
            for (int channel = 0; channel < channels; ++channel) {
                interleaved[static_cast<size_t>(frame * channelCount + channel)] = 0.f;
            }
        }
    }
}
