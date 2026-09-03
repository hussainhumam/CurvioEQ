#pragma once

class SurroundProcessor
{
public:
    static constexpr int kChannelCount = 8;

    enum Channel {
        FrontLeft = 0,
        FrontRight,
        FrontCenter,
        Lfe,
        BackLeft,
        BackRight,
        SideLeft,
        SideRight,
    };
};
