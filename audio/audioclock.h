#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

inline double audioClockSeconds()
{
    static const double invFrequency = []() -> double {
        LARGE_INTEGER frequency{};
        QueryPerformanceFrequency(&frequency);
        return frequency.QuadPart > 0 ? 1.0 / static_cast<double>(frequency.QuadPart) : 0.0;
    }();

    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart) * invFrequency;
}
