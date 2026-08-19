#include "virtualsurroundprocessor.h"

#include "hrtfloader.h"

#include <QCoreApplication>
#include <QDir>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr float kLfeCrossoverHz = 120.f;
constexpr float kSpatialCrossoverHz = 250.f;
constexpr float kWetOutputCrossoverHz = 400.f;

} // namespace

void VirtualSurroundProcessor::configureHighPass(BiquadState *filter, float sampleRate, float cutoffHz)
{
    if (!filter || sampleRate <= 0.f || cutoffHz <= 0.f) {
        return;
    }

    const float omega = 2.f * 3.14159265358979323846f * cutoffHz / sampleRate;
    const float sinOmega = std::sin(omega);
    const float cosOmega = std::cos(omega);
    const float alpha = sinOmega / (2.f * 0.70710678f);

    const float b0 = (1.f + cosOmega) * 0.5f;
    const float b1 = -(1.f + cosOmega);
    const float b2 = (1.f + cosOmega) * 0.5f;
    const float a0 = 1.f + alpha;
    const float a1 = -2.f * cosOmega;
    const float a2 = 1.f - alpha;

    filter->b0 = b0 / a0;
    filter->b1 = b1 / a0;
    filter->b2 = b2 / a0;
    filter->a1 = a1 / a0;
    filter->a2 = a2 / a0;
    filter->reset();
}

void VirtualSurroundProcessor::configureCrossoverFilters()
{
    configureHighPass(&m_spatialHighPassLeft, m_sampleRate, kSpatialCrossoverHz);
    configureHighPass(&m_spatialHighPassRight, m_sampleRate, kSpatialCrossoverHz);
    configureHighPass(&m_wetHighPassLeft, m_sampleRate, kWetOutputCrossoverHz);
    configureHighPass(&m_wetHighPassRight, m_sampleRate, kWetOutputCrossoverHz);
    configureHighPass(&m_wetHighPassLeft2, m_sampleRate, kWetOutputCrossoverHz);
    configureHighPass(&m_wetHighPassRight2, m_sampleRate, kWetOutputCrossoverHz);
    configureLfeLowPass();
}

void VirtualSurroundProcessor::configureLowPass(BiquadState *filter, float sampleRate, float cutoffHz)
{
    if (!filter || sampleRate <= 0.f || cutoffHz <= 0.f) {
        return;
    }

    const float omega = 2.f * 3.14159265358979323846f * cutoffHz / sampleRate;
    const float sinOmega = std::sin(omega);
    const float cosOmega = std::cos(omega);
    const float alpha = sinOmega / (2.f * 0.70710678f);

    const float b0 = (1.f - cosOmega) * 0.5f;
    const float b1 = 1.f - cosOmega;
    const float b2 = (1.f - cosOmega) * 0.5f;
    const float a0 = 1.f + alpha;
    const float a1 = -2.f * cosOmega;
    const float a2 = 1.f - alpha;

    filter->b0 = b0 / a0;
    filter->b1 = b1 / a0;
    filter->b2 = b2 / a0;
    filter->a1 = a1 / a0;
    filter->a2 = a2 / a0;
    filter->reset();
}

float VirtualSurroundProcessor::BiquadState::process(float input)
{
    const float output = b0 * input + z1;
    z1 = b1 * input - a1 * output + z2;
    z2 = b2 * input - a2 * output;
    return output;
}

void VirtualSurroundProcessor::BiquadState::reset()
{
    z1 = 0.f;
    z2 = 0.f;
}

VirtualSurroundProcessor::VirtualSurroundProcessor()
{
    for (auto &level : m_levels) {
        level.store(50);
    }
    m_levels[SurroundProcessor::Lfe].store(0);
    configureCrossoverFilters();
}

void VirtualSurroundProcessor::setEnabled(bool enabled)
{
    m_enabled.store(enabled);
}

bool VirtualSurroundProcessor::isEnabled() const
{
    return m_enabled.load();
}

void VirtualSurroundProcessor::setPreset(int presetId)
{
    const int clamped = std::clamp(presetId, 0, static_cast<int>(HrtfPresetId::Count) - 1);
    m_presetId.store(clamped);
    m_configured = false;
}

void VirtualSurroundProcessor::setStrength(int strength)
{
    m_strength.store(std::clamp(strength, 0, 100));
}

