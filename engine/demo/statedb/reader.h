#pragma once

#include "core/error.h"
#include "core/file.h"
#include "core/json.h"
#include "core/time.h"
#include "demo/events.h"
#include "demo/statedb/format.h"
#include "demo/types.h"

#include <array>
#include <filesystem>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace gmdr::demo::statedb {

struct SchemaProp {
    std::string name;
    std::string table;
    PropType type = PropType::Int;
    std::uint32_t flags = 0;
    int bits = 0;
    float low = 0, high = 0;
    int elements = 0;
    bool hasElement = false;
    PropType elementType = PropType::Int;
    std::uint32_t elementFlags = 0;
    int elementBits = 0;
};

struct SchemaClass {
    int id = 0;
    std::string name;
    std::string table;
    std::vector<SchemaProp> props;
};

struct EntityState {
    int index = 0;
    int classId = 0;
    int serial = 0;
    std::uint32_t life = 0;
    bool inPvs = false;
    std::vector<PropValue> props;
};

struct TableState {
    std::string name;
    std::map<int, std::pair<std::string, std::vector<std::uint8_t>>> entries;
    const std::string* string(int index) const {
        auto it = entries.find(index);
        return it == entries.end() ? nullptr : &it->second.first;
    }
};

struct WorldState {
    Tick tick = -1;
    std::vector<std::optional<EntityState>> entities; // by entity index
    std::map<int, TableState> tables;                 // by string table id
    const TableState* table(const std::string& name) const;
};

struct LifeInfo {
    std::uint32_t life = 0;
    int index = 0;
    int classId = 0;
    int serial = 0;
    Tick firstTick = 0;
    Tick lastTick = -1;
    std::vector<std::pair<Tick, Tick>> pvs;
    std::uint64_t uid = 0;
};

struct CameraSample {
    Tick tick = 0;
    Vec3 origin, angles, localAngles;
};

struct EventRecord {
    Tick tick = 0;
    EventKind kind = EventKind::GameEvent;
    std::int32_t entity = -1;
    std::string name;
    std::string summary;
    std::uint64_t bitOffset = 0;
    std::uint32_t bitLength = 0;
};

// Reads a state.gmstate file, complete or still being written by the importer. It trusts nothing: every
// chunk is checked (magic, sizes, XXH3, exact zstd size) and every record is bounds-checked. Thread-safe.
class StateReader {
public:
    static Result<std::unique_ptr<StateReader>> open(const std::filesystem::path& path, bool requireComplete);

    // Registers a chunk announced by the importer while it writes the file.
    Result<void> addChunk(const ChunkRef& ref);

    bool complete() const;
    std::array<std::uint8_t, 32> demoHash() const { return demoHash_; }
    Tick readyTick() const; // highest tick covered by delta chunks
    std::size_t chunkCount() const;

    Result<Json> info() const;
    Result<Json> manifest() const;
    Result<std::shared_ptr<const std::vector<SchemaClass>>> schema() const;
    Result<std::shared_ptr<const std::vector<LifeInfo>>> lives() const;
    // Pass 1 index: available right after the importer's fast pass.
    Result<std::shared_ptr<const std::vector<IndexEntry>>> index() const;
    Tick lastTick() const; // last tick of the demo from the index, -1 before it exists

    // Calls `fn` with the world state at `tick` (after every record with tick <= `tick`).
    Result<void> withStateAt(Tick tick, const std::function<void(const WorldState&)>& fn);

    Result<std::vector<EventRecord>> events(Tick from, Tick to, std::size_t maxCount) const;
    Result<std::vector<CameraSample>> camera(Tick from, Tick to) const;

private:
    explicit StateReader(File file) : file_(std::move(file)) {}

    using Blob = std::shared_ptr<const std::vector<std::uint8_t>>;
    Result<Blob> loadChunk(const ChunkRef& ref) const;
    Result<void> loadDirectory(std::uint64_t offset);
    Result<void> addChunkLocked(const ChunkRef& ref);
    Result<std::shared_ptr<const std::vector<SchemaClass>>> schemaLocked() const;
    Result<std::shared_ptr<const WorldState>> keyframeState(const ChunkRef& key) const;
    Result<std::shared_ptr<const std::map<int, TableState>>> tablesAt(Tick snapshotTick) const;
    Result<void> applyDeltas(WorldState& state, const Blob& deltas, std::size_t& pos, Tick& lastTick,
                             Tick upTo) const;
    std::optional<ChunkRef> latest(ChunkKind kind) const;

    mutable std::mutex mutex_;
    File file_;
    std::array<std::uint8_t, 32> demoHash_{};
    bool complete_ = false;
    std::vector<ChunkRef> chunks_;
    std::map<Tick, ChunkRef> keyframes_, keyTables_, deltas_;
    std::vector<ChunkRef> events_, camera_;

    // LRU of decompressed chunks (by offset), budget in bytes.
    mutable std::list<std::pair<std::uint64_t, Blob>> lru_;
    mutable std::unordered_map<std::uint64_t, std::list<std::pair<std::uint64_t, Blob>>::iterator> lruIndex_;
    mutable std::size_t lruBytes_ = 0;
    mutable std::shared_ptr<const std::vector<SchemaClass>> schema_;
    mutable std::shared_ptr<const std::vector<LifeInfo>> lives_;
    mutable std::shared_ptr<const std::vector<IndexEntry>> index_;
    mutable std::map<Tick, std::shared_ptr<const WorldState>> keyframeCache_;
    mutable std::map<Tick, std::shared_ptr<const std::map<int, TableState>>> tablesCache_;

    // Incremental cursor for forward playback.
    struct Cursor {
        Tick segment = -2;
        Tick upTo = -2; // last tick the state was advanced to
        std::shared_ptr<WorldState> state;
        std::size_t pos = 0;
        Tick lastRecordTick = 0;
        Blob deltas;
    };
    Cursor cursor_;
};

} // namespace gmdr::demo::statedb
