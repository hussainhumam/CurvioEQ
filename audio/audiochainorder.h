#pragma once

#include <array>
#include <cstdint>
#include <cstring>

enum class AudioChainStage : uint8_t {
    Eq = 0,
    VirtualSurround = 1,
    Dynamics = 2,
    Loudness = 3,
};

inline constexpr int kAudioChainStageCount = 4;

struct AudioChainOrder {
    std::array<AudioChainStage, kAudioChainStageCount> stages{
        AudioChainStage::Eq,
        AudioChainStage::VirtualSurround,
        AudioChainStage::Dynamics,
        AudioChainStage::Loudness,
    };
};

inline AudioChainOrder defaultAudioChainOrder()
{
    return {};
}

inline bool audioChainOrdersEqual(const AudioChainOrder &left, const AudioChainOrder &right)
{
    return left.stages == right.stages;
}

inline const char *audioChainStageId(AudioChainStage stage)
{
    switch (stage) {
    case AudioChainStage::Eq:
        return "eq";
    case AudioChainStage::VirtualSurround:
        return "surround";
    case AudioChainStage::Dynamics:
        return "dynamics";
    case AudioChainStage::Loudness:
        return "loudness";
    }
    return "eq";
}

inline bool audioChainStageFromId(const char *id, AudioChainStage *stage)
{
    if (!id || !stage) {
        return false;
    }
    if (std::strcmp(id, "eq") == 0) {
        *stage = AudioChainStage::Eq;
        return true;
    }
    if (std::strcmp(id, "surround") == 0) {
        *stage = AudioChainStage::VirtualSurround;
        return true;
    }
    if (std::strcmp(id, "dynamics") == 0) {
        *stage = AudioChainStage::Dynamics;
        return true;
    }
    if (std::strcmp(id, "loudness") == 0) {
        *stage = AudioChainStage::Loudness;
        return true;
    }
    return false;
}

inline AudioChainOrder normalizeAudioChainOrder(AudioChainOrder order)
{
    bool seen[kAudioChainStageCount] = {};
    for (AudioChainStage stage : order.stages) {
        const int id = static_cast<int>(stage);
        if (id < 0 || id >= kAudioChainStageCount || seen[id]) {
            return defaultAudioChainOrder();
        }
        seen[id] = true;
    }
    return order;
}

inline uint32_t packAudioChainOrder(const AudioChainOrder &order)
{
    const AudioChainOrder normalized = normalizeAudioChainOrder(order);
    uint32_t packed = 0;
    for (int i = 0; i < kAudioChainStageCount; ++i) {
        packed |= static_cast<uint32_t>(normalized.stages[static_cast<size_t>(i)]) << (i * 8);
    }
    return packed;
}

inline AudioChainOrder unpackAudioChainOrder(uint32_t packed)
{
    AudioChainOrder order;
    for (int i = 0; i < kAudioChainStageCount; ++i) {
        order.stages[static_cast<size_t>(i)] =
            static_cast<AudioChainStage>((packed >> (i * 8)) & 0xFFu);
    }
    return normalizeAudioChainOrder(order);
}
