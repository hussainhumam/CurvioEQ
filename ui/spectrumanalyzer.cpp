#include "spectrumanalyzer.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kPi = 3.14159265358979323846f;

float mixdownSample(const float *samples, int frameIndex, int channelCount)
{
    float sum = 0.f;
    for (int channel = 0; channel < channelCount; ++channel) {
        sum += samples[static_cast<size_t>(frameIndex * channelCount + channel)];
    }
    return sum / static_cast<float>(channelCount);
}
} // namespace

void SpectrumCapture::reset()
{
    QMutexLocker lock(&m_mutex);
    m_beforeRing.assign(static_cast<size_t>(kFftSize), 0.f);
    m_afterRing.assign(static_cast<size_t>(kFftSize), 0.f);
    m_beforeWrite = 0;
    m_afterWrite = 0;
    m_beforeFilled = false;
    m_afterFilled = false;
}

void SpectrumCapture::setSampleRate(int sampleRate)
{
    QMutexLocker lock(&m_mutex);
    m_sampleRate = sampleRate > 0 ? sampleRate : 48000;
}

void SpectrumCapture::pushRing(std::vector<float> *ring,
                               size_t *writeIndex,
                               bool *filled,
                               const float *samples,
                               int frameCount,
                               int channelCount)
{
    if (!ring || ring->size() != static_cast<size_t>(kFftSize)) {
        ring->assign(static_cast<size_t>(kFftSize), 0.f);
    }

    for (int frame = 0; frame < frameCount; ++frame) {
        (*ring)[*writeIndex] = mixdownSample(samples, frame, channelCount);
        *writeIndex = (*writeIndex + 1) % static_cast<size_t>(kFftSize);
        if (*writeIndex == 0) {
            *filled = true;
        }
    }
}

void SpectrumCapture::pushBefore(const float *samples, int frameCount, int channelCount)
{
    QMutexLocker lock(&m_mutex);
    pushRing(&m_beforeRing, &m_beforeWrite, &m_beforeFilled, samples, frameCount, channelCount);
}

void SpectrumCapture::pushAfter(const float *samples, int frameCount, int channelCount)
{
    QMutexLocker lock(&m_mutex);
    pushRing(&m_afterRing, &m_afterWrite, &m_afterFilled, samples, frameCount, channelCount);
}

bool SpectrumCapture::snapshot(std::vector<float> *before,
                               std::vector<float> *after,
                               int *sampleRate)
{
    QMutexLocker lock(&m_mutex);
    if (!m_beforeFilled || !m_afterFilled) {
        return false;
    }

    before->assign(static_cast<size_t>(kFftSize), 0.f);
    after->assign(static_cast<size_t>(kFftSize), 0.f);

    for (size_t i = 0; i < static_cast<size_t>(kFftSize); ++i) {
        const size_t index = (m_beforeWrite + i) % static_cast<size_t>(kFftSize);
        (*before)[i] = m_beforeRing[index];
        (*after)[i] = m_afterRing[index];
    }

    if (sampleRate) {
        *sampleRate = m_sampleRate;
    }
    return true;
}

void SpectrumAnalyzer::fftRadix2(std::vector<float> &real, std::vector<float> &imag)
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

void SpectrumAnalyzer::computeBarMagnitudes(const std::vector<float> &timeDomain,
                                            int sampleRate,
                                            int barCount,
                                            QVector<float> *magnitudes)
{
    if (!magnitudes || barCount <= 0) {
        return;
    }

    const size_t n = timeDomain.size();
    if (n < 64) {
        magnitudes->fill(0.f, barCount);
        return;
    }

    std::vector<float> real(timeDomain.begin(), timeDomain.end());
    std::vector<float> imag(n, 0.f);

    for (size_t i = 0; i < n; ++i) {
        const float window = 0.5f * (1.f - std::cos(2.f * kPi * static_cast<float>(i) / static_cast<float>(n - 1)));
        real[i] *= window;
    }

    fftRadix2(real, imag);

    const int halfBins = static_cast<int>(n / 2);
    magnitudes->fill(0.f, barCount);

    const float minFreq = 20.f;
    const float maxFreq = static_cast<float>(sampleRate) * 0.5f;
    const float logMin = std::log10(minFreq);
    const float logMax = std::log10(maxFreq);

    for (int bar = 0; bar < barCount; ++bar) {
        const float t0 = static_cast<float>(bar) / static_cast<float>(barCount);
        const float t1 = static_cast<float>(bar + 1) / static_cast<float>(barCount);
        const float freq0 = std::pow(10.f, logMin + (logMax - logMin) * t0);
        const float freq1 = std::pow(10.f, logMin + (logMax - logMin) * t1);

        const int bin0 = std::max(1, static_cast<int>(freq0 * static_cast<float>(n) / static_cast<float>(sampleRate)));
        const int bin1 = std::min(halfBins - 1, static_cast<int>(freq1 * static_cast<float>(n) / static_cast<float>(sampleRate)));

        float peak = 0.f;
        for (int bin = bin0; bin <= bin1; ++bin) {
            const float mag = std::sqrt(real[static_cast<size_t>(bin)] * real[static_cast<size_t>(bin)]
                                        + imag[static_cast<size_t>(bin)] * imag[static_cast<size_t>(bin)]);
            peak = std::max(peak, mag);
        }

        const float normalized = std::min(1.f, std::log10(1.f + peak * 8.f) / 2.f);
        (*magnitudes)[bar] = normalized;
    }
}
