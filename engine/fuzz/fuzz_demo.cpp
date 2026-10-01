// The whole .dem pipeline on untrusted bytes: header, command walk, net messages, string tables, SendTables,
// entities and props. Each input is parsed as-is and also behind a valid header (to reach the commands fast).
#include "demo/parser.h"
#include "fuzz.h"

#include <cstring>

using namespace gmdr::demo;

namespace {

std::vector<std::uint8_t> header() {
    std::vector<std::uint8_t> h(1072, 0);
    std::memcpy(h.data(), "HL2DEMO", 7);
    const std::int32_t demoProtocol = 3, netProtocol = 24;
    std::memcpy(h.data() + 8, &demoProtocol, 4);
    std::memcpy(h.data() + 12, &netProtocol, 4);
    return h;
}

void push32(std::vector<std::uint8_t>& v, std::int32_t x) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(&x);
    v.insert(v.end(), p, p + 4);
}

// A dem_packet command with the given net-message payload.
void packet(std::vector<std::uint8_t>& v, std::int32_t tick, const std::vector<std::uint8_t>& payload) {
    v.push_back(2);
    push32(v, tick);
    v.insert(v.end(), 76 + 8, 0); // democmdinfo + sequence numbers
    push32(v, static_cast<std::int32_t>(payload.size()));
    v.insert(v.end(), payload.begin(), payload.end());
}

} // namespace

std::vector<std::vector<std::uint8_t>> fuzzSeeds() {
    std::vector<std::vector<std::uint8_t>> seeds;
    auto a = header();
    a.push_back(7); // dem_stop
    push32(a, 0);
    seeds.push_back(a);
    // Packets whose first bits select a few message types (6-bit ids), with some payload.
    for (int type : {0, 3, 5, 8, 12, 13, 14, 23, 26, 33}) {
        auto b = header();
        std::vector<std::uint8_t> payload = {static_cast<std::uint8_t>(type),
                                             0x10,
                                             0x20,
                                             0x40,
                                             0x80,
                                             0xFF,
                                             0x01,
                                             0x02,
                                             0x03,
                                             0x04,
                                             0x05,
                                             0x06};
        packet(b, 1, payload);
        packet(b, 2, payload);
        b.push_back(7);
        push32(b, 2);
        seeds.push_back(b);
    }
    // dem_datatables with a tiny body, dem_consolecmd, dem_stringtables.
    for (int cmd : {4, 6, 8}) {
        auto c = header();
        c.push_back(static_cast<std::uint8_t>(cmd));
        push32(c, 0);
        push32(c, 16);
        for (int i = 0; i < 16; ++i)
            c.push_back(static_cast<std::uint8_t>(i * 17));
        seeds.push_back(c);
    }
    return seeds;
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size > (8u << 20))
        return 0;
    DemoSink sink;
    {
        DemoParser parser(std::span(data, size), sink);
        (void)parser.run();
    }
    std::vector<std::uint8_t> withHeader = header();
    withHeader.insert(withHeader.end(), data, data + size);
    {
        DemoParser parser(withHeader, sink);
        (void)parser.run();
    }
    return 0;
}
