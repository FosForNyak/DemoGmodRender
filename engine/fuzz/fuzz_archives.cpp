// Content archives and configs on untrusted bytes: VPK, GMA, ZIP, BSP header, VDF. The first byte picks one.
#include "assets/archives.h"
#include "assets/vdf.h"
#include "fuzz.h"

#include <cstring>
#include <string_view>

using namespace gmdr::assets;

namespace {

template <class T> void put(std::vector<std::uint8_t>& v, T x) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(&x);
    v.insert(v.end(), p, p + sizeof x);
}

void str(std::vector<std::uint8_t>& v, std::string_view s) {
    v.insert(v.end(), s.begin(), s.end());
    v.push_back(0);
}

} // namespace

std::vector<std::vector<std::uint8_t>> fuzzSeeds() {
    std::vector<std::vector<std::uint8_t>> seeds;
    {
        std::vector<std::uint8_t> tree;
        str(tree, "mdl");
        str(tree, "models");
        str(tree, "box");
        put<std::uint32_t>(tree, 0);
        put<std::uint16_t>(tree, 0);
        put<std::uint16_t>(tree, 0);
        put<std::uint32_t>(tree, 0);
        put<std::uint32_t>(tree, 4);
        put<std::uint16_t>(tree, 0xFFFF);
        tree.insert(tree.end(), {0, 0, 0});
        std::vector<std::uint8_t> v = {0};
        put<std::uint32_t>(v, 0x55AA1234);
        put<std::uint32_t>(v, 1);
        put<std::uint32_t>(v, static_cast<std::uint32_t>(tree.size()));
        v.insert(v.end(), tree.begin(), tree.end());
        seeds.push_back(v);
    }
    {
        std::vector<std::uint8_t> g = {1, 'G', 'M', 'A', 'D', 3};
        put<std::uint64_t>(g, 1);
        put<std::uint64_t>(g, 2);
        str(g, "");
        str(g, "name");
        str(g, "desc");
        str(g, "author");
        put<std::int32_t>(g, 1);
        put<std::uint32_t>(g, 1);
        str(g, "sound/a.wav");
        put<std::int64_t>(g, 3);
        put<std::uint32_t>(g, 0);
        put<std::uint32_t>(g, 0);
        g.insert(g.end(), {'a', 'b', 'c', 0, 0, 0, 0});
        seeds.push_back(g);
    }
    {
        std::vector<std::uint8_t> z = {2};
        put<std::uint32_t>(z, 0x02014B50);
        z.insert(z.end(), 24, 0);
        put<std::uint16_t>(z, 5);
        z.insert(z.end(), 16, 0);
        z.insert(z.end(), {'a', '.', 'v', 'm', 't'});
        put<std::uint32_t>(z, 0x06054B50);
        put<std::uint16_t>(z, 0);
        put<std::uint16_t>(z, 0);
        put<std::uint16_t>(z, 1);
        put<std::uint16_t>(z, 1);
        put<std::uint32_t>(z, 51);
        put<std::uint32_t>(z, 0);
        put<std::uint16_t>(z, 0);
        seeds.push_back(z);
    }
    {
        std::vector<std::uint8_t> b = {3, 'V', 'B', 'S', 'P'};
        put<std::int32_t>(b, 20);
        b.insert(b.end(), 64 * 16 + 4, 0);
        seeds.push_back(b);
    }
    for (int which : {4, 5}) {
        std::vector<std::uint8_t> t = {static_cast<std::uint8_t>(which)};
        const std::string_view s =
            "\"libraryfolders\" { \"0\" { \"path\" \"C:\\\\Games\" \"apps\" { \"4000\" \"1\" } } } // c";
        t.insert(t.end(), s.begin(), s.end());
        seeds.push_back(t);
    }
    return seeds;
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size < 1 || size > (4u << 20))
        return 0;
    std::span<const std::uint8_t> rest(data + 1, size - 1);
    switch (data[0] % 6) {
    case 0:
        (void)parseVpkDirectory(rest);
        break;
    case 1:
        (void)parseGmaHeader(rest, rest.size());
        break;
    case 2:
        (void)parseZipDirectory(rest);
        break;
    case 3:
        (void)parseBspHeader(rest, rest.size());
        break;
    case 4:
    case 5:
        (void)parseVdf(std::string_view(reinterpret_cast<const char*>(rest.data()), rest.size()),
                       data[0] % 6 == 4);
        break;
    }
    return 0;
}
