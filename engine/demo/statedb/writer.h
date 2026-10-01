#pragma once

#include "core/error.h"
#include "core/file.h"
#include "core/hash.h"
#include "demo/parser.h"
#include "demo/statedb/codec.h"
#include "demo/statedb/format.h"

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace gmdr::demo::statedb {

// A DemoSink that turns the parse into a state.gmstate file: keyframes every 30 s of demo time, delta
// records in between, events, the recorder's camera, entity lives and the content manifest. Each written
// chunk is announced through `onChunk` so a reader can use the file while it grows.
class StateWriter : public DemoSink {
public:
    struct Callbacks {
        std::function<void(const ChunkRef&)> onChunk;
        std::function<void(Tick)> onProgress;
    };

    StateWriter(File& out, const Blake3Digest& demoHash, Callbacks callbacks);

    Result<void> begin();
    // Pass 1 result: where each tick starts in the .dem (written before the full decode).
    Result<void> writeIndex(std::span<const IndexEntry> entries, Tick lastTick);
    // Writes the final chunks and marks the file complete.
    Result<void> finish(const ParseStats& stats);
    // First write error, if any (sink callbacks cannot return errors).
    const std::optional<Error>& writeError() const { return writeError_; }

    void onHeader(const DemoHeader& h) override;
    void onServerInfo(const ServerInfo& s) override;
    void onDataTables(const DataTables& dt) override;
    void onStringTableChanged(Tick tick, int tableId, const StringTable& table, std::span<const int> changed,
                              bool created) override;
    void onPacket(const CommandRecord& rec) override;
    void onEntityEnter(Tick tick, const EntityRef& ref, bool newLife,
                       std::span<const PropValue> state) override;
    void onEntityUpdate(Tick tick, const EntityRef& ref, std::span<const int> changed,
                        std::span<const PropValue> state) override;
    void onEntityLeave(Tick tick, const EntityRef& ref, bool deleted) override;
    void onEvent(const DemoEvent& e) override;
    void onTickEnd(Tick tick) override;
    void onDecodeError(Tick tick, std::uint64_t offset, const Error& e) override;

private:
    struct EntityMirror {
        bool active = false;
        bool inPvs = false;
        int classId = -1;
        int serial = 0;
        std::uint32_t life = 0;
        std::vector<PropValue> props;
    };
    struct TableMirror {
        std::string name;
        std::map<int, std::pair<std::string, std::vector<std::uint8_t>>> entries;
    };
    struct Life {
        std::uint32_t life = 0;
        int index = 0;
        int classId = 0;
        int serial = 0;
        Tick firstTick = 0;
        Tick lastTick = -1;                     // -1: still alive at the end
        std::vector<std::pair<Tick, Tick>> pvs; // [enter, leave]; leave -1 = until end
    };

    Result<void> writeChunk(ChunkKind kind, Tick from, Tick to, const std::vector<std::uint8_t>& raw);
    void writeOrRemember(ChunkKind kind, Tick from, Tick to, const std::vector<std::uint8_t>& raw);
    void flushSegment(Tick tick);
    void cutBefore(Tick tick);
    void writeKeyframe(Tick tick);
    void writeInfo(const ParseStats* stats);
    void deltaHeader(DeltaOp op, Tick tick);
    void encodeProps(Encoder& e, std::span<const PropValue> state);
    std::string manifestJson() const;

    File& out_;
    Blake3Digest demoHash_;
    Callbacks cb_;
    std::uint64_t writeOffset_ = kFileHeaderSize;
    std::vector<ChunkRef> directory_;
    std::optional<Error> writeError_;

    DemoHeader header_;
    ServerInfo serverInfo_;
    bool haveServerInfo_ = false;
    bool haveTables_ = false;
    Tick ticksPerKeyframe_ = 1980;

    Encoder deltas_, events_, camera_;
    // Records in a chunk store ticks relative to the previous record, starting from the chunk's tickFrom.
    Tick deltaLastTick_ = -1, eventsLastTick_ = -1, cameraLastTick_ = -1;
    Tick segmentStart_ = -1; // tick of the last keyframe; -1 before the first
    Tick lastTick_ = 0;
    bool keyframeWritten_ = false;
    std::optional<Tick> pendingCut_; // keyframe due at this tick, written when a later tick starts

    std::vector<EntityMirror> entities_;
    std::map<int, TableMirror> tables_;
    std::set<int> dirtyTables_; // changed since the last STRINGTABLES snapshot
    std::map<std::uint32_t, Life> lives_;
    std::vector<std::string> decodeErrors_;
};

} // namespace gmdr::demo::statedb
