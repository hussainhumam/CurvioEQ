#pragma once

#include "surroundprocessor.h"

#include <array>

inline std::array<int, SurroundProcessor::kChannelCount> defaultVirtualSurroundChannelLevels()
{
    return {50, 50, 50, 0, 50, 50, 50, 50};
}

struct VirtualSurroundSettings {
    bool enabled = false;
    int presetId = 0;
    int strength = 75;
    std::array<int, SurroundProcessor::kChannelCount> channelLevels = defaultVirtualSurroundChannelLevels();
};
