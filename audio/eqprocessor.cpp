#include "eqprocessor.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kGainRampSeconds = 0.015f;
}

float EqProcessor::Biquad::processSample(float input)
{
    const int coeffIndex = activeCoeffIndex.load(std::memory_order_acquire);
    const BiquadCoeffs &c = coeffs[static_cast<size_t>(coeffIndex)];

    const float output = c.b0 * input + z1;
    z1 = c.b1 * input - c.a1 * output + z2;
    z2 = c.b2 * input - c.a2 * output;
    return output;
}

void EqProcessor::Biquad::reset()
{
    z1 = 0.f;
    z2 = 0.f;
}

void EqProcessor::Biquad::publishCoeffs(const BiquadCoeffs &updated)
{
    const int inactiveIndex = 1 - activeCoeffIndex.load(std::memory_order_relaxed);
    coeffs[static_cast<size_t>(inactiveIndex)] = updated;
    activeCoeffIndex.store(inactiveIndex, std::memory_order_release);
}

EqProcessor::EqProcessor()
{
    for (auto &gain : m_targetGainsDb) {
        gain.store(0.f, std::memory_order_relaxed);
    }
    m_currentGainsDb.fill(0.f);
    m_gainRampPerSample.fill(0.f);

    for (int i = 0; i < kMaxParametricFilters; ++i) {
        m_targetType[static_cast<size_t>(i)].store(static_cast<int>(EqFilterType::Peaking),
                                                   std::memory_order_relaxed);
        m_targetFreqHz[static_cast<size_t>(i)].store(1000.f, std::memory_order_relaxed);
        m_targetParamGainDb[static_cast<size_t>(i)].store(0.f, std::memory_order_relaxed);
        m_targetQ[static_cast<size_t>(i)].store(EqResponse::kDefaultQ, std::memory_order_relaxed);
        m_currentParamGainDb[static_cast<size_t>(i)] = 0.f;
        m_paramGainRampPerSample[static_cast<size_t>(i)] = 0.f;
        m_currentParamFilters[static_cast<size_t>(i)] = {};
        publishBypass(i);
    }

    setSampleRate(48000.f);
}

void EqProcessor::setSampleRate(float sampleRate)
{
    m_sampleRate = std::max(sampleRate, 1.f);
    for (int band = 0; band < kBandCount; ++band) {
        m_currentGainsDb[static_cast<size_t>(band)] =
            m_targetGainsDb[static_cast<size_t>(band)].load(std::memory_order_relaxed);
        updateSimpleBandCoefficients(band, m_currentGainsDb[static_cast<size_t>(band)]);
        m_simpleLeft[static_cast<size_t>(band)].reset();
        m_simpleRight[static_cast<size_t>(band)].reset();
    }

    syncParametricFromTargets();
    for (int i = 0; i < kMaxParametricFilters; ++i) {
        m_paramLeft[static_cast<size_t>(i)].reset();
        m_paramRight[static_cast<size_t>(i)].reset();
    }
}

void EqProcessor::reset()
{
    for (int band = 0; band < kBandCount; ++band) {
        m_simpleLeft[static_cast<size_t>(band)].reset();
        m_simpleRight[static_cast<size_t>(band)].reset();
    }
    for (int i = 0; i < kMaxParametricFilters; ++i) {
        m_paramLeft[static_cast<size_t>(i)].reset();
        m_paramRight[static_cast<size_t>(i)].reset();
    }
}

void EqProcessor::setBandGain(int band, float gainDb)
{
    if (band < 0 || band >= kBandCount) {
        return;
    }
    m_targetGainsDb[static_cast<size_t>(band)].store(gainDb, std::memory_order_relaxed);
}

void EqProcessor::setGains(const std::array<float, kBandCount> &gainsDb)
{
    m_advanced.store(false, std::memory_order_release);
    for (int band = 0; band < kBandCount; ++band) {
        setBandGain(band, gainsDb[static_cast<size_t>(band)]);
    }
}

void EqProcessor::setParametricFilters(const EqFilter *filters, int count)
{
    m_advanced.store(true, std::memory_order_release);
    const int safeCount = std::clamp(count, 0, kMaxParametricFilters);
    for (int i = 0; i < safeCount; ++i) {
        const EqFilter &filter = filters[i];
        m_targetType[static_cast<size_t>(i)].store(static_cast<int>(filter.type),
                                                   std::memory_order_relaxed);
        m_targetFreqHz[static_cast<size_t>(i)].store(filter.freqHz, std::memory_order_relaxed);
        m_targetParamGainDb[static_cast<size_t>(i)].store(filter.gainDb, std::memory_order_relaxed);
        m_targetQ[static_cast<size_t>(i)].store(std::max(filter.q, 0.05f), std::memory_order_relaxed);
    }
    for (int i = safeCount; i < kMaxParametricFilters; ++i) {
        m_targetParamGainDb[static_cast<size_t>(i)].store(0.f, std::memory_order_relaxed);
    }
    m_parametricCount.store(safeCount, std::memory_order_release);
}

