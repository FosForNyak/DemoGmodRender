#include "demo/statedb/writer.h"

#include "core/compress.h"
#include "core/json.h"
#include "core/limits.h"
#include "core/text.h"

#include <cmath>
#include <cstring>

namespace gmdr::demo::statedb {

const char* chunkKindName(ChunkKind kind) {
    switch (kind) {
    case ChunkKind::Info:
        return "info";
    case ChunkKind::Schema:
        return "schema";
    case ChunkKind::Keyframe:
        return "keyframe";
    case ChunkKind::StringTables:
        return "stringtables";
    case ChunkKind::Deltas:
        return "deltas";
    case ChunkKind::Events:
        return "events";
    case ChunkKind::Camera:
        return "camera";
    case ChunkKind::Lives:
        return "lives";
    case ChunkKind::Manifest:
        return "manifest";
    case ChunkKind::Directory:
        return "directory";
    case ChunkKind::Index:
        return "index";
    }
    return "unknown";
}

StateWriter::StateWriter(File& out, const Blake3Digest& demoHash, Callbacks callbacks)
    : out_(out), demoHash_(demoHash), cb_(std::move(callbacks)) {
    entities_.resize(limits::kMaxEntities);
}

Result<void> StateWriter::begin() {
    FileHeader h{};
    std::memcpy(h.magic, kFileMagic, sizeof h.magic);
    h.formatVersion = kFormatVersion;
    h.flags = 0;
    h.demoHash = demoHash_.bytes;
    h.parserVersion = kParserVersion;
    return out_.writeAt(0, std::span(reinterpret_cast<const std::uint8_t*>(&h), sizeof h));
}

Result<void> StateWriter::writeChunk(ChunkKind kind, Tick from, Tick to,
                                     const std::vector<std::uint8_t>& raw) {
    // Deltas are most of the file: level 7 saves ~11% over 3 for ~45% more import time (measured).
    auto packed = zstdCompress(raw, kind == ChunkKind::Deltas ? 7 : 3);
    if (!packed)
        return packed.error();
    ChunkHeader ch{};
    ch.magic = kChunkMagic;
    ch.kind = static_cast<std::uint16_t>(kind);
    ch.tickFrom = from;
    ch.tickTo = to;
    ch.rawSize = static_cast<std::uint32_t>(raw.size());
    ch.storedSize = static_cast<std::uint32_t>(packed->size());
    ch.checksum = xxh3_64(*packed);
    GMDR_TRY(out_.writeAt(writeOffset_, std::span(reinterpret_cast<const std::uint8_t*>(&ch), sizeof ch)));
    GMDR_TRY(out_.writeAt(writeOffset_ + sizeof ch, *packed));
    ChunkRef ref{kind, writeOffset_, from, to, ch.rawSize, ch.storedSize};
    writeOffset_ += sizeof ch + packed->size();
    directory_.push_back(ref);
    if (cb_.onChunk)
        cb_.onChunk(ref);
    return {};
}

void StateWriter::writeOrRemember(ChunkKind kind, Tick from, Tick to, const std::vector<std::uint8_t>& raw) {
    if (writeError_)
        return;
    if (raw.size() > limits::kMaxChunkBytes) {
        writeError_ = makeError("statedb.chunk_too_large", "state chunk exceeds the size limit");
        return;
    }
    auto r = writeChunk(kind, from, to, raw);
    if (!r)
        writeError_ = r.error();
}

Result<void> StateWriter::writeIndex(std::span<const IndexEntry> entries, Tick lastTick) {
    Encoder e;
    e.varint(entries.size());
    Tick tick = 0;
    std::uint64_t offset = 0;
    for (const auto& x : entries) {
        e.svarint(x.tick - tick);
        e.svarint(static_cast<std::int64_t>(x.offset - offset));
        tick = x.tick;
        offset = x.offset;
    }
    writeOrRemember(ChunkKind::Index, 0, lastTick, e.data());
    if (writeError_)
        return *writeError_;
    return {};
}

void StateWriter::writeInfo(const ParseStats* stats) {
    Json j;
    j["demo"] = {
        {"magic", header_.magic},
        {"server", sanitizeUtf8(header_.serverName)},
        {"client", sanitizeUtf8(header_.clientName)},
        {"map", sanitizeUtf8(header_.mapName)},
        {"game", sanitizeUtf8(header_.gameDirectory)},
        {"playbackTime", header_.playbackTime},
        {"ticks", header_.ticks},
        {"frames", header_.frames},
    };
    if (haveServerInfo_) {
        j["server"] = {
            {"hostName", sanitizeUtf8(serverInfo_.hostName)},
            {"map", sanitizeUtf8(serverInfo_.mapName)},
            {"gamemode", sanitizeUtf8(serverInfo_.gamemode)},
            {"loadingUrl", sanitizeUtf8(serverInfo_.loadingUrl)},
            {"skyName", sanitizeUtf8(serverInfo_.skyName)},
            {"tickInterval", serverInfo_.tickInterval},
            {"maxClients", serverInfo_.maxClients},
            {"playerSlot", serverInfo_.playerSlot},
            {"dedicated", serverInfo_.dedicated},
            {"os", std::string(1, serverInfo_.os ? serverInfo_.os : '?')},
        };
    }
    j["lastTick"] = lastTick_;
    if (stats) {
        j["stats"] = {
            {"packets", stats->packets},
            {"entityEnters", stats->entityEnters},
            {"entityUpdates", stats->entityUpdates},
            {"entityLeaves", stats->entityLeaves},
            {"events", stats->events},
            {"decodeErrors", stats->decodeErrors},
        };
        j["decodeErrors"] = decodeErrors_;
    }
    const std::string s = j.dump();
    writeOrRemember(ChunkKind::Info, 0, lastTick_, std::vector<std::uint8_t>(s.begin(), s.end()));
}

void StateWriter::onHeader(const DemoHeader& h) {
    header_ = h;
}

void StateWriter::onServerInfo(const ServerInfo& s) {
    serverInfo_ = s;
    haveServerInfo_ = true;
    ticksPerKeyframe_ =
        std::max<Tick>(1, static_cast<Tick>(std::llround(kKeyframeIntervalSeconds / s.tickInterval)));
    writeInfo(nullptr);
}

void StateWriter::onDataTables(const DataTables& dt) {
    Encoder e;
    e.varint(dt.classes.size());
    for (const auto& c : dt.classes) {
        e.varint(static_cast<std::uint64_t>(c.id));
        e.string(c.name);
        e.string(c.tableName);
        auto flat = flattenClass(dt, c.tableName);
        if (!flat) {
            e.varint(0);
            continue;
        }
        e.varint(flat->size());
        for (const auto& fp : *flat) {
            const SendProp& p = *fp.prop;
            e.string(p.name);
            e.string(fp.table->name);
            e.u8(static_cast<std::uint8_t>(p.type));
            e.varint(p.flags);
            e.svarint(p.bits);
            e.f32(p.low);
            e.f32(p.high);
            e.varint(static_cast<std::uint64_t>(std::max(0, p.elements)));
            if (fp.element) {
                e.u8(1);
                e.u8(static_cast<std::uint8_t>(fp.element->type));
                e.varint(fp.element->flags);
                e.svarint(fp.element->bits);
                e.f32(fp.element->low);
                e.f32(fp.element->high);
            } else {
                e.u8(0);
            }
        }
    }
    haveTables_ = true;
    writeOrRemember(ChunkKind::Schema, 0, 0, e.data());
}

void StateWriter::deltaHeader(DeltaOp op, Tick tick) {
    deltas_.u8(static_cast<std::uint8_t>(op));
    deltas_.svarint(tick - deltaLastTick_);
    deltaLastTick_ = tick;
}

void StateWriter::encodeProps(Encoder& e, std::span<const PropValue> state) {
    std::size_t count = 0;
    for (const auto& v : state)
        count += v.isSet() ? 1 : 0;
    e.varint(count);
    std::int64_t last = -1;
    for (std::size_t i = 0; i < state.size(); ++i) {
        if (!state[i].isSet())
            continue;
        e.varint(static_cast<std::uint64_t>(static_cast<std::int64_t>(i) - last - 1));
        last = static_cast<std::int64_t>(i);
        e.value(state[i]);
    }
}

void StateWriter::onStringTableChanged(Tick tick, int tableId, const StringTable& table,
                                       std::span<const int> changed, bool created) {
    cutBefore(tick);
    auto& mirror = tables_[tableId];
    dirtyTables_.insert(tableId);
    // Instance baselines are already applied by the parser; their binary userdata would only bloat every
    // snapshot. Their strings (class ids) are kept.
    const bool keepUserData = table.name() != "instancebaseline";
    if (created) {
        mirror.name = table.name();
        deltaHeader(DeltaOp::TableCreate, tick);
        deltas_.varint(static_cast<std::uint64_t>(tableId));
        deltas_.string(table.name());
    }
    for (int idx : changed) {
        const auto* entry = table.entry(static_cast<std::size_t>(idx));
        if (!entry)
            continue;
        auto& m = mirror.entries[idx];
        m.first = entry->string;
        if (keepUserData)
            m.second = entry->userData;
        deltaHeader(DeltaOp::StringEntry, tick);
        deltas_.varint(static_cast<std::uint64_t>(tableId));
        deltas_.varint(static_cast<std::uint64_t>(idx));
        deltas_.string(entry->string);
        deltas_.bytes(m.second);
    }
}

void StateWriter::onPacket(const CommandRecord& rec) {
    cutBefore(rec.tick);
    lastTick_ = std::max<Tick>(lastTick_, rec.tick);
    camera_.svarint(rec.tick - cameraLastTick_);
    cameraLastTick_ = rec.tick;
    const auto& i = rec.info;
    for (const Vec3* v : {&i.viewOrigin, &i.viewAngles, &i.localViewAngles}) {
        camera_.f32(v->x);
        camera_.f32(v->y);
        camera_.f32(v->z);
    }
}

void StateWriter::onEntityEnter(Tick tick, const EntityRef& ref, bool newLife,
                                std::span<const PropValue> state) {
    cutBefore(tick);
    deltaHeader(DeltaOp::Enter, tick);
    deltas_.varint(static_cast<std::uint64_t>(ref.index));
    deltas_.varint(static_cast<std::uint64_t>(ref.classId));
    deltas_.varint(static_cast<std::uint64_t>(ref.serial));
    deltas_.varint(ref.life);
    deltas_.u8(newLife ? 1 : 0);
    encodeProps(deltas_, state);

    auto& m = entities_[static_cast<std::size_t>(ref.index)];
    m.active = true;
    m.inPvs = true;
    m.classId = ref.classId;
    m.serial = ref.serial;
    m.life = ref.life;
    m.props.assign(state.begin(), state.end());

    auto& life = lives_[ref.life];
    if (newLife || life.life == 0) {
        life.life = ref.life;
        life.index = ref.index;
        life.classId = ref.classId;
        life.serial = ref.serial;
        life.firstTick = tick;
    }
    life.pvs.emplace_back(tick, -1);
}

void StateWriter::onEntityUpdate(Tick tick, const EntityRef& ref, std::span<const int> changed,
                                 std::span<const PropValue> state) {
    cutBefore(tick);
    deltaHeader(DeltaOp::Update, tick);
    deltas_.varint(static_cast<std::uint64_t>(ref.index));
    deltas_.varint(changed.size());
    int last = -1;
    auto& m = entities_[static_cast<std::size_t>(ref.index)];
    for (int idx : changed) {
        // Indices arrive in increasing order from the property stream.
        deltas_.varint(static_cast<std::uint64_t>(idx - last - 1));
        last = idx;
        deltas_.value(state[static_cast<std::size_t>(idx)]);
        if (static_cast<std::size_t>(idx) < m.props.size())
            m.props[static_cast<std::size_t>(idx)] = state[static_cast<std::size_t>(idx)];
    }
}

void StateWriter::onEntityLeave(Tick tick, const EntityRef& ref, bool deleted) {
    cutBefore(tick);
    deltaHeader(DeltaOp::Leave, tick);
    deltas_.varint(static_cast<std::uint64_t>(ref.index));
    deltas_.u8(deleted ? 1 : 0);
    auto& m = entities_[static_cast<std::size_t>(ref.index)];
    m.inPvs = false;
    if (deleted)
        m.active = false;
    auto it = lives_.find(ref.life);
    if (it != lives_.end()) {
        if (!it->second.pvs.empty() && it->second.pvs.back().second < 0)
            it->second.pvs.back().second = tick;
        if (deleted)
            it->second.lastTick = tick;
    }
}

void StateWriter::onEvent(const DemoEvent& e) {
    cutBefore(e.tick);
    events_.svarint(e.tick - eventsLastTick_);
    eventsLastTick_ = e.tick;
    events_.u8(static_cast<std::uint8_t>(e.kind));
    events_.svarint(e.entity);
    events_.string(sanitizeUtf8(e.name));
    events_.string(sanitizeUtf8(e.summary));
    events_.varint(e.bitOffset);
    events_.varint(e.bitLength);
}

void StateWriter::onDecodeError(Tick tick, std::uint64_t offset, const Error& e) {
    if (decodeErrors_.size() < 100)
        decodeErrors_.push_back("tick " + std::to_string(tick) + " offset " + std::to_string(offset) + ": " +
                                e.code + " " + e.message);
}

void StateWriter::writeKeyframe(Tick tick) {
    Encoder e;
    std::size_t count = 0;
    for (const auto& m : entities_)
        count += m.active ? 1 : 0;
    e.varint(count);
    for (std::size_t i = 0; i < entities_.size(); ++i) {
        const auto& m = entities_[i];
        if (!m.active)
            continue;
        e.varint(i);
        e.varint(static_cast<std::uint64_t>(m.classId));
        e.varint(static_cast<std::uint64_t>(m.serial));
        e.varint(m.life);
        e.u8(m.inPvs ? 1 : 0);
        encodeProps(e, m.props);
    }
    writeOrRemember(ChunkKind::Keyframe, tick, tick, e.data());

    // Only the tables changed since the previous snapshot, each in full; the reader takes the latest
    // version of every table at or before a keyframe.
    Encoder t;
    t.varint(dirtyTables_.size());
    for (int id : dirtyTables_) {
        const auto& table = tables_[id];
        t.varint(static_cast<std::uint64_t>(id));
        t.string(table.name);
        t.varint(table.entries.size());
        for (const auto& [idx, entry] : table.entries) {
            t.varint(static_cast<std::uint64_t>(idx));
            t.string(entry.first);
            t.bytes(entry.second);
        }
    }
    writeOrRemember(ChunkKind::StringTables, tick, tick, t.data());
    dirtyTables_.clear();
}

void StateWriter::flushSegment(Tick tick) {
    writeOrRemember(ChunkKind::Deltas, segmentStart_, tick, deltas_.data());
    if (events_.size())
        writeOrRemember(ChunkKind::Events, segmentStart_, tick, events_.data());
    if (camera_.size())
        writeOrRemember(ChunkKind::Camera, segmentStart_, tick, camera_.data());
    deltas_.clear();
    events_.clear();
    camera_.clear();
    // Every chunk restarts its tick deltas from its own start tick.
    deltaLastTick_ = eventsLastTick_ = cameraLastTick_ = tick;
}

// A keyframe falls due at the end of a packet, but more packets may share its tick. The cut happens at the
// first record of a later tick, so a segment (k, next] always holds every record with tick <= next.
void StateWriter::cutBefore(Tick tick) {
    if (!pendingCut_ || tick <= *pendingCut_)
        return;
    const Tick k = *pendingCut_;
    pendingCut_.reset();
    flushSegment(k);
    writeKeyframe(k);
    segmentStart_ = k;
    keyframeWritten_ = true;
}

void StateWriter::onTickEnd(Tick tick) {
    lastTick_ = std::max(lastTick_, tick);
    if (haveTables_ && !pendingCut_ && (!keyframeWritten_ || tick - segmentStart_ >= ticksPerKeyframe_))
        pendingCut_ = tick;
    if (cb_.onProgress)
        cb_.onProgress(tick);
}

std::string StateWriter::manifestJson() const {
    Json j;
    j["map"] = sanitizeUtf8(haveServerInfo_ ? serverInfo_.mapName : header_.mapName);
    auto listTable = [&](const char* name) {
        Json arr = Json::array();
        for (const auto& [id, t] : tables_) {
            if (t.name != name)
                continue;
            for (const auto& [idx, e] : t.entries)
                if (!e.first.empty())
                    arr.push_back(sanitizeUtf8(e.first));
        }
        return arr;
    };
    j["models"] = listTable("modelprecache");
    j["sounds"] = listTable("soundprecache");
    j["decals"] = listTable("decalprecache");
    j["generic"] = listTable("genericprecache");
    j["downloadables"] = listTable("downloadables");
    j["particles"] = listTable("ParticleEffectNames");
    j["clientLua"] = listTable("client_lua_files");
    return j.dump();
}

Result<void> StateWriter::finish(const ParseStats& stats) {
    if (pendingCut_)
        cutBefore(*pendingCut_ + 1);
    if (!keyframeWritten_ && haveTables_) {
        flushSegment(lastTick_);
        writeKeyframe(lastTick_);
        segmentStart_ = lastTick_;
        keyframeWritten_ = true;
    }
    flushSegment(lastTick_);

    Encoder l;
    l.varint(lives_.size());
    for (const auto& [id, life] : lives_) {
        l.varint(life.life);
        l.varint(static_cast<std::uint64_t>(life.index));
        l.varint(static_cast<std::uint64_t>(life.classId));
        l.varint(static_cast<std::uint64_t>(life.serial));
        l.svarint(life.firstTick);
        l.svarint(life.lastTick);
        l.varint(life.pvs.size());
        for (const auto& [a, b] : life.pvs) {
            l.svarint(a);
            l.svarint(b);
        }
    }
    writeOrRemember(ChunkKind::Lives, 0, lastTick_, l.data());
    const std::string manifest = manifestJson();
    writeOrRemember(ChunkKind::Manifest, 0, lastTick_,
                    std::vector<std::uint8_t>(manifest.begin(), manifest.end()));
    writeInfo(&stats);

    Encoder d;
    d.varint(directory_.size());
    for (const auto& ref : directory_) {
        d.u8(static_cast<std::uint8_t>(ref.kind));
        d.u64(ref.offset);
        d.svarint(ref.tickFrom);
        d.svarint(ref.tickTo);
        d.varint(ref.rawSize);
        d.varint(ref.storedSize);
    }
    const std::uint64_t dirOffset = writeOffset_;
    writeOrRemember(ChunkKind::Directory, 0, lastTick_, d.data());
    if (writeError_)
        return *writeError_;

    FileHeader h{};
    std::memcpy(h.magic, kFileMagic, sizeof h.magic);
    h.formatVersion = kFormatVersion;
    h.flags = kComplete;
    h.demoHash = demoHash_.bytes;
    h.parserVersion = kParserVersion;
    h.directoryOffset = dirOffset;
    return out_.writeAt(0, std::span(reinterpret_cast<const std::uint8_t*>(&h), sizeof h));
}

} // namespace gmdr::demo::statedb
