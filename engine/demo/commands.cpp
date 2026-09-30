#include "demo/commands.h"

#include "core/byte_reader.h"
#include "core/limits.h"
#include "demo/header.h"

#include <string>

namespace gmdr::demo {

CommandReader::CommandReader(std::span<const std::uint8_t> file) : file_(file), pos_(kHeaderSize) {}

namespace {

void readVec3(ByteReader& r, Vec3& v) {
    r.f32(v.x);
    r.f32(v.y);
    r.f32(v.z);
}

Error truncated(std::uint64_t offset) {
    return makeError("demo.truncated", "demo ends in the middle of a command", "offset " + std::to_string(offset));
}

} // namespace

Result<bool> CommandReader::next(CommandRecord& out) {
    if (stopped_ || pos_ >= file_.size())
        return false;
    ByteReader r(file_.subspan(static_cast<std::size_t>(pos_)));
    std::uint8_t cmd = 0;
    std::int32_t tick = 0;
    r.u8(cmd);
    r.i32(tick);
    if (!r.ok())
        return truncated(pos_);

    out = CommandRecord{};
    out.cmd = static_cast<DemoCommand>(cmd);
    out.tick = tick;
    out.offset = pos_;

    std::int32_t length = 0;
    switch (out.cmd) {
    case DemoCommand::Signon:
    case DemoCommand::Packet: {
        r.i32(out.info.flags);
        readVec3(r, out.info.viewOrigin);
        readVec3(r, out.info.viewAngles);
        readVec3(r, out.info.localViewAngles);
        readVec3(r, out.info.viewOrigin2);
        readVec3(r, out.info.viewAngles2);
        readVec3(r, out.info.localViewAngles2);
        std::int32_t seqIn = 0, seqOut = 0;
        r.i32(seqIn);
        r.i32(seqOut);
        r.i32(length);
        break;
    }
    case DemoCommand::SyncTick:
        break;
    case DemoCommand::ConsoleCmd:
    case DemoCommand::DataTables:
    case DemoCommand::StringTables:
        r.i32(length);
        break;
    case DemoCommand::UserCmd: {
        std::int32_t outgoing = 0;
        r.i32(outgoing);
        r.i32(length);
        break;
    }
    case DemoCommand::Stop:
        stopped_ = true;
        pos_ += r.position();
        return false;
    default:
        stopped_ = true;
        return makeError("demo.unknown_command", "unknown demo command",
                         "command " + std::to_string(cmd) + " at offset " + std::to_string(pos_));
    }
    if (!r.ok())
        return truncated(pos_);
    if (length < 0 || static_cast<std::size_t>(length) > limits::kMaxPacketBytes)
        return makeError("demo.bad_length", "invalid command length", "offset " + std::to_string(pos_));
    if (static_cast<std::size_t>(length) > r.remaining())
        return truncated(pos_);

    out.payloadOffset = pos_ + r.position();
    out.payloadSize = static_cast<std::uint32_t>(length);
    pos_ = out.payloadOffset + static_cast<std::uint64_t>(length);
    return true;
}

} // namespace gmdr::demo
