#pragma once

#include "core/error.h"

#include <cstdint>
#include <span>
#include <string>

namespace gmdr::demo {

inline constexpr std::size_t kHeaderSize = 1072;

struct DemoHeader {
    std::string magic; // "HL2DEMO" or "GMODEMO"
    std::int32_t demoProtocol = 0;
    std::int32_t networkProtocol = 0;
    std::string serverName;
    std::string clientName;
    std::string mapName;
    std::string gameDirectory;
    float playbackTime = 0;
    std::int32_t ticks = 0;
    std::int32_t frames = 0;
    std::int32_t signonLength = 0;
};

Result<DemoHeader> parseHeader(std::span<const std::uint8_t> file);

} // namespace gmdr::demo
