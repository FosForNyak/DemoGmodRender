#pragma once

#include "core/error.h"
#include "demo/types.h"

#include <cstdint>
#include <span>

namespace gmdr::demo {

enum class DemoCommand : std::uint8_t {
    Signon = 1,
    Packet = 2,
    SyncTick = 3,
    ConsoleCmd = 4,
    UserCmd = 5,
    DataTables = 6,
    Stop = 7,
    StringTables = 8,
};

// democmdinfo_t: the recording player's view for this packet.
struct CmdInfo {
    std::int32_t flags = 0;
    Vec3 viewOrigin, viewAngles, localViewAngles;
    Vec3 viewOrigin2, viewAngles2, localViewAngles2;
};

struct CommandRecord {
    DemoCommand cmd = DemoCommand::Stop;
    std::int32_t tick = 0;
    std::uint64_t offset = 0;        // file offset of the command byte
    std::uint64_t payloadOffset = 0; // file offset of the payload (net messages for packets)
    std::uint32_t payloadSize = 0;
    CmdInfo info; // packets and signon only
};

// Walks the command stream of a demo held in memory (usually memory-mapped). Validates every length
// against the file size; never reads outside the buffer.
class CommandReader {
public:
    explicit CommandReader(std::span<const std::uint8_t> file);

    // Returns true and fills `out` for each command; false at dem_stop or the end of the file.
    // A truncated file ends the walk with an error.
    Result<bool> next(CommandRecord& out);

    std::uint64_t position() const { return pos_; }
    bool stopped() const { return stopped_; }

private:
    std::span<const std::uint8_t> file_;
    std::uint64_t pos_;
    bool stopped_ = false;
};

} // namespace gmdr::demo
