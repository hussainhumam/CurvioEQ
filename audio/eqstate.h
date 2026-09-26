#pragma once

#include "ui/appconstants.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

enum class EqFilterType {
    Peaking = 0,
    LowShelf = 1,
    HighShelf = 2
};

struct EqFilter {
    EqFilterType type = EqFilterType::Peaking;
    float freqHz = 1000.f;
    float gainDb = 0.f;
    float q = 1.41f;
};

struct EqState {
    static constexpr int kBandCount = 10;
    static constexpr int kMaxParametricFilters = 16;
    static constexpr std::array<float, kBandCount> kBandFreqs = {
        20.f, 40.f, 160.f, 300.f, 600.f,
        1200.f, 2400.f, 5000.f, 10000.f, 20000.f
    };

    bool advanced = false;
    std::array<float, kBandCount> gainsDb{};
    std::array<EqFilter, kMaxParametricFilters> filters{};
    int filterCount = 0;
    int balance = 0;

    static void stereoBalanceGains(int balance, float *leftGain, float *rightGain)
    {
        const int clamped = std::clamp(balance, AppConstants::kMinBalance, AppConstants::kMaxBalance);
        if (clamped <= 0) {
            *leftGain = 1.f;
            *rightGain = 1.f + static_cast<float>(clamped) / static_cast<float>(-AppConstants::kMinBalance);
        } else {
            *leftGain = 1.f - static_cast<float>(clamped) / static_cast<float>(AppConstants::kMaxBalance);
            *rightGain = 1.f;
        }
    }

    void clearFilters()
    {
        filters = {};
        filterCount = 0;
    }
};

