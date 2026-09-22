// =============================================================================
//  fuzz_demo.cpp — фазинг парсера демо і голосу (libFuzzer).
//
//  Демо приходять ззовні, тож будь-який битий файл має закінчуватися
//  зрозумілою помилкою, а не падінням. libFuzzer сам генерує мільйони
//  варіантів вхідних даних, а санітайзер ловить вихід за межі пам'яті.
//
//  Збірка: cmake -DGMDR_BUILD_FUZZERS=ON (Clang або MSVC з /fsanitize=fuzzer)
//  Запуск: gmdr-fuzz-demo corpus_dir  (у corpus_dir — кілька справжніх .dem,
//          напр. згенерованих tests/tools/make_test_demo.py)
// =============================================================================
#include "core/demo/analysis.hpp"
#include "core/util/log.hpp"
#include "core/voice/voice_decoder.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    static const bool quiet = [] {
        gmdr::set_min_log_level(gmdr::LogLevel::Error);
        return true;
    }();
    (void)quiet;
    try {
        gmdr::demo::DemoFile f(std::vector<uint8_t>(data, data + size));
        gmdr::demo::AnalyzeOptions opt;
        opt.detect_packets = 200;
        auto a = gmdr::demo::analyze_demo(f, opt);
        const std::string chat = gmdr::demo::format_chat_log(a.events, a.tick_interval);   // чат і події теж
        (void)chat;
        auto v = gmdr::voice::decode_voice(a);
        for (const auto& sp : v.speakers) {
            auto pcm = gmdr::voice::decode_range(sp, 0, std::min<int64_t>(sp.end_sample(), 48000));
            (void)pcm;
        }
    } catch (const std::exception&) {
        // відмова з поясненням — очікувана реакція на битий файл
    }
    return 0;
}
