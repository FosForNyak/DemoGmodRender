#include "demo/parser.h"

#include "core/limits.h"
#include "demo/lzss.h"
#include "demo/props.h"

#include <string>

namespace gmdr::demo {

const char* eventKindName(EventKind kind) {
    switch (kind) {
    case EventKind::GameEvent: return "game_event";
    case EventKind::Sound: return "sound";
    case EventKind::TempEntities: return "temp_entities";
    case EventKind::UserMessage: return "user_message";
    case EventKind::EntityMessage: return "entity_message";
    case EventKind::GModNet: return "net_message";
    case EventKind::Decal: return "decal";
    case EventKind::SetView: return "set_view";
    case EventKind::FixAngle: return "fix_angle";
    case EventKind::Voice: return "voice";
    case EventKind::ConsoleCmd: return "console";
    case EventKind::Print: return "print";
    case EventKind::StringCmd: return "string_cmd";
    case EventKind::DecodeError: return "decode_error";
    }
    return "unknown";
}

namespace {

int log2Floor(std::uint32_t v) {
    int r = 0;
    while (v > 1) {
        v >>= 1;
        ++r;
    }
    return r;
}

Error overflowError(const char* what) {
    return makeError("demo.message_truncated", std::string(what) + " ends early");
}

} // namespace

DemoParser::DemoParser(std::span<const std::uint8_t> file, DemoSink& sink, ParseOptions options)
    : file_(file), sink_(sink), options_(std::move(options)),
      variant_(options_.variant ? options_.variant : &knownProtocolVariants()[0]) {
    entities_.resize(limits::kMaxEntities);
    entityBaselines_[0].resize(limits::kMaxEntities);
    entityBaselines_[1].resize(limits::kMaxEntities);
}

DemoParser::~DemoParser() = default;

const std::vector<FlatProp>* DemoParser::flatProps(int classId) {
    if (!dataTables_ || classId < 0 || static_cast<std::size_t>(classId) >= dataTables_->classes.size())
        return nullptr;
    auto& slot = flat_[static_cast<std::size_t>(classId)];
    if (!slot) {
        auto f = flattenClass(*dataTables_, dataTables_->classes[static_cast<std::size_t>(classId)].tableName);
        if (!f)
            return nullptr;
        slot = std::move(f).value();
    }
    return &*slot;
}

Result<void> DemoParser::run() {
    auto header = parseHeader(file_);
    if (!header)
        return header.error();
    sink_.onHeader(*header);

    CommandReader reader(file_);
    TimelineClock clock;
    CommandRecord rec;
    while (true) {
        if (options_.cancelled && options_.cancelled())
            return makeError("import.cancelled", "import was cancelled");
        auto more = reader.next(rec);
        if (!more) {
            // A truncated tail is common for demos of crashed games: keep what was read.
            ++stats_.decodeErrors;
            sink_.onDecodeError(rec.tick, reader.position(), more.error());
            break;
        }
        if (!*more)
            break;
        rec.tick = clock.next(rec);
        switch (rec.cmd) {
        case DemoCommand::Signon:
        case DemoCommand::Packet: {
            ++stats_.packets;
            sink_.onPacket(rec);
            auto r = parsePacket(rec);
            if (!r) {
                ++stats_.decodeErrors;
                sink_.onDecodeError(rec.tick, rec.offset, r.error());
                emitEvent(rec.tick, EventKind::DecodeError, r.error().code, r.error().message, rec.payloadOffset * 8,
                          rec.payloadSize * 8);
            }
            sink_.onTickEnd(rec.tick);
            break;
        }
        case DemoCommand::DataTables: {
            auto r = parseDataTables(rec);
            if (!r)
                return r.error(); // without tables no entity can be decoded
            break;
        }
        case DemoCommand::ConsoleCmd: {
            BitReader r(file_.subspan(static_cast<std::size_t>(rec.payloadOffset), rec.payloadSize));
            std::string cmd = r.string(limits::kMaxStringBytes);
            emitEvent(rec.tick, EventKind::ConsoleCmd, std::move(cmd), {}, rec.payloadOffset * 8, rec.payloadSize * 8);
            break;
        }
        case DemoCommand::StringTables: // snapshot is truncated at 512 KB in GMod; tables come from messages
        case DemoCommand::UserCmd:
        case DemoCommand::SyncTick:
        case DemoCommand::Stop:
            break;
        }
    }
    return {};
}

Result<void> DemoParser::parseDataTables(const CommandRecord& rec) {
    auto dt = demo::parseDataTables(file_.subspan(static_cast<std::size_t>(rec.payloadOffset), rec.payloadSize),
                                    *variant_);
    if (!dt)
        return dt.error();
    dataTables_ = std::move(dt).value();
    flat_.assign(dataTables_->classes.size(), std::nullopt);
    instanceBaselines_.assign(dataTables_->classes.size(), std::nullopt);
    sink_.onDataTables(*dataTables_);
    return {};
}

Result<void> DemoParser::parsePacket(const CommandRecord& rec) {
    BitReader r(file_.subspan(static_cast<std::size_t>(rec.payloadOffset), rec.payloadSize));
    packetBitBase_ = rec.payloadOffset * 8;
    while (r.remaining() >= static_cast<std::size_t>(variant_->netMsgTypeBits)) {
        const int type = static_cast<int>(r.ubit(variant_->netMsgTypeBits));
        GMDR_TRY(parseMessage(type, r, rec));
        if (r.overflowed())
            return makeError("demo.message_truncated", std::string(netMsgName(type)) + " ends early");
    }
    return {};
}

void DemoParser::emitEvent(Tick tick, EventKind kind, std::string name, std::string summary,
                           std::uint64_t bitOffset, std::uint32_t bitLength, std::int32_t entity) {
    DemoEvent e;
    e.tick = tick;
    e.kind = kind;
    e.entity = entity;
    e.name = std::move(name);
    e.summary = std::move(summary);
    e.bitOffset = bitOffset;
    e.bitLength = bitLength;
    ++stats_.events;
    sink_.onEvent(e);
}

Result<void> DemoParser::parseServerInfo(BitReader& r) {
    ServerInfo& s = serverInfo_;
    s.protocol = static_cast<int>(r.ubit(16));
    s.serverCount = r.sbit(32);
    s.hltv = r.bit();
    s.dedicated = r.bit();
    s.clientCrc = r.ubit(32);
    s.maxClasses = static_cast<int>(r.ubit(16));
    r.bytes(s.mapMd5.data(), s.mapMd5.size());
    s.playerSlot = static_cast<int>(r.ubit(8));
    s.maxClients = static_cast<int>(r.ubit(8));
    s.tickInterval = r.float32();
    s.os = static_cast<char>(r.ubit(8));
    s.gameDir = r.string(limits::kMaxStringBytes);
    s.mapName = r.string(limits::kMaxStringBytes);
    s.skyName = r.string(limits::kMaxStringBytes);
    s.hostName = r.string(limits::kMaxStringBytes);
    if (variant_->serverInfoExtraStrings >= 1)
        s.loadingUrl = r.string(limits::kMaxStringBytes);
    if (variant_->serverInfoExtraStrings >= 2)
        s.gamemode = r.string(limits::kMaxStringBytes);
    r.skip(static_cast<std::size_t>(variant_->serverInfoExtraBits));
    if (r.overflowed())
        return overflowError("svc_ServerInfo");
    if (!(s.tickInterval > 0.0005f && s.tickInterval < 1.0f))
        return makeError("demo.serverinfo_tick", "server tick interval is out of range");
    sink_.onServerInfo(s);
    return {};
}

void DemoParser::onTableChanged(int tableId, std::span<const int> changed) {
    if (tableId != instanceBaselineTable_ || !dataTables_)
        return;
    const auto& table = *tables_[static_cast<std::size_t>(tableId)];
    for (int idx : changed) {
        const auto* e = table.entry(static_cast<std::size_t>(idx));
        if (!e)
            continue;
        const auto classId = std::strtol(e->string.c_str(), nullptr, 10);
        if (classId >= 0 && static_cast<std::size_t>(classId) < instanceBaselines_.size())
            instanceBaselines_[static_cast<std::size_t>(classId)].reset();
    }
}

Result<void> DemoParser::parseCreateStringTable(BitReader& r, Tick tick) {
    std::string name = r.string(limits::kMaxStringBytes);
    const int maxBits = static_cast<int>(r.ubit(variant_->createTableMaxEntriesBits));
    if (maxBits > 16)
        return makeError("demo.stringtable_size", "string table is too large", name);
    const int numEntries = static_cast<int>(r.ubit(maxBits + 1));
    const std::size_t length = variant_->createTableVarintLength ? r.varint32() : r.ubit(20);
    const bool fixed = r.bit();
    int udSize = 0, udBits = 0;
    if (fixed) {
        udSize = static_cast<int>(r.ubit(12));
        udBits = static_cast<int>(r.ubit(4));
    }
    const bool compressed = r.bit();
    BitReader data = r.take(length);
    if (r.overflowed())
        return overflowError("svc_CreateStringTable");
    if (tables_.size() >= limits::kMaxStringTables)
        return makeError("demo.stringtable_limit", "too many string tables");

    auto table = std::make_unique<StringTable>(name, maxBits, fixed, udSize, udBits);
    std::vector<int> changed;
    if (compressed) {
        const std::uint32_t decompressedSize = data.ubit(32);
        const std::uint32_t compressedSize = data.ubit(32);
        if (compressedSize * 8ull > data.remaining() || decompressedSize > limits::kMaxDecompressedBytes)
            return makeError("demo.stringtable_compressed", "compressed string table header is invalid", name);
        std::vector<std::uint8_t> packed(compressedSize);
        data.bytes(packed.data(), packed.size());
        auto raw = lzssDecompress(packed);
        if (!raw)
            return raw.error();
        BitReader rr(*raw);
        GMDR_TRY(table->parseEntries(rr, numEntries, variant_->userDataLengthBits, changed));
    } else {
        GMDR_TRY(table->parseEntries(data, numEntries, variant_->userDataLengthBits, changed));
    }
    const int id = static_cast<int>(tables_.size());
    if (name == "instancebaseline")
        instanceBaselineTable_ = id;
    else if (name == "networkstring")
        networkStringTable_ = id;
    tables_.push_back(std::move(table));
    onTableChanged(id, changed);
    sink_.onStringTableChanged(tick, id, *tables_.back(), changed, true);
    return {};
}

Result<void> DemoParser::parseUpdateStringTable(BitReader& r, Tick tick) {
    const int id = static_cast<int>(r.ubit(5));
    const int changedCount = r.bit() ? static_cast<int>(r.ubit(16)) : 1;
    const std::size_t length = r.ubit(variant_->updateTableLengthBits);
    BitReader data = r.take(length);
    if (r.overflowed())
        return overflowError("svc_UpdateStringTable");
    if (id < 0 || static_cast<std::size_t>(id) >= tables_.size())
        return makeError("demo.stringtable_unknown", "update for an unknown string table");
    std::vector<int> changed;
    GMDR_TRY(tables_[static_cast<std::size_t>(id)]->parseEntries(data, changedCount, variant_->userDataLengthBits,
                                                                 changed));
    onTableChanged(id, changed);
    sink_.onStringTableChanged(tick, id, *tables_[static_cast<std::size_t>(id)], changed, false);
    return {};
}

Result<const std::vector<PropValue>*> DemoParser::instanceBaseline(int classId) {
    auto& cached = instanceBaselines_[static_cast<std::size_t>(classId)];
    if (cached)
        return &*cached;
    const auto* flat = flatProps(classId);
    if (!flat)
        return makeError("demo.class_flatten", "cannot flatten the class's SendTable");
    std::vector<PropValue> state(flat->size());
    if (instanceBaselineTable_ >= 0) {
        const auto& table = *tables_[static_cast<std::size_t>(instanceBaselineTable_)];
        const int idx = table.find(std::to_string(classId));
        if (idx >= 0) {
            const auto* e = table.entry(static_cast<std::size_t>(idx));
            if (e && !e->userData.empty()) {
                BitReader r(e->userData);
                std::vector<int> changed;
                GMDR_TRY(readPropList(r, *flat, state, changed));
            }
        }
    }
    cached = std::move(state);
    return &*cached;
}

Result<void> DemoParser::parsePacketEntities(BitReader& r, Tick tick) {
    const int edictBits = variant_->edictBits;
    r.ubit(edictBits); // max entries
    const bool isDelta = r.bit();
    if (isDelta)
        r.sbit(32); // delta-from tick
    const int baselineSet = r.bit() ? 1 : 0;
    const int updated = static_cast<int>(r.ubit(edictBits));
    const std::size_t length = r.ubit(variant_->deltaSizeBits);
    const bool updateBaseline = r.bit();
    BitReader eb = r.take(length);
    if (r.overflowed())
        return overflowError("svc_PacketEntities");
    if (!dataTables_)
        return makeError("demo.entities_before_tables", "entities arrived before dem_datatables");

    const int classBits = dataTables_->classBits();
    int index = -1;
    for (int n = 0; n < updated; ++n) {
        index += 1 + static_cast<int>(eb.ubitVar());
        if (eb.overflowed())
            return overflowError("entity header");
        if (index < 0 || static_cast<std::size_t>(index) >= entities_.size())
            return makeError("demo.entity_index", "entity index out of range", std::to_string(index));
        EntitySlot& slot = entities_[static_cast<std::size_t>(index)];

        if (!eb.bit()) {
            if (eb.bit()) { // enter PVS
                const int classId = static_cast<int>(eb.ubit(classBits));
                const int serial = static_cast<int>(eb.ubit(variant_->serialBits));
                if (static_cast<std::size_t>(classId) >= dataTables_->classes.size())
                    return makeError("demo.entity_class", "entity class id out of range", std::to_string(classId));
                const auto* flat = flatProps(classId);
                if (!flat)
                    return makeError("demo.class_flatten", "cannot flatten the class's SendTable");
                const Baseline& eb0 = entityBaselines_[static_cast<std::size_t>(baselineSet)][static_cast<std::size_t>(index)];
                if (eb0.classId == classId) {
                    slot.props = eb0.props;
                } else {
                    auto base = instanceBaseline(classId);
                    if (!base)
                        return base.error();
                    slot.props = **base;
                }
                changedScratch_.clear();
                GMDR_TRY(readPropList(eb, *flat, slot.props, changedScratch_));

                const bool newLife = slot.lifeDeleted || slot.classId != classId || slot.serial != serial;
                if (newLife)
                    slot.life = nextLife_++;
                slot.active = true;
                slot.inPvs = true;
                slot.lifeDeleted = false;
                slot.classId = classId;
                slot.serial = serial;
                ++stats_.entityEnters;
                sink_.onEntityEnter(tick, EntityRef{index, classId, serial, slot.life}, newLife, slot.props);
                if (updateBaseline) {
                    auto& nb = entityBaselines_[static_cast<std::size_t>(baselineSet ^ 1)][static_cast<std::size_t>(index)];
                    nb.classId = classId;
                    nb.props = slot.props;
                }
            } else { // delta update (also for dormant entities that left the PVS without deletion)
                if (!slot.active)
                    return makeError("demo.entity_unknown", "update for an unknown entity", std::to_string(index));
                const auto* flat = flatProps(slot.classId);
                changedScratch_.clear();
                GMDR_TRY(readPropList(eb, *flat, slot.props, changedScratch_));
                ++stats_.entityUpdates;
                sink_.onEntityUpdate(tick, EntityRef{index, slot.classId, slot.serial, slot.life}, changedScratch_,
                                     slot.props);
            }
        } else {
            const bool deleted = eb.bit();
            if (slot.active && (slot.inPvs || deleted)) {
                ++stats_.entityLeaves;
                sink_.onEntityLeave(tick, EntityRef{index, slot.classId, slot.serial, slot.life}, deleted);
            }
            slot.inPvs = false;
            if (deleted) {
                slot.active = false;
                slot.lifeDeleted = true;
            }
        }
    }
    if (isDelta) {
        while (eb.bit()) {
            const auto idx = static_cast<std::size_t>(eb.ubit(edictBits));
            if (idx >= entities_.size())
                return makeError("demo.entity_index", "deleted entity index out of range");
            EntitySlot& slot = entities_[idx];
            if (slot.active) {
                ++stats_.entityLeaves;
                sink_.onEntityLeave(tick, EntityRef{static_cast<int>(idx), slot.classId, slot.serial, slot.life}, true);
            }
            slot.active = false;
            slot.inPvs = false;
            slot.lifeDeleted = true;
            if (eb.overflowed())
                break;
        }
    }
    if (eb.overflowed())
        return overflowError("svc_PacketEntities payload");
    return {};
}

Result<void> DemoParser::parseMessage(int type, BitReader& r, const CommandRecord& rec) {
    const Tick tick = rec.tick;
    const auto bitAt = [&](std::size_t pos) { return packetBitBase_ + pos; };
    switch (static_cast<NetMsg>(type)) {
    case NetMsg::Nop:
        return {};
    case NetMsg::Disconnect:
        r.string(limits::kMaxStringBytes);
        return {};
    case NetMsg::File:
        r.ubit(32);
        r.string(limits::kMaxStringBytes);
        r.bit();
        return {};
    case NetMsg::Tick:
        r.ubit(32);
        r.ubit(16);
        r.ubit(16);
        return {};
    case NetMsg::StringCmd: {
        const auto start = r.position();
        std::string s = r.string(limits::kMaxStringBytes);
        emitEvent(tick, EventKind::StringCmd, std::move(s), {}, bitAt(start), static_cast<std::uint32_t>(r.position() - start));
        return {};
    }
    case NetMsg::SetConVar: {
        const std::uint32_t n = r.ubit(8);
        for (std::uint32_t i = 0; i < n && !r.overflowed(); ++i) {
            r.string(limits::kMaxStringBytes);
            r.string(limits::kMaxStringBytes);
        }
        return {};
    }
    case NetMsg::SignonState:
        r.ubit(8);
        r.sbit(32);
        return {};
    case NetMsg::Print: {
        const auto start = r.position();
        std::string s = r.string(limits::kMaxStringBytes);
        emitEvent(tick, EventKind::Print, "print", std::move(s), bitAt(start), static_cast<std::uint32_t>(r.position() - start));
        return {};
    }
    case NetMsg::ServerInfo:
        return parseServerInfo(r);
    case NetMsg::SendTable: {
        r.bit();
        r.skip(r.ubit(16));
        return {};
    }
    case NetMsg::ClassInfo: {
        const std::uint32_t n = r.ubit(16);
        const bool createOnClient = r.bit();
        if (!createOnClient) {
            if (n > limits::kMaxServerClasses)
                return makeError("demo.classinfo_limit", "too many classes in svc_ClassInfo");
            const int bits = log2Floor(n) + 1;
            for (std::uint32_t i = 0; i < n && !r.overflowed(); ++i) {
                r.ubit(bits);
                r.string(limits::kMaxStringBytes);
                r.string(limits::kMaxStringBytes);
            }
        }
        return {};
    }
    case NetMsg::SetPause:
        r.bit();
        return {};
    case NetMsg::CreateStringTable:
        return parseCreateStringTable(r, tick);
    case NetMsg::UpdateStringTable:
        return parseUpdateStringTable(r, tick);
    case NetMsg::VoiceInit: {
        r.string(limits::kMaxStringBytes);
        if (r.ubit(8) == 255)
            r.ubit(16);
        return {};
    }
    case NetMsg::VoiceData: {
        const int client = static_cast<int>(r.ubit(8));
        r.ubit(8); // proximity
        const std::uint32_t len = r.ubit(16);
        const auto start = r.position();
        r.skip(len);
        emitEvent(tick, EventKind::Voice, "voice", {}, bitAt(start), len, client + 1);
        return {};
    }
    case NetMsg::Sounds: {
        const bool reliable = r.bit();
        std::uint32_t count = 1, len = 0;
        if (reliable) {
            len = r.ubit(8);
        } else {
            count = r.ubit(8);
            len = r.ubit(16);
        }
        const auto start = r.position();
        r.skip(len);
        emitEvent(tick, EventKind::Sound, "sounds", "count=" + std::to_string(count), bitAt(start), len);
        return {};
    }
    case NetMsg::SetView: {
        const int ent = static_cast<int>(r.ubit(variant_->edictBits));
        emitEvent(tick, EventKind::SetView, "set_view", {}, bitAt(r.position()), 0, ent);
        return {};
    }
    case NetMsg::FixAngle: {
        const bool relative = r.bit();
        const float a = r.ubit(16) * (360.0f / 65536.0f);
        const float b = r.ubit(16) * (360.0f / 65536.0f);
        const float c = r.ubit(16) * (360.0f / 65536.0f);
        emitEvent(tick, EventKind::FixAngle, relative ? "fix_angle_relative" : "fix_angle",
                  std::to_string(a) + " " + std::to_string(b) + " " + std::to_string(c), bitAt(r.position()), 0);
        return {};
    }
    case NetMsg::CrosshairAngle:
        r.ubit(16);
        r.ubit(16);
        r.ubit(16);
        return {};
    case NetMsg::BSPDecal: {
        const auto start = r.position();
        Vec3 pos;
        const bool hx = r.bit(), hy = r.bit(), hz = r.bit();
        if (hx)
            pos.x = readBitCoord(r);
        if (hy)
            pos.y = readBitCoord(r);
        if (hz)
            pos.z = readBitCoord(r);
        const std::uint32_t decal = r.ubit(variant_->maxDecalIndexBits);
        int ent = -1;
        if (r.bit()) {
            ent = static_cast<int>(r.ubit(variant_->edictBits));
            r.ubit(variant_->modelIndexBits);
        }
        r.bit(); // low priority
        emitEvent(tick, EventKind::Decal, "decal",
                  "index=" + std::to_string(decal) + " pos=" + std::to_string(pos.x) + "," + std::to_string(pos.y) +
                      "," + std::to_string(pos.z),
                  bitAt(start), static_cast<std::uint32_t>(r.position() - start), ent);
        return {};
    }
    case NetMsg::UserMessage: {
        const std::uint32_t id = r.ubit(8);
        const std::uint32_t len = r.ubit(variant_->userMessageLengthBits);
        const auto start = r.position();
        r.skip(len);
        emitEvent(tick, EventKind::UserMessage, "user_message_" + std::to_string(id), {}, bitAt(start), len);
        return {};
    }
    case NetMsg::EntityMessage: {
        const int ent = static_cast<int>(r.ubit(variant_->edictBits));
        const std::uint32_t cls = r.ubit(9);
        const std::uint32_t len = r.ubit(variant_->entityMessageLengthBits);
        const auto start = r.position();
        r.skip(len);
        emitEvent(tick, EventKind::EntityMessage, "entity_message", "class=" + std::to_string(cls), bitAt(start), len, ent);
        return {};
    }
    case NetMsg::GameEvent: {
        const std::uint32_t len = r.ubit(variant_->gameEventLengthBits);
        const auto start = r.position();
        BitReader ev = r.take(len);
        std::string name, summary;
        if (auto dr = gameEvents_.decode(ev, name, summary); !dr) {
            name = "game_event";
            summary = dr.error().code;
        }
        emitEvent(tick, EventKind::GameEvent, std::move(name), std::move(summary), bitAt(start), len);
        return {};
    }
    case NetMsg::PacketEntities:
        return parsePacketEntities(r, tick);
    case NetMsg::TempEntities: {
        const std::uint32_t count = r.ubit(8);
        const std::size_t len = variant_->tempEntitiesVarintLength ? r.varint32() : r.ubit(17);
        const auto start = r.position();
        r.skip(len);
        emitEvent(tick, EventKind::TempEntities, "temp_entities", "count=" + std::to_string(count), bitAt(start),
                  static_cast<std::uint32_t>(len));
        return {};
    }
    case NetMsg::Prefetch:
        r.ubit(variant_->soundIndexBits);
        return {};
    case NetMsg::Menu: {
        r.ubit(16);
        const std::uint32_t len = r.ubit(16);
        r.skip(static_cast<std::size_t>(len) * 8);
        return {};
    }
    case NetMsg::GameEventList: {
        const int count = static_cast<int>(r.ubit(9));
        const std::size_t len = r.ubit(20);
        BitReader list = r.take(len);
        return gameEvents_.parseList(list, count);
    }
    case NetMsg::GetCvarValue:
        r.ubit(32);
        r.string(limits::kMaxStringBytes);
        return {};
    case NetMsg::CmdKeyValues: {
        const std::uint32_t len = r.ubit(32);
        r.skip(static_cast<std::size_t>(len) * 8);
        return {};
    }
    case NetMsg::GModServerToClient: {
        const std::uint32_t len = r.ubit(variant_->gmodNetLengthBits);
        const auto start = r.position();
        BitReader msg = r.take(len);
        std::string name = "net_message";
        if (len >= 16 && networkStringTable_ >= 0) {
            const std::uint32_t id = msg.ubit(16);
            const auto* e = tables_[static_cast<std::size_t>(networkStringTable_)]->entry(id);
            if (e)
                name = e->string;
        }
        emitEvent(tick, EventKind::GModNet, std::move(name), {}, bitAt(start), len);
        return {};
    }
    }
    return makeError("demo.unknown_message", "unknown network message", std::to_string(type));
}

} // namespace gmdr::demo
