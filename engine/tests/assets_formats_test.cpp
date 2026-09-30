#include "assets/archives.h"
#include "assets/content.h"
#include "assets/locator.h"
#include "assets/vdf.h"
#include "assets/vfs.h"
#include "core/file.h"

#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <random>

using namespace gmdr;
using namespace gmdr::assets;
namespace fs = std::filesystem;

namespace {

struct Bytes {
    std::vector<std::uint8_t> b;
    template <class T> Bytes& put(T v) {
        const auto* p = reinterpret_cast<const std::uint8_t*>(&v);
        b.insert(b.end(), p, p + sizeof(T));
        return *this;
    }
    Bytes& str(std::string_view s) {
        b.insert(b.end(), s.begin(), s.end());
        b.push_back(0);
        return *this;
    }
    Bytes& raw(std::string_view s) {
        b.insert(b.end(), s.begin(), s.end());
        return *this;
    }
    Bytes& raw(const std::vector<std::uint8_t>& v) {
        b.insert(b.end(), v.begin(), v.end());
        return *this;
    }
};

std::vector<std::uint8_t> makeVpk() {
    Bytes tree;
    auto entry = [&](std::uint32_t offset, std::uint32_t length) {
        tree.put<std::uint32_t>(0x1234).put<std::uint16_t>(0).put<std::uint16_t>(0).put(offset).put(length);
        tree.put<std::uint16_t>(0xFFFF);
    };
    tree.str("mdl").str("models/props").str("Box");
    entry(0, 10);
    tree.str("").str("").str("vmt").str("materials/decals").str("shot1");
    entry(10, 20);
    tree.str("").str(" ").str("readme"); // no path
    entry(30, 5);
    tree.str("").str("").str("");
    Bytes vpk;
    vpk.put<std::uint32_t>(0x55AA1234)
        .put<std::uint32_t>(1)
        .put<std::uint32_t>(static_cast<std::uint32_t>(tree.b.size()));
    vpk.raw(tree.b);
    return vpk.b;
}

std::vector<std::uint8_t> makeGma(const std::vector<std::pair<std::string, std::string>>& files) {
    Bytes g;
    g.raw("GMAD")
        .put<std::uint8_t>(3)
        .put<std::uint64_t>(76561198000000000ull)
        .put<std::uint64_t>(1700000000);
    g.str(""); // required content
    g.str("Test Addon").str("{\"description\":\"x\"}").str("Author").put<std::int32_t>(1);
    std::uint32_t n = 1;
    for (const auto& [name, data] : files)
        g.put(n++).str(name).put<std::int64_t>(static_cast<std::int64_t>(data.size())).put<std::uint32_t>(0);
    g.put<std::uint32_t>(0);
    for (const auto& [name, data] : files)
        g.raw(data);
    g.put<std::uint32_t>(0); // trailing CRC of the whole file
    return g.b;
}

std::vector<std::uint8_t> makeZip(const std::vector<std::string>& names) {
    Bytes z;
    std::vector<std::uint32_t> offsets;
    for (const auto& n : names) {
        offsets.push_back(static_cast<std::uint32_t>(z.b.size()));
        z.put<std::uint32_t>(0x04034B50).put<std::uint16_t>(10).put<std::uint16_t>(0).put<std::uint16_t>(0);
        z.put<std::uint16_t>(0)
            .put<std::uint16_t>(0)
            .put<std::uint32_t>(0)
            .put<std::uint32_t>(3)
            .put<std::uint32_t>(3);
        z.put<std::uint16_t>(static_cast<std::uint16_t>(n.size())).put<std::uint16_t>(0).raw(n).raw("abc");
    }
    const auto cdOffset = static_cast<std::uint32_t>(z.b.size());
    for (std::size_t i = 0; i < names.size(); ++i) {
        const auto& n = names[i];
        z.put<std::uint32_t>(0x02014B50).put<std::uint16_t>(20).put<std::uint16_t>(10).put<std::uint16_t>(0);
        z.put<std::uint16_t>(0).put<std::uint16_t>(0).put<std::uint16_t>(0).put<std::uint32_t>(0);
        z.put<std::uint32_t>(3).put<std::uint32_t>(3).put<std::uint16_t>(
            static_cast<std::uint16_t>(n.size()));
        z.put<std::uint16_t>(0)
            .put<std::uint16_t>(0)
            .put<std::uint16_t>(0)
            .put<std::uint16_t>(0)
            .put<std::uint32_t>(0);
        z.put(offsets[i]).raw(n);
    }
    const auto cdSize = static_cast<std::uint32_t>(z.b.size()) - cdOffset;
    z.put<std::uint32_t>(0x06054B50).put<std::uint16_t>(0).put<std::uint16_t>(0);
    z.put<std::uint16_t>(static_cast<std::uint16_t>(names.size()))
        .put<std::uint16_t>(static_cast<std::uint16_t>(names.size()));
    z.put(cdSize).put(cdOffset).put<std::uint16_t>(0);
    return z.b;
}

std::vector<std::uint8_t> makeBsp(const std::vector<std::uint8_t>& pak) {
    Bytes b;
    b.raw("VBSP").put<std::int32_t>(20);
    const std::int32_t pakOffset = 8 + 64 * 16 + 4;
    for (int i = 0; i < 64; ++i) {
        if (i == 40)
            b.put(pakOffset)
                .put(static_cast<std::int32_t>(pak.size()))
                .put<std::int32_t>(0)
                .put<std::int32_t>(0);
        else
            b.put<std::int32_t>(0).put<std::int32_t>(0).put<std::int32_t>(0).put<std::int32_t>(0);
    }
    b.put<std::int32_t>(7).raw(pak);
    return b.b;
}

void writeFile(const fs::path& p, const std::vector<std::uint8_t>& data) {
    fs::create_directories(p.parent_path());
    auto f = File::open(p, File::Mode::CreateTruncate);
    REQUIRE(f);
    REQUIRE(f->writeAt(0, data));
}

void writeText(const fs::path& p, std::string_view text) {
    writeFile(p, std::vector<std::uint8_t>(text.begin(), text.end()));
}

struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path() / ("gmdr_assets_test_" + std::to_string(std::random_device{}()));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

} // namespace