void VirtualSurroundProcessor::setChannelLevels(const std::array<int, kSpeakerCount> &levels)
{
    for (int channel = 0; channel < kSpeakerCount; ++channel) {
        int level = std::clamp(levels[static_cast<size_t>(channel)], 0, 100);
        if (channel == SurroundProcessor::Lfe) {
            level = 0;
        }
        m_levels[static_cast<size_t>(channel)].store(level);
    }
}

void VirtualSurroundProcessor::setSampleRate(float sampleRate)
{
    if (sampleRate <= 0.f) {
        return;
    }
    if (std::fabs(m_sampleRate - sampleRate) > 0.5f) {
        m_sampleRate = sampleRate;
        m_configured = false;
        configureCrossoverFilters();
    }
}

void VirtualSurroundProcessor::configureLfeLowPass()
{
    configureLowPass(&m_lfeLowPass, m_sampleRate, kLfeCrossoverHz);
}

int VirtualSurroundProcessor::latencyFrames() const
{
    int maxLatency = 0;
    for (const HrtfConvolver &convolver : m_convolvers) {
        maxLatency = std::max(maxLatency, convolver.latencyFrames());
    }
    return maxLatency;
}

float VirtualSurroundProcessor::levelToMultiplier(int level)
{
    return static_cast<float>(std::clamp(level, 0, 100)) / 50.f;
}

float VirtualSurroundProcessor::strengthToMix(int strength)
{
    return static_cast<float>(std::clamp(strength, 0, 100)) / 100.f;
}

void VirtualSurroundProcessor::ensureConfigured()
{
    const int presetId = m_presetId.load();
    if (m_configured && m_activePresetId == presetId && std::fabs(m_activeSampleRate - m_sampleRate) < 0.5f) {
        return;
    }

    const auto presetEnum = static_cast<HrtfPresetId>(presetId);
    HrtfPresetData presetData = HrtfPresets::preset(presetEnum, m_sampleRate);

    if (QCoreApplication::instance() != nullptr) {
        const QString bundledRoot =
            HrtfLoader::bundledHrtfRoot(QCoreApplication::applicationDirPath());
        const QString presetDir =
            QDir(bundledRoot).filePath(HrtfLoader::presetDirectoryName(presetEnum));
        HrtfPresetData filePreset;
        QString loadError;
        if (HrtfLoader::loadPresetFromDirectory(presetDir, presetEnum, m_sampleRate, &filePreset, &loadError)
            && filePreset.irLength == HrtfPresets::kIrLength) {
            presetData = std::move(filePreset);
        }
    }

    for (int channel = 0; channel < kSpeakerCount; ++channel) {
        HrtfSpeakerImpulse &impulse = presetData.speakers[static_cast<size_t>(channel)];
        if (channel == SurroundProcessor::Lfe) {
            impulse.left.assign(static_cast<size_t>(presetData.irLength), 0.f);
            impulse.right.assign(static_cast<size_t>(presetData.irLength), 0.f);
        } else {
            HrtfPresets::highPassImpulse(&impulse, m_sampleRate, kSpatialCrossoverHz);
            HrtfPresets::normalizeImpulse(&impulse);
        }
        m_convolvers[static_cast<size_t>(channel)].configure(impulse.left.data(),
                                                              impulse.right.data(),
                                                              presetData.irLength,
                                                              HrtfPresets::kConvolverBlockSize);
        m_convolvers[static_cast<size_t>(channel)].reset();
    }

    m_lfeLowPass.reset();
    m_spatialHighPassLeft.reset();
    m_spatialHighPassRight.reset();
    m_wetHighPassLeft.reset();
    m_wetHighPassRight.reset();
    m_wetHighPassLeft2.reset();
    m_wetHighPassRight2.reset();
    m_activePresetId = presetId;
    m_activeSampleRate = m_sampleRate;
    m_configured = true;
}

void VirtualSurroundProcessor::upmixFrame(float left, float right, std::array<float, kSpeakerCount> *speakers)
{
    if (!speakers) {
        return;
    }

    const float center = 0.70710678f * (left + right);

    (*speakers)[SurroundProcessor::FrontLeft] = left;
    (*speakers)[SurroundProcessor::FrontRight] = right;
    (*speakers)[SurroundProcessor::FrontCenter] = center;
    (*speakers)[SurroundProcessor::Lfe] = 0.f;
    (*speakers)[SurroundProcessor::BackLeft] = 0.5f * left;
    (*speakers)[SurroundProcessor::BackRight] = 0.5f * right;
    (*speakers)[SurroundProcessor::SideLeft] = 0.3f * left;
    (*speakers)[SurroundProcessor::SideRight] = 0.3f * right;

    for (int channel = 0; channel < kSpeakerCount; ++channel) {
        (*speakers)[static_cast<size_t>(channel)] *=
            levelToMultiplier(m_levels[static_cast<size_t>(channel)].load());
    }
}