void EqProcessor::setEqState(const EqState &state)
{
    if (state.advanced) {
        setParametricFilters(state.filters.data(), state.filterCount);
        for (int band = 0; band < kBandCount; ++band) {
            setBandGain(band, state.gainsDb[static_cast<size_t>(band)]);
        }
    } else {
        setGains(state.gainsDb);
    }
}

void EqProcessor::publishBypass(int slot)
{
    BiquadCoeffs bypass;
    bypass.b0 = 1.f;
    bypass.b1 = 0.f;
    bypass.b2 = 0.f;
    bypass.a1 = 0.f;
    bypass.a2 = 0.f;
    m_paramLeft[static_cast<size_t>(slot)].publishCoeffs(bypass);
    m_paramRight[static_cast<size_t>(slot)].publishCoeffs(bypass);
}

void EqProcessor::updateSimpleBandCoefficients(int band, float gainDb)
{
    const EqResponse::BiquadTf tf = EqResponse::makeBiquad(
        EqFilterType::Peaking,
        kBandFreqs[static_cast<size_t>(band)],
        gainDb,
        EqResponse::kDefaultQ,
        m_sampleRate);

    BiquadCoeffs updated;
    updated.b0 = tf.b0;
    updated.b1 = tf.b1;
    updated.b2 = tf.b2;
    updated.a1 = tf.a1;
    updated.a2 = tf.a2;

    m_simpleLeft[static_cast<size_t>(band)].publishCoeffs(updated);
    m_simpleRight[static_cast<size_t>(band)].publishCoeffs(updated);
}

void EqProcessor::updateParametricSlot(int slot, const EqFilter &filter)
{
    if (std::fabs(filter.gainDb) < 0.001f) {
        publishBypass(slot);
        return;
    }

    const EqResponse::BiquadTf tf = EqResponse::makeBiquad(
        filter.type, filter.freqHz, filter.gainDb, filter.q, m_sampleRate);

    BiquadCoeffs updated;
    updated.b0 = tf.b0;
    updated.b1 = tf.b1;
    updated.b2 = tf.b2;
    updated.a1 = tf.a1;
    updated.a2 = tf.a2;

    m_paramLeft[static_cast<size_t>(slot)].publishCoeffs(updated);
    m_paramRight[static_cast<size_t>(slot)].publishCoeffs(updated);
}

void EqProcessor::syncParametricFromTargets()
{
    const int count = m_parametricCount.load(std::memory_order_acquire);
    for (int i = 0; i < kMaxParametricFilters; ++i) {
        if (i >= count) {
            m_currentParamGainDb[static_cast<size_t>(i)] = 0.f;
            m_paramGainRampPerSample[static_cast<size_t>(i)] = 0.f;
            m_currentParamFilters[static_cast<size_t>(i)] = {};
            publishBypass(i);
            continue;
        }

        EqFilter filter;
        filter.type = static_cast<EqFilterType>(
            m_targetType[static_cast<size_t>(i)].load(std::memory_order_relaxed));
        filter.freqHz = m_targetFreqHz[static_cast<size_t>(i)].load(std::memory_order_relaxed);
        filter.gainDb = m_targetParamGainDb[static_cast<size_t>(i)].load(std::memory_order_relaxed);
        filter.q = m_targetQ[static_cast<size_t>(i)].load(std::memory_order_relaxed);
        m_currentParamGainDb[static_cast<size_t>(i)] = filter.gainDb;
        m_currentParamFilters[static_cast<size_t>(i)] = filter;
        m_paramGainRampPerSample[static_cast<size_t>(i)] = 0.f;
        updateParametricSlot(i, filter);
    }
}

