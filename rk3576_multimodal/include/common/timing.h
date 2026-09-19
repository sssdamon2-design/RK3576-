#pragma once
#include <chrono>
#include "common/types.h"

inline double elapsedMilliseconds(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
}
inline double frameAgeMilliseconds(const ImageFrame& frame)
{
    if (frame.received_at == std::chrono::steady_clock::time_point{}) return -1.0;
    return elapsedMilliseconds(frame.received_at);
}