TEST_CASE("VDF: nested objects, escapes, comments, conditionals, case-insensitive lookup") {
    const char* text = "\"libraryfolders\"\n{\n  // comment\n  \"0\"\n  {\n    \"path\"  \"C:\\\\Games\"\n"
                       "    \"apps\" { \"4000\" \"123\" }\n  }\n  \"flag\" \"1\" [$WIN32]\n}\n";
    auto v = parseVdf(text, true);
    REQUIRE(v);
    const VdfNode* lf = v->child("LibraryFolders");
    REQUIRE(lf);
    const VdfNode* zero = lf->child("0");
    REQUIRE(zero);
    CHECK(zero->get("path") == "C:\\Games");
    CHECK(zero->child("apps")->get("4000") == "123");
    CHECK(lf->get("flag") == "1");

    auto raw = parseVdf("\"mountcfg\" { \"tf\" \"C:\\tf\\cstrike\" }", false);
    REQUIRE(raw);
    CHECK(raw->child("mountcfg")->get("tf") == "C:\\tf\\cstrike");

    CHECK_FALSE(parseVdf("\"a\" { \"b\" \"c\"", true)); // unterminated object
    CHECK_FALSE(parseVdf("\"a\" \"b\" }", true));       // stray close
    CHECK_FALSE(parseVdf("\"a\" { \"b\" \"unterminated", true));
    std::string deep;
    for (int i = 0; i < 100; ++i)
        deep += "\"k\" { ";
    CHECK_FALSE(parseVdf(deep, true));
}

TEST_CASE("VPK directory: paths, extensions, empty path, bounds") {
    auto vpk = makeVpk();
    auto idx = parseVpkDirectory(vpk);
    REQUIRE(idx);
    CHECK(idx->files.size() == 3);
    REQUIRE(idx->files.count("models/props/box.mdl"));
    CHECK(idx->files.at("materials/decals/shot1.vmt").offset == 10);
    CHECK(idx->files.count("readme.vmt"));

    auto truncated = vpk;
    truncated.resize(truncated.size() - 5);
    CHECK_FALSE(parseVpkDirectory(truncated));
    auto badTerminator = vpk;
    // The first entry's terminator is the last 2 bytes of its 18-byte record after "Box\0".
    const std::size_t at = 12 + 4 + 13 + 4 + 16;
    badTerminator[at] = 0;
    CHECK_FALSE(parseVpkDirectory(badTerminator));
    auto badMagic = vpk;
    badMagic[0] = 0;
    CHECK(parseVpkDirectory(badMagic).error().code == "vpk.bad_magic");
}

