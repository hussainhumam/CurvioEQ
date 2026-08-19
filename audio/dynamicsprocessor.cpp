#include "dynamicsprocessor.h"

#include "dynamicrangesettings.h"

#include <algorithm>
#include <cmath>

namespace {

float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

float dbToLinear(float db)
{
    return std::pow(10.f, db / 20.f);
}

float coeffForTime(float seconds, float sampleRate)
{
    if (seconds <= 0.f || sampleRate <= 0.f) {
        return 0.f;
    }
    return std::exp(-1.f / (seconds * sampleRate));
}

struct DynamicsCurvePoint {
    float controlT = 0.f;
    float thresholdDb = 0.f;
    float ratio = 1.f;
    float kneeDb = 6.f;
    float attackSeconds = 0.03f;
    float releaseSeconds = 0.25f;
    float blend = 0.f;
};

float mapAmountToControlT(int amount)
{
    const int clamped = clampDynamicRangeAmount(amount);
    if (clamped < 0) {
        const float wideMix = (static_cast<float>(clamped) + 50.f) / 50.f;
        return lerp(-0.45f, 0.f, wideMix);
    }
    if (clamped > 100) {
        const float tightMix = static_cast<float>(clamped - 100) / 50.f;
        return lerp(1.f, 1.4f, tightMix);
    }
    return static_cast<float>(clamped) / 100.f;
}

float interpolateCurve(const DynamicsCurvePoint *points, int pointCount, float controlT, float DynamicsCurvePoint::*field)
{
    if (pointCount <= 0) {
        return 0.f;
    }

    if (controlT <= points[0].controlT) {
        return points[0].*field;
    }

    for (int i = 1; i < pointCount; ++i) {
        if (controlT <= points[i].controlT) {
            const float span = points[i].controlT - points[i - 1].controlT;
            const float mix = span > 1e-6f ? (controlT - points[i - 1].controlT) / span : 0.f;
            return lerp(points[i - 1].*field, points[i].*field, mix);
        }
    }

    return points[pointCount - 1].*field;
}

} // namespace

DynamicsProcessor::DynamicsProcessor()
{
    reset();
}

void DynamicsProcessor::setEnabled(bool enabled)
{
    m_enabled.store(enabled);
}

void DynamicsProcessor::setAmount(int amount)
{
    m_amount.store(clampDynamicRangeAmount(amount));
}

void DynamicsProcessor::setSampleRate(float sampleRate)
{
    if (sampleRate <= 0.f) {
        return;
    }
    if (std::fabs(m_sampleRate - sampleRate) > 0.5f) {
        m_sampleRate = sampleRate;
        reset();
    }
}

void DynamicsProcessor::reset()
{
    m_envelope = 0.f;
    m_rmsState = 0.f;
}

DynamicsProcessor::CompressorParams DynamicsProcessor::paramsForAmount(int amount)
{
    static constexpr DynamicsCurvePoint kCurve[] = {
        {-0.45f, -1.5f, 1.02f, 12.f, 0.060f, 0.500f, 0.f},
        {0.f, -6.f, 1.2f, 9.f, 0.030f, 0.250f, 0.15f},
        {1.f, -20.f, 5.f, 6.f, 0.008f, 0.100f, 1.f},
        {1.4f, -32.f, 9.f, 4.f, 0.003f, 0.060f, 1.f},
    };

    const float controlT = mapAmountToControlT(amount);

    CompressorParams params;
    const float thresholdDb = interpolateCurve(kCurve, 4, controlT, &DynamicsCurvePoint::thresholdDb);
    params.threshold = dbToLinear(thresholdDb);
    params.ratio = interpolateCurve(kCurve, 4, controlT, &DynamicsCurvePoint::ratio);
    const float kneeDb = interpolateCurve(kCurve, 4, controlT, &DynamicsCurvePoint::kneeDb);
    params.knee = std::max(params.threshold * (1.f - std::pow(10.f, -kneeDb / 20.f)), 0.001f);
    const float attackSeconds = interpolateCurve(kCurve, 4, controlT, &DynamicsCurvePoint::attackSeconds);
    const float releaseSeconds = interpolateCurve(kCurve, 4, controlT, &DynamicsCurvePoint::releaseSeconds);
    params.attackCoeff = coeffForTime(attackSeconds, m_sampleRate > 0.f ? m_sampleRate : 48000.f);
    params.releaseCoeff = coeffForTime(releaseSeconds, m_sampleRate > 0.f ? m_sampleRate : 48000.f);
    params.blend = interpolateCurve(kCurve, 4, controlT, &DynamicsCurvePoint::blend);
    return params;
}

float DynamicsProcessor::computeDetection(float peak, float rms)
{
    return std::max(rms, peak * 0.85f);
}

float DynamicsProcessor::softKneeGain(float envelope, const CompressorParams &params)
{
    if (envelope <= 1e-9f) {
        return 1.f;
    }

    const float kneeStart = std::max(params.threshold - params.knee, 1e-6f);
    const float kneeEnd = params.threshold + params.knee;

    if (envelope <= kneeStart) {
        return 1.f;
    }

    if (envelope >= kneeEnd) {
        const float compressed = params.threshold + (envelope - params.threshold) / params.ratio;
        return compressed / envelope;
    }

    const float x = envelope - kneeStart;
    const float kneeWidth = std::max(kneeEnd - kneeStart, 1e-6f);
    const float slope = (1.f / params.ratio - 1.f) / (2.f * kneeWidth);
    const float gainReductionDb = slope * x * x;
    const float target = envelope * std::pow(10.f, gainReductionDb / 20.f);
    return target / envelope;
}

void DynamicsProcessor::process(float *interleaved, int frameCount, int channelCount)
{
    if (!interleaved || frameCount <= 0 || channelCount <= 0 || !m_enabled.load()) {
        return;
    }

    const CompressorParams params = paramsForAmount(m_amount.load());
    const float rmsCoeff = std::exp(-1.f / (0.010f * m_sampleRate));

    for (int frame = 0; frame < frameCount; ++frame) {
        float peak = 0.f;
        float sumSquares = 0.f;
        for (int channel = 0; channel < channelCount; ++channel) {
            const float sample = interleaved[static_cast<size_t>(frame * channelCount + channel)];
            peak = std::max(peak, std::fabs(sample));
            sumSquares += sample * sample;
        }

        const float rmsInstant = std::sqrt(sumSquares / static_cast<float>(channelCount));
        m_rmsState = rmsCoeff * m_rmsState + (1.f - rmsCoeff) * rmsInstant;
        const float detection = computeDetection(peak, m_rmsState);

        if (detection > m_envelope) {
            m_envelope = params.attackCoeff * m_envelope + (1.f - params.attackCoeff) * detection;
        } else {
            m_envelope = params.releaseCoeff * m_envelope + (1.f - params.releaseCoeff) * detection;
        }

        const float gain = softKneeGain(m_envelope, params);
        const float blend = params.blend;

        for (int channel = 0; channel < channelCount; ++channel) {
            const size_t index = static_cast<size_t>(frame * channelCount + channel);
            const float dry = interleaved[index];
            const float compressed = dry * gain;
            float wet = dry + blend * (compressed - dry);

            if (gain < dbToLinear(-12.f)) {
                wet = std::tanh(wet);
            }

            interleaved[index] = wet;
        }
    }
}
