#include "demo/statedb/reader.h"

#include "core/compress.h"
#include "core/hash.h"
#include "core/limits.h"
#include "demo/statedb/codec.h"

#include <algorithm>
#include <cstring>

namespace gmdr::demo::statedb {

namespace {

constexpr std::size_t kLruBudget = 512u << 20;
constexpr std::size_t kKeyframeCacheSize = 4;
// History-coded string-table entries can exceed one message string (prefix + suffix).
constexpr std::size_t kMaxEntryString = 2 * limits::kMaxStringBytes;

Error corrupt(const std::string& what) {
    return makeError("statedb.corrupt", "state file is damaged", what);
}

bool decodeProps(Decoder& d, std::vector<PropValue>& props) {
    const std::uint64_t count = d.varint();
    if (!d.ok() || count > props.size())
        return false;
    std::int64_t index = -1;
    for (std::uint64_t i = 0; i < count; ++i) {
        index += 1 + static_cast<std::int64_t>(d.varint());
        if (!d.ok() || index < 0 || static_cast<std::size_t>(index) >= props.size())
            return false;
        if (!d.value(props[static_cast<std::size_t>(index)]))
            return false;
    }
    return d.ok();
}

} // namespace

std::uint64_t lifeUid(const std::array<std::uint8_t, 32>& demoHash, std::uint32_t life, int index,
                      int serial) {
    std::uint8_t bytes[32 + 12];
    std::memcpy(bytes, demoHash.data(), 32);
    const std::uint32_t fields[3] = {life, static_cast<std::uint32_t>(index),
                                     static_cast<std::uint32_t>(serial)};
    std::memcpy(bytes + 32, fields, sizeof fields);
    return stableId64(bytes);
}

namespace {

// Records the props a record at the target tick touched.
void noteChanged(WorldState& state, Tick tick, int index, const std::vector<PropValue>* all,
                 std::span<const int> some) {
    if (state.changedTick != tick) {
        state.changed.clear();
        state.changedTick = tick;
    }
    auto& list = state.changed[index];
    if (all) {
        for (std::size_t i = 0; i < all->size(); ++i)
            if ((*all)[i].isSet())
                list.push_back(static_cast<int>(i));
    } else {
        list.insert(list.end(), some.begin(), some.end());
    }
    std::sort(list.begin(), list.end());
    list.erase(std::unique(list.begin(), list.end()), list.end());
}

} // namespace

const TableState* WorldState::table(const std::string& name) const {
    for (const auto& [id, t] : tables)
        if (t.name == name)
            return &t;
    return nullptr;
}

Result<std::unique_ptr<StateReader>> StateReader::open(const std::filesystem::path& path,
                                                       bool requireComplete) {
    auto file = File::open(path, File::Mode::Read);
    if (!file)
        return file.error();
    FileHeader h{};
    GMDR_TRY(file->readExactAt(0, std::span(reinterpret_cast<std::uint8_t*>(&h), sizeof h)));
    if (std::memcmp(h.magic, kFileMagic, sizeof h.magic) != 0)
        return makeError("statedb.bad_magic", "not a state file");
    if (h.formatVersion != kFormatVersion || h.parserVersion != kParserVersion)
        return makeError("statedb.version", "state file was written by another version");
    std::unique_ptr<StateReader> r(new StateReader(std::move(*file)));
    r->demoHash_ = h.demoHash;
    r->complete_ = (h.flags & kComplete) != 0;
    if (requireComplete && !r->complete_)
        return makeError("statedb.incomplete", "state file is not complete");
    if (r->complete_)
        GMDR_TRY(r->loadDirectory(h.directoryOffset));
    return r;
}

Result<void> StateReader::loadDirectory(std::uint64_t offset) {
    ChunkHeader ch{};
    GMDR_TRY(file_.readExactAt(offset, std::span(reinterpret_cast<std::uint8_t*>(&ch), sizeof ch)));
    if (ch.magic != kChunkMagic || ch.kind != static_cast<std::uint16_t>(ChunkKind::Directory))
        return corrupt("directory header");
    ChunkRef ref{ChunkKind::Directory, offset, ch.tickFrom, ch.tickTo, ch.rawSize, ch.storedSize};
    auto blob = loadChunk(ref);
    if (!blob)
        return blob.error();
    Decoder d(**blob);
    const std::uint64_t n = d.varint();
    if (!d.ok() || n > 1'000'000)
        return corrupt("directory size");
    for (std::uint64_t i = 0; i < n; ++i) {
        ChunkRef c;
        c.kind = static_cast<ChunkKind>(d.u8());
        c.offset = d.u64();
        c.tickFrom = d.svarint();
        c.tickTo = d.svarint();
        c.rawSize = static_cast<std::uint32_t>(d.varint());
        c.storedSize = static_cast<std::uint32_t>(d.varint());
        if (!d.ok())
            return corrupt("directory entry");
        GMDR_TRY(addChunkLocked(c));
    }
    return {};
}

Result<void> StateReader::addChunk(const ChunkRef& ref) {
    std::lock_guard lock(mutex_);
    return addChunkLocked(ref);
}

Result<void> StateReader::addChunkLocked(const ChunkRef& ref) {
    if (ref.rawSize > limits::kMaxChunkBytes || ref.storedSize > limits::kMaxChunkBytes)
        return corrupt("chunk size");
    chunks_.push_back(ref);
    switch (ref.kind) {
    case ChunkKind::Keyframe:
        keyframes_[ref.tickFrom] = ref;
        break;
    case ChunkKind::StringTables:
        keyTables_[ref.tickFrom] = ref;
        break;
    case ChunkKind::Deltas:
        deltas_[ref.tickFrom] = ref;
        break;
    case ChunkKind::Events:
        events_.push_back(ref);
        break;
    case ChunkKind::Camera:
        camera_.push_back(ref);
        break;
    case ChunkKind::Schema:
        schema_.reset();
        break;
    case ChunkKind::Lives:
        lives_.reset();
        break;
    case ChunkKind::Index:
        index_.reset();
        break;
    default:
        break;
    }
    return {};
}

bool StateReader::complete() const {
    std::lock_guard lock(mutex_);
    return complete_;
}

std::size_t StateReader::chunkCount() const {
    std::lock_guard lock(mutex_);
    return chunks_.size();
}

Tick StateReader::readyTick() const {
    std::lock_guard lock(mutex_);
    Tick t = -1;
    for (const auto& [from, ref] : deltas_)
        t = std::max(t, ref.tickTo);
    return t;
}

std::optional<ChunkRef> StateReader::latest(ChunkKind kind) const {
    std::optional<ChunkRef> best;
    for (const auto& c : chunks_)
        if (c.kind == kind && (!best || c.offset > best->offset))
            best = c;
    return best;
}

Result<StateReader::Blob> StateReader::loadChunk(const ChunkRef& ref) const {
    auto it = lruIndex_.find(ref.offset);
    if (it != lruIndex_.end()) {
        lru_.splice(lru_.begin(), lru_, it->second);
        return it->second->second;
    }
    ChunkHeader ch{};
    GMDR_TRY(file_.readExactAt(ref.offset, std::span(reinterpret_cast<std::uint8_t*>(&ch), sizeof ch)));
    if (ch.magic != kChunkMagic || ch.kind != static_cast<std::uint16_t>(ref.kind) ||
        ch.rawSize != ref.rawSize || ch.storedSize != ref.storedSize || ch.rawSize > limits::kMaxChunkBytes ||
        ch.storedSize > limits::kMaxChunkBytes)
        return corrupt(std::string("chunk header for ") + chunkKindName(ref.kind));
    std::vector<std::uint8_t> stored(ch.storedSize);
    GMDR_TRY(file_.readExactAt(ref.offset + sizeof ch, stored));
    if (xxh3_64(stored) != ch.checksum)
        return corrupt(std::string("checksum of ") + chunkKindName(ref.kind));
    auto raw = zstdDecompress(stored, ch.rawSize);
    if (!raw)
        return raw.error();
    auto blob = std::make_shared<const std::vector<std::uint8_t>>(std::move(raw).value());
    lru_.emplace_front(ref.offset, blob);
    lruIndex_[ref.offset] = lru_.begin();
    lruBytes_ += blob->size();
    while (lruBytes_ > kLruBudget && lru_.size() > 1) {
        lruBytes_ -= lru_.back().second->size();
        lruIndex_.erase(lru_.back().first);
        lru_.pop_back();
    }
    return blob;
}

Result<Json> StateReader::info() const {
    std::lock_guard lock(mutex_);
    auto ref = latest(ChunkKind::Info);
    if (!ref)
        return makeError("statedb.no_info", "the demo information is not ready yet");
    auto blob = loadChunk(*ref);
    if (!blob)
        return blob.error();
    auto j = Json::parse((*blob)->begin(), (*blob)->end(), nullptr, false);
    if (j.is_discarded())
        return corrupt("info json");
    return j;
}

Result<Json> StateReader::manifest() const {
    std::lock_guard lock(mutex_);
    auto ref = latest(ChunkKind::Manifest);
    if (!ref)
        return makeError("statedb.no_manifest", "the content list is available after the import finishes");
    auto blob = loadChunk(*ref);
    if (!blob)
        return blob.error();
    auto j = Json::parse((*blob)->begin(), (*blob)->end(), nullptr, false);
    if (j.is_discarded())
        return corrupt("manifest json");
    return j;
}

Result<std::shared_ptr<const std::vector<SchemaClass>>> StateReader::schema() const {
    std::lock_guard lock(mutex_);
    return schemaLocked();
}

Result<std::shared_ptr<const std::vector<SchemaClass>>> StateReader::schemaLocked() const {
    if (schema_)
        return schema_;
    auto ref = latest(ChunkKind::Schema);
    if (!ref)
        return makeError("statedb.no_schema", "the entity schema is not ready yet");
    auto blob = loadChunk(*ref);
    if (!blob)
        return blob.error();
    Decoder d(**blob);
    auto classes = std::make_shared<std::vector<SchemaClass>>();
    const std::uint64_t n = d.varint();
    if (!d.ok() || n > limits::kMaxServerClasses)
        return corrupt("schema class count");
    classes->resize(static_cast<std::size_t>(n));
    for (auto& c : *classes) {
        c.id = static_cast<int>(d.varint());
        c.name = d.string(1024);
        c.table = d.string(1024);
        const std::uint64_t np = d.varint();
        if (!d.ok() || np > limits::kMaxFlatProps)
            return corrupt("schema prop count");
        c.props.resize(static_cast<std::size_t>(np));
        for (auto& p : c.props) {
            p.name = d.string(1024);
            p.table = d.string(1024);
            p.type = static_cast<PropType>(d.u8());
            p.flags = static_cast<std::uint32_t>(d.varint());
            p.bits = static_cast<int>(d.svarint());
            p.low = d.f32();
            p.high = d.f32();
            p.elements = static_cast<int>(d.varint());
            p.hasElement = d.u8() != 0;
            if (p.hasElement) {
                p.elementType = static_cast<PropType>(d.u8());
                p.elementFlags = static_cast<std::uint32_t>(d.varint());
                p.elementBits = static_cast<int>(d.svarint());
                d.f32();
                d.f32();
            }
        }
        if (!d.ok())
            return corrupt("schema");
    }
    schema_ = classes;
    return schema_;
}

Result<std::shared_ptr<const std::vector<LifeInfo>>> StateReader::lives() const {
    std::lock_guard lock(mutex_);
    if (lives_)
        return lives_;
    auto ref = latest(ChunkKind::Lives);
    if (!ref)
        return makeError("statedb.no_lives", "entity lifetimes are available after the import finishes");
    auto blob = loadChunk(*ref);
    if (!blob)
        return blob.error();
    Decoder d(**blob);
    const std::uint64_t n = d.varint();
    if (!d.ok() || n > 10'000'000)
        return corrupt("lives count");
    auto out = std::make_shared<std::vector<LifeInfo>>();
    out->reserve(static_cast<std::size_t>(n));
    for (std::uint64_t i = 0; i < n; ++i) {
        LifeInfo l;
        l.life = static_cast<std::uint32_t>(d.varint());
        l.index = static_cast<int>(d.varint());
        l.classId = static_cast<int>(d.varint());
        l.serial = static_cast<int>(d.varint());
        l.firstTick = d.svarint();
        l.lastTick = d.svarint();
        const std::uint64_t k = d.varint();
        if (!d.ok() || k > 1'000'000)
            return corrupt("lives intervals");
        l.pvs.reserve(static_cast<std::size_t>(k));
        for (std::uint64_t j = 0; j < k; ++j) {
            const Tick a = d.svarint();
            const Tick b = d.svarint();
            l.pvs.emplace_back(a, b);
        }
        l.uid = lifeUid(demoHash_, l.life, l.index, l.serial);
        out->push_back(std::move(l));
    }
    if (!d.ok())
        return corrupt("lives");
    lives_ = out;
    return lives_;
}

Result<std::shared_ptr<const std::vector<IndexEntry>>> StateReader::index() const {
    std::lock_guard lock(mutex_);
    if (index_)
        return index_;
    auto ref = latest(ChunkKind::Index);
    if (!ref)
        return makeError("statedb.no_index", "the demo index is not ready yet");
    auto blob = loadChunk(*ref);
    if (!blob)
        return blob.error();
    Decoder d(**blob);
    const std::uint64_t n = d.varint();
    if (!d.ok() || n > (*blob)->size())
        return corrupt("index count");
    auto out = std::make_shared<std::vector<IndexEntry>>();
    out->reserve(static_cast<std::size_t>(n));
    Tick tick = 0;
    std::int64_t offset = 0;
    for (std::uint64_t i = 0; i < n; ++i) {
        tick += d.svarint();
        offset += d.svarint();
        if (!d.ok() || offset < 0)
            return corrupt("index entry");
        out->push_back({tick, static_cast<std::uint64_t>(offset)});
    }
    index_ = out;
    return index_;
}

Tick StateReader::lastTick() const {
    std::lock_guard lock(mutex_);
    auto ref = latest(ChunkKind::Index);
    return ref ? ref->tickTo : -1;
}

Result<std::shared_ptr<const std::map<int, TableState>>> StateReader::tablesAt(Tick tick) const {
    if (auto it = tablesCache_.find(tick); it != tablesCache_.end())
        return it->second;
    // Start from the nearest composed state at or before `tick`, then apply later snapshots in order
    // (each STRINGTABLES chunk holds only the tables that changed, in full).
    auto tables = std::make_shared<std::map<int, TableState>>();
    auto from = keyTables_.begin();
    if (auto c = tablesCache_.upper_bound(tick); c != tablesCache_.begin()) {
        --c;
        *tables = *c->second;
        from = keyTables_.upper_bound(c->first);
    }
    for (auto it = from; it != keyTables_.end() && it->first <= tick; ++it) {
        auto tb = loadChunk(it->second);
        if (!tb)
            return tb.error();
        Decoder t(**tb);
        const std::uint64_t nt = t.varint();
        if (!t.ok() || nt > limits::kMaxStringTables)
            return corrupt("string table count");
        for (std::uint64_t i = 0; i < nt; ++i) {
            const int id = static_cast<int>(t.varint());
            TableState ts;
            ts.name = t.string(1024);
            const std::uint64_t ne = t.varint();
            if (!t.ok() || ne > limits::kMaxStringTableEntries)
                return corrupt("string table entries");
            for (std::uint64_t j = 0; j < ne; ++j) {
                const int idx = static_cast<int>(t.varint());
                std::string s = t.string(kMaxEntryString);
                auto ud = t.bytes(limits::kMaxUserDataBytes);
                ts.entries[idx] = {std::move(s), std::move(ud)};
            }
            if (!t.ok())
                return corrupt("string tables");
            (*tables)[id] = std::move(ts);
        }
    }
    if (tablesCache_.size() >= 8)
        tablesCache_.erase(tablesCache_.begin());
    tablesCache_[tick] = tables;
    return std::shared_ptr<const std::map<int, TableState>>(tables);
}

Result<std::shared_ptr<const WorldState>> StateReader::keyframeState(const ChunkRef& key) const {
    if (auto it = keyframeCache_.find(key.tickFrom); it != keyframeCache_.end())
        return it->second;
    auto sch = schema_;
    if (!sch)
        return makeError("statedb.no_schema", "the entity schema is not ready yet");
    auto state = std::make_shared<WorldState>();
    state->tick = key.tickFrom;
    state->entities.resize(limits::kMaxEntities);
    auto blob = loadChunk(key);
    if (!blob)
        return blob.error();
    Decoder d(**blob);
    const std::uint64_t n = d.varint();
    if (!d.ok() || n > limits::kMaxEntities)
        return corrupt("keyframe entity count");
    for (std::uint64_t i = 0; i < n; ++i) {
        EntityState e;
        e.index = static_cast<int>(d.varint());
        e.classId = static_cast<int>(d.varint());
        e.serial = static_cast<int>(d.varint());
        e.life = static_cast<std::uint32_t>(d.varint());
        e.inPvs = d.u8() != 0;
        if (!d.ok() || e.index < 0 || static_cast<std::size_t>(e.index) >= limits::kMaxEntities ||
            e.classId < 0 || static_cast<std::size_t>(e.classId) >= sch->size())
            return corrupt("keyframe entity");
        e.props.resize((*sch)[static_cast<std::size_t>(e.classId)].props.size());
        if (!decodeProps(d, e.props))
            return corrupt("keyframe props");
        const auto idx = static_cast<std::size_t>(e.index);
        state->entities[idx] = std::move(e);
    }
    auto tables = tablesAt(key.tickFrom);
    if (!tables)
        return tables.error();
    state->tables = **tables;
    if (keyframeCache_.size() >= kKeyframeCacheSize)
        keyframeCache_.erase(keyframeCache_.begin());
    keyframeCache_[key.tickFrom] = state;
    return std::shared_ptr<const WorldState>(state);
}

Result<void> StateReader::applyDeltas(WorldState& state, const Blob& deltas, std::size_t& pos, Tick& lastTick,
                                      Tick upTo) const {
    const auto& sch = *schema_;
    const std::span<const std::uint8_t> all(*deltas);
    while (pos < all.size()) {
        Decoder d(all.subspan(pos));
        const auto op = static_cast<DeltaOp>(d.u8());
        const Tick tick = lastTick + d.svarint();
        if (!d.ok())
            return corrupt("delta record");
        if (tick > upTo)
            break; // do not consume: the cursor resumes here
        switch (op) {
        case DeltaOp::Enter: {
            EntityState e;
            e.index = static_cast<int>(d.varint());
            e.classId = static_cast<int>(d.varint());
            e.serial = static_cast<int>(d.varint());
            e.life = static_cast<std::uint32_t>(d.varint());
            d.u8(); // new life flag (lives come from the Lives chunk)
            e.inPvs = true;
            if (!d.ok() || e.index < 0 || static_cast<std::size_t>(e.index) >= state.entities.size() ||
                e.classId < 0 || static_cast<std::size_t>(e.classId) >= sch.size())
                return corrupt("enter record");
            e.props.resize(sch[static_cast<std::size_t>(e.classId)].props.size());
            if (!decodeProps(d, e.props))
                return corrupt("enter props");
            const auto idx = static_cast<std::size_t>(e.index);
            state.entities[idx] = std::move(e);
            if (tick == upTo)
                noteChanged(state, tick, static_cast<int>(idx), &state.entities[idx]->props, {});
            break;
        }
        case DeltaOp::Update: {
            const auto idx = static_cast<std::size_t>(d.varint());
            const std::uint64_t n = d.varint();
            if (!d.ok() || idx >= state.entities.size() || !state.entities[idx])
                return corrupt("update record");
            auto& props = state.entities[idx]->props;
            if (n > props.size())
                return corrupt("update count");
            std::int64_t index = -1;
            thread_local std::vector<int> touched;
            touched.clear();
            for (std::uint64_t i = 0; i < n; ++i) {
                index += 1 + static_cast<std::int64_t>(d.varint());
                if (!d.ok() || index < 0 || static_cast<std::size_t>(index) >= props.size())
                    return corrupt("update prop index");
                if (!d.value(props[static_cast<std::size_t>(index)]))
                    return corrupt("update value");
                if (tick == upTo)
                    touched.push_back(static_cast<int>(index));
            }
            if (tick == upTo)
                noteChanged(state, tick, static_cast<int>(idx), nullptr, touched);
            break;
        }
        case DeltaOp::Leave: {
            const auto idx = static_cast<std::size_t>(d.varint());
            const bool deleted = d.u8() != 0;
            if (!d.ok() || idx >= state.entities.size())
                return corrupt("leave record");
            if (state.entities[idx]) {
                if (deleted)
                    state.entities[idx].reset();
                else
                    state.entities[idx]->inPvs = false;
            }
            break;
        }
        case DeltaOp::StringEntry: {
            const int id = static_cast<int>(d.varint());
            const int idx = static_cast<int>(d.varint());
            std::string s = d.string(kMaxEntryString);
            auto ud = d.bytes(limits::kMaxUserDataBytes);
            if (!d.ok())
                return corrupt("string record");
            state.tables[id].entries[idx] = {std::move(s), std::move(ud)};
            break;
        }
        case DeltaOp::TableCreate: {
            const int id = static_cast<int>(d.varint());
            std::string name = d.string(1024);
            if (!d.ok())
                return corrupt("table record");
            state.tables[id].name = std::move(name);
            break;
        }
        default:
            return corrupt("unknown delta op");
        }
        pos += d.position();
        lastTick = tick;
    }
    return {};
}

Result<void> StateReader::withStateAt(Tick tick, const std::function<void(const WorldState&)>& fn) {
    std::lock_guard lock(mutex_);
    if (auto sch = schemaLocked(); !sch)
        return sch.error();
    // Segment = the latest keyframe strictly before `tick` whose string tables are also known (the writer
    // announces KEYFRAME before STRINGTABLES), or the initial segment starting at -1. Strictly before, so the
    // records at `tick` itself are replayed and the changed-props set is known.
    Tick segment = -1;
    std::optional<ChunkRef> key;
    for (auto it = keyframes_.lower_bound(tick); it != keyframes_.begin();) {
        --it;
        if (keyTables_.count(it->first)) {
            segment = it->first;
            key = it->second;
            break;
        }
    }

    const bool canContinue = cursor_.state && cursor_.segment == segment && cursor_.upTo <= tick;
    if (!canContinue) {
        cursor_ = Cursor{};
        cursor_.segment = segment;
        cursor_.lastRecordTick = segment;
        if (key) {
            auto base = keyframeState(*key);
            if (!base)
                return base.error();
            cursor_.state = std::make_shared<WorldState>(**base);
        } else {
            cursor_.state = std::make_shared<WorldState>();
            cursor_.state->entities.resize(limits::kMaxEntities);
        }
    }
    // The segment's delta chunk appears only when the importer reaches the next keyframe.
    if (!cursor_.deltas) {
        if (auto it = deltas_.find(segment); it != deltas_.end()) {
            auto blob = loadChunk(it->second);
            if (!blob)
                return blob.error();
            cursor_.deltas = *blob;
        }
    }
    if (cursor_.deltas) {
        auto r = applyDeltas(*cursor_.state, cursor_.deltas, cursor_.pos, cursor_.lastRecordTick, tick);
        if (!r) {
            cursor_ = Cursor{};
            return r.error();
        }
    }
    cursor_.upTo = tick;
    cursor_.state->tick = tick;
    if (cursor_.state->changedTick != tick) {
        cursor_.state->changed.clear();
        cursor_.state->changedTick = tick;
    }
    fn(*cursor_.state);
    return {};
}

Result<std::vector<EventRecord>> StateReader::events(Tick from, Tick to, std::size_t maxCount) const {
    std::lock_guard lock(mutex_);
    std::vector<EventRecord> out;
    for (const auto& ref : events_) {
        if (ref.tickTo < from || ref.tickFrom > to)
            continue;
        auto blob = loadChunk(ref);
        if (!blob)
            return blob.error();
        Decoder d(**blob);
        Tick last = ref.tickFrom;
        while (!d.atEnd()) {
            EventRecord e;
            e.tick = last + d.svarint();
            last = e.tick;
            e.kind = static_cast<EventKind>(d.u8());
            e.entity = static_cast<std::int32_t>(d.svarint());
            e.name = d.string();
            e.summary = d.string();
            e.bitOffset = d.varint();
            e.bitLength = static_cast<std::uint32_t>(d.varint());
            if (!d.ok())
                return corrupt("event record");
            if (e.tick >= from && e.tick <= to) {
                out.push_back(std::move(e));
                if (out.size() >= maxCount)
                    return out;
            }
        }
    }
    std::sort(out.begin(), out.end(),
              [](const EventRecord& a, const EventRecord& b) { return a.tick < b.tick; });
    return out;
}

Result<std::vector<CameraSample>> StateReader::camera(Tick from, Tick to) const {
    std::lock_guard lock(mutex_);
    std::vector<CameraSample> out;
    for (const auto& ref : camera_) {
        if (ref.tickTo < from || ref.tickFrom > to)
            continue;
        auto blob = loadChunk(ref);
        if (!blob)
            return blob.error();
        Decoder d(**blob);
        Tick last = ref.tickFrom;
        while (!d.atEnd()) {
            CameraSample s;
            s.tick = last + d.svarint();
            last = s.tick;
            for (Vec3* v : {&s.origin, &s.angles, &s.localAngles}) {
                v->x = d.f32();
                v->y = d.f32();
                v->z = d.f32();
            }
            if (!d.ok())
                return corrupt("camera record");
            if (s.tick >= from && s.tick <= to)
                out.push_back(s);
        }
    }
    std::sort(out.begin(), out.end(),
              [](const CameraSample& a, const CameraSample& b) { return a.tick < b.tick; });
    return out;
}

} // namespace gmdr::demo::statedb