TEST_CASE("GMA: header, file table, data offsets, truncation") {
    auto gma = makeGma({{"sound/Test/A.wav", "abcd"}, {"models/x.mdl", "123456"}});
    auto idx = parseGmaHeader(gma, gma.size());
    REQUIRE(idx);
    CHECK(idx->name == "Test Addon");
    CHECK(idx->files.size() == 2);
    const auto& a = idx->files.at("sound/test/a.wav");
    CHECK(a.size == 4);
    CHECK(std::memcmp(gma.data() + a.offset, "abcd", 4) == 0);
    CHECK(std::memcmp(gma.data() + idx->files.at("models/x.mdl").offset, "123456", 6) == 0);

    // A short head of a longer file asks for more; a short file is truncated.
    std::span<const std::uint8_t> head(gma.data(), 40);
    CHECK(parseGmaHeader(head, gma.size()).error().code == "gma.need_more");
    std::vector<std::uint8_t> cut(gma.begin(), gma.begin() + 40);
    CHECK(parseGmaHeader(cut, cut.size()).error().code == "gma.truncated");
    // Data size larger than the file.
    auto huge = makeGma({{"a.txt", "x"}});
    auto bad = parseGmaHeader(std::span(huge.data(), huge.size()), 20);
    CHECK_FALSE(bad);
}

TEST_CASE("ZIP directory and BSP pakfile") {
    auto zip = makeZip({"materials/Nook/Wall.vmt", "sound/nook/music.mp3", "maps/"});
    auto idx = parseZipDirectory(zip);
    REQUIRE(idx);
    CHECK(idx->files.size() == 2); // directory entries are skipped
    CHECK(idx->files.count("materials/nook/wall.vmt"));

    auto broken = zip;
    broken[broken.size() - 6] = 0xFF; // central directory offset past the file
    CHECK_FALSE(parseZipDirectory(broken));
    CHECK_FALSE(parseZipDirectory(std::vector<std::uint8_t>(10, 0)));

    TempDir tmp;
    auto bsp = makeBsp(zip);
    // Embedded at an offset inside a bigger file, as in a GMA.
    std::vector<std::uint8_t> container(100, 0xAA);
    container.insert(container.end(), bsp.begin(), bsp.end());
    writeFile(tmp.path / "container.bin", container);
    auto f = File::open(tmp.path / "container.bin", File::Mode::Read);
    REQUIRE(f);
    auto names = listBspPakfile(*f, 100, bsp.size());
    REQUIRE(names);
    CHECK(*names == std::vector<std::string>{"materials/nook/wall.vmt", "sound/nook/music.mp3"});
    CHECK_FALSE(listBspPakfile(*f, 100, bsp.size() + 1000));
    CHECK_FALSE(listBspPakfile(*f, 0, bsp.size()));

    auto badLump = bsp;
    const std::size_t lump40 = 8 + 40 * 16;
    const std::int32_t far = 1 << 30;
    std::memcpy(badLump.data() + lump40, &far, 4);
    CHECK_FALSE(parseBspHeader(badLump, badLump.size()));
}

