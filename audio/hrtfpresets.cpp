#include "hrtfpresets.h"

#include <cmath>
#include <mutex>
#include <unordered_map>

namespace {

struct PresetCacheKey {
    int preset = 0;
    int sampleRate = 0;

    bool operator==(const PresetCacheKey &other) const
    {
        return preset == other.preset && sampleRate == other.sampleRate;
    }
};

struct PresetCacheKeyHash {
    std::size_t operator()(const PresetCacheKey &key) const
    {
        return static_cast<std::size_t>(key.preset * 100000 + key.sampleRate);
    }
};

std::mutex g_cacheMutex;
std::unordered_map<PresetCacheKey, HrtfPresetData, PresetCacheKeyHash> g_cache;

float speakerAzimuth(SurroundProcessor::Channel channel)
{
    switch (channel) {
    case SurroundProcessor::FrontLeft:
        return -30.f;
    case SurroundProcessor::FrontRight:
        return 30.f;
    case SurroundProcessor::FrontCenter:
        return 0.f;
    case SurroundProcessor::Lfe:
        return 0.f;
    case SurroundProcessor::BackLeft:
        return -150.f;
    case SurroundProcessor::BackRight:
        return 150.f;
    case SurroundProcessor::SideLeft:
        return -90.f;
    case SurroundProcessor::SideRight:
        return 90.f;
    default:
        return 0.f;
    }
}

} // namespace

void HrtfPresets::normalizeImpulse(HrtfSpeakerImpulse *impulse)
{
    if (!impulse) {
        return;
    }

    for (std::vector<float> *ear : {&impulse->left, &impulse->right}) {
        float energy = 0.f;
        for (float sample : *ear) {
            energy += sample * sample;
        }
        if (energy <= 1e-9f) {
            continue;
        }
        const float scale = 1.f / std::sqrt(energy);
        for (float &sample : *ear) {
            sample *= scale;
        }
    }
}

void HrtfPresets::highPassImpulse(HrtfSpeakerImpulse *impulse, float sampleRate, float cutoffHz)
{
    if (!impulse || sampleRate <= 0.f || cutoffHz <= 0.f) {
        return;
    }

    const float rc = 1.f / (2.f * 3.14159265358979323846f * cutoffHz);
    const float dt = 1.f / sampleRate;
    const float alpha = rc / (rc + dt);

    for (std::vector<float> *ear : {&impulse->left, &impulse->right}) {
        if (ear->empty()) {
            continue;
        }
        float previousInput = (*ear)[0];
        float previousOutput = 0.f;
        (*ear)[0] = 0.f;
        for (size_t i = 1; i < ear->size(); ++i) {
            const float input = (*ear)[i];
            const float output = alpha * (previousOutput + input - previousInput);
            (*ear)[i] = output;
            previousInput = input;
            previousOutput = output;
        }
    }
}

void HrtfPresets::generateSpeakerImpulse(float azimuthDegrees,
                                         float elevationDegrees,
                                         float itdScale,
                                         float ildScale,
                                         float sampleRate,
                                         HrtfSpeakerImpulse *impulse)
{
    if (!impulse) {
        return;
    }

    impulse->left.assign(static_cast<size_t>(kIrLength), 0.f);
    impulse->right.assign(static_cast<size_t>(kIrLength), 0.f);

    const float headRadiusMeters = 0.0875f;
    const float speedOfSound = 343.f;
    const float azimuthRad = azimuthDegrees * 3.14159265358979323846f / 180.f;
    const float elevationRad = elevationDegrees * 3.14159265358979323846f / 180.f;

    const float interAuralTimeSec =
        std::max(0.f, (headRadiusMeters / speedOfSound) * (std::sin(azimuthRad) + 1.f)) * itdScale;
    const float itdSamples = interAuralTimeSec * sampleRate;

    const float absAz = std::min(1.f, std::fabs(azimuthDegrees) / 90.f);
    const float leftGain = std::sqrt(0.5f + 0.5f * std::cos(azimuthRad)) * (1.f + ildScale * absAz);
    const float rightGain = std::sqrt(0.5f - 0.5f * std::cos(azimuthRad)) * (1.f + ildScale * absAz);

    const float elevationAttenuation = std::cos(elevationRad);
    const float decay = 0.965f;

    const int leftDelay = static_cast<int>(std::round(azimuthDegrees <= 0.f ? 0.f : itdSamples));
    const int rightDelay = static_cast<int>(std::round(azimuthDegrees >= 0.f ? 0.f : itdSamples));

    impulse->left[static_cast<size_t>(std::min(leftDelay, kIrLength - 1))] = leftGain * elevationAttenuation;
    impulse->right[static_cast<size_t>(std::min(rightDelay, kIrLength - 1))] = rightGain * elevationAttenuation;

    for (int i = 1; i < kIrLength; ++i) {
        impulse->left[static_cast<size_t>(i)] = impulse->left[static_cast<size_t>(i - 1)] * decay;
        impulse->right[static_cast<size_t>(i)] = impulse->right[static_cast<size_t>(i - 1)] * decay;
    }
}

