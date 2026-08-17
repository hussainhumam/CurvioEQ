#pragma once

#include <xmmintrin.h>

namespace AudioThreadUtils {

inline void enableFlushToZero()
{
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
}

} // namespace AudioThreadUtils