TEST_CASE("locator, VFS order and content check on a fake install") {
    TempDir tmp;
    const fs::path lib = tmp.path / "lib";
    const fs::path gm = lib / "steamapps" / "common" / "GarrysMod";
    const fs::path garrysmod = gm / "garrysmod";
    writeFile(garrysmod / "garrysmod_dir.vpk", makeVpk());
    writeText(garrysmod / "materials" / "decals" / "shot1.vmt",
              "\"LightmappedGeneric\" {}"); // overrides the VPK
    writeText(garrysmod / "cfg" / "mount.cfg",
              "\"mountcfg\" { \"other\" \"" + (tmp.path / "other").generic_string() + "\" }");
    writeText(tmp.path / "other" / "sound" / "other.wav", "x");
    writeFile(garrysmod / "cache" / "workshop" / "3001397905.gma", makeGma({{"sound/test/a.wav", "abcd"}}));
    writeFile(lib / "steamapps" / "workshop" / "content" / "4000" / "129739986" / "123_legacy.bin",
              {1, 2, 3});
    writeText(garrysmod / "download" / "resource" / "fonts" / "f.ttf", "font");
    writeFile(garrysmod / "maps" / "gm_test.bsp", makeBsp(makeZip({"materials/nook/wall.vmt"})));

    auto g = locateGmod(gm);
    REQUIRE(g);
    CHECK(g->origin == "manual");
    REQUIRE(g->mounts.size() == 1);
    CHECK(g->mounts[0].found);
    auto sub = locateGmod(garrysmod); // the inner folder is accepted too
    REQUIRE(sub);
    CHECK_FALSE(locateGmod(tmp.path / "nowhere"));

    auto vfs = Vfs::build(*g);
    REQUIRE(vfs);
    CHECK(vfs->stats().gmas == 1);
    CHECK(vfs->stats().workshopLegacy == 1);
    CHECK(vfs->find("MODELS\\props\\box.mdl")->kind == SourceKind::Vpk);
    CHECK(vfs->find("materials/decals/shot1.vmt")->kind == SourceKind::Folder);
    CHECK(vfs->find("sound/test/a.wav")->workshopId == 3001397905ull);
    CHECK(vfs->find("sound/other.wav")->kind == SourceKind::MountFolder);
    CHECK_FALSE(vfs->find("../../secret.txt"));
    CHECK_FALSE(vfs->find("C:/Windows/win.ini"));

    auto slice = vfs->locate("maps/gm_test.bsp");
    REQUIRE(slice);
    auto mapFile = File::open(slice->path, File::Mode::Read);
    REQUIRE(mapFile);
    auto pak = listBspPakfile(*mapFile, slice->offset, slice->size);
    REQUIRE(pak);
    ContentOverlay overlay;
    overlay.name = "maps/gm_test.bsp";
    for (const auto& n : *pak)
        overlay.add(n);

    Json manifest = {
        {"map", "gm_test"},
        {"models",
         {"maps/gm_test.bsp", "*1", "models/props/box.mdl", "models/missing.mdl", "sprites/glow.vmt"}},
        {"sounds", {")test/a.wav", "^other.wav", "Default.Tile.Standing", "!HG_ALERT", "nope.wav"}},
        {"decals", {"decals/shot1"}},
        {"downloadables",
         {"3001397905.gma", "129739986.gma", "42.gma", "resource/fonts/f.ttf", "materials/nook/wall.vmt"}},
        {"particles", {"blood_impact_red_01"}},
    };
    auto report = checkContent(manifest, *vfs, &overlay);
    auto status = [&](const std::string& name) {
        for (const auto& i : report.items)
            if (i.name == name)
                return std::string(contentStatusName(i.status));
        return std::string("absent");
    };
    CHECK(status("gm_test") == "found");
    CHECK(status("*1") == "builtin");
    CHECK(status("models/props/box.mdl") == "found");
    CHECK(status("models/missing.mdl") == "missing");
    CHECK(status("sprites/glow.vmt") == "missing");
    CHECK(status(")test/a.wav") == "found");
    CHECK(status("^other.wav") == "found");
    CHECK(status("Default.Tile.Standing") == "not-checked");
    CHECK(status("!HG_ALERT") == "builtin");
    CHECK(status("nope.wav") == "missing");
    CHECK(status("decals/shot1") == "found");
    CHECK(status("3001397905.gma") == "workshop-installed");
    CHECK(status("129739986.gma") == "workshop-legacy");
    CHECK(status("42.gma") == "workshop-missing");
    CHECK(status("resource/fonts/f.ttf") == "found");
    CHECK(status("materials/nook/wall.vmt") == "found");
    CHECK(status("blood_impact_red_01") == "not-checked");
    CHECK(report.workshopInstalled == 1);
    CHECK(report.workshopMissing == 2);

    auto j = report.toJson();
    CHECK(j["summary"]["missing"] == report.missing);
    for (const auto& i : j["items"])
        if (i["name"] == "materials/nook/wall.vmt")
            CHECK(i["where"] == "pakfile");
}

TEST_CASE("archive parsers survive random corruption") {
    std::mt19937 rng(2026);
    const std::vector<std::vector<std::uint8_t>> seeds = {
        makeVpk(), makeGma({{"a/b.txt", "hello"}, {"c.mdl", "xyz"}}), makeZip({"x/y.vmt", "z.wav"}),
        makeBsp(makeZip({"m.vmt"})),
        std::vector<std::uint8_t>{'"', 'a', '"', ' ', '{', '"', 'b', '"', ' ', '"', 'c', '"', '}'}};
    for (int round = 0; round < 3000; ++round) {
        auto data = seeds[static_cast<std::size_t>(round) % seeds.size()];
        const int flips = 1 + static_cast<int>(rng() % 4);
        for (int i = 0; i < flips; ++i)
            data[rng() % data.size()] = static_cast<std::uint8_t>(rng());
        if (rng() % 4 == 0)
            data.resize(rng() % (data.size() + 1));
        (void)parseVpkDirectory(data);
        (void)parseGmaHeader(data, data.size());
        (void)parseZipDirectory(data);
        (void)parseBspHeader(data, data.size());
        (void)parseVdf(std::string_view(reinterpret_cast<const char*>(data.data()), data.size()),
                       round % 2 == 0);
    }
    CHECK(true);
}