namespace EqResponse {

inline constexpr float kDefaultQ = 1.41f;
inline constexpr float kPi = 3.14159265358979323846f;

struct BiquadTf {
    float b0 = 1.f;
    float b1 = 0.f;
    float b2 = 0.f;
    float a1 = 0.f;
    float a2 = 0.f;
};

inline BiquadTf makeBiquad(EqFilterType type, float freqHz, float gainDb, float q, float sampleRate)
{
    BiquadTf out;
    const float safeRate = std::max(sampleRate, 1.f);
    const float safeFreq = std::clamp(freqHz, 20.f, safeRate * 0.49f);
    const float safeQ = std::max(q, 0.05f);
    const float A = std::pow(10.f, gainDb / 40.f);
    const float omega = 2.f * kPi * safeFreq / safeRate;
    const float sinOmega = std::sin(omega);
    const float cosOmega = std::cos(omega);

    float b0 = 1.f;
    float b1 = 0.f;
    float b2 = 0.f;
    float a0 = 1.f;
    float a1 = 0.f;
    float a2 = 0.f;

    if (type == EqFilterType::LowShelf) {
        const float alpha = sinOmega / 2.f * std::sqrt((A + 1.f / A) * (1.f / safeQ - 1.f) + 2.f);
        const float twoSqrtAAlpha = 2.f * std::sqrt(A) * alpha;
        b0 = A * ((A + 1.f) - (A - 1.f) * cosOmega + twoSqrtAAlpha);
        b1 = 2.f * A * ((A - 1.f) - (A + 1.f) * cosOmega);
        b2 = A * ((A + 1.f) - (A - 1.f) * cosOmega - twoSqrtAAlpha);
        a0 = (A + 1.f) + (A - 1.f) * cosOmega + twoSqrtAAlpha;
        a1 = -2.f * ((A - 1.f) + (A + 1.f) * cosOmega);
        a2 = (A + 1.f) + (A - 1.f) * cosOmega - twoSqrtAAlpha;
    } else if (type == EqFilterType::HighShelf) {
        const float alpha = sinOmega / 2.f * std::sqrt((A + 1.f / A) * (1.f / safeQ - 1.f) + 2.f);
        const float twoSqrtAAlpha = 2.f * std::sqrt(A) * alpha;
        b0 = A * ((A + 1.f) + (A - 1.f) * cosOmega + twoSqrtAAlpha);
        b1 = -2.f * A * ((A - 1.f) + (A + 1.f) * cosOmega);
        b2 = A * ((A + 1.f) + (A - 1.f) * cosOmega - twoSqrtAAlpha);
        a0 = (A + 1.f) - (A - 1.f) * cosOmega + twoSqrtAAlpha;
        a1 = 2.f * ((A - 1.f) - (A + 1.f) * cosOmega);
        a2 = (A + 1.f) - (A - 1.f) * cosOmega - twoSqrtAAlpha;
    } else {
        const float alpha = sinOmega / (2.f * safeQ);
        b0 = 1.f + alpha * A;
        b1 = -2.f * cosOmega;
        b2 = 1.f - alpha * A;
        a0 = 1.f + alpha / A;
        a1 = -2.f * cosOmega;
        a2 = 1.f - alpha / A;
    }

    out.b0 = b0 / a0;
    out.b1 = b1 / a0;
    out.b2 = b2 / a0;
    out.a1 = a1 / a0;
    out.a2 = a2 / a0;
    return out;
}

inline float magnitudeDb(const BiquadTf &tf, float freqHz, float sampleRate)
{
    const float w = 2.f * kPi * freqHz / std::max(sampleRate, 1.f);
    const float cosW = std::cos(w);
    const float sinW = std::sin(w);
    const float cos2W = std::cos(2.f * w);
    const float sin2W = std::sin(2.f * w);

    const float numRe = tf.b0 + tf.b1 * cosW + tf.b2 * cos2W;
    const float numIm = -(tf.b1 * sinW + tf.b2 * sin2W);
    const float denRe = 1.f + tf.a1 * cosW + tf.a2 * cos2W;
    const float denIm = -(tf.a1 * sinW + tf.a2 * sin2W);

    const float numMag2 = numRe * numRe + numIm * numIm;
    const float denMag2 = denRe * denRe + denIm * denIm;
    if (denMag2 < 1.0e-20f || numMag2 < 1.0e-40f) {
        return -120.f;
    }
    return 10.f * std::log10(numMag2 / denMag2);
}

inline float cascadeMagnitudeDb(const EqFilter *filters, int count, float freqHz, float sampleRate)
{
    float totalDb = 0.f;
    if (!filters || count <= 0) {
        return totalDb;
    }
    for (int i = 0; i < count; ++i) {
        const EqFilter &f = filters[i];
        if (std::fabs(f.gainDb) < 0.001f) {
            continue;
        }
        const BiquadTf tf = makeBiquad(f.type, f.freqHz, f.gainDb, f.q, sampleRate);
        totalDb += magnitudeDb(tf, freqHz, sampleRate);
    }
    return totalDb;
}

inline EqState simpleToAdvanced(const std::array<float, EqState::kBandCount> &gainsDb)
{
    EqState state;
    state.advanced = true;
    state.gainsDb = gainsDb;
    state.filterCount = EqState::kBandCount;
    for (int i = 0; i < EqState::kBandCount; ++i) {
        EqFilter filter;
        filter.type = EqFilterType::Peaking;
        filter.freqHz = EqState::kBandFreqs[static_cast<size_t>(i)];
        filter.gainDb = gainsDb[static_cast<size_t>(i)];
        filter.q = kDefaultQ;
        state.filters[static_cast<size_t>(i)] = filter;
    }
    return state;
}

inline EqState advancedToSimple(const EqState &advanced, float sampleRate = 48000.f)
{
    EqState state;
    state.advanced = false;
    state.filters = advanced.filters;
    state.filterCount = advanced.filterCount;
    for (int i = 0; i < EqState::kBandCount; ++i) {
        const float freq = EqState::kBandFreqs[static_cast<size_t>(i)];
        state.gainsDb[static_cast<size_t>(i)] =
            cascadeMagnitudeDb(advanced.filters.data(), advanced.filterCount, freq, sampleRate);
    }
    return state;
}

} // namespace EqResponse