void EqProcessor::advanceGainRamps(int frameCount)
{
    const float rampStep = kGainRampSeconds * m_sampleRate;

    if (!m_advanced.load(std::memory_order_acquire)) {
        for (int band = 0; band < kBandCount; ++band) {
            const float target = m_targetGainsDb[static_cast<size_t>(band)].load(std::memory_order_relaxed);
            float &current = m_currentGainsDb[static_cast<size_t>(band)];
            if (std::fabs(current - target) < 0.001f) {
                m_gainRampPerSample[static_cast<size_t>(band)] = 0.f;
                if (current != target) {
                    current = target;
                    updateSimpleBandCoefficients(band, current);
                }
                continue;
            }

            m_gainRampPerSample[static_cast<size_t>(band)] = (target - current) / rampStep;
            current += m_gainRampPerSample[static_cast<size_t>(band)] * static_cast<float>(frameCount);

            if ((m_gainRampPerSample[static_cast<size_t>(band)] > 0.f && current >= target)
                || (m_gainRampPerSample[static_cast<size_t>(band)] < 0.f && current <= target)) {
                current = target;
                m_gainRampPerSample[static_cast<size_t>(band)] = 0.f;
            }

            updateSimpleBandCoefficients(band, current);
        }
        return;
    }

    const int count = m_parametricCount.load(std::memory_order_acquire);
    for (int i = 0; i < kMaxParametricFilters; ++i) {
        if (i >= count) {
            if (m_currentParamFilters[static_cast<size_t>(i)].gainDb != 0.f
                || m_currentParamGainDb[static_cast<size_t>(i)] != 0.f) {
                m_currentParamGainDb[static_cast<size_t>(i)] = 0.f;
                m_currentParamFilters[static_cast<size_t>(i)] = {};
                publishBypass(i);
            }
            continue;
        }

        EqFilter target;
        target.type = static_cast<EqFilterType>(
            m_targetType[static_cast<size_t>(i)].load(std::memory_order_relaxed));
        target.freqHz = m_targetFreqHz[static_cast<size_t>(i)].load(std::memory_order_relaxed);
        target.gainDb = m_targetParamGainDb[static_cast<size_t>(i)].load(std::memory_order_relaxed);
        target.q = m_targetQ[static_cast<size_t>(i)].load(std::memory_order_relaxed);

        float &currentGain = m_currentParamGainDb[static_cast<size_t>(i)];
        EqFilter &current = m_currentParamFilters[static_cast<size_t>(i)];

        const bool metaChanged = current.type != target.type
                                 || std::fabs(current.freqHz - target.freqHz) > 0.01f
                                 || std::fabs(current.q - target.q) > 0.0001f;

        if (metaChanged) {
            current = target;
            currentGain = target.gainDb;
            m_paramGainRampPerSample[static_cast<size_t>(i)] = 0.f;
            updateParametricSlot(i, current);
            continue;
        }

        if (std::fabs(currentGain - target.gainDb) < 0.001f) {
            m_paramGainRampPerSample[static_cast<size_t>(i)] = 0.f;
            if (currentGain != target.gainDb) {
                currentGain = target.gainDb;
                current.gainDb = currentGain;
                updateParametricSlot(i, current);
            }
            continue;
        }

        m_paramGainRampPerSample[static_cast<size_t>(i)] = (target.gainDb - currentGain) / rampStep;
        currentGain += m_paramGainRampPerSample[static_cast<size_t>(i)] * static_cast<float>(frameCount);
        if ((m_paramGainRampPerSample[static_cast<size_t>(i)] > 0.f && currentGain >= target.gainDb)
            || (m_paramGainRampPerSample[static_cast<size_t>(i)] < 0.f && currentGain <= target.gainDb)) {
            currentGain = target.gainDb;
            m_paramGainRampPerSample[static_cast<size_t>(i)] = 0.f;
        }
        current.gainDb = currentGain;
        updateParametricSlot(i, current);
    }
}

void EqProcessor::process(float *interleavedSamples, int frameCount, int channelCount)
{
    if (!interleavedSamples || frameCount <= 0 || channelCount <= 0) {
        return;
    }

    advanceGainRamps(frameCount);

    const int channelsToProcess = std::min(channelCount, kMaxChannels);
    const bool advanced = m_advanced.load(std::memory_order_acquire);
    const int parametricCount = m_parametricCount.load(std::memory_order_acquire);

    for (int frame = 0; frame < frameCount; ++frame) {
        for (int channel = 0; channel < channelsToProcess; ++channel) {
            const int index = frame * channelCount + channel;
            float sample = interleavedSamples[index];

            if (!advanced) {
                const float dry = sample;
                auto &biquads = (channel == 0) ? m_simpleLeft : m_simpleRight;
                float output = dry;
                for (int band = 0; band < kBandCount; ++band) {
                    const float peaked = biquads[static_cast<size_t>(band)].processSample(dry);
                    output += (peaked - dry);
                }
                sample = output;
            } else {
                auto &biquads = (channel == 0) ? m_paramLeft : m_paramRight;
                for (int i = 0; i < parametricCount; ++i) {
                    sample = biquads[static_cast<size_t>(i)].processSample(sample);
                }
            }

            interleavedSamples[index] = sample;
        }
    }
}
