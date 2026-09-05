#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

enum class AudioChainStage : uint8_t {
    Eq = 0,
    VirtualSurround = 1,
    Dynamics = 2,
    Loudness = 3,
    Addon0 = 4,
    Addon1 = 5,
    Addon2 = 6,
    Addon3 = 7,
};

inline constexpr int kAudioChainBuiltinCount = 4;
inline constexpr int kAudioChainAddonCount = 4;
inline constexpr int kAudioChainMaxStages = 8;
inline constexpr int kAudioChainStageCount = kAudioChainBuiltinCount;

struct AudioChainOrder {
    std::array<AudioChainStage, kAudioChainMaxStages> stages{
        AudioChainStage::Eq,
        AudioChainStage::VirtualSurround,
        AudioChainStage::Dynamics,
        AudioChainStage::Loudness,
        AudioChainStage::Eq,
        AudioChainStage::Eq,
        AudioChainStage::Eq,
        AudioChainStage::Eq,
    };
    int count = kAudioChainBuiltinCount;
};

inline AudioChainOrder defaultAudioChainOrder()
{
    return {};
}

inline bool audioChainStageIsBuiltin(AudioChainStage stage)
{
    return static_cast<int>(stage) >= 0 && static_cast<int>(stage) < kAudioChainBuiltinCount;
}

inline bool audioChainStageIsAddon(AudioChainStage stage)
{
    const int id = static_cast<int>(stage);
    return id >= kAudioChainBuiltinCount && id < kAudioChainMaxStages;
}

inline int audioChainAddonIndex(AudioChainStage stage)
{
    return static_cast<int>(stage) - kAudioChainBuiltinCount;
}

inline AudioChainStage audioChainAddonStage(int slot)
{
    return static_cast<AudioChainStage>(kAudioChainBuiltinCount + slot);
}

inline bool audioChainOrdersEqual(const AudioChainOrder &left, const AudioChainOrder &right)
{
    if (left.count != right.count) {
        return false;
    }
    for (int i = 0; i < left.count; ++i) {
        if (left.stages[static_cast<size_t>(i)] != right.stages[static_cast<size_t>(i)]) {
            return false;
        }
    }
    return true;
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
    case AudioChainStage::Addon0:
        return "addon0";
    case AudioChainStage::Addon1:
        return "addon1";
    case AudioChainStage::Addon2:
        return "addon2";
    case AudioChainStage::Addon3:
        return "addon3";
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
    if (std::strcmp(id, "addon0") == 0) {
        *stage = AudioChainStage::Addon0;
        return true;
    }
    if (std::strcmp(id, "addon1") == 0) {
        *stage = AudioChainStage::Addon1;
        return true;
    }
    if (std::strcmp(id, "addon2") == 0) {
        *stage = AudioChainStage::Addon2;
        return true;
    }
    if (std::strcmp(id, "addon3") == 0) {
        *stage = AudioChainStage::Addon3;
        return true;
    }
    return false;
}

inline AudioChainOrder builtinsOnly(const AudioChainOrder &order)
{
    AudioChainOrder builtins = defaultAudioChainOrder();
    int count = 0;
    const int inCount = std::clamp(order.count, 0, kAudioChainMaxStages);
    for (int i = 0; i < inCount; ++i) {
        if (audioChainStageIsBuiltin(order.stages[static_cast<size_t>(i)])) {
            builtins.stages[static_cast<size_t>(count++)] = order.stages[static_cast<size_t>(i)];
        }
    }
    builtins.count = count;
    return builtins;
}

inline AudioChainOrder normalizeAudioChainOrder(AudioChainOrder order)
{
    const int inCount = std::clamp(order.count, 0, kAudioChainMaxStages);
    bool seen[kAudioChainMaxStages] = {};
    bool builtinSeen[kAudioChainBuiltinCount] = {};
    std::array<AudioChainStage, kAudioChainMaxStages> kept{};
    int keptCount = 0;

    for (int i = 0; i < inCount; ++i) {
        const int id = static_cast<int>(order.stages[static_cast<size_t>(i)]);
        if (id < 0 || id >= kAudioChainMaxStages || seen[id]) {
            continue;
        }
        seen[id] = true;
        if (id < kAudioChainBuiltinCount) {
            builtinSeen[id] = true;
        }
        kept[static_cast<size_t>(keptCount++)] = order.stages[static_cast<size_t>(i)];
    }

    const bool allBuiltins = builtinSeen[0] && builtinSeen[1] && builtinSeen[2] && builtinSeen[3];
    if (!allBuiltins) {
        AudioChainOrder restored = defaultAudioChainOrder();
        for (int i = 0; i < keptCount; ++i) {
            if (audioChainStageIsAddon(kept[static_cast<size_t>(i)])
                && restored.count < kAudioChainMaxStages) {
                restored.stages[static_cast<size_t>(restored.count++)] = kept[static_cast<size_t>(i)];
            }
        }
        return restored;
    }

    AudioChainOrder normalized;
    normalized.stages = kept;
    normalized.count = keptCount;
    return normalized;
}

inline AudioChainOrder appendAddonToChain(AudioChainOrder order, int slot)
{
    if (slot < 0 || slot >= kAudioChainAddonCount) {
        return normalizeAudioChainOrder(order);
    }
    AudioChainOrder normalized = normalizeAudioChainOrder(order);
    const AudioChainStage addon = audioChainAddonStage(slot);
    for (int i = 0; i < normalized.count; ++i) {
        if (normalized.stages[static_cast<size_t>(i)] == addon) {
            return normalized;
        }
    }
    if (normalized.count >= kAudioChainMaxStages) {
        return normalized;
    }
    normalized.stages[static_cast<size_t>(normalized.count++)] = addon;
    return normalized;
}

inline AudioChainOrder removeAddonFromChain(AudioChainOrder order, int slot)
{
    if (slot < 0 || slot >= kAudioChainAddonCount) {
        return normalizeAudioChainOrder(order);
    }
    const AudioChainStage addon = audioChainAddonStage(slot);
    AudioChainOrder filtered = defaultAudioChainOrder();
    filtered.count = 0;
    const AudioChainOrder normalized = normalizeAudioChainOrder(order);
    for (int i = 0; i < normalized.count; ++i) {
        if (normalized.stages[static_cast<size_t>(i)] == addon) {
            continue;
        }
        filtered.stages[static_cast<size_t>(filtered.count++)] = normalized.stages[static_cast<size_t>(i)];
    }
    return normalizeAudioChainOrder(filtered);
}

inline uint64_t packAudioChainOrder(const AudioChainOrder &order)
{
    const AudioChainOrder normalized = normalizeAudioChainOrder(order);
    uint64_t packed = 0;
    for (int i = 0; i < kAudioChainMaxStages; ++i) {
        const uint64_t byte = (i < normalized.count)
                                  ? static_cast<uint64_t>(
                                        static_cast<uint8_t>(normalized.stages[static_cast<size_t>(i)]))
                                  : 0xFFull;
        packed |= byte << (i * 8);
    }
    return packed;
}

inline AudioChainOrder unpackAudioChainOrder(uint64_t packed)
{
    AudioChainOrder order;
    order.count = 0;
    for (int i = 0; i < kAudioChainMaxStages; ++i) {
        const uint8_t byte = static_cast<uint8_t>((packed >> (i * 8)) & 0xFFu);
        if (byte == 0xFFu) {
            break;
        }
        order.stages[static_cast<size_t>(order.count++)] = static_cast<AudioChainStage>(byte);
    }
    return normalizeAudioChainOrder(order);
}