HrtfPresetData HrtfPresets::buildPreset(HrtfPresetId presetId, float sampleRate)
{
    HrtfPresetData data;
    data.sampleRate = sampleRate;
    data.irLength = kIrLength;

    float itdScale = 1.f;
    float ildScale = 0.35f;
    switch (presetId) {
    case HrtfPresetId::Wide:
        itdScale = 1.35f;
        ildScale = 0.55f;
        break;
    case HrtfPresetId::Close:
        itdScale = 0.65f;
        ildScale = 0.2f;
        break;
    case HrtfPresetId::Default:
    default:
        break;
    }

    for (int channel = 0; channel < SurroundProcessor::kChannelCount; ++channel) {
        const auto speaker = static_cast<SurroundProcessor::Channel>(channel);
        const float azimuth = speakerAzimuth(speaker);
        const float elevation = speaker == SurroundProcessor::Lfe ? -20.f : 0.f;
        generateSpeakerImpulse(azimuth,
                               elevation,
                               itdScale,
                               ildScale,
                               sampleRate,
                               &data.speakers[static_cast<size_t>(channel)]);

        if (speaker == SurroundProcessor::Lfe) {
            HrtfSpeakerImpulse &impulse = data.speakers[static_cast<size_t>(channel)];
            impulse.left.assign(static_cast<size_t>(kIrLength), 0.f);
            impulse.right.assign(static_cast<size_t>(kIrLength), 0.f);
            const int pulseLength = std::min(kIrLength, static_cast<int>(sampleRate * 0.025f));
            for (int i = 0; i < pulseLength; ++i) {
                const float t = static_cast<float>(i) / sampleRate;
                const float envelope = std::exp(-t * 120.f);
                impulse.left[static_cast<size_t>(i)] = 0.35f * envelope;
                impulse.right[static_cast<size_t>(i)] = 0.35f * envelope;
            }
        } else {
            highPassImpulse(&data.speakers[static_cast<size_t>(channel)], sampleRate, 200.f);
        }

        normalizeImpulse(&data.speakers[static_cast<size_t>(channel)]);
    }

    return data;
}

const HrtfPresetData &HrtfPresets::preset(HrtfPresetId presetId, float sampleRate)
{
    const PresetCacheKey key{static_cast<int>(presetId), static_cast<int>(std::lround(sampleRate))};
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    auto it = g_cache.find(key);
    if (it != g_cache.end()) {
        return it->second;
    }

    HrtfPresetData built = buildPreset(presetId, sampleRate);
    auto inserted = g_cache.emplace(key, std::move(built));
    return inserted.first->second;
}

const char *HrtfPresets::presetDisplayName(HrtfPresetId presetId)
{
    switch (presetId) {
    case HrtfPresetId::Wide:
        return "Wide";
    case HrtfPresetId::Close:
        return "Close";
    case HrtfPresetId::Default:
    default:
        return "Default";
    }
}
