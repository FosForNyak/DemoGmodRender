#include "demo/import.h"

#include "demo/commands.h"
#include "demo/header.h"
#include "demo/statedb/writer.h"

#include <algorithm>

namespace gmdr::demo {

namespace {

// Forwards every callback to the state writer and, if present, to an observer.
class TeeSink : public DemoSink {
public:
    TeeSink(DemoSink& a, DemoSink* b) : a_(a), b_(b) {}
    void onHeader(const DemoHeader& h) override {
        a_.onHeader(h);
        if (b_)
            b_->onHeader(h);
    }
    void onServerInfo(const ServerInfo& s) override {
        a_.onServerInfo(s);
        if (b_)
            b_->onServerInfo(s);
    }
    void onDataTables(const DataTables& dt) override {
        a_.onDataTables(dt);
        if (b_)
            b_->onDataTables(dt);
    }
    void onStringTableChanged(Tick tick, int id, const StringTable& t, std::span<const int> changed,
                              bool created) override {
        a_.onStringTableChanged(tick, id, t, changed, created);
        if (b_)
            b_->onStringTableChanged(tick, id, t, changed, created);
    }
    void onPacket(const CommandRecord& rec) override {
        a_.onPacket(rec);
        if (b_)
            b_->onPacket(rec);
    }
    void onEntityEnter(Tick tick, const EntityRef& ref, bool newLife,
                       std::span<const PropValue> state) override {
        a_.onEntityEnter(tick, ref, newLife, state);
        if (b_)
            b_->onEntityEnter(tick, ref, newLife, state);
    }
    void onEntityUpdate(Tick tick, const EntityRef& ref, std::span<const int> changed,
                        std::span<const PropValue> state) override {
        a_.onEntityUpdate(tick, ref, changed, state);
        if (b_)
            b_->onEntityUpdate(tick, ref, changed, state);
    }
    void onEntityLeave(Tick tick, const EntityRef& ref, bool deleted) override {
        a_.onEntityLeave(tick, ref, deleted);
        if (b_)
            b_->onEntityLeave(tick, ref, deleted);
    }
    void onEvent(const DemoEvent& e) override {
        a_.onEvent(e);
        if (b_)
            b_->onEvent(e);
    }
    void onTickEnd(Tick tick) override {
        a_.onTickEnd(tick);
        if (b_)
            b_->onTickEnd(tick);
    }
    void onDecodeError(Tick tick, std::uint64_t offset, const Error& e) override {
        a_.onDecodeError(tick, offset, e);
        if (b_)
            b_->onDecodeError(tick, offset, e);
    }

private:
    DemoSink& a_;
    DemoSink* b_;
};

} // namespace

Result<ParseStats> importDemo(std::span<const std::uint8_t> demo, File& out, const Blake3Digest& demoHash,
                              const ImportCallbacks& cb) {
    auto header = parseHeader(demo);
    if (!header)
        return header.error();

    statedb::StateWriter writer(out, demoHash, {cb.onChunk, cb.onProgress});
    GMDR_TRY(writer.begin());

    // Pass 1: walk the commands only (no message decoding) so the timeline has its full length at once.
    std::vector<statedb::IndexEntry> index;
    IndexSummary summary;
    summary.headerPlaybackTime = header->playbackTime;
    {
        CommandReader reader(demo);
        TimelineClock clock;
        CommandRecord rec;
        bool first = true;
        Tick lastIndexed = -1;
        while (true) {
            auto more = reader.next(rec);
            if (!more || !*more)
                break; // a truncated tail is reported by pass 2
            ++summary.commands;
            rec.tick = clock.next(rec);
            if (rec.cmd != DemoCommand::Packet && rec.cmd != DemoCommand::Signon)
                continue;
            ++summary.packets;
            if (first) {
                summary.firstTick = rec.tick;
                first = false;
            }
            summary.lastTick = std::max<Tick>(summary.lastTick, rec.tick);
            if (rec.tick != lastIndexed) {
                index.push_back({rec.tick, rec.offset});
                lastIndexed = rec.tick;
            }
            if (cb.cancelled && (summary.commands & 0xFFF) == 0 && cb.cancelled())
                return makeError("import.cancelled", "import was cancelled");
        }
    }
    GMDR_TRY(writer.writeIndex(index, summary.lastTick));
    if (cb.onIndexed)
        cb.onIndexed(summary);

    // Pass 2: full decode.
    TeeSink tee(writer, cb.observer);
    ParseOptions options;
    options.cancelled = cb.cancelled;
    DemoParser parser(demo, tee, options);
    auto r = parser.run();
    if (writer.writeError())
        return *writer.writeError();
    if (!r)
        return r.error();
    GMDR_TRY(writer.finish(parser.stats()));
    GMDR_TRY(out.flush());
    return parser.stats();
}

} // namespace gmdr::demo
