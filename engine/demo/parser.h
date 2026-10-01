#pragma once

#include "core/error.h"
#include "core/time.h"
#include "demo/commands.h"
#include "demo/events.h"
#include "demo/game_events.h"
#include "demo/header.h"
#include "demo/protocol.h"
#include "demo/send_tables.h"
#include "demo/string_tables.h"
#include "demo/types.h"

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace gmdr::demo {

struct EntityRef {
    int index = 0;
    int classId = 0;
    int serial = 0;
    std::uint32_t life = 0; // ordinal of this entity life within the demo (1-based)
};

// Receives everything the parser learns, in demo order. All spans are valid only during the call.
class DemoSink {
public:
    virtual ~DemoSink() = default;
    virtual void onHeader(const DemoHeader&) {}
    virtual void onServerInfo(const ServerInfo&) {}
    virtual void onDataTables(const DataTables&) {}
    virtual void onStringTableChanged(Tick, int /*tableId*/, const StringTable&,
                                      std::span<const int> /*changed*/, bool /*created*/) {}
    virtual void onPacket(const CommandRecord&) {}
    virtual void onEntityEnter(Tick, const EntityRef&, bool /*newLife*/,
                               std::span<const PropValue> /*state*/) {}
    virtual void onEntityUpdate(Tick, const EntityRef&, std::span<const int> /*changed*/,
                                std::span<const PropValue> /*state*/) {}
    virtual void onEntityLeave(Tick, const EntityRef&, bool /*deleted*/) {}
    virtual void onEvent(const DemoEvent&) {}
    virtual void onTickEnd(Tick) {}
    virtual void onDecodeError(Tick, std::uint64_t /*fileOffset*/, const Error&) {}
};

struct ParseOptions {
    const ProtocolVariant* variant = nullptr; // nullptr: the first known variant
    std::function<bool()> cancelled;          // polled between commands
};

struct ParseStats {
    std::uint64_t packets = 0;
    std::uint64_t entityEnters = 0;
    std::uint64_t entityUpdates = 0;
    std::uint64_t entityLeaves = 0;
    std::uint64_t events = 0;
    std::uint64_t decodeErrors = 0;
};

class DemoParser {
public:
    DemoParser(std::span<const std::uint8_t> file, DemoSink& sink, ParseOptions options = {});
    ~DemoParser();

    Result<void> run();

    const ParseStats& stats() const { return stats_; }
    const DataTables* dataTables() const { return dataTables_.get(); }
    const std::vector<FlatProp>* flatProps(int classId);
    const std::vector<std::unique_ptr<StringTable>>& stringTables() const { return tables_; }

private:
    struct EntitySlot {
        bool active = false; // state is known (in PVS or dormant after leaving it without deletion)
        bool inPvs = false;
        int classId = -1;
        int serial = 0;
        std::uint32_t life = 0;
        bool lifeDeleted = true;
        std::vector<PropValue> props;
    };
    struct Baseline {
        int classId = -1;
        std::vector<PropValue> props;
    };

    Result<void> parsePacket(const CommandRecord& rec);
    Result<void> parseMessage(int type, BitReader& r, const CommandRecord& rec);
    Result<void> parseServerInfo(BitReader& r);
    Result<void> parseCreateStringTable(BitReader& r, Tick tick);
    Result<void> parseUpdateStringTable(BitReader& r, Tick tick);
    void applyStringTableSnapshot(const CommandRecord& rec);
    Result<void> parsePacketEntities(BitReader& r, Tick tick);
    Result<void> parseDataTables(const CommandRecord& rec);
    void emitEvent(Tick tick, EventKind kind, std::string name, std::string summary, std::uint64_t bitOffset,
                   std::uint32_t bitLength, std::int32_t entity = -1);
    Result<const std::vector<PropValue>*> instanceBaseline(int classId);
    EntitySlot& slotAt(std::size_t index);
    void onTableChanged(int tableId, std::span<const int> changed);

    std::span<const std::uint8_t> file_;
    DemoSink& sink_;
    ParseOptions options_;
    const ProtocolVariant* variant_;
    ParseStats stats_;
    ServerInfo serverInfo_;

    std::unique_ptr<DataTables> dataTables_;
    std::vector<std::optional<std::vector<FlatProp>>> flat_;
    std::vector<std::unique_ptr<StringTable>> tables_;
    int instanceBaselineTable_ = -1;
    int networkStringTable_ = -1;
    std::vector<std::optional<std::vector<PropValue>>> instanceBaselines_;
    GameEventSchema gameEvents_;

    std::vector<EntitySlot> entities_;
    std::array<std::vector<Baseline>, 2> entityBaselines_;
    std::uint32_t nextLife_ = 1;
    std::vector<int> changedScratch_;
    std::uint64_t packetBitBase_ = 0;
};

} // namespace gmdr::demo