void VirtualSurroundProcessor::normalizeAndLimitWet(int frameCount)
{
    const size_t sampleCount = static_cast<size_t>(frameCount * 2);
    for (size_t i = 0; i < sampleCount; ++i) {
        m_wetScratch[i] *= kWetBusGain;
    }
}

void VirtualSurroundProcessor::process(const float *stereoIn, float *stereoOut, int frameCount)
{
    if (!stereoIn || !stereoOut || frameCount <= 0) {
        return;
    }

    if (!m_enabled.load()) {
        const size_t sampleCount = static_cast<size_t>(frameCount * 2);
        std::memcpy(stereoOut, stereoIn, sampleCount * sizeof(float));
        return;
    }

    ensureConfigured();

    const size_t wetSampleCount = static_cast<size_t>(frameCount * 2);
    if (m_wetScratch.size() < wetSampleCount) {
        m_wetScratch.resize(wetSampleCount);
    }

    std::fill(m_wetScratch.begin(), m_wetScratch.begin() + static_cast<ptrdiff_t>(wetSampleCount), 0.f);

    std::array<std::vector<float>, kSpeakerCount> speakerBuffers{};
    std::vector<float> highLeft(static_cast<size_t>(frameCount), 0.f);
    std::vector<float> highRight(static_cast<size_t>(frameCount), 0.f);

    for (int frame = 0; frame < frameCount; ++frame) {
        const float left = stereoIn[static_cast<size_t>(frame * 2)];
        const float right = stereoIn[static_cast<size_t>(frame * 2 + 1)];
        highLeft[static_cast<size_t>(frame)] = m_spatialHighPassLeft.process(left);
        highRight[static_cast<size_t>(frame)] = m_spatialHighPassRight.process(right);
    }

    for (int channel = 0; channel < kSpeakerCount; ++channel) {
        speakerBuffers[static_cast<size_t>(channel)].assign(static_cast<size_t>(frameCount), 0.f);
    }

    std::array<float, kSpeakerCount> speakers{};
    for (int frame = 0; frame < frameCount; ++frame) {
        upmixFrame(highLeft[static_cast<size_t>(frame)], highRight[static_cast<size_t>(frame)], &speakers);
        for (int channel = 0; channel < kSpeakerCount; ++channel) {
            if (speakers[static_cast<size_t>(channel)] == 0.f) {
                continue;
            }
            speakerBuffers[static_cast<size_t>(channel)][static_cast<size_t>(frame)] =
                speakers[static_cast<size_t>(channel)];
        }
    }

    for (int channel = 0; channel < kSpeakerCount; ++channel) {
        if (speakerBuffers[static_cast<size_t>(channel)].empty()) {
            continue;
        }
        bool hasSignal = false;
        for (float sample : speakerBuffers[static_cast<size_t>(channel)]) {
            if (sample != 0.f) {
                hasSignal = true;
                break;
            }
        }
        if (!hasSignal) {
            continue;
        }
        m_convolvers[static_cast<size_t>(channel)].processAccumulate(
            speakerBuffers[static_cast<size_t>(channel)].data(), m_wetScratch.data(), frameCount);
    }

    normalizeAndLimitWet(frameCount);

    const float wetMix = strengthToMix(m_strength.load());
    for (int frame = 0; frame < frameCount; ++frame) {
        const size_t index = static_cast<size_t>(frame * 2);
        const float inLeft = stereoIn[index];
        const float inRight = stereoIn[index + 1];
        const float wetLeft =
            std::tanh(m_wetHighPassLeft2.process(m_wetHighPassLeft.process(m_wetScratch[index])));
        const float wetRight =
            std::tanh(m_wetHighPassRight2.process(m_wetHighPassRight.process(m_wetScratch[index + 1])));
        stereoOut[index] = inLeft + wetMix * wetLeft;
        stereoOut[index + 1] = inRight + wetMix * wetRight;
    }
}
