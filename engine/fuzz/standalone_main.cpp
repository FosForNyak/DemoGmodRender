// Driver for compilers without libFuzzer: deterministic mutations of the target's seeds plus random inputs.
// Usage: fuzz_<target> [--runs=N] [--seed=S]
//        seeds_<target> --write-seeds=DIR   (writes the seeds as files: the starting corpus for libFuzzer)
#include "fuzz.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace {

int writeSeeds(const std::filesystem::path& dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const auto seeds = fuzzSeeds();
    for (std::size_t i = 0; i < seeds.size(); ++i) {
        std::ofstream out(dir / ("seed-" + std::to_string(i)), std::ios::binary);
        out.write(reinterpret_cast<const char*>(seeds[i].data()),
                  static_cast<std::streamsize>(seeds[i].size()));
        if (!out) {
            std::fprintf(stderr, "cannot write seeds to %s\n", dir.string().c_str());
            return 1;
        }
    }
    std::printf("%zu seeds written\n", seeds.size());
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::size_t runs = 3000;
    std::uint64_t seed = 20260930;
    for (int i = 1; i < argc; ++i) {
        if (std::strncmp(argv[i], "--runs=", 7) == 0)
            runs = std::strtoull(argv[i] + 7, nullptr, 10);
        else if (std::strncmp(argv[i], "--seed=", 7) == 0)
            seed = std::strtoull(argv[i] + 7, nullptr, 10);
        else if (std::strncmp(argv[i], "--write-seeds=", 14) == 0)
            return writeSeeds(argv[i] + 14);
    }
    const auto seeds = fuzzSeeds();
    std::mt19937_64 rng(seed);
    std::vector<std::uint8_t> buf;
    for (const auto& s : seeds)
        LLVMFuzzerTestOneInput(s.data(), s.size());
    for (std::size_t run = 0; run < runs; ++run) {
        const unsigned mode = rng() % 4;
        if (mode == 0 || seeds.empty()) {
            buf.resize(rng() % 2048);
            for (auto& b : buf)
                b = static_cast<std::uint8_t>(rng());
        } else {
            buf = seeds[rng() % seeds.size()];
            const unsigned edits = 1 + static_cast<unsigned>(rng() % 8);
            for (unsigned e = 0; e < edits && !buf.empty(); ++e) {
                const std::size_t at = rng() % buf.size();
                switch (rng() % 5) {
                case 0:
                    buf[at] = static_cast<std::uint8_t>(rng());
                    break; // replace a byte
                case 1:
                    buf[at] ^= static_cast<std::uint8_t>(1u << (rng() % 8));
                    break; // flip a bit
                case 2:
                    buf.resize(at);
                    break; // truncate
                case 3:
                    buf.insert(buf.begin() + static_cast<std::ptrdiff_t>(at),
                               static_cast<std::uint8_t>(rng()));
                    break;
                case 4: { // interesting values
                    static const std::uint8_t kSpecial[] = {0x00, 0x01, 0x7F, 0x80, 0xFF};
                    buf[at] = kSpecial[rng() % sizeof kSpecial];
                    break;
                }
                }
            }
        }
        LLVMFuzzerTestOneInput(buf.data(), buf.size());
    }
    std::printf("%zu runs, %zu seeds: no crash\n", runs, seeds.size());
    return 0;
}
