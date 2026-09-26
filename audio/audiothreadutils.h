#pragma once

#include <xmmintrin.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <avrt.h>

namespace AudioThreadUtils {

inline void enableFlushToZero()
{
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
}

inline HANDLE enableProAudioMmcss()
{
    DWORD taskIndex = 0;
    return AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
}

inline void disableMmcss(HANDLE taskHandle)
{
    if (taskHandle) {
        AvRevertMmThreadCharacteristics(taskHandle);
    }
}

inline void applyCpuAffinity(int coreIndex)
{
    if (coreIndex < 0) {
        return;
    }
    const int bitCount = static_cast<int>(sizeof(DWORD_PTR) * 8);
    if (coreIndex >= bitCount) {
        return;
    }
    SetThreadAffinityMask(GetCurrentThread(), static_cast<DWORD_PTR>(1) << coreIndex);
}

} // namespace AudioThreadUtils
