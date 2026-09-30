#pragma once

#include <cmath>
#include <cstdint>

namespace gmdr {

using Tick = std::int64_t;   // demo/server tick
using Flicks = std::int64_t; // timeline time, 1/705 600 000 s: divides all common frame and audio rates exactly

inline constexpr Flicks kFlicksPerSecond = 705'600'000;

inline Flicks secondsToFlicks(double seconds) {
    return static_cast<Flicks>(std::llround(seconds * static_cast<double>(kFlicksPerSecond)));
}

inline double flicksToSeconds(Flicks f) {
    return static_cast<double>(f) / static_cast<double>(kFlicksPerSecond);
}

inline Flicks ticksToFlicks(Tick ticks, double tickInterval) {
    return secondsToFlicks(static_cast<double>(ticks) * tickInterval);
}

} // namespace gmdr
