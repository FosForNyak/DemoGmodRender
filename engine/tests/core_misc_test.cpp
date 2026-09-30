#include "core/compress.h"
#include "core/file.h"
#include "core/hash.h"
#include "core/text.h"
#include "core/time.h"

#include <doctest/doctest.h>
#include <filesystem>
#include <string>

TEST_CASE("blake3 of empty input matches the reference digest") {
    const auto d = gmdr::blake3({});
    CHECK(d.hex() == "af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262");
}

TEST_CASE("blake3 streaming equals one-shot") {
    std::string s(100000, 'x');
    const auto* p = reinterpret_cast<const std::uint8_t*>(s.data());
    gmdr::Blake3Hasher h;
    h.update({p, 12345});
    h.update({p + 12345, s.size() - 12345});
    CHECK(h.finalize() == gmdr::blake3({p, s.size()}));
}

TEST_CASE("zstd round trip and size check") {
    std::string s;
    for (int i = 0; i < 1000; ++i)
        s += "entity " + std::to_string(i % 17) + ";";
    const std::span<const std::uint8_t> in(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
    auto c = gmdr::zstdCompress(in);
    REQUIRE(c);
    CHECK(c->size() < s.size());
    auto d = gmdr::zstdDecompress(*c, s.size());
    REQUIRE(d);
    CHECK(std::string(d->begin(), d->end()) == s);
    CHECK_FALSE(gmdr::zstdDecompress(*c, s.size() + 1));
    CHECK_FALSE(gmdr::zstdDecompress(*c, (1ull << 40)));
}

TEST_CASE("sanitizeUtf8 keeps valid text and replaces broken sequences") {
    CHECK(gmdr::sanitizeUtf8("Привіт, gm_construct") == "Привіт, gm_construct");
    CHECK(gmdr::sanitizeUtf8(std::string("a\xFF" "b")) == "a\xEF\xBF\xBD" "b");
    CHECK(gmdr::sanitizeUtf8(std::string("\xC0\xAF")) == "\xEF\xBF\xBD\xEF\xBF\xBD"); // overlong '/'
    CHECK(gmdr::sanitizeUtf8(std::string("x\x01y")) == "x\xEF\xBF\xBDy");
}

TEST_CASE("normalizeGamePath") {
    CHECK(gmdr::normalizeGamePath("Models\\Props_C17//Oildrum001.MDL") == "models/props_c17/oildrum001.mdl");
    CHECK(gmdr::normalizeGamePath("./maps/gm_construct.bsp") == "maps/gm_construct.bsp");
    CHECK(gmdr::normalizeGamePath("/sound/x.wav") == "sound/x.wav");
}

TEST_CASE("flicks divide common rates exactly") {
    CHECK(gmdr::kFlicksPerSecond % 24 == 0);
    CHECK(gmdr::kFlicksPerSecond % 60 == 0);
    CHECK(gmdr::kFlicksPerSecond % 44100 == 0);
    CHECK(gmdr::kFlicksPerSecond % 48000 == 0);
    CHECK(gmdr::secondsToFlicks(0.015) == 10584000);
}

TEST_CASE("File positional IO") {
    const auto path = std::filesystem::temp_directory_path() / "gmdr_file_test.bin";
    {
        auto f = gmdr::File::open(path, gmdr::File::Mode::CreateTruncate);
        REQUIRE(f);
        const std::uint8_t a[3] = {1, 2, 3};
        REQUIRE(f->writeAt(10, a));
        auto size = f->size();
        REQUIRE(size);
        CHECK(*size == 13);
    }
    {
        auto f = gmdr::File::open(path, gmdr::File::Mode::Read);
        REQUIRE(f);
        std::uint8_t b[3] = {};
        REQUIRE(f->readExactAt(10, b));
        CHECK(b[2] == 3);
        std::uint8_t c[5] = {};
        CHECK_FALSE(f->readExactAt(10, c));
        auto m = gmdr::MappedFile::map(*f);
        REQUIRE(m);
        CHECK(m->data().size() == 13);
        CHECK(m->data()[11] == 2);
    }
    std::filesystem::remove(path);
    CHECK_FALSE(gmdr::File::open(path, gmdr::File::Mode::Read));
}
