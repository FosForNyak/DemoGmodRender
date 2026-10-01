// Individual demo decoders on untrusted bytes. The first byte picks the decoder.
#include "core/bit_reader.h"
#include "demo/game_events.h"
#include "demo/lzss.h"
#include "demo/protocol.h"
#include "demo/send_tables.h"
#include "demo/string_tables.h"
#include "fuzz.h"

using namespace gmdr;
using namespace gmdr::demo;

std::vector<std::vector<std::uint8_t>> fuzzSeeds() {
    return {
        {0, 'L', 'Z', 'S', 'S', 8, 0, 0, 0, 0x00, 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'},
        {1, 0x03, 0xAA, 0x55, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90},
        {2, 12, 0, 0, 3, 0xFF, 0x0F, 'm', 'o', 'd', 'e', 'l', 's', 0, 0x01, 0x02},
        {3, 2, 0x05, 'e', 'v', 0, 0x11, 'k', 0, 0x22, 0x33, 0x44},
    };
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size < 2 || size > (1u << 20))
        return 0;
    const std::uint8_t which = data[0] % 4;
    std::span<const std::uint8_t> rest(data + 1, size - 1);
    switch (which) {
    case 0:
        (void)lzssDecompress(rest);
        break;
    case 1: {
        auto dt = parseDataTables(rest, knownProtocolVariants()[0]);
        if (dt)
            for (const auto& c : (*dt)->classes)
                (void)flattenClass(**dt, c.tableName);
        break;
    }
    case 2: {
        // Table parameters from the first bytes, then entries.
        const int maxBits = 1 + rest[0] % 16;
        const bool fixed = rest.size() > 1 && (rest[1] & 1);
        const int udSize = rest.size() > 2 ? rest[2] : 0;
        const int udBits = rest.size() > 3 ? rest[3] % 33 : 0;
        StringTable table("fuzz", maxBits, fixed, udSize, udBits);
        if (rest.size() > 4) {
            BitReader r(rest.subspan(4));
            std::vector<int> changed;
            for (int round = 0; round < 2; ++round)
                (void)table.parseEntries(r, 1 + rest[0] % 64, 19, changed);
        }
        break;
    }
    case 3: {
        GameEventSchema schema;
        BitReader r(rest);
        if (schema.parseList(r, 1 + rest[0] % 8)) {
            std::string name, summary;
            for (int i = 0; i < 4; ++i)
                (void)schema.decode(r, name, summary);
        }
        break;
    }
    }
    return 0;
}
