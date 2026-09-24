// =============================================================================
//  test_main.cpp — модульні тести ядра (без зовнішніх фреймворків).
//
//  Запуск:  gmdr-tests [папка_з_тестовими_даними]
//  Тести демо використовують синтетичні файли з tests/tools/make_test_demo.py
// =============================================================================
#include "core/audio/audio_filter.hpp"
#include "core/audio/audio_inputs.hpp"
#include "core/audio/voice_clean.hpp"
#include "core/audio/voice_preview.hpp"
#include "core/audio/wav.hpp"
#include "core/speech/transcribe.hpp"
#include "core/demo/analysis.hpp"
#include "core/demo/bitreader.hpp"
#include "core/demo/chat.hpp"
#include "core/demo/game_events.hpp"
#include "core/demo/library.hpp"
#include "core/demo/string_tables.hpp"
#include "core/frames/blender.hpp"
#include "core/frames/frame_pipe.hpp"
#include "core/frames/image_decode.hpp"
#include "core/frames/tga.hpp"
#include "core/frames/sequence_reader.hpp"
#include "core/game/lua_driver.hpp"
#include "core/game/process.hpp"
#include "core/game/rtx.hpp"
#include "core/dub/dub_mix.hpp"
#include "core/dub/tts.hpp"
#include "core/dub/voice_library.hpp"
#include "core/render/dubbing.hpp"
#include "core/render/jobs.hpp"
#include "core/translate/translate.hpp"
#include "core/util/secret.hpp"
#include "core/render/markers.hpp"
#include "core/render/subtitles.hpp"
#include "core/render/versions.hpp"
#include "core/render/derived.hpp"
#include "core/render/overlay.hpp"
#include "core/render/edit_package.hpp"
#include "core/render/frame_transport.hpp"
#include "core/render/report.hpp"
#include "core/util/zip_writer.hpp"
#include "core/util/file_assoc.hpp"
#include "core/util/power.hpp"
#include "core/util/update_check.hpp"
#include "core/media/muxer.hpp"
#include "core/util/file_util.hpp"
#include "core/util/i18n.hpp"
#include "core/media/ffmpeg_util.hpp"
#include "core/media/video_encoder.hpp"
#include "core/util/json.hpp"
#include "core/util/log.hpp"
#include "core/util/strings.hpp"
#include "core/util/vdf.hpp"
#include "core/voice/steam_voice.hpp"
#include "core/voice/voice_decoder.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <complex>
#include <cstring>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <random>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace gmdr;

static bool pix_fmt_bit_depth_16(AVPixelFormat f) { return media::pix_fmt_bit_depth(f) > 8; }

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                                                  \
    do {                                                                                             \
        if (cond) { ++g_pass; }                                                                      \
        else { ++g_fail; std::printf("  ПРОВАЛ %s:%d: %s\n", __FILE__, __LINE__, #cond); }            \
    } while (0)
#define CHECK_NEAR(a, b, eps)                                                                        \
    do {                                                                                             \
        const double _a = (a), _b = (b);                                                             \
        if (std::abs(_a - _b) <= (eps)) { ++g_pass; }                                                \
        else { ++g_fail; std::printf("  ПРОВАЛ %s:%d: %s = %g, очікувалось %g ± %g\n", __FILE__, __LINE__, #a, _a, _b, (double)(eps)); } \
    } while (0)

// Рівень RMS відрізка [from_s, to_s) секунд (48 кГц; stride 2 — лівий канал стерео), дБ.
static double rms_db(const std::vector<float>& x, double from_s, double to_s, size_t stride = 1);

static void test_bitreader() {
    std::printf("[bitreader]\n");
    const uint8_t data[] = {0b10110101, 0b11001010, 0xFF, 0x00, 0x41, 0x42, 0x00};
    demo::BitReader br(data, sizeof(data));
    CHECK(br.read_ubits(3) == 0b101);
    CHECK(br.read_ubits(5) == 0b10110);
    CHECK(br.read_ubits(4) == 0b1010);
    CHECK(br.read_ubits(12) == 0xFFC);
    CHECK(br.read_byte() == 0x00);
    CHECK(br.read_string() == "AB");
    CHECK(!br.overflowed());
    br.read_ubits(8);
    CHECK(br.overflowed());
    // varint
    const uint8_t v[] = {0xAC, 0x02};
    demo::BitReader bv(v, 2);
    CHECK(bv.read_varint32() == 300);
    // sbits
    const uint8_t s[] = {0x0F};
    demo::BitReader bs(s, 1);
    CHECK(bs.read_sbits(4) == -1);
}

static void test_strings_json_vdf() {
    std::printf("[strings/json/vdf]\n");
    CHECK_NEAR(*parse_timecode("95.5"), 95.5, 1e-9);
    CHECK_NEAR(*parse_timecode("1:35"), 95.0, 1e-9);
    CHECK_NEAR(*parse_timecode("1:02:03.25"), 3723.25, 1e-9);
    CHECK_NEAR(*parse_timecode(" 00:12,5 "), 12.5, 1e-9);
    CHECK(!parse_timecode("1:75"));
    CHECK(!parse_timecode("1.5:10"));
    CHECK(!parse_timecode("1::3"));
    CHECK(!parse_timecode("abc"));
    CHECK(format_timecode(3723.25) == "1:02:03.25");
    CHECK(format_timecode(12.5) == "00:12.50");
    CHECK_NEAR(*parse_timecode(format_timecode(11370.47)), 11370.47, 0.006);
    auto r = parse_rational("59.94");
    CHECK(r && r->num == 60000 && r->den == 1001);
    r = parse_rational("60");
    CHECK(r && r->num == 60 && r->den == 1);
    r = parse_rational("120000/1001");
    CHECK(r && r->num == 120000);
    auto sz = parse_size("1920x1080");
    CHECK(sz && sz->first == 1920 && sz->second == 1080);
    CHECK(parse_bitrate("20M").value_or(0) == 20000000);
    CHECK(parse_bitrate("320k").value_or(0) == 320000);
    CHECK(format_steamid(76561197960265728ULL + 2 * 1111 + 1) == "STEAM_0:1:1111");
    auto kv = parse_key_values("crf=18; preset = slow\nx264-params=aq-mode=3");
    CHECK(kv.size() == 3 && kv[1].second == "slow" && kv[2].second == "aq-mode=3");

    auto j = json::parse(R"({"a":1,"b":"текст \u0442","c":[true,false,null],"d":{"e":2.5}})");
    CHECK(j.has_value());
    if (j) {
        CHECK((*j)["a"].as_int() == 1);
        CHECK((*j)["b"].as_string() == "текст т");
        CHECK((*j)["c"][0].as_bool() == true);
        CHECK((*j)["d"]["e"].as_number() == 2.5);
        auto again = json::parse(j->dump());
        CHECK(again && (*again)["b"].as_string() == "текст т");
    }
    CHECK(!json::parse("{\"a\":}").has_value());

    auto v = vdf::parse(R"("libraryfolders" { "0" { "path" "C:\\Program Files (x86)\\Steam" "apps" { "4000" "123" } } })");
    CHECK(v != nullptr);
    if (v) {
        const auto* lf = v->child("libraryfolders");
        CHECK(lf && lf->child("0") && lf->child("0")->get("path") == "C:\\Program Files (x86)\\Steam");
        CHECK(lf && lf->child("0")->child("apps")->get("4000") == "123");
    }
}

static void test_steam_voice_crc() {
    std::printf("[steam voice]\n");
    const char* s = "123456789";
    CHECK(voice::crc32_ieee(reinterpret_cast<const uint8_t*>(s), 9) == 0xCBF43926u);
}

static void test_lzss() {
    std::printf("[lzss]\n");
    // "LZSS" + розмір 6 + команда(0b00001000: 3 літерали, потім посилання) "abc" + посилання(позиція 2, кількість 3) + кінець
    const uint8_t comp[] = {'L', 'Z', 'S', 'S', 6, 0, 0, 0, 0x18, 'a', 'b', 'c', 0x00, 0x22, 0x00, 0x00};
    auto out = demo::lzss_decompress(comp, sizeof(comp));
    CHECK(out.has_value());
    if (out) CHECK(std::string(out->begin(), out->end()) == "abcabc");
}

// Потужність частоти f у сигналі (алгоритм Герцеля)
static double goertzel(const std::vector<float>& x, size_t from, size_t n, double f, double rate) {
    const double w = 2 * 3.14159265358979323846 * f / rate;
    const double c = 2 * std::cos(w);
    double s1 = 0, s2 = 0;
    for (size_t i = from; i < from + n && i < x.size(); ++i) {
        const double s = x[i] + c * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}

static void test_demo(const std::filesystem::path& demo_path, int expect_pe_bits, bool expect_gmod2026 = false) {
    std::printf("[demo %s]\n", demo_path.filename().string().c_str());
    demo::DemoAnalysis a;
    try {
        a = demo::analyze_demo(demo_path);
    } catch (const std::exception& e) {
        std::printf("  виняток: %s\n", e.what());
        CHECK(false);
        return;
    }
    CHECK(a.header.map_name == "gm_construct");
    CHECK(a.header.client_name == "Recorder Юзер");
    CHECK(a.has_server_info);
    CHECK(a.server_info.gamemode == "sandbox");
    CHECK(a.server_info.host_name == "Test Server [UA]");
    CHECK_NEAR(a.tick_interval, 1.0 / 66.0, 1e-6);
    CHECK(a.local_slot == 0);
    CHECK(a.variant.packet_entities_len_bits == expect_pe_bits);
    CHECK(a.variant.gmod_2026 == expect_gmod2026);
    CHECK(a.packets_failed == 0);
    CHECK(a.voice_codec == "steam");
    CHECK(a.players.size() == 4);
    CHECK(a.players.count(5) && a.players.at(5).name == "LateJoiner");
    CHECK(a.players.count(0) && a.players.at(0).steamid64 == 76561197960265728ULL + 2 * 1111 + 1);
    CHECK(a.players.count(2) && a.players.at(2).fake_player);
    CHECK(a.last_tick == 825);
    CHECK_NEAR(a.duration_seconds, 12.5, 0.02);
    // Чат і події: SayText/TextMsg визначено за формою, player_say не дублює чат
    CHECK(a.user_message_types.say_text == 3);
    CHECK(a.user_message_types.text_msg == 4);
    CHECK(a.count_events(demo::DemoEventKind::Chat) == 3);
    CHECK(a.count_events(demo::DemoEventKind::Server) == 1);
    CHECK(a.count_events(demo::DemoEventKind::Join) == 1);
    CHECK(a.count_events(demo::DemoEventKind::Leave) == 1);
    {
        std::vector<const demo::DemoEvent*> chat;
        for (const auto& e : a.events)
            if (e.kind == demo::DemoEventKind::Chat) chat.push_back(&e);
        CHECK(chat.size() == 3 && chat[0]->tick == 100 && chat[0]->who == "Friend" && chat[0]->text == "Привіт усім");
        CHECK(chat.size() == 3 && chat[1]->who == "Recorder Юзер" && chat[1]->text == "gg 100%");
        CHECK(chat.size() == 3 && chat[2]->who == "LateJoiner" && chat[2]->slot == 5);
        for (const auto& e : a.events) {
            if (e.kind == demo::DemoEventKind::Join) CHECK(e.tick == 330 && e.who == "LateJoiner" && e.slot == 5);
            if (e.kind == demo::DemoEventKind::Leave) CHECK(e.who == "Friend" && e.slot == 1 && e.text == "Disconnect by user.");
            if (e.kind == demo::DemoEventKind::Server) CHECK(e.text == "Server restart in 5 minutes");
        }
        CHECK(a.players.count(5) && a.players.at(5).userid == 9);
        const std::string log = demo::format_chat_log(a.events, a.tick_interval);
        CHECK(log.find("[00:01] Friend: Привіт усім") != std::string::npos);
        CHECK(log.find("← Friend вийшов (Disconnect by user.)") != std::string::npos);
    }
    std::printf("  пакетів %d, голосових %zu, гравців %zu, попереджень %zu\n", a.packets_total, a.voice_packets.size(),
                a.players.size(), a.warnings.size());
    for (auto& w : a.warnings) std::printf("  попередження: %s\n", w.c_str());

    auto vr = voice::decode_voice(a);
    CHECK(vr.bad_packets == 0);
    CHECK(vr.speakers.size() == 3);
    if (vr.speakers.size() != 3) return;
    const auto& me = vr.speakers[0];
    CHECK(me.is_local);
    CHECK(me.name == "Recorder Юзер");
    CHECK(me.lost_frames == 0);
    // Очікувана кількість мовлення: 1.5 + 1.0 с
    CHECK_NEAR(me.seconds, 2.5, 0.1);
    CHECK(me.segments.size() == 2);
    if (me.segments.size() == 2) {
        // Перший сегмент: початок ~2.0 с + затримка доставки (до ~70 мс)
        const double s0 = me.segments[0].start / 48000.0;
        CHECK(s0 > 2.0 && s0 < 2.12);
        const double s1 = me.segments[1].start / 48000.0;
        CHECK(s1 > 7.0 && s1 < 7.12);
        // Частота 440 Гц має домінувати над 660 Гц (звук декодується потоково)
        const auto pcm = voice::decode_range(me, me.segments[0].start, me.segments[0].end());
        const double p440 = goertzel(pcm, 4800, 24000, 440, 48000);
        const double p660 = goertzel(pcm, 4800, 24000, 660, 48000);
        std::printf("  власний голос: 440Гц=%.3g 660Гц=%.3g\n", p440, p660);
        CHECK(p440 > 50 * p660);
    }
    const voice::SpeakerTrack* friend_track = nullptr;
    const voice::SpeakerTrack* late = nullptr;
    for (auto& s : vr.speakers) {
        if (s.name == "Friend") friend_track = &s;
        if (s.name == "LateJoiner") late = &s;
    }
    CHECK(friend_track && !friend_track->is_local);
    CHECK(late != nullptr);
    if (friend_track) {
        CHECK_NEAR(friend_track->seconds, 3.2, 0.1);
        const auto pcm = voice::decode_range(*friend_track, friend_track->segments[0].start,
                                             friend_track->segments[0].end());
        CHECK(goertzel(pcm, 4800, 24000, 660, 48000) > 50 * goertzel(pcm, 4800, 24000, 440, 48000));
    }
    std::filesystem::path wav = std::filesystem::temp_directory_path() / "gmdr_test_voice.wav";
    CHECK(voice::export_speaker_audio(me, wav, 0, -1));
    {
        audio::WavReader rd;
        CHECK(rd.open(wav, false));
        CHECK(rd.sample_rate() == 48000 && rd.channels() == 1);
        CHECK(rd.frames_available() == me.end_sample());
        // Вміст WAV збігається з потоковим декодуванням того самого відрізка
        std::vector<float> w(static_cast<size_t>(rd.frames_available()));
        CHECK(rd.read(w.data(), w.size()) == w.size());
        const auto ref = voice::decode_range(me, 0, me.end_sample());
        double max_diff = 0;
        for (size_t i = 0; i < w.size() && i < ref.size(); ++i)
            max_diff = std::max(max_diff, std::fabs(static_cast<double>(w[i]) - ref[i]));
        CHECK(max_diff < 2.0 / 32768.0);
    }
    std::filesystem::remove(wav);
    // Потокове декодування шматками (як під час рендеру) дає те саме, що й одним шматком,
    // а довільний стрибок усередину фрази — ті самі семпли після "розгону" декодера.
    {
        const int64_t total = me.end_sample();
        const auto whole = voice::decode_range(me, 0, total);
        voice::VoiceStream st(me);
        std::vector<float> chunks(static_cast<size_t>(total), 0.0f);
        for (int64_t p = 0; p < total; p += 4096)
            st.mix_into(p, static_cast<size_t>(std::min<int64_t>(4096, total - p)), chunks.data() + p);
        double md = 0;
        for (size_t i = 0; i < whole.size(); ++i) md = std::max(md, std::fabs(static_cast<double>(whole[i]) - chunks[i]));
        CHECK(md < 1e-6);
        CHECK(st.decode_errors() == 0);
    }
    // FLAC-експорт
    std::filesystem::path flac = std::filesystem::temp_directory_path() / "gmdr_test_voice.flac";
    std::string ferr;
    CHECK(voice::export_speaker_audio(me, flac, 0, -1, &ferr));
    CHECK(std::filesystem::exists(flac) && std::filesystem::file_size(flac) > 1000);
    std::filesystem::remove(flac);
    // Уривок для прослуховування: з першої фрази після from, сирий збігається з декодованим
    {
        const int64_t s0 = me.segments[0].start;
        const auto clip = audio::make_voice_clip(me, 0, nullptr, 0.5);
        const int64_t pad = 48000 * 15 / 100;
        CHECK(clip.start == std::max<int64_t>(0, s0 - pad));
        CHECK(clip.mono.size() == static_cast<size_t>(std::min(s0, pad) + 24000 + pad));
        const auto ref = voice::decode_range(me, clip.start, clip.start + static_cast<int64_t>(clip.mono.size()));
        double diff = 0;
        for (size_t i = 0; i < ref.size() && i < clip.mono.size(); ++i) diff = std::max(diff, std::fabs(ref[i] - clip.mono[i]) * 1.0);
        CHECK(diff < 1e-6);
        // Після кінця мовлення — з початку; з обробкою (підсилення) — гучніше
        audio::VoiceCleanup fx;
        fx.level = 2.0f;
        const auto loud = audio::make_voice_clip(me, me.end_sample() + 48000, &fx, 0.5);
        CHECK(loud.start == clip.start && loud.mono.size() == clip.mono.size());
        CHECK(rms_db(loud.mono, 0, 0.6) > rms_db(clip.mono, 0, 0.6) + 5.5);
        // Довгі паузи між фразами стиснуто: уривок коротший за відрізок демо, який він охоплює
        const auto longer = audio::make_voice_clip(me, 0, nullptr, 30.0);
        CHECK(longer.speech_seconds > 0 && longer.mono.size() <= static_cast<size_t>(me.end_sample() - longer.start + pad));
    }
    // Тривалість кадру Opus за TOC: CELT 20 мс (config 31), один кадр
    const uint8_t toc_celt20[] = {static_cast<uint8_t>((31 << 3) | 0)};
    CHECK(voice::opus_packet_samples_48k(toc_celt20, 1) == 960);
    const uint8_t toc_silk2x20[] = {static_cast<uint8_t>((1 << 3) | 1)};   // SILK 20 мс, два кадри
    CHECK(voice::opus_packet_samples_48k(toc_silk2x20, 1) == 1920);
}

static void test_tga() {
    std::printf("[tga]\n");
    frames::Image img;
    img.allocate(5, 3, frames::PixelLayout::BGR24);
    for (size_t i = 0; i < img.data.size(); ++i) img.data[i] = static_cast<uint8_t>(i * 7);
    auto same_pixels = [&](const frames::Image& a) {
        if (a.width != img.width || a.height != img.height) return false;
        for (int y = 0; y < img.height; ++y)
            if (std::memcmp(a.row(0, y), img.row(0, y), img.row_bytes(0)) != 0) return false;
        return true;
    };
    for (bool bottom_up : {false, true}) {
        for (bool rle : {false, true}) {
            auto bytes = frames::encode_tga(img, bottom_up, rle);
            frames::Image out;
            std::string err;
            CHECK(frames::decode_tga(bytes.data(), bytes.size(), out, &err));
            CHECK(same_pixels(out));
            int w = 0, h = 0, bpp = 0;
            size_t expected = 0;
            CHECK(frames::tga_header_info(bytes.data(), bytes.size(), w, h, bpp, expected));
            if (!rle) CHECK(expected == bytes.size() || expected + 26 == bytes.size());
            // Декодування "на місці" (без копії): кадр описується зсувом і кроком рядка
            frames::Image inplace;
            CHECK(frames::decode_image_file(bytes, ".tga", inplace, &err));
            CHECK(same_pixels(inplace));
            if (!rle) CHECK(bottom_up ? inplace.stride[0] < 0 : inplace.stride[0] > 0);
            // Обрізаний файл (гра ще пише) — помилка, а не читання за межами буфера
            std::vector<uint8_t> cut(bytes.begin(), bytes.begin() + static_cast<ptrdiff_t>(bytes.size() / 2));
            frames::Image bad;
            CHECK(!frames::decode_image_file(cut, ".tga", bad, &err));
        }
    }
}

static frames::Image solid_bgr(int w, int h, uint8_t v) {
    frames::Image img;
    img.allocate(w, h, frames::PixelLayout::BGR24);
    std::fill(img.data.begin(), img.data.end(), v);
    return img;
}

static void test_blender() {
    std::printf("[blender]\n");
    ThreadPool pool(3);
    frames::MotionBlender bl(4, 360.0, true, &pool);
    for (int k = 0; k < 4; ++k) {
        auto out = bl.push(solid_bgr(4, 2, static_cast<uint8_t>(k * 60)));   // 0, 60, 120, 180 -> середнє 90
        if (k < 3) CHECK(!out.has_value());
        else {
            CHECK(out.has_value());
            if (out) {
                CHECK(out->layout == frames::PixelLayout::BGR48);
                uint16_t v;
                std::memcpy(&v, out->row(0, 1), 2);
                CHECK_NEAR(v, 90.0 * 65535.0 / 255.0, 1.0);
            }
        }
    }
    frames::MotionBlender bl2(4, 180.0, false, &pool);   // шторка 180° — беремо 2 з 4
    for (int k = 0; k < 4; ++k) {
        auto out = bl2.push(solid_bgr(2, 1, static_cast<uint8_t>(k == 0 ? 100 : k == 1 ? 200 : 0)));
        if (k == 3) {
            CHECK(out.has_value());
            if (out) CHECK(out->layout == frames::PixelLayout::BGR24 && out->row(0, 0)[0] == 150);
        }
    }
    // Кадри JPEG змішуються прямо в YUV (площини різного розміру)
    frames::MotionBlender bl3(2, 360.0, true, &pool);
    for (int k = 0; k < 2; ++k) {
        frames::Image y;
        y.allocate(6, 4, frames::PixelLayout::YUV420P);
        for (int p = 0; p < 3; ++p)
            for (int r = 0; r < y.plane_height(p); ++r)
                std::memset(y.row(p, r), p == 0 ? (k ? 200 : 100) : 128, y.row_bytes(p));
        auto out = bl3.push(std::move(y));
        if (k == 1) {
            CHECK(out && out->layout == frames::PixelLayout::YUV420P16 && out->full_range);
            if (out) {
                CHECK(out->plane_width(1) == 3 && out->plane_height(1) == 2);
                uint16_t v;
                std::memcpy(&v, out->row(0, 3) + 10, 2);
                CHECK_NEAR(v, 150.0 * 65535.0 / 255.0, 1.0);
                std::memcpy(&v, out->row(2, 1) + 4, 2);
                CHECK_NEAR(v, 128.0 * 65535.0 / 255.0, 1.0);
            }
        }
    }
    // Кадр TGA з від'ємним кроком рядка змішується так само, як звичайний
    frames::Image a = solid_bgr(3, 2, 10), b = solid_bgr(3, 2, 30);
    std::memset(b.row(0, 0), 50, b.row_bytes(0));   // верхній рядок інший
    auto bu = frames::encode_tga(b, true, false);
    frames::Image b_up;
    std::string err;
    CHECK(frames::decode_image_file(bu, ".tga", b_up, &err) && b_up.stride[0] < 0);
    frames::MotionBlender bl4(2, 360.0, false, &pool);
    bl4.push(std::move(a));
    auto o4 = bl4.push(std::move(b_up));
    CHECK(o4 && o4->row(0, 0)[0] == 30 && o4->row(0, 1)[0] == 20);
}

// Точність перетворення кольору в кодері: RGB і YUV кадрів JPEG -> YUV BT.709
static void expect_709(int r, int g, int b, double& y, double& cb, double& cr) {
    const double R = r / 255.0, G = g / 255.0, B = b / 255.0;
    const double Y = 0.2126 * R + 0.7152 * G + 0.0722 * B;
    y = 16 + 219 * Y;
    cb = 128 + 224 * (B - Y) / 1.8556;
    cr = 128 + 224 * (R - Y) / 1.5748;
}

static void test_color_conversion() {
    std::printf("[color conversion]\n");
    const int colors[][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {40, 120, 200}, {250, 250, 250}, {16, 8, 4}};
    for (const char* codec : {"ffv1"}) {
        for (int depth : {8, 10}) {
            media::VideoEncoderSettings vs;
            vs.codec = codec;
            vs.width = 64;
            vs.height = 32;
            vs.bit_depth = depth;
            vs.chroma = 444;
            media::VideoEncoder enc;
            std::string err;
            if (!enc.open(vs, 64, 32, false, &err)) {
                std::printf("  пропуск %s: %s\n", codec, err.c_str());
                continue;
            }
            const AVPixelFormat outf = enc.output_pix_fmt();
            const double scale = depth > 8 ? 4.0 : 1.0;
            auto frame = media::make_frame();
            frame->format = outf;
            frame->width = 64;
            frame->height = 32;
            frame->colorspace = AVCOL_SPC_BT709;
            frame->color_range = AVCOL_RANGE_MPEG;
            frame->color_primaries = AVCOL_PRI_BT709;
            frame->color_trc = AVCOL_TRC_BT709;
            av_frame_get_buffer(frame.get(), 64);
            auto sample = [&](int p, int x, int yy) -> double {
                if (pix_fmt_bit_depth_16(outf)) return reinterpret_cast<uint16_t*>(frame->data[p] + yy * frame->linesize[p])[x] / scale;
                return frame->data[p][yy * frame->linesize[p] + x];
            };
            for (const auto& c : colors) {
                double ey, ecb, ecr;
                expect_709(c[0], c[1], c[2], ey, ecb, ecr);
                // Вхідні варіанти: BGR24, BGR24 знизу вгору (TGA), BGRA32, BGR48, YUV кадру JPEG
                std::vector<frames::Image> inputs;
                inputs.push_back(solid_bgr(64, 32, 0));
                for (int yy = 0; yy < 32; ++yy)
                    for (int x = 0; x < 64; ++x) {
                        uint8_t* px = inputs.back().row(0, yy) + x * 3;
                        px[0] = static_cast<uint8_t>(c[2]);
                        px[1] = static_cast<uint8_t>(c[1]);
                        px[2] = static_cast<uint8_t>(c[0]);
                    }
                {
                    auto bytes = frames::encode_tga(inputs[0], true, false);
                    frames::Image up;
                    frames::decode_image_file(bytes, ".tga", up, &err);
                    inputs.push_back(std::move(up));
                }
                {
                    frames::Image bgra;
                    bgra.allocate(64, 32, frames::PixelLayout::BGRA32);
                    for (int yy = 0; yy < 32; ++yy)
                        for (int x = 0; x < 64; ++x) {
                            uint8_t* px = bgra.row(0, yy) + x * 4;
                            px[0] = static_cast<uint8_t>(c[2]);
                            px[1] = static_cast<uint8_t>(c[1]);
                            px[2] = static_cast<uint8_t>(c[0]);
                            px[3] = 0;
                        }
                    inputs.push_back(std::move(bgra));
                }
                {
                    frames::Image b48;
                    b48.allocate(64, 32, frames::PixelLayout::BGR48);
                    for (int yy = 0; yy < 32; ++yy)
                        for (int x = 0; x < 64; ++x) {
                            uint16_t* px = reinterpret_cast<uint16_t*>(b48.row(0, yy)) + x * 3;
                            px[0] = static_cast<uint16_t>(c[2] * 257);
                            px[1] = static_cast<uint16_t>(c[1] * 257);
                            px[2] = static_cast<uint16_t>(c[0] * 257);
                        }
                    inputs.push_back(std::move(b48));
                }
                {
                    // JPEG: YUV BT.601, повний діапазон, 4:2:0
                    const double R = c[0], G = c[1], B = c[2];
                    const double Y = 0.299 * R + 0.587 * G + 0.114 * B;
                    const double Cb = 128 + (B - Y) / 1.772, Cr = 128 + (R - Y) / 1.402;
                    frames::Image j;
                    j.allocate(64, 32, frames::PixelLayout::YUV420P);
                    const uint8_t vals[3] = {static_cast<uint8_t>(std::lround(std::clamp(Y, 0.0, 255.0))),
                                             static_cast<uint8_t>(std::lround(std::clamp(Cb, 0.0, 255.0))),
                                             static_cast<uint8_t>(std::lround(std::clamp(Cr, 0.0, 255.0)))};
                    for (int p = 0; p < 3; ++p)
                        for (int yy = 0; yy < j.plane_height(p); ++yy) std::memset(j.row(p, yy), vals[p], j.row_bytes(p));
                    inputs.push_back(std::move(j));
                }
                const char* names[] = {"BGR24", "TGA знизу вгору", "BGRA32", "BGR48", "JPEG YUV"};
                for (size_t i = 0; i < inputs.size(); ++i) {
                    const bool ok = enc.convert(inputs[i], frame.get(), &err);
                    CHECK(ok);
                    if (!ok) continue;
                    const double tol = i == 4 ? 3.0 : 1.6;   // JPEG: округлення 8-бітного YUV
                    const double gy = sample(0, 20, 10), gcb = sample(1, 20, 10), gcr = sample(2, 20, 10);
                    const bool good = std::abs(gy - ey) <= tol && std::abs(gcb - ecb) <= tol && std::abs(gcr - ecr) <= tol;
                    if (!good)
                        std::printf("  %s %d біт, колір (%d,%d,%d): Y=%.1f Cb=%.1f Cr=%.1f, очікувалось %.1f %.1f %.1f\n",
                                    names[i], depth, c[0], c[1], c[2], gy, gcb, gcr, ey, ecb, ecr);
                    CHECK(good);
                }
            }
        }
    }
}


static void test_subtitles_and_sizes() {
    std::printf("[subtitles/target size]\n");
    CHECK(render::srt_timestamp(0) == "00:00:00,000");
    CHECK(render::srt_timestamp(3723.4567) == "01:02:03,457");
    // Два гравці: A говорить 1.0–2.0 і (після короткої паузи) 2.3–3.0, B — 2.5–4.0
    voice::SpeakerTrack a, b;
    auto seg = [](double s0, double s1) {
        voice::VoiceSegment g;
        g.start = static_cast<int64_t>(s0 * 48000);
        g.length = static_cast<int64_t>((s1 - s0) * 48000);
        return g;
    };
    a.segments = {seg(1.0, 2.0), seg(2.3, 3.0)};
    b.segments = {seg(2.5, 4.0)};
    const std::string srt = render::make_speaker_srt({{&a, "A"}, {&b, "B"}}, 0, 10.0);
    CHECK(srt.find("00:00:01,000 --> 00:00:02,500\nA\n") != std::string::npos);   // паузу 0.3 с об'єднано
    CHECK(srt.find("00:00:02,500 --> 00:00:03,000\nA, B\n") != std::string::npos);
    CHECK(srt.find("00:00:03,000 --> 00:00:04,000\nB\n") != std::string::npos);
    // Зсув початку відео і обрізання кінцем
    const std::string srt2 = render::make_speaker_srt({{&b, "B"}}, 48000 * 2, 1.0);
    CHECK(srt2.find("00:00:00,500 --> 00:00:01,000\nB\n") != std::string::npos);
    // Бітрейт під розмір: 10 МБ на 60 с зі звуком 128 кбіт/с
    const int64_t br = render::bitrate_for_target_size(10, 60, 128000);
    CHECK(br > 1000000 && br < 1300000);
    CHECK(render::bitrate_for_target_size(1, 3600, 128000) == 150000);   // не менше мінімуму
    CHECK(game::window_mode_from_string("offscreen") == game::WindowMode::Offscreen);
    CHECK(game::window_mode_from_string("behind") == game::WindowMode::Behind);
    CHECK(game::window_mode_from_string("щось") == game::WindowMode::Normal);
}

static void test_frame_files() {
    std::printf("[frame files / live sequence reader]\n");
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "gmdr_test_frames";
    fs::remove_all(dir);
    fs::create_directories(dir);
    frames::Image img;
    img.allocate(8, 4, frames::PixelLayout::BGR24);
    auto frame_bytes = [&](int k) {
        std::fill(img.data.begin(), img.data.end(), static_cast<uint8_t>(k * 10));
        return frames::encode_tga(img, true, false);
    };
    // Обрізаний файл не видаляється, повний — видаляється тим самим дескриптором
    {
        auto bytes = frame_bytes(1);
        const fs::path f = dir / "one.tga";
        write_file_text(f, std::string(bytes.begin(), bytes.begin() + static_cast<ptrdiff_t>(bytes.size() - 10)));
        frames::FrameFileRead rd;
        CHECK(frames::read_frame_file(f, ".tga", true, rd) == frames::ReadStatus::Ok);
        CHECK(!rd.complete && !rd.deleted && fs::exists(f));
        write_file_text(f, std::string(bytes.begin(), bytes.end()));
        CHECK(frames::read_frame_file(f, ".tga", true, rd) == frames::ReadStatus::Ok);
        CHECK(rd.complete && rd.size == bytes.size());
        CHECK(rd.deleted && !fs::exists(f));
        CHECK(frames::read_frame_file(f, ".tga", true, rd) == frames::ReadStatus::Missing);
    }
    // "Живий" режим: гра пише кадри по одному, читач віддає їх по порядку і видаляє файли
    frames::SequenceOptions so;
    so.dir = dir;
    so.prefix = "f";
    so.live = true;
    so.delete_after_read = true;
    so.decode_threads = 3;
    frames::FrameSequenceReader reader(so);
    const int total = 40;
    std::thread producer([&] {
        for (int k = 0; k < total; ++k) {
            auto bytes = frame_bytes(k % 25);
            const fs::path tmp = dir / std::format("f{:04d}.tmp", k);
            write_file_text(tmp, std::string(bytes.begin(), bytes.end()));
            fs::rename(tmp, dir / std::format("f{:04d}.tga", k));
            std::this_thread::sleep_for(std::chrono::milliseconds(k % 7 == 0 ? 30 : 3));
        }
        reader.set_producer_done();
    });
    int got = 0;
    bool order_ok = true, pixels_ok = true;
    frames::Image out;
    for (;;) {
        const auto w = reader.next_for(out, 2000);
        if (w == frames::FrameSequenceReader::Wait::End) break;
        if (w == frames::FrameSequenceReader::Wait::Timeout) break;
        if (out.index != got) order_ok = false;
        if (out.row(0, 0)[0] != static_cast<uint8_t>((got % 25) * 10)) pixels_ok = false;
        ++got;
    }
    producer.join();
    CHECK(got == total);
    CHECK(order_ok);
    CHECK(pixels_ok);
    CHECK(reader.skipped() == 0);
    int left = 0;
    for (auto& e : fs::directory_iterator(dir)) left += e.path().extension() == ".tga";
    CHECK(left == 0);
    fs::remove_all(dir);
}

// Кадри каналом (frames/frame_pipe.hpp): "гра" пише кожен кадр так само, як startmovie, — відкриває
// "файл" <назва>0000.tga за назвою для startmovie, пише і закриває, — а читач віддає кадри по порядку,
// не створюючи на диску жодного файлу кадру.
static void test_frame_pipe() {
    std::printf("[frame pipe]\n");
    namespace fs = std::filesystem;
    if (!frames::frame_pipes_supported()) {
        std::printf("  канали не підтримуються — пропускаю\n");
        return;
    }
    const fs::path dir = fs::temp_directory_path() / "gmdr_test_pipe";
    fs::remove_all(dir);
    fs::create_directories(dir);
    frames::Image img;
    img.allocate(64, 36, frames::PixelLayout::BGR24);
    auto frame_bytes = [&](int k) {
        std::fill(img.data.begin(), img.data.end(), static_cast<uint8_t>(k * 7));
        return frames::encode_tga(img, true, false);
    };
    const std::string prefix = "gmdr_t" + make_unique_id() + "_";
    frames::PipeOptions po;
    po.dir = dir;
    po.prefix = prefix;
    po.decode_threads = 2;
    po.max_buffered = 3;   // мало місця: гра чекатиме на записі, поки тест не забере кадри
    auto reader = std::make_unique<frames::FramePipeReader>(po);
    CHECK(reader->ok());
    if (!reader->ok()) {
        std::printf("  %s\n", reader->last_error().c_str());
        return;
    }
    const std::string movie = frames::pipe_movie_name(path_to_utf8(dir), prefix);   // як отримає гра
    std::mutex fail_mutex;
    std::string fail_log;
    auto note_failure = [&](const std::string& what) {
#ifdef _WIN32
        const unsigned long code = GetLastError();
#else
        const unsigned long code = 0;
#endif
        const int e = errno;
        std::lock_guard lock(fail_mutex);
        fail_log += std::format("  не відкрилось: {} (errno {}, код {})\n", what, e, code);
    };
    auto write = [&](int k, const std::vector<uint8_t>& bytes, size_t n) {
        std::FILE* f = audio::open_file_utf8(path_from_utf8(std::format("{}{:04d}.tga", movie, k)), "wb");
        if (!f) {
            note_failure(std::format("кадр {} ({} байт)", k, n));
            return false;
        }
        if (n > 0) std::fwrite(bytes.data(), 1, n, f);
        std::fclose(f);
        return true;
    };
    const int total = 30, skip = 7, cut = 12, probe = 4;
    std::atomic<int> write_failures{0};
    std::vector<uint8_t> wav_expected;
    std::thread producer([&] {
#ifdef _WIN32
        // Звук — як у рушії: заголовок одним відкриттям, далі кожен шматок — знову відкрити й дописати
        auto wav_write = [&](const std::vector<uint8_t>& b, const char* mode) {
            std::FILE* f = audio::open_file_utf8(path_from_utf8(movie + ".wav"), mode);
            if (!f) {
                note_failure(std::format("звук, {} байт, \"{}\"", b.size(), mode));
                ++write_failures;
                return;
            }
            std::fwrite(b.data(), 1, b.size(), f);
            std::fclose(f);
            wav_expected.insert(wav_expected.end(), b.begin(), b.end());
        };
        std::vector<uint8_t> head(44, 0);
        std::memcpy(head.data(), "RIFF", 4);
        wav_write(head, "wb");
#endif
        for (int k = 0; k < total; ++k) {
            if (k == skip) continue;   // гра пропустила номер
            const auto bytes = frame_bytes(k);
            if (k == probe && !write(k, bytes, 0)) ++write_failures;   // відкрила й закрила, нічого не записавши
            if (!write(k, bytes, k == cut ? bytes.size() / 2 : bytes.size())) ++write_failures;
#ifdef _WIN32
            wav_write(std::vector<uint8_t>(400 + k, static_cast<uint8_t>(k)), "r+b");
#endif
        }
    });
    int got = 0;
    int64_t last = -1;
    bool order_ok = true, pixels_ok = true;
    frames::Image out;
    bool producer_joined = false;
    for (int guard = 0; guard < 1000; ++guard) {
        const auto w = reader->next_for(out, 100);
        if (w == frames::FrameSource::Wait::End) break;
        if (w == frames::FrameSource::Wait::Timeout) {
            if (!producer_joined) {
                producer.join();
                producer_joined = true;
                reader->set_producer_done();
            }
            continue;
        }
        if (out.index <= last) order_ok = false;
        last = out.index;
        if (out.row(0, 0)[0] != static_cast<uint8_t>(out.index * 7)) pixels_ok = false;
        ++got;
        // Повільний "кодер": канал має тримати гру, а не губити кадри
        if (got < 5) std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    if (!producer_joined) {
        producer.join();
        reader->set_producer_done();
    }
    CHECK(write_failures == 0);
    if (!fail_log.empty()) std::printf("%s", fail_log.c_str());
    CHECK(got == total - 2);   // без пропущеного номера й обрізаного кадру
    CHECK(order_ok);
    CHECK(pixels_ok);
    CHECK(reader->skipped() == 2);
    CHECK(reader->saw_any_file());
    CHECK(reader->audio_connected());
    CHECK(reader->audio_flowing());
    CHECK(reader->bytes_received() > 0);
    reader.reset();
    // На диску — жодного файлу кадру (FIFO прибрано разом із читачем)
    int left = 0;
    for (auto& e : fs::directory_iterator(dir)) left += e.path().extension() == ".tga";
    CHECK(left == 0);
#ifdef _WIN32
    // Звук: усе, що "гра" писала в канал, по порядку — у звичайному WAV у тимчасовій папці
    auto wav = read_file_bytes(dir / path_from_utf8(prefix + ".wav"));
    CHECK(wav && *wav == wav_expected);
#endif
    // Гра так і не відкрила канал: читач закривається без очікування
    {
        frames::PipeOptions po2 = po;
        po2.prefix = "gmdr_t" + make_unique_id() + "_";
        frames::FramePipeReader idle(po2);
        CHECK(idle.ok());
        CHECK(idle.next_for(out, 50) == frames::FrameSource::Wait::Timeout);
        idle.set_producer_done();
        CHECK(idle.next_for(out, 2000) == frames::FrameSource::Wait::End);
        CHECK(!idle.saw_any_file());
    }
    fs::remove_all(dir);
}

// Вибір способу передачі кадрів і пам'ять про невдачу каналу з конкретною грою
static void test_frame_transport_choice() {
    std::printf("[frame transport]\n");
    namespace fs = std::filesystem;
    const fs::path exe = fs::temp_directory_path() / ("gmdr_fake_exe_" + make_unique_id());
    write_file_text(exe, "exe");
    render::RenderSettings s;
    CHECK(render::normalize_frame_transport("щось") == "auto");
    CHECK(render::normalize_frame_transport(" PIPE ") == "pipe");
    s.frame_transport = "files";
    CHECK(!render::choose_frame_transport(s, exe).pipe);
    if (frames::frame_pipes_supported()) {
        s.frame_transport = "auto";
        auto c = render::choose_frame_transport(s, exe);
        CHECK(c.pipe && !c.strict);
        render::remember_pipe_failure(exe, "тест.");
        c = render::choose_frame_transport(s, exe);
        CHECK(!c.pipe && c.note.find("тест)") != std::string::npos);
        s.frame_transport = "pipe";   // явний вибір — канал попри попередню невдачу
        c = render::choose_frame_transport(s, exe);
        CHECK(c.pipe && c.strict);
        write_file_text(exe, "exe, оновлена гра");   // інший розмір exe — пробуємо канал знову
        s.frame_transport = "auto";
        CHECK(render::choose_frame_transport(s, exe).pipe);
        render::remember_pipe_failure(exe, "ще раз");
        render::forget_pipe_failure(exe);
        CHECK(render::choose_frame_transport(s, exe).pipe);
        s.manual_mode = true;   // startmovie вводить сам гравець — лише файли
        CHECK(!render::choose_frame_transport(s, exe).pipe);
    }
    fs::remove(exe);
}

// Фазинг: демо приходять ззовні, тож пошкоджений файл не повинен ронити програму.
static void test_fuzz_demo(const std::filesystem::path& demo_path) {
    std::printf("[fuzz %s]\n", demo_path.filename().string().c_str());
    auto orig = read_file_bytes(demo_path);
    CHECK(orig.has_value());
    if (!orig) return;
    std::mt19937 rng(12345);
    int parsed = 0, rejected = 0;
    for (int iter = 0; iter < 120; ++iter) {
        std::vector<uint8_t> d = *orig;
        const int mode = iter % 4;
        if (mode == 0) {   // випадкові байти
            for (int k = 0; k < 40; ++k) d[1072 + rng() % (d.size() - 1072)] = static_cast<uint8_t>(rng());
        } else if (mode == 1) {   // обрізання
            d.resize(1072 + rng() % (d.size() - 1072));
        } else if (mode == 2) {   // зіпсовані довжини команд (великі числа)
            for (int k = 0; k < 10; ++k) {
                const size_t at = 1072 + rng() % (d.size() - 1076);
                const uint32_t v = rng();
                std::memcpy(d.data() + at, &v, 4);
            }
        } else {   // зіпсований заголовок
            for (int k = 0; k < 8; ++k) d[rng() % 1072] = static_cast<uint8_t>(rng());
        }
        try {
            demo::DemoFile f(std::move(d));
            auto a = demo::analyze_demo(f);
            auto v = voice::decode_voice(a);
            for (const auto& sp : v.speakers) {
                const auto pcm = voice::decode_range(sp, 0, std::min<int64_t>(sp.end_sample(), 48000 * 3));
                (void)pcm;
            }
            ++parsed;
        } catch (const std::exception&) {
            ++rejected;   // відмова з поясненням — нормальна реакція
        }
    }
    std::printf("  розібрано %d, відхилено %d — без падінь\n", parsed, rejected);
    CHECK(parsed + rejected == 120);
}

// Лічильник "Минуло" зупиняється, коли завдання завершилося
struct SleepJob final : render::Job {
    std::string name() const override { return "test"; }
    void run() override {
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        succeed("ok");
    }
};

static void test_job_elapsed() {
    std::printf("[job elapsed]\n");
    SleepJob j;
    j.start();
    j.wait();
    const double a = j.progress().elapsed;
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    const double b = j.progress().elapsed;
    CHECK(j.state() == render::JobState::Succeeded);
    CHECK(a >= 0.05 && a < 1.0);
    CHECK_NEAR(a, b, 1e-9);
}

// Додаткові версії: вирізання центру кадру і налаштування пресетів
static void test_extra_versions() {
    std::printf("[extra versions]\n");
    auto c = media::center_crop(1920, 1080, 9.0 / 16.0);
    CHECK(c.w == 608 && c.h == 1080 && c.x == 656 && c.y == 0);
    c = media::center_crop(1080, 1920, 16.0 / 9.0);   // навпаки: смуга по висоті
    CHECK(c.w == 1080 && c.h == 608 && c.x == 0 && c.y == 656);
    c = media::center_crop(1280, 720, 0);
    CHECK(c.w == 1280 && c.h == 720 && c.x == 0 && c.y == 0);
    render::EncodeSettings main;
    main.video.width = 2560;
    main.video.height = 1440;
    main.video.fps = {60, 1};
    main.output_path = "D:/відео/бій.mp4";
    auto x = render::make_extra_outputs("vertical, discord,нема,master", main, 60.0);
    CHECK(x.size() == 3);
    if (x.size() == 3) {
        CHECK(x[0].video.width == 1080 && x[0].video.height == 1920 && x[0].video.crop_aspect > 0.56);
        CHECK(x[0].output_path.ends_with("бій_vertical.mp4"));
        CHECK(x[1].video.height == 720 && x[1].video.width == 1280 && x[1].video.bitrate > 0);
        // 9.5 МБ за хвилину: відео + звук 128 кбіт/с не більше 9.5 МБ
        CHECK((x[1].video.bitrate + x[1].audio.bitrate) * 60 / 8 <= static_cast<int64_t>(9.5 * 1024 * 1024));
        CHECK(x[2].video.codec == "prores_ks" && x[2].output_path.ends_with("бій_master.mov") &&
              x[2].audio.codec == "pcm_s16le");
    }
    main.output_path = "D:/кадри/f_%05d.png";   // послідовність зображень
    x = render::make_extra_outputs("480p", main, 0);
    CHECK(x.size() == 1 && x[0].output_path.ends_with("video_480p.mp4") && x[0].video.height == 480 &&
          x[0].video.width == 854);
    std::string bad;
    CHECK(render::valid_version_ids("discord, 480p", &bad));
    CHECK(!render::valid_version_ids("discord,4k", &bad) && bad == "4k");
}

// Обкладинка, GIF і WebP з готового відео
static void test_derived_outputs() {
    std::printf("[thumbnail, gif, webp]\n");
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "gmdr_test_derived";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string video = path_to_utf8(dir / path_from_utf8("кліп.mp4"));
    // 2 с відео 160×90, 30 кадрів/с: смуга, що рухається
    {
        media::Muxer mux;
        media::VideoEncoder enc;
        media::VideoEncoderSettings vs;
        vs.codec = "libx264";
        vs.fps = {30, 1};
        vs.preset = "ultrafast";
        std::string err;
        const bool opened = mux.open(video, "", &err) && enc.open(vs, 160, 90, mux.needs_global_header(), &err);
        if (!opened) {
            std::printf("  пропуск: %s\n", err.c_str());
            return;
        }
        const int st = mux.add_stream(enc.context(), "t");
        CHECK(mux.write_header(true, false, &err));
        auto sink = [&](AVPacket* pk) { return mux.write_packet(st, pk, enc.context()->time_base); };
        for (int f = 0; f < 60; ++f) {
            frames::Image img;
            img.allocate(160, 90, frames::PixelLayout::BGR24);
            for (int y = 0; y < 90; ++y)
                for (int x = 0; x < 160; ++x) {
                    uint8_t* px = img.row(0, y) + x * 3;
                    const bool bar = std::abs(x - f * 2) < 10;
                    px[0] = bar ? 255 : 40;
                    px[1] = static_cast<uint8_t>(y * 2);
                    px[2] = bar ? 255 : 90;
                }
            CHECK(enc.encode(img, f, sink, &err));
        }
        CHECK(enc.flush(sink, &err) && mux.finish(&err));
    }
    std::string err;
    const std::string jpg = path_to_utf8(dir / path_from_utf8("кліп.jpg"));
    CHECK(render::make_thumbnail(video, jpg, -1, 1280, 720, &err));
    media::MediaFileInfo info;
    CHECK(media::probe_media_file(jpg, info, &err) && info.has_video);
    const std::string gif = path_to_utf8(dir / path_from_utf8("кліп.gif"));
    CHECK(render::make_animation(video, gif, render::AnimFormat::Gif, 1.0, 480, 10, &err));
    CHECK(media::probe_media_file(gif, info, &err) && info.has_video);
    if (avcodec_find_encoder_by_name("libwebp_anim")) {
        const std::string webp = path_to_utf8(dir / path_from_utf8("кліп.webp"));
        CHECK(render::make_animation(video, webp, render::AnimFormat::WebP, 1.0, 640, 10, &err));
        auto bytes = read_file_bytes(path_from_utf8(webp));
        CHECK(bytes && bytes->size() > 100 && std::memcmp(bytes->data(), "RIFF", 4) == 0 &&
              std::memcmp(bytes->data() + 8, "WEBPVP8X", 8) == 0);
    }
    // Не відео — помилка з поясненням, а не падіння
    write_file_text(dir / path_from_utf8("не_відео.mp4"), "hello");
    CHECK(!render::make_thumbnail(path_to_utf8(dir / path_from_utf8("не_відео.mp4")), jpg, -1, 640, 360, &err) && !err.empty());
    fs::remove_all(dir);
}

// ZIP для звіту: CRC32, заголовки, імена UTF-8; прибирання шляху до профілю
static void test_report_zip() {
    std::printf("[report zip]\n");
    namespace fs = std::filesystem;
    const char* check = "123456789";
    CHECK(crc32(reinterpret_cast<const uint8_t*>(check), 9) == 0xCBF43926u);
    const fs::path zp = fs::temp_directory_path() / path_from_utf8("gmdr_test_звіт.zip");
    {
        ZipWriter z;
        std::string err;
        CHECK(z.open(zp, &err));
        CHECK(z.add("журнал.txt", "рядок 1\nрядок 2\n"));
        CHECK(z.add("порожній.txt", ""));
        CHECK(!z.add_file("немає.txt", fs::temp_directory_path() / path_from_utf8("gmdr_такого_файлу_нема")));
        CHECK(z.close(&err));
    }
    auto bytes = read_file_bytes(zp);
    CHECK(bytes.has_value());
    if (bytes) {
        const auto& b = *bytes;
        auto u16 = [&](size_t o) { return static_cast<uint32_t>(b[o] | (b[o + 1] << 8)); };
        auto u32 = [&](size_t o) { return u16(o) | (u16(o + 2) << 16); };
        CHECK(u32(0) == 0x04034b50 && (u16(6) & 0x0800));   // перший файл, імена UTF-8
        const size_t eocd = b.size() - 22;
        CHECK(u32(eocd) == 0x06054b50 && u16(eocd + 10) == 2);   // два записи в каталозі
        const size_t cd = u32(eocd + 16);
        CHECK(u32(cd) == 0x02014b50 && u32(cd + 16) == crc32(reinterpret_cast<const uint8_t*>("рядок 1\nрядок 2\n"),
                                                               std::strlen("рядок 1\nрядок 2\n")));
        const std::string name(reinterpret_cast<const char*>(&b[cd + 46]), u16(cd + 28));
        CHECK(name == "журнал.txt");
    }
    std::error_code ec;
    fs::remove(zp, ec);
    // Шлях до профілю — у звичайному вигляді, з прямими скісними і в JSON (подвоєні \)
    const std::string prof = "C:\\Users\\Олена";
    const std::string text = "лог: c:\\users\\Олена\\Desktop\\a.dem; C:/Users/Олена/x; {\"p\":\"C:\\\\Users\\\\Олена\\\\y\"}";
    const std::string an = render::anonymize_paths(text, prof);
    CHECK(an.find("Олена") == std::string::npos);   // регістр латинської частини шляху не важить
    CHECK(an.find("%USERPROFILE%\\Desktop") != std::string::npos && an.find("%USERPROFILE%/x") != std::string::npos);
    CHECK(render::default_report_name().ends_with(".zip"));
    CHECK(render::system_summary().find("GMod Demo Render") != std::string::npos);
}

// Асоціація .dem: що пишеться в реєстр. Сам запис — лише з GMDR_TEST_REGISTRY=1 (у CI, на
// одноразовій машині) і в окремий розділ HKCU\Software\GModDemoRenderTest, не в справжній Classes.
static void test_dem_association() {
    std::printf("[dem association]\n");
    const std::filesystem::path exe = path_from_utf8("C:/Програми/GMDR/gmdr.exe");
    const auto vals = dem_association_values(exe);
    bool cmd_ok = false, openwith = false;
    for (const auto& v : vals) {
        if (v.key == std::string(kDemProgId) + "\\shell\\open\\command")
            cmd_ok = v.value == "\"" + path_to_utf8(exe) + "\" \"%1\"";
        if (v.key == ".dem\\OpenWithProgids" && v.name == kDemProgId) openwith = true;
    }
    CHECK(cmd_ok && openwith);
#ifdef _WIN32
    if (!std::getenv("GMDR_TEST_REGISTRY")) {
        std::printf("  запис у реєстр пропущено (GMDR_TEST_REGISTRY не задано)\n");
        return;
    }
    const std::string root = "Software\\GModDemoRenderTest\\Classes";
    std::string err;
    CHECK(!dem_association_registered(exe, root));
    CHECK(register_dem_association(exe, &err, root));
    CHECK(dem_association_registered(exe, root));
    CHECK(!dem_association_registered(path_from_utf8("D:/інша/gmdr.exe"), root));   // інша копія програми
    CHECK(unregister_dem_association(&err, root));
    CHECK(!dem_association_registered(exe, root));
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\GModDemoRenderTest");
#endif
}

// Дія після рендеру: розбір параметра (саму дію тести не виконують)
static void test_power_action() {
    std::printf("[power action]\n");
    CHECK(parse_power_action("shutdown") == PowerAction::Shutdown);
    CHECK(parse_power_action("sleep") == PowerAction::Sleep);
    CHECK(parse_power_action("") == PowerAction::None && parse_power_action("none") == PowerAction::None);
    CHECK(!parse_power_action("reboot").has_value());
    CHECK(std::string(power_action_name(PowerAction::Shutdown)) == "вимкнути ПК");
    std::string err;
    CHECK(do_power_action(PowerAction::None, &err));   // "нічого" — завжди успіх і без дій
    CHECK(power_countdown_seconds() >= 1);
}

// Перевірка оновлень: порівняння версій і розбір відповіді GitHub (без мережі)
static void test_update_check() {
    std::printf("[update check]\n");
    CHECK(compare_versions("1.2.0", "1.2") == 0);
    CHECK(compare_versions("v1.10.0", "1.9.9") > 0);
    CHECK(compare_versions("1.2.1", "1.10") < 0);
    CHECK(compare_versions("2.0-beta", "1.99") > 0);
    std::string err;
    auto r = parse_latest_release(R"({"tag_name":"v1.3.0","html_url":"https://github.com/FosForNyak/DemoGmodRender/releases/tag/v1.3.0",
        "published_at":"2026-10-01T12:00:00Z","body":"## Що нового\r\n\r\n- Швидше\r\n- Краще"})", &err);
    CHECK(r && r->version == "1.3.0" && r->published == "2026-10-01" && r->url.ends_with("v1.3.0"));
    CHECK(r && r->notes == "## Що нового\n- Швидше\n- Краще\n");
    CHECK(!parse_latest_release(R"({"message":"Not Found"})", &err) && !err.empty());
    CHECK(!parse_latest_release("не json", &err));
}

// Підписи «хто говорить» на кадрі: з'являються лише під час мовлення, праворуч унизу, у всіх форматах
static void test_speaker_overlay() {
    std::printf("[speaker overlay]\n");
    const std::string font = render::SpeakerOverlay::find_font();
    if (font.empty()) {
        std::printf("  пропуск: немає системного шрифту\n");
        return;
    }
    render::SpeakerOverlay ov;
    std::string err;
    CHECK(!ov.init({}, 640, 360, font, &err));   // нема кого показувати
    CHECK(ov.init({{"Олег", {{1.0, 3.0}}}, {"Friend", {{2.0, 4.0}}}}, 640, 360, font, &err));
    CHECK(ov.label_count() == 2);
    auto changed = [](const frames::Image& a, const frames::Image& b, int p, int x0, int x1, int y0, int y1) {
        int n = 0;
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x)
                if (std::memcmp(a.row(p, y) + x, b.row(p, y) + x, 1) != 0) ++n;
        return n;
    };
    for (auto layout : {frames::PixelLayout::BGR24, frames::PixelLayout::BGR48, frames::PixelLayout::YUV420P,
                        frames::PixelLayout::YUV420P16}) {
        frames::Image base, img;
        base.allocate(640, 360, layout);
        img.allocate(640, 360, layout);
        for (int p = 0; p < base.planes(); ++p)
            for (int y = 0; y < base.plane_height(p); ++y) {
                std::memset(base.row(p, y), 100, base.row_bytes(p));
                std::memset(img.row(p, y), 100, img.row_bytes(p));
            }
        ov.draw(img, 0.5);   // ще ніхто не говорить
        CHECK(changed(base, img, 0, 0, static_cast<int>(img.row_bytes(0)), 0, 360) == 0);
        ov.draw(img, 2.5);   // говорять обоє
        const int right = changed(base, img, 0, static_cast<int>(img.row_bytes(0)) / 2, static_cast<int>(img.row_bytes(0)), 150, 300);
        const int left = changed(base, img, 0, 0, static_cast<int>(img.row_bytes(0)) / 2, 0, 360);
        CHECK(right > 500 && left == 0);
    }
    // Кадр іншого розміру (гра віддала менший) — без виходу за межі
    frames::Image small;
    small.allocate(100, 60, frames::PixelLayout::BGRA32);
    ov.draw(small, 2.5);
    // Відрізки з доріжок: коротші за 0.25 с відкидаються, близькі — зливаються
    voice::SpeakerTrack tr;
    tr.segments = {{48000, 48000}, {100000, 4800}, {106000, 48000}};   // 1-2 с; 0.1 с; 2.2-3.2 с
    const auto sp = render::SpeakerOverlay::speakers_for({{&tr, "X"}}, 0, 10.0, 0.0);
    CHECK(sp.size() == 1 && sp[0].spans.size() == 1 && std::abs(sp[0].spans[0].a - 1.0) < 0.01 &&
          std::abs(sp[0].spans[0].b - (106000 + 48000) / 48000.0) < 0.01);
}

// Пакет для монтажу: проєкт xmeml (Premiere / DaVinci Resolve)
static void test_edit_package() {
    std::printf("[edit package]\n");
    CHECK(render::fcp_path_url("C:\\Відео\\a b.mp4") == "file://localhost/C%3a/%d0%92%d1%96%d0%b4%d0%b5%d0%be/a%20b.mp4");
    CHECK(render::safe_file_name("Голос: Олег (STEAM_0:1:2)") == "Голос_ Олег (STEAM_0_1_2)");
    CHECK(render::safe_file_name(" ?*. ") == "__");      // крапка в кінці імені у Windows зникає
    CHECK(render::safe_file_name("  . ") == "доріжка");
    render::EditProject p;
    p.name = "бій <1>";
    p.video_path = "C:/v/бій.mp4";
    p.fps_num = 60000;
    p.fps_den = 1001;
    p.frames = 600;
    p.stems = {{"Гра", "C:/v/бій_монтаж/01 Гра.wav"}, {"Олег & Ко", "C:/v/бій_монтаж/02 Олег.wav"}};
    p.markers = {{0, 2, "Початок"}, {2.0, 10, "Бій"}};
    const std::string x = render::make_fcp7_xml(p);
    CHECK(x.find("<timebase>60</timebase><ntsc>TRUE</ntsc>") != std::string::npos);   // 59.94
    CHECK(x.find("<name>бій &lt;1&gt;</name>") != std::string::npos);
    CHECK(x.find("<name>Олег &amp; Ко</name>") != std::string::npos);
    CHECK(x.find("<in>120</in><out>-1</out>") != std::string::npos);                  // маркер на 2 с
    auto count = [&](const std::string& what) {
        size_t n = 0;
        for (size_t pos = 0; (pos = x.find(what, pos)) != std::string::npos; ++pos) ++n;
        return n;
    };
    CHECK(count("<track>") == 4);   // відео, мікс з відео, два WAV
    CHECK(count("<clipitem ") == 4 && count("</clipitem>") == 4);
}

// Бібліотека демо: заголовки, підтеки, пошук; не-демо — у списку з поясненням
static void test_demo_library(const std::filesystem::path& demos) {
    std::printf("[demo library]\n");
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "gmdr_test_library";
    fs::remove_all(dir);
    const fs::path old = dir / path_from_utf8("старі");   // кирилиця в шляхах — лише через UTF-8
    fs::create_directories(old / "2025");
    std::error_code ec;
    fs::copy_file(demos / "test24.dem", dir / path_from_utf8("бій.dem"), ec);
    fs::copy_file(demos / "test20c.dem", old / "2025" / path_from_utf8("раунд.DEM"), ec);
    write_file_text(dir / path_from_utf8("не демо.dem"), "просто текст");
    write_file_text(dir / path_from_utf8("нотатки.txt"), "не демо");
    const auto lib = demo::scan_demo_library({dir, old});   // та сама тека двічі — без дублікатів
    CHECK(lib.size() == 3);
    int ok = 0, bad = 0;
    for (const auto& e : lib) {
        if (e.error.empty() && e.map == "gm_construct" && e.seconds > 1 && e.size > 1000) ++ok;
        if (!e.error.empty() && e.name == "не демо.dem") ++bad;
    }
    CHECK(ok == 2 && bad == 1);
    if (ok != 2 || bad != 1)
        for (const auto& x : lib)
            std::printf("  %s | %s | %.1f | %llu | %s\n", x.name.c_str(), x.map.c_str(), x.seconds,
                        static_cast<unsigned long long>(x.size), x.error.c_str());
    demo::LibraryEntry e;
    e.name = "match_final.dem";
    e.map = "gm_construct";
    e.server = "Test Server [UA]";
    CHECK(demo::library_match(e, "construct final") && demo::library_match(e, "") && !demo::library_match(e, "flatgrass"));
    CHECK(demo::library_match(e, "SERVER"));   // без урахування регістру
    fs::remove_all(dir);
}

static void test_driver_cfg() {
    std::printf("[driver cfg]\n");
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "gmdr_test_gmod" / "GarrysMod";
    fs::remove_all(root.parent_path());
    fs::create_directories(root / "garrysmod" / "cfg");
    fs::create_directories(root / "garrysmod" / "lua" / "menu");
    write_file_text(root / "garrysmod" / "gameinfo.txt", "GameInfo {}");
    write_file_text(root / "garrysmod" / "lua" / "menu" / "menu.lua", "include( \"util.lua\" )\n");
    write_file_text(root / "garrysmod" / "cfg" / "config.cfg", "fps_max \"144\"\nvoice_scale \"0.8\"\nbind \"w\" \"+forward\"\n");
    write_file_text(root / "hl2.exe", "");
    auto g = game::gmod_from_dir(root / "garrysmod");   // можна вказати і вкладену папку
    CHECK(g.has_value() && g->valid());
    if (!g) return;
    CHECK(game::driver_state(*g) == game::DriverState::NotInstalled);
    CHECK(game::install_driver(*g, nullptr));
    CHECK(game::driver_state(*g) == game::DriverState::Installed);
    CHECK(game::install_driver(*g, nullptr));   // повторне встановлення не дублює рядок
    auto menu = read_file_text(root / "garrysmod" / "lua" / "menu" / "menu.lua");
    CHECK(menu && menu->find("gmdr_driver.lua") == menu->rfind("gmdr_driver.lua"));
    auto vals = game::read_config_values(*g, {"fps_max", "voice_scale", "mat_vsync"});
    CHECK(vals["fps_max"] == "144" && vals["voice_scale"] == "0.8" && vals["mat_vsync"] == "0");
    game::DriverJob job;
    job.id = "t1";
    job.demo = "gmdr_tmp/t1/demo";
    job.movie = "gmdr_tmp/t1/f";
    job.host_framerate = 240;
    job.hide_hud = true;
    const std::string cfg = game::make_job_cfg(job, true, "r_3dsky 0\nmat_motion_blur_enabled 1", vals);
    CHECK(cfg.find("host_framerate 240") != std::string::npos);
    CHECK(cfg.find("alias gmdr_start \"startmovie gmdr_tmp/t1/f raw\"") != std::string::npos);
    CHECK(cfg.find("alias gmdr_play \"playdemo gmdr_tmp/t1/demo\"") != std::string::npos);
    CHECK(cfg.find("fps_max 144;") != std::string::npos);
    CHECK(cfg.find("voice_scale 0\n") != std::string::npos);
    CHECK(cfg.find("cl_drawhud 0") != std::string::npos);
    CHECK(cfg.find("mat_motion_blur_enabled 1") != std::string::npos);
    CHECK(game::write_job_files(*g, job, cfg, true, nullptr));
    CHECK(fs::exists(root / "garrysmod" / "data" / "gmdr" / "job.txt"));
    game::remove_job_files(*g, "t1");
    CHECK(!fs::exists(root / "garrysmod" / "data" / "gmdr" / "job.txt"));
    CHECK(game::uninstall_driver(*g, nullptr));
    CHECK(game::driver_state(*g) == game::DriverState::NotInstalled);
    menu = read_file_text(root / "garrysmod" / "lua" / "menu" / "menu.lua");
    CHECK(menu && menu->find("gmdr") == std::string::npos);
    CHECK(frames::FrameSequenceReader::parse_index("f0123.tga", "f", {".tga"}) == 123);
    CHECK(frames::FrameSequenceReader::parse_index("f12345.TGA", "f", {".tga"}) == 12345);
    CHECK(frames::FrameSequenceReader::parse_index("f.wav", "f", {".tga"}) == -1);
    CHECK(frames::FrameSequenceReader::parse_index("fx12.tga", "f", {".tga"}) == -1);
    fs::remove_all(root.parent_path());
}

static demo::RawBitsMsg raw_bytes(int type, const std::string& bytes) {
    demo::RawBitsMsg m;
    m.type = type;
    m.data.assign(bytes.begin(), bytes.end());
    m.data_bits = m.data.size() * 8;
    return m;
}

static void test_chat_and_markers() {
    std::printf("[chat, markers]\n");
    using namespace std::string_literals;
    // ---- SayText / TextMsg ----
    int ent = 0, dest = 0;
    std::string text;
    bool team = false, dead = false;
    CHECK(demo::parse_say_text(raw_bytes(3, "\x07" "Don Gordon\0\x01\x00\x00"s), ent, text, team, dead));
    CHECK(ent == 7 && text == "Don Gordon" && !team && !dead);
    CHECK(demo::parse_say_text(raw_bytes(3, "\x13\xd0\xbc\xd0\xb4\0\x01\x00\x01"s), ent, text, team, dead));
    CHECK(ent == 19 && text == "\xd0\xbc\xd0\xb4" && dead);
    CHECK(!demo::parse_say_text(raw_bytes(3, "\x07" "Don\0\x01\x00"s), ent, text, team, dead));        // 2 байти прапорців
    CHECK(!demo::parse_say_text(raw_bytes(3, "\x07\xff\xfe\0\x01\x00\x00"s), ent, text, team, dead));  // не UTF-8
    CHECK(demo::parse_text_msg(raw_bytes(4, "\x03Vote: %s1 wins\n\0Bob\0\0\0\0"s), dest, text));
    CHECK(dest == 3 && text == "Vote: Bob wins");
    CHECK(!demo::parse_text_msg(raw_bytes(4, "\x09text\0\0\0\0\0"s), dest, text));
    // Класифікатор: SayText — там, де так виглядає більшість повідомлень
    demo::UserMessageClassifier cls;
    for (int i = 0; i < 10; ++i) cls.add(raw_bytes(5, "\x02hello\0\x01\x00\x00"s));
    cls.add(raw_bytes(5, "\x02\x01\x02"s));
    for (int i = 0; i < 4; ++i) cls.add(raw_bytes(9, "\x03Server says\0\0\0\0\0"s));
    for (int i = 0; i < 3; ++i) cls.add(raw_bytes(3, "\x02hi\0\x01\x00\x00"s));
    for (int i = 0; i < 5; ++i) cls.add(raw_bytes(3, "\x99\x98\x97"s));   // 3 з 8 — не SayText
    const auto types = cls.decide();
    CHECK(types.say_text == 5 && types.text_msg == 9);
    // net-повідомлення аддона чату: JSON + номер сутності в 13 бітах
    CHECK(demo::is_chat_net_message("customchat.say") && !demo::is_chat_net_message("customchat.player_spawned"));
    CHECK(!demo::is_chat_net_message("ash.player::network"));
    {
        const std::string json = R"({"text":"hi there","channel":"team"})";
        std::string b = "\x00\x1c\x00"s + json + "\0"s;
        b.push_back(0x0b);   // сутність 11 (13 біт: 0x0b, 0x00)
        b.push_back(0x00);
        auto m = raw_bytes(0, b);
        m.data_bits -= 3;   // 13 біт, а не 16
        std::string channel;
        CHECK(demo::parse_chat_net_message(m, ent, text, channel));
        CHECK(ent == 11 && text == "hi there" && channel == "team");
    }
    CHECK(demo::clean_text("  a\n\tb  c \r\n") == "a b c");

    // ---- ігрові події: опис і декодування ----
    {
        struct BW {
            std::vector<uint8_t> d;
            size_t n = 0;
            void bits(uint32_t v, int k) {
                for (int i = 0; i < k; ++i, ++n) {
                    if (n % 8 == 0) d.push_back(0);
                    if ((v >> i) & 1) d.back() |= static_cast<uint8_t>(1u << (n % 8));
                }
            }
            void str(const std::string& s) {
                for (char c : s) bits(static_cast<uint8_t>(c), 8);
                bits(0, 8);
            }
        } list, ev;
        list.bits(7, 9); list.str("player_hurt");
        list.bits(4, 3); list.str("userid");
        list.bits(5, 3); list.str("health");
        list.bits(2, 3); list.str("dmg");
        list.bits(6, 3); list.str("crit");
        list.bits(1, 3); list.str("weapon");
        list.bits(0, 3);
        demo::RawBitsMsg lm;
        lm.type = 1;
        lm.data = list.d;
        lm.data_bits = list.n;
        demo::GameEventDecoder dec;
        CHECK(dec.load_list(lm));
        ev.bits(7, 9);
        ev.bits(static_cast<uint16_t>(-5), 16);
        ev.bits(42, 8);
        float f = 12.5f;
        uint32_t fb;
        std::memcpy(&fb, &f, 4);
        ev.bits(fb, 32);
        ev.bits(1, 1);
        ev.str("pistol");
        demo::RawBitsMsg em;
        em.data = ev.d;
        em.data_bits = ev.n;
        auto e = dec.decode(em);
        CHECK(e && e->name == "player_hurt");
        CHECK(e && e->get_int("userid") == -5 && e->get_int("health") == 42 && e->get_int("crit") == 1);
        CHECK(e && e->get_str("weapon") == "pistol" && e->find("dmg") && std::abs(e->find("dmg")->num - 12.5) < 1e-9);
        em.data_bits -= 20;   // обрізана подія — не розбирається
        CHECK(!dec.decode(em));
    }

    // ---- позначки і розділи ----
    auto ms = render::parse_markers("300\tБій\n100\tВступ\nсміття\n100\tВступ 2\n");
    CHECK(ms.size() == 2 && ms[0].tick == 100 && ms[0].title == "Вступ 2" && ms[1].title == "Бій");
    CHECK(render::parse_markers(render::format_markers(ms)) == ms);
    render::add_marker(ms, {200, "Сере\tдина"});
    CHECK(ms.size() == 3 && ms[1].tick == 200 && ms[1].title == "Сере дина");
    const double ti = 0.1;
    auto ch = render::chapters_for_range(ms, 50, 400, ti);   // позначки на 5, 15, 25 с від початку відео
    CHECK(ch.size() == 4 && ch[0].title == "Початок" && std::abs(ch[1].start - 5.0) < 1e-9);
    CHECK(ch.size() == 4 && std::abs(ch[3].end - 35.0) < 1e-9 && std::abs(ch[2].end - ch[3].start) < 1e-9);
    ch = render::chapters_for_range(ms, 100, 250, ti);        // перша позначка на самому початку
    CHECK(ch.size() == 2 && ch[0].title == "Вступ 2" && ch[0].start == 0 && std::abs(ch[1].end - 15.0) < 1e-9);
    CHECK(render::chapters_for_range(ms, 310, 400, ti).empty());
    CHECK(render::chapters_as_text({{0, 5, "Початок"}, {65, 90, "Бій"}, {3700, 3800, "Кінець"}}) ==
          "0:00 Початок\n1:05 Бій\n1:01:40 Кінець\n");
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "gmdr_test_markers";
        fs::remove_all(dir);
        fs::create_directories(dir);
        write_file_text(dir / "a.dem", "demo-a");
        write_file_text(dir / "b.dem", "demo-bb");
        const fs::path store = dir / "markers.json";
        CHECK(render::save_demo_markers(store, path_to_utf8(dir / "a.dem"), ms));
        CHECK(render::save_demo_markers(store, path_to_utf8(dir / "b.dem"), {{7, "b"}}));
        CHECK(render::load_demo_markers(store, path_to_utf8(dir / "a.dem")) == ms);
        // Переміщене демо (те саме ім'я і розмір) — позначки ті самі
        fs::create_directories(dir / "moved");
        fs::rename(dir / "a.dem", dir / "moved" / "a.dem");
        CHECK(render::load_demo_markers(store, path_to_utf8(dir / "moved" / "a.dem")) == ms);
        CHECK(render::save_demo_markers(store, path_to_utf8(dir / "b.dem"), {}));
        CHECK(render::load_demo_markers(store, path_to_utf8(dir / "b.dem")).empty());
        CHECK(read_file_text(store).value_or("").find("b.dem") == std::string::npos);   // порожні не зберігаються
        fs::remove_all(dir);
    }

    // ---- субтитри чату ----
    std::vector<demo::DemoEvent> evs = {
        {10, demo::DemoEventKind::Chat, 1, "Ann", "hi", ""},
        {30, demo::DemoEventKind::Join, 2, "Bob", "", ""},
        {500, demo::DemoEventKind::Chat, 1, "Ann", "later", ""},
        {5, demo::DemoEventKind::Chat, 1, "Ann", "before", ""},
    };
    const std::string srt = render::make_chat_srt(evs, 10, 400, ti, 39.0);
    CHECK(srt.find("before") == std::string::npos && srt.find("later") == std::string::npos);
    CHECK(srt.find("1\n00:00:00,000 --> 00:00:02,000\nAnn: hi\n") != std::string::npos);
    CHECK(srt.find("00:00:02,000 --> 00:00:07,000\nAnn: hi\n→ Bob зайшов на сервер\n") != std::string::npos);
    CHECK(srt.find("00:00:07,000 --> 00:00:09,000\n→ Bob зайшов на сервер\n") != std::string::npos);
    CHECK(render::make_chat_srt(evs, 600, 700, ti, 10.0).empty());
}

// Моно-сигнал з вектора (позиція 0 = перший семпл), для перевірки обробки звуку.
class VecInput final : public audio::AudioInput {
public:
    explicit VecInput(std::vector<float> x, int64_t avail = INT64_MAX) : x_(std::move(x)), avail_(avail) {}
    std::string name() const override { return "тест"; }
    int64_t available() override { return avail_; }
    void mix(int64_t pos, float* out, size_t frames, float gain) override {
        for (size_t i = 0; i < frames; ++i) {
            const int64_t p = pos + static_cast<int64_t>(i);
            if (p < 0 || p >= static_cast<int64_t>(x_.size())) continue;
            out[i * 2] += x_[static_cast<size_t>(p)] * gain;
            out[i * 2 + 1] += x_[static_cast<size_t>(p)] * gain;
        }
    }
    void set_available(int64_t a) { avail_ = a; }

private:
    std::vector<float> x_;
    int64_t            avail_;
};

static double rms_db(const std::vector<float>& x, double from_s, double to_s, size_t stride) {
    const size_t a = static_cast<size_t>(from_s * 48000), b = std::min(x.size() / stride, static_cast<size_t>(to_s * 48000));
    double s = 0;
    for (size_t i = a; i < b; ++i) s += static_cast<double>(x[i * stride]) * x[i * stride];
    return 10.0 * std::log10(s / std::max<size_t>(1, b - a) + 1e-20);
}

// Звук гри після перезапуску гри: недописаний хвіст старого WAV відкидається, новий файл —
// рівно з потрібної позиції, пропуск між ними — тиша; старе ще можна дозміксувати
// Склеювання частин паралельного рендеру: відео — пакетами без перекодування (з B-кадрами),
// звук гри — з WAV кожної частини на своєму місці, додаткова версія — теж з частин
// Поділ фрагмента на частини для кількох копій гри
// Фрагментований MP4 (захист від збою): копія файлу посеред запису відкривається, а готовий
// файл після переупаковки починається з нуля — відео не відстає від звуку на затримку B-кадрів
static void test_fragmented_mp4() {
    std::printf("[fragmented mp4]\n");
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "gmdr_test_frag";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const std::string path = path_to_utf8(dir / "f.mp4"), copy = path_to_utf8(dir / "crash.mp4");
    media::Muxer mux;
    media::VideoEncoder enc;
    media::VideoEncoderSettings vs;
    vs.codec = "libx264";
    vs.fps = {30, 1};
    vs.preset = "veryfast";   // з B-кадрами
    vs.gop_seconds = 1;
    std::string err;
    if (!mux.open(path, "", &err) || !enc.open(vs, 320, 180, mux.needs_global_header(), &err)) {
        std::printf("  пропуск: %s\n", err.c_str());
        return;
    }
    const int st = mux.add_stream(enc.context(), "t");
    CHECK(mux.write_header(true, true, &err) && mux.is_fragmented());
    auto sink = [&](AVPacket* pk) { return mux.write_packet(st, pk, enc.context()->time_base); };
    uint32_t rnd = 12345;   // шум — щоб дані справді йшли на диск, а не лишались у буфері запису
    for (int f = 0; f < 90; ++f) {
        frames::Image img;
        img.allocate(320, 180, frames::PixelLayout::BGR24);
        for (int y = 0; y < 180; ++y) {
            uint8_t* row = img.row(0, y);
            for (int x = 0; x < 320 * 3; ++x) row[x] = static_cast<uint8_t>((rnd = rnd * 1664525u + 1013904223u) >> 24);
        }
        CHECK(enc.encode(img, f, sink, &err));
        if (f == 75) fs::copy_file(path_from_utf8(path), path_from_utf8(copy), fs::copy_options::overwrite_existing, ec);
    }
    CHECK(enc.flush(sink, &err) && mux.finish(&err));
    media::MediaFileInfo info;
    const bool opened = media::probe_media_file(copy, info, &err);
    if (!opened) std::printf("  копія: %s\n", err.c_str());
    CHECK(opened && info.has_video && info.video_seconds >= 0.9);   // хоча б перший фрагмент (1 с)
    // Ключові кадри обірваної копії: з 0, по зростанню, не далі записаного і не рідше GOP (30 кадрів)
    const auto keys = media::keyframe_frames(copy, vs.fps, &err);
    CHECK(!keys.empty() && keys.front() == 0 && keys.back() <= 75);
    for (size_t i = 1; i < keys.size(); ++i) CHECK(keys[i] > keys[i - 1] && keys[i] - keys[i - 1] <= 30);
    CHECK(media::keyframe_frames(path_to_utf8(dir / "missing.mp4"), vs.fps, &err).empty());
    CHECK(media::remux_file(path, true, &err));
    AVFormatContext* in = nullptr;
    CHECK(avformat_open_input(&in, path.c_str(), nullptr, nullptr) >= 0);
    if (in) {
        avformat_find_stream_info(in, nullptr);
        const AVStream* vst = in->streams[0];
        CHECK(vst->start_time == 0 || vst->start_time == AV_NOPTS_VALUE);
        avformat_close_input(&in);
    }
    CHECK(media::probe_media_file(path, info, &err) && info.video_frames == 90);
    fs::remove_all(dir, ec);
}

static void test_plan_parts() {
    std::printf("[parallel plan]\n");
    const double ti = 0.015;   // 66.7 тік/с
    // 1000 тіків = 15 с, 60 кадрів/с = 900 кадрів на 3 частини
    auto parts = render::plan_parts(100, 1100, ti, 1.0 / 60, 3, 60);
    CHECK(parts.size() == 3);
    int64_t next = 0;
    for (const auto& p : parts) {
        CHECK(p.first_frame == next);   // кадри йдуть підряд, без пропусків і повторів
        next += p.frames;
        // Гра починає з запасом (драйвер вмикає запис на кілька тіків пізніше): 8..20 тіків раніше
        const double lead = p.first_time - p.start_tick * ti;
        CHECK(lead >= 8 * ti - 1e-9 && lead < 20 * ti);
        CHECK_NEAR(p.first_time, 100 * ti + static_cast<double>(p.first_frame) / 60, 1e-9);
        CHECK(p.end_tick <= 1100);
    }
    CHECK(next == 900);
    // 9 кадрів 60 fps = рівно 10 тіків: межі — на кратних 9, запас — 10 тіків, тож кадри гри
    // лягають рівно на сітку кадрів відео (зайві 9 кадрів на початку відкидаються)
    CHECK(parts[0].start_tick == 90 && parts[0].frames == 297 && parts[2].end_tick == 1100);
    CHECK(parts[1].first_frame == 297 && parts[1].start_tick == 420 && parts[2].first_frame == 603);
    for (const auto& p : parts) {
        const double lead_frames = (p.first_time - p.start_tick * ti) * 60;
        CHECK_NEAR(lead_frames, std::round(lead_frames), 1e-6);
    }
    CHECK(parts[0].end_tick == 432);   // кінець частини (тік 430) + 2 тіки запасу
    // Float-тривалість тіку з демо (12.5 с / 825 тіків) і 30 кадрів/с: 5 кадрів = 11 тіків
    const double fti = static_cast<double>(static_cast<float>(12.5 / 825));
    parts = render::plan_parts(66, 726, fti, 1.0 / 30, 2, 60);
    CHECK(parts.size() == 2 && parts[1].first_frame == 150 && parts[1].start_tick == 396 - 11);
    // 59.94 кадр/с — рівних меж немає: гра почне на тіку з запасом 8 тіків
    parts = render::plan_parts(0, 4000, ti, 1001.0 / 60000, 2, 60);
    CHECK(parts.size() == 2 && parts[1].start_tick * ti <= parts[1].first_time &&
          parts[1].first_time - parts[1].start_tick * ti < 9 * ti);
    // Уповільнення ×0.5: кадр — 1/120 с демо, кадрів удвічі більше
    parts = render::plan_parts(0, 1000, ti, 0.5 / 60, 2, 60);
    CHECK(parts.size() == 2 && parts[0].frames + parts[1].frames == 1800);
    // Закороткий фрагмент — менше частин (кожна не коротша за min_frames)
    parts = render::plan_parts(0, 400, ti, 1.0 / 60, 4, 120);   // 360 кадрів
    CHECK(parts.size() == 3);
    CHECK(render::plan_parts(0, 100, ti, 1.0 / 60, 4, 120).size() == 1);
    CHECK(render::plan_parts(100, 100, ti, 1.0 / 60, 2, 1).empty());
}

static void test_part_assembly() {
    std::printf("[part assembly]\n");
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "gmdr_test_parts";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const auto p = [&](const char* name) { return path_to_utf8(dir / name); };
    render::EncodeSettings base;
    base.video.codec = "libx264";
    base.video.width = 160;
    base.video.height = 90;
    base.video.fps = {30, 1};
    base.video.preset = "veryfast";   // з B-кадрами: dts < pts
    base.video.gop_seconds = 1;       // ключові кадри хоча б кожні 30 кадрів — є де обрізати
    base.audio_enabled = false;
    base.audio.codec = "pcm_s16le";
    base.audio.sample_rate = 48000;
    render::ExtraOutput small;
    small.label = "small";
    small.video = base.video;
    small.video.width = 80;
    small.video.height = 46;
    small.audio = base.audio;
    // Частина: frames кадрів, спалах (білий кадр) на кадрі flash
    auto make_part = [&](const std::string& out, const std::string& out_small, int frames, int flash) {
        render::EncodeSettings es = base;
        es.output_path = out;
        render::ExtraOutput x = small;
        x.output_path = out_small;
        es.extras.push_back(x);
        render::EncodeSession ses(es, nullptr);
        std::string err;
        if (!ses.begin(160, 90, {}, &err)) {
            std::printf("  пропуск: %s\n", err.c_str());
            return false;
        }
        for (int f = 0; f < frames; ++f) {
            frames::Image img;
            img.allocate(160, 90, frames::PixelLayout::BGR24);
            const uint8_t v = f == flash ? 255 : 30;
            for (int y = 0; y < 90; ++y) std::memset(img.row(0, y), v, 160 * 3);
            CHECK(ses.push_subframe(std::move(img), &err));
        }
        CHECK(ses.finish(&err));
        return true;
    };
    if (!make_part(p("a.mov"), p("a_small.mov"), 45, 30) || !make_part(p("b.mov"), p("b_small.mov"), 30, 15)) return;
    // Звук гри: частина A записала 1.6 с (трохи далі свого кінця), B почала на 1.5 с
    auto write_wav = [&](const char* name, double seconds, float value) {
        audio::WavWriter w;
        w.open(dir / name, 48000, 2, audio::WavWriter::Format::Float32);
        std::vector<float> v(static_cast<size_t>(seconds * 48000) * 2, value);
        w.write(v.data(), v.size() / 2);
        w.close();
    };
    write_wav("a.wav", 1.6, 0.25f);
    write_wav("b.wav", 1.2, 0.5f);

    render::EncodeSettings fs_ = base;
    fs_.audio_enabled = true;
    fs_.output_path = p("out.mov");
    fs_.video_parts = {p("a.mov"), p("b.mov")};
    render::ExtraOutput x = small;
    x.output_path = p("out_small.mov");
    x.video_parts = {p("a_small.mov"), p("b_small.mov")};
    fs_.extras.push_back(x);
    render::AudioSourcesSpec spec;
    spec.game_wav = dir / "a.wav";
    spec.game_segments = {{dir / "a.wav", 0.0}, {dir / "b.wav", 1.5}};
    spec.game_read_ahead = 0.3;   // готові файли читаються потроху
    render::EncodeSession ses(fs_, nullptr);
    std::string err;
    CHECK(ses.begin(0, 0, spec, &err));
    double last_progress = -1;
    CHECK(ses.copy_video(nullptr, [&](double f) { last_progress = f; }, &err));
    CHECK(ses.finish(&err));
    CHECK(ses.frames_encoded() == 75);
    CHECK(ses.finished_extras().size() == 1);
    media::MediaFileInfo info;
    CHECK(media::probe_media_file(p("out.mov"), info, &err));
    CHECK(info.video_frames == 75);
    CHECK_NEAR(info.video_seconds, 2.5, 0.05);
    CHECK_NEAR(info.audio_seconds, 2.5, 0.05);
    CHECK(media::probe_media_file(p("out_small.mov"), info, &err) && info.video_frames == 75);

    // Декодуємо: номери кадрів зі спалахом (за pts) і звук (pcm_s16le)
    std::vector<int> bright;
    std::vector<int16_t> pcm;
    auto decode = [&](const std::string& path) {
        bright.clear();
        pcm.clear();
        AVFormatContext* in = nullptr;
        CHECK(avformat_open_input(&in, path.c_str(), nullptr, nullptr) >= 0);
        if (!in) return;
        avformat_find_stream_info(in, nullptr);
        const int vs = av_find_best_stream(in, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        const int as = av_find_best_stream(in, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        const AVCodec* dec = avcodec_find_decoder(in->streams[vs]->codecpar->codec_id);
        AVCodecContext* dc = avcodec_alloc_context3(dec);
        avcodec_parameters_to_context(dc, in->streams[vs]->codecpar);
        CHECK(avcodec_open2(dc, dec, nullptr) >= 0);
        AVPacket* pk = av_packet_alloc();
        AVFrame* fr = av_frame_alloc();
        auto drain = [&] {
            while (avcodec_receive_frame(dc, fr) >= 0) {
                const int64_t n = av_rescale_q(fr->pts, in->streams[vs]->time_base, AVRational{1, 30});
                if (fr->data[0][45 * fr->linesize[0] + 80] > 200) bright.push_back(static_cast<int>(n));
                av_frame_unref(fr);
            }
        };
        while (av_read_frame(in, pk) >= 0) {
            if (pk->stream_index == vs) {
                avcodec_send_packet(dc, pk);
                drain();
            } else if (as >= 0 && pk->stream_index == as) {
                const auto* smp = reinterpret_cast<const int16_t*>(pk->data);
                pcm.insert(pcm.end(), smp, smp + pk->size / 2);
            }
            av_packet_unref(pk);
        }
        avcodec_send_packet(dc, nullptr);
        drain();
        av_frame_free(&fr);
        av_packet_free(&pk);
        avcodec_free_context(&dc);
        avformat_close_input(&in);
    };
    // Спалахи мають бути рівно на кадрах 30 і 45 + 15 = 60, звук — 0.25 до 1.5 с, далі 0.5
    decode(p("out.mov"));
    CHECK(bright == std::vector<int>({30, 60}));
    auto sample = [&](double t) {
        const size_t i = static_cast<size_t>(t * 48000) * 2;
        return i < pcm.size() ? pcm[i] / 32768.0 : -1.0;
    };
    CHECK_NEAR(sample(0.5), 0.25, 0.01);
    CHECK_NEAR(sample(1.49), 0.25, 0.01);
    CHECK_NEAR(sample(1.51), 0.5, 0.01);
    CHECK_NEAR(sample(2.4), 0.5, 0.01);
    CHECK(last_progress > 0.5);

    // Дописування після збою: з першої частини береться лише початок до ключового кадру k
    // (далі група кадрів могла не дописатись), решта — з другої частини
    const auto keys = media::keyframe_frames(p("a.mov"), base.video.fps, &err);
    int64_t k = 0;
    for (const int64_t key : keys)
        if (key > 0 && key < 45) k = key;
    CHECK(k > 0);
    if (k > 0) {
        render::EncodeSettings cut = base;
        cut.output_path = p("cut.mov");
        cut.video_parts = {p("a.mov"), p("b.mov")};
        cut.video_part_frames = {k, 0};
        render::EncodeSession cs(cut, nullptr);
        CHECK(cs.begin(0, 0, {}, &err));
        CHECK(cs.copy_video(nullptr, nullptr, &err));
        CHECK(cs.finish(&err));
        CHECK(cs.frames_encoded() == k + 30);
        CHECK(media::probe_media_file(p("cut.mov"), info, &err) && info.video_frames == k + 30);
        decode(p("cut.mov"));
        std::vector<int> expect;
        if (k > 30) expect.push_back(30);
        expect.push_back(static_cast<int>(k) + 15);
        CHECK(bright == expect);
    }
    fs::remove_all(dir, ec);
}

static void test_resume_record() {
    std::printf("[resume record]\n");
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "gmdr_test_resume";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    render::ResumeRecord r;
    r.id = "test_" + make_unique_id();
    r.settings.demo_path = "C:/demos/a.dem";
    r.settings.output_path = path_to_utf8(dir / "відео.mp4");
    r.settings.fps = "30";
    r.settings.parallel_games = 2;
    r.video_t0 = 1.5;
    r.frames = 180;
    r.seconds = 10;
    r.wavs = {{path_to_utf8(dir / "g.wav"), 1.25}};
    std::string err;
    CHECK(render::save_resume(r, &err));
    auto l = render::load_resume(render::resume_dir() / (r.id + ".json"));
    CHECK(l.has_value());
    if (l) {
        CHECK(l->id == r.id && l->settings.output_path == r.settings.output_path && l->settings.parallel_games == 2);
        CHECK(l->frames == 180 && l->video_t0 == 1.5 && l->seconds == 10);
        CHECK(l->wavs.size() == 1 && l->wavs[0].first == r.wavs[0].first && l->wavs[0].second == 1.25);
        CHECK(l->updated > 0);
    }
    auto find = [&] {
        for (const auto& x : render::pending_resumes())
            if (x.id == r.id) return true;
        return false;
    };
    // Файлу ще немає — запис застарілий і прибирається
    CHECK(!find());
    CHECK(!fs::exists(render::resume_dir() / (r.id + ".json")));
    // Є частковий файл (чи вже перенесений початок у папці частин) — пропонується
    CHECK(render::save_resume(r, &err));
    fs::create_directories(render::parts_dir_for(r.settings.output_path), ec);
    write_file_atomic(render::resume_head_path(r.settings.output_path), "x", &err);
    CHECK(render::resume_head_path(r.settings.output_path) == dir / "відео.gmdr_parts" / "part0.mp4");
    CHECK(find());
    // Залишки в теці гри після збою: recover_leftovers прибирає gmdr_tmp, але звук гри урваного
    // рендеру спершу переносить до його теки частин (інакше дописане відео було б без нього)
    game::GModInstall g;
    g.root = dir / "GarrysMod";
    g.garrysmod = g.root / "garrysmod";
    const fs::path gtmp = g.garrysmod / "gmdr_tmp" / r.id;
    fs::create_directories(gtmp, ec);
    write_file_atomic(gtmp / "gmdr_x.wav", "RIFF", &err);
    write_file_atomic(gtmp / "gmdr_x_000001.tga", "t", &err);
    r.wavs = {{path_to_utf8(gtmp / "gmdr_x.wav"), 1.25}};
    CHECK(render::save_resume(r, &err));
    CHECK(path_is_inside(gtmp / "gmdr_x.wav", g.garrysmod / "gmdr_tmp") && !path_is_inside(dir / "g.wav", gtmp) &&
          !path_is_inside(g.garrysmod / "gmdr_tmp2" / "a.wav", g.garrysmod / "gmdr_tmp"));
    if (game::GameProcess::find_by_name({"gmod.exe", "hl2.exe", "gmod", "hl2_linux"}).empty()) {
        render::recover_leftovers(g);
        const fs::path moved = render::parts_dir_for(r.settings.output_path) / "part0_1.wav";
        CHECK(fs::exists(moved) && !fs::exists(g.garrysmod / "gmdr_tmp"));
        l = render::load_resume(render::resume_dir() / (r.id + ".json"));
        CHECK(l && l->wavs.size() == 1 && path_from_utf8(l->wavs[0].first) == moved && l->wavs[0].second == 1.25);
    } else {
        std::printf("  пропуск перевірки залишків: запущено GMod\n");
    }
    // «Забути»: запис і допоміжне прибрано, частковий файл — знову на місці відео
    render::forget_resume(r.id);
    CHECK(!find());
    CHECK(fs::exists(dir / "відео.mp4") && !fs::exists(render::parts_dir_for(r.settings.output_path)));
    fs::remove_all(dir, ec);
}

static void test_game_audio_segments() {
    std::printf("[game audio segments]\n");
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "gmdr_test_segments";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    auto write_wav = [&](const fs::path& p, double seconds, float value) {
        audio::WavWriter w;
        w.open(p, 48000, 2, audio::WavWriter::Format::Float32);
        std::vector<float> v(static_cast<size_t>(seconds * 48000) * 2, value);
        w.write(v.data(), v.size() / 2);
        w.close();
    };
    write_wav(dir / "a.wav", 1.0, 0.25f);   // гра встигла записати 1 с і впала
    write_wav(dir / "b.wav", 1.0, 0.5f);    // перезапущена гра
    std::vector<float> out(200);
    auto at = [&](audio::GameAudioInput& in, int64_t pos) {
        std::fill(out.begin(), out.end(), 0.0f);
        in.mix(pos, out.data(), 100, 1.0f);
        return static_cast<double>(out[0]);
    };
    {
        audio::GameAudioInput in(dir / "a.wav", true, 0.0);
        CHECK(in.available() == 48000);
        in.start_segment(dir / "b.wav", 38400);   // новий запуск — з 0.8 с: хвіст 0.2 с відкинуто
        CHECK(in.available() == 38400 + 48000);
        CHECK_NEAR(at(in, 1000), 0.25, 1e-6);
        CHECK_NEAR(at(in, 38300), 0.25, 1e-6);
        CHECK_NEAR(at(in, 38400), 0.5, 1e-6);
        CHECK_NEAR(at(in, 38400 + 47000), 0.5, 1e-6);
    }
    {
        audio::GameAudioInput in(dir / "a.wav", true, 0.0);
        in.available();
        in.start_segment(dir / "b.wav", 60000);   // новий запуск пізніше, ніж скінчився старий
        CHECK(in.available() == 60000 + 48000);
        CHECK_NEAR(at(in, 50000), 0.0, 1e-9);
        CHECK_NEAR(at(in, 60000), 0.5, 1e-6);
    }
    {
        // Нового WAV ще немає: доступне лишається зі старого (і його можна дозміксувати), а чужий
        // .wav у тій самій папці не підхоплюється
        audio::GameAudioInput in(dir / "a.wav", true, 0.0);
        in.available();
        in.start_segment(dir / "c.wav", 48000);
        CHECK(in.available() == 48000);
        CHECK_NEAR(at(in, 40000), 0.25, 1e-6);
    }
    fs::remove_all(dir, ec);
}

// Розпізнавання мовлення без whisper: фрази -> стиснуте аудіо -> час демо, розбір JSON,
// фільтр "галюцинацій", покриття відрізків і субтитри з текстом
static void test_i18n() {
    std::printf("[i18n]\n");
    CHECK(ui_language() == "uk");
    CHECK(translation_count() > 1000);
    CHECK(std::string(tr("Звук гри")) == "Звук гри");
    set_ui_language("en");
    CHECK(ui_language() == "en");
    CHECK(std::string(tr("Звук гри")) == "Game audio");
    CHECK(trf("Знайдено: {}", "C:/x") == "Found: C:/x");
    CHECK(std::string(tr("Огляд...##mic")) == "Browse...##mic");
    CHECK(trf("Почати чергу ({})", 3) == "Start the queue (3)");
    CHECK(std::string(tr("Монітор###program")) == "Monitor###program");
    CHECK(tr(std::string("Огляд")) == "Overview");
    // Немає перекладу — лишається як є (імена гравців, шляхи тощо)
    CHECK(std::string(tr("Невідомий рядок##x")) == "Невідомий рядок##x");
    CHECK(std::string(tr("")).empty());
    // Інші мови: чого ще немає в мові — англійською; невідома мова — англійська
    CHECK(ui_languages().size() == 21 && std::string(ui_languages()[0].code) == "uk" && std::string(ui_languages()[1].code) == "en");
    set_ui_language("xx");
    CHECK(ui_language() == "en");
    set_ui_language("pt");
    CHECK(ui_language() == "pt-BR");
    for (const auto& l : ui_languages()) {
        set_ui_language(l.code);
        CHECK(ui_language() == l.code);
        CHECK(std::string(tr("Невідомий рядок##x")) == "Невідомий рядок##x");
        CHECK(std::string(tr("")).empty());
        if (std::string(l.code) != "uk") {
            const std::string t = tr("Почати рендер");
            CHECK(!t.empty() && t != "Почати рендер");
            CHECK(trf("Знайдено: {}", "C:/x").find("C:/x") != std::string::npos);
        }
    }
    CHECK(std::string(tr_lang("en", "Мова")) == "Language");
    set_ui_language("uk");
    CHECK(ui_language() == "uk");
    CHECK(std::string(tr("Огляд...##mic")) == "Огляд...##mic");
    const std::string sys = system_ui_language();
    CHECK(std::any_of(ui_languages().begin(), ui_languages().end(), [&](const UiLanguage& l) { return sys == l.code; }));
}

// Переклад і озвучення: запити сервісів, розстановка фраз, мікс, мукс доріжок, ключі
static void test_translate_dub() {
    std::printf("[translate & dub]\n");
    namespace fs = std::filesystem;
    // ---- Мови й сервіси ----
    CHECK(translate::find_language("pt") && std::string(translate::find_language("pt")->code) == "pt-BR");
    CHECK(translate::base_code("pt-BR") == "pt");
    CHECK(!translate::supports("deepl", "be") && translate::supports("google", "be") && translate::supports("openai", "eo"));
    CHECK(dub::engine_supports("omnivoice", "", "lt") && !dub::engine_supports("elevenlabs", "eleven_multilingual_v2", "lt"));
    CHECK(dub::engine_supports("elevenlabs", "eleven_v3", "lt") && !dub::engine_supports("elevenlabs", "eleven_v3", "eo"));
    CHECK(dub::engine_language("pt-BR") == "pt" && dub::engine_language("zh") == "zh");
    {
        translate::Config c{"deepl", "abc:fx", "", ""};
        const auto r = translate::build_request(c, {"Привіт"}, "uk", "de");
        CHECK(r.url == "https://api-free.deepl.com/v2/translate");
        CHECK(r.body.find("\"target_lang\":\"DE\"") != std::string::npos && r.body.find("\"source_lang\":\"UK\"") != std::string::npos);
        c.key = "paid";
        CHECK(translate::build_request(c, {"x"}, "auto", "en").url == "https://api.deepl.com/v2/translate");
        const auto p = translate::parse_response(c, R"({"translations":[{"text":"Hallo"}]})", 1, nullptr);
        CHECK(p && p->size() == 1 && (*p)[0] == "Hallo");
        CHECK(!translate::parse_response(c, R"({"translations":[]})", 1, nullptr));
    }
    {
        translate::Config c{"google", "k", "", ""};
        const auto r = translate::build_request(c, {"a", "b"}, "auto", "zh");
        CHECK(r.url.find("/language/translate/v2?key=k") != std::string::npos && r.body.find("\"target\":\"zh-CN\"") != std::string::npos);
        const auto p = translate::parse_response(c, R"({"data":{"translations":[{"translatedText":"1"},{"translatedText":"2"}]}})", 2, nullptr);
        CHECK(p && (*p)[1] == "2");
    }
    {
        translate::Config c{"libre", "", "http://host:5000/", ""};
        CHECK(translate::build_request(c, {"a"}, "uk", "en").url == "http://host:5000/translate");
        const auto p = translate::parse_response(c, R"({"translatedText":["x","y"]})", 2, nullptr);
        CHECK(p && (*p)[0] == "x");
    }
    {
        translate::Config c{"openai", "", "", "qwen2.5:7b"};
        const auto r = translate::build_request(c, {"Привіт", "Бувай"}, "uk", "de");
        CHECK(r.url == "http://localhost:11434/v1/chat/completions" && r.body.find("qwen2.5:7b") != std::string::npos);
        // Модель загорнула відповідь у ```json ...```
        const auto p = translate::parse_response(
            c, R"({"choices":[{"message":{"content":"```json\n[\"Hallo\", \"Tschüss\"]\n```"}}]})", 2, nullptr);
        CHECK(p && p->size() == 2 && (*p)[1] == "Tschüss");
        std::string err;
        CHECK(!translate::parse_response(c, R"({"choices":[{"message":{"content":"[\"one\"]"}}]})", 2, &err) && !err.empty());
    }
    {
        std::string err;
        const auto r = translate::translate({"fake", "", "", ""}, {"a", "", "a", " b "}, "uk", "de", {}, nullptr, &err);
        CHECK(r && r->size() == 4 && (*r)[0] == "[de] a" && (*r)[1].empty() && (*r)[2] == "[de] a" && (*r)[3] == "[de] b");
        CHECK(!translate::translate({"deepl", "", "", ""}, {"a"}, "uk", "de", {}, nullptr, &err) && !err.empty());   // без ключа
        CHECK(!translate::translate({"openai", "", "", ""}, {"a"}, "uk", "de", {}, nullptr, &err));                    // без моделі
    }
    // ---- Ключі: шифрування і звіт ----
    {
        const std::string sec = protect_secret("sk-TEST-123");
#ifdef _WIN32
        CHECK(sec.rfind("dpapi:", 0) == 0 && sec.find("sk-TEST") == std::string::npos);   // DPAPI: не видно відкритим текстом
#endif
        CHECK(unprotect_secret(sec) == "sk-TEST-123");
        CHECK(unprotect_secret("") .empty() && protect_secret("").empty());
        CHECK(base64_decode(base64_encode(std::string("\x00\xff\x10z", 4))) == std::string("\x00\xff\x10z", 4));
        render::RenderSettings s;
        s.deepl_key = sec;
        s.output_path = "x.mp4";
        const std::string red = render::redact_secrets_json(s.to_json().dump());
        CHECK(red.find(sec) == std::string::npos && red.find("(removed)") != std::string::npos && red.find("x.mp4") != std::string::npos);
        CHECK(render::is_secret_field("elevenlabs_key") && !render::is_secret_field("key") && !render::is_secret_field("ui_page"));
        CHECK(render::translator_config(s).key == "sk-TEST-123" && render::translator_config(s).provider == "deepl");
    }
    // ---- Налаштування озвучення і шаблони ----
    {
        render::RenderSettings s;
        s.dub_languages = "de, xx ,DE,en,pt";
        CHECK((render::dub_language_list(s) == std::vector<std::string>{"de", "en", "pt-BR"}));
        CHECK(!render::translation_requested(s));
        s.dub = true;
        CHECK(render::translation_requested(s) && render::dub_needs_stems(s));
        s.dub_outputs = "videos, audio";
        CHECK(render::dub_output(s, "audio") && !render::dub_output(s, "tracks"));
        render::apply_template(s, *render::find_template("youtube"));
        CHECK(s.dub_outputs == "audio" && s.dub_audio_format == "mp3" && s.translate_subtitles && s.loudness_target == -14);
        CHECK(render::find_template("shorts") && !render::find_template("nope"));
        s.tts_clone = true;
        CHECK(!render::clone_allowed(s));
        s.tts_clone_ack = true;
        CHECK(render::clone_allowed(s));
    }
    // ---- Помічник OmniVoice: завдання і протокол ----
    {
        std::vector<dub::SpeakerVoice> voices(2);
        voices[0].ref_wav = "C:/r/ref.wav";
        voices[0].ref_text = "текст";
        voices[1].instruct = dub::generic_instruct(1);
        const std::string job = dub::make_job_json({}, voices, {{"Hallo", "de", 0, "a.wav"}, {"Olá", "pt-BR", 1, "b.wav"}});
        const auto j = json::parse(job);
        CHECK(j && (*j)["items"].items().size() == 2);
        if (j && (*j)["items"].items().size() == 2) {
            CHECK((*j)["items"][0]["ref_audio"].as_string() == "C:/r/ref.wav" && (*j)["items"][0]["ref_text"].as_string() == "текст");
            CHECK((*j)["items"][1]["instruct"].as_string() == "male, low pitch" && (*j)["items"][1]["language"].as_string() == "pt");
            CHECK(!(*j)["items"][1].has("ref_audio"));
        }
        auto e = dub::parse_helper_line("GMDR_FAIL 12 CUDA out of memory\r");
        CHECK(e.kind == dub::HelperEvent::Fail && e.index == 12 && e.text == "CUDA out of memory");
        e = dub::parse_helper_line("GMDR_READY cuda:0");
        CHECK(e.kind == dub::HelperEvent::Ready && e.text == "cuda:0");
        CHECK(dub::parse_helper_line("GMDR_DONE 3").index == 3 && dub::parse_helper_line("Loading weights...").kind == dub::HelperEvent::None);
        CHECK(dub::parse_helper_line("GMDR_DONE x").kind == dub::HelperEvent::None);
    }
    // ---- Розстановка фраз ----
    {
        // Влазить — як є; довша — пришвидшення; ще довша — до межі, а наступна фраза гравця зсувається
        auto p = dub::place_clips({{1.0, 3.0, "a", 1.5}, {5.0, 6.0, "a", 1.0}});
        CHECK_NEAR(p[0].at, 1.0, 1e-9);
        CHECK_NEAR(p[0].tempo, 1.0, 1e-9);
        p = dub::place_clips({{1.0, 3.0, "a", 4.4}, {5.0, 6.0, "a", 1.0}});   // до наступної 3.88 с
        CHECK(p[0].tempo > 1.1 && p[0].tempo < 1.2);
        CHECK_NEAR(p[1].at, 5.0, 1e-6);
        p = dub::place_clips({{1.0, 3.0, "a", 8.0}, {5.0, 6.0, "a", 1.0}});
        CHECK_NEAR(p[0].tempo, 1.35, 1e-9);
        CHECK_NEAR(p[1].at, 1.0 + 8.0 / 1.35 + 0.12, 1e-6);
        // Інший гравець не заважає; неозвучена фраза нічого не зсуває
        p = dub::place_clips({{1.0, 2.0, "a", 1.0}, {1.2, 2.0, "b", 3.0}, {1.5, 2.0, "a", 0}});
        CHECK_NEAR(p[1].at, 1.2, 1e-9);
        CHECK_NEAR(p[1].tempo, 1.35, 1e-9);   // остання фраза b: до кінця оригіналу + 0.6 с, не швидше за 1.35
        CHECK_NEAR(p[0].tempo, 1.0, 1e-9);
    }
    // ---- Фрази: синтез (тестовий рушій), читання, темп, мікс ----
    const fs::path dir = fs::temp_directory_path() / "gmdr_test_dub";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    {
        dub::TtsConfig c;
        c.engine = "fake";
        std::string err;
        const std::vector<dub::TtsItem> items = {{"Hallo Welt, wie geht's?", "de", 0, dir / "0.wav"}, {"Ja", "de", 1, dir / "1.wav"}};
        CHECK(dub::synthesize(c, {}, items, dir, {}, nullptr, nullptr, &err));
        const auto a = dub::load_clip(dir / "0.wav", &err);
        CHECK(a.has_value());
        if (a) {
            const double secs = static_cast<double>(a->size() / 2) / 48000;
            CHECK_NEAR(secs, 0.3 + 23.0 / 14.0 + 0.04, 0.03);   // тон + 40 мс запасу в кінці
            const auto fast = dub::change_tempo(*a, 1.25);
            CHECK_NEAR(static_cast<double>(fast.size() / 2) / 48000, secs / 1.25, 0.05);
            double sum = 0;
            for (float v : *a) sum += static_cast<double>(v) * v;
            CHECK_NEAR(20 * std::log10(std::sqrt(sum / a->size())), -20.0, 1.5);   // рівень вирівняно
            // Джерело для змішувача: фраза з 0.5 с
            dub::ClipsInput in({{24000, *a}});
            std::vector<float> buf(48000 * 2, 0.0f);
            in.mix(0, buf.data(), 48000, 1.0f);
            CHECK(std::abs(buf[2 * 20000]) < 1e-9f);
            double after = 0;
            for (size_t k = 30000; k < 48000; ++k) after = std::max(after, static_cast<double>(std::abs(buf[2 * k])));
            CHECK(after > 0.05);
            // Мікс: звук «гри» (тиша) + фраза → FLAC і MKA потрібної тривалості
            audio::WavWriter w;
            std::vector<float> silence(48000 * 2 * 3, 0.0f);
            CHECK(w.open(dir / "game.wav", 48000, 2, audio::WavWriter::Format::Int16, &err) && w.write(silence.data(), 48000 * 3) &&
                  w.close(&err));
            dub::MixSpec spec;
            spec.game = dir / "game.wav";
            spec.seconds = 3.0;
            spec.duck = true;
            media::AudioEncoderSettings aac;
            aac.codec = "aac";
            aac.bitrate = 128000;
            media::AudioEncoderSettings flac;
            flac.codec = "flac";
            const std::vector<dub::MixOutput> outs = {{path_to_utf8(dir / "d.mka"), "matroska", aac, "Deutsch", "deu"},
                                                      {path_to_utf8(dir / "d.flac"), "", flac, "Deutsch", "deu"}};
            CHECK(dub::mix_dub(spec, {{24000, *a}}, outs, {}, nullptr, &err));
            media::MediaFileInfo info;
            CHECK(media::probe_media_file(path_to_utf8(dir / "d.flac"), info, &err) && std::abs(info.audio_seconds - 3.0) < 0.05);
            // Мукс: «відео» з d.mka як основний файл + ще доріжка з мітками мови
            CHECK(media::mux_files({{path_to_utf8(dir / "d.mka"), true, true, true, {}, "ukr", 1},
                                    {path_to_utf8(dir / "d.flac"), false, true, false, "Deutsch (AI)", "deu", 0}},
                                   path_to_utf8(dir / "both.mkv"), false, &err));
            CHECK(media::probe_media_file(path_to_utf8(dir / "both.mkv"), info, &err) && info.audio_streams == 2);
        }
    }
    // ---- Бібліотека голосів: JSON ----
    {
        dub::VoiceProfile p;
        p.key = "steam:76561198000000001";
        p.names = {"Гравець"};
        p.samples.push_back({"a.wav", "Привіт усім", "uk", 3.5, "match", 12.25, 1700000000});
        p.elevenlabs_voice_id = "abc";
        const auto q = dub::profile_from_json(dub::profile_to_json(p));
        CHECK(q && q->key == p.key && q->name() == "Гравець" && q->samples.size() == 1 && q->samples[0].demo_time == 12.25 &&
              q->elevenlabs_voice_id == "abc" && q->total_seconds() == 3.5);
        CHECK(dub::persistent_key("steam:1") && !dub::persistent_key("slot:3") && !dub::persistent_key("steam:"));
        speech::Line l1{1.0, 4.0, "steam:1", "A", "Це досить довга фраза"}, l2{5.0, 5.5, "steam:1", "A", "коротко"},
            l3{6.0, 8.0, "steam:2", "B", "Чужа фраза гравця"};
        const auto cand = dub::sample_candidates("steam:1", {l2, l1, l3});
        CHECK(cand.size() == 1 && cand[0].start == 1.0);
    }
    fs::remove_all(dir, ec);
}

static void test_speech() {
    std::printf("[speech]\n");
    voice::SpeakerTrack t;
    t.key = "steam:1";
    auto seg = [](double a, double b) {
        voice::VoiceSegment g;
        g.start = std::llround(a * 48000);
        g.length = std::llround(b * 48000) - g.start;
        return g;
    };
    t.segments = {seg(1.0, 2.0), seg(2.3, 3.0), seg(10.0, 10.2), seg(20.0, 21.5)};
    const auto p = speech::plan_pieces(t);   // пауза 0.3 с зливається, 0.2 с мовлення — відкинуто
    CHECK(p.size() == 2);
    if (p.size() == 2) {
        CHECK_NEAR(p[0].compact, 0.0, 1e-9);
        CHECK_NEAR(p[0].demo, 1.0, 1e-9);
        CHECK_NEAR(p[0].length, 2.0, 1e-9);
        CHECK_NEAR(p[1].compact, 3.0, 1e-9);   // 2 с фрази + 1 с тиші
        CHECK_NEAR(p[1].demo, 20.0, 1e-9);
        CHECK_NEAR(speech::compact_to_demo(p, 0.5), 1.5, 1e-9);
        CHECK_NEAR(speech::compact_to_demo(p, 3.2), 20.2, 1e-9);
        CHECK_NEAR(speech::compact_to_demo(p, 2.2), 3.0, 1e-9);   // у паузі — кінець попередньої фрази
        CHECK(speech::piece_at(p, 2.7) == 1);
    }
    const auto pr = speech::plan_pieces(t, 0.8, 0.3, 1.0, 2.5, 21.0);   // лише відрізок 2.5..21 с
    CHECK(pr.size() == 2 && std::abs(pr[0].demo - 2.5) < 1e-9 && std::abs(pr[0].length - 0.5) < 1e-9 &&
          std::abs(pr[1].length - 1.0) < 1e-9);

    std::string err;
    const auto segs = speech::parse_whisper_json(
        R"({"result":{"language":"ru"},"transcription":[{"offsets":{"from":100,"to":1800},"text":" Привіт усім"},)"
        R"({"offsets":{"from":2100,"to":2900},"text":" Субтитры сделал DimaTorzok"},)"
        R"({"offsets":{"from":1500,"to":3800},"text":" через паузу"},)"
        R"({"offsets":{"from":3000,"to":4400},"text":" Друга  фраза"}]})", &err);
    CHECK(err.empty() && segs.size() == 4);
    const auto lines = speech::segments_to_lines(segs, p, t.key, "Гравець");
    CHECK(lines.size() == 3);
    if (lines.size() == 3) {
        CHECK(lines[0].text == "Привіт усім" && std::abs(lines[0].start - 1.1) < 1e-9 && std::abs(lines[0].end - 2.8) < 1e-9);
        CHECK(std::abs(lines[1].end - 3.0) < 1e-9);   // сегмент через паузу обрізано кінцем фрази
        CHECK(lines[2].text == "Друга фраза" && std::abs(lines[2].start - 20.0) < 1e-9 && lines[2].speaker == "Гравець");
    }
    CHECK(speech::parse_whisper_json("{}", &err).empty() && !err.empty());
    for (const char* noise : {"", " ... ", "[музыка]", "(Смех)", "♪ ♪", "Редактор субтитров А.Семкин Корректор А.Егорова",
                              "ДЯКУЮ ЗА ПЕРЕГЛЯД!", "Thanks for watching!"})
        CHECK(speech::is_noise_text(noise));
    CHECK(!speech::is_noise_text("Привіт, як справи?"));
    CHECK(!speech::is_noise_text("ok"));

    // Покриття і злиття: новий відрізок доповнює, повторний — замінює репліки
    speech::Transcript tr;
    tr.covered = {{"k", 0, 10}};
    tr.lines = {{3, 4, "k", "K", "старе"}, {5, 6, "q", "Q", "інший"}};
    CHECK(speech::covers(tr, "k", 2, 8) && !speech::covers(tr, "k", 5, 12) && !speech::covers(tr, "q", 0, 1));
    speech::Transcript fresh;
    fresh.covered = {{"k", 10, 20}};
    fresh.lines = {{12, 13, "k", "K", "нове"}};
    speech::merge_transcript(tr, fresh);
    CHECK(speech::covers(tr, "k", 0, 20) && tr.covered.size() == 1 && tr.lines.size() == 3);
    speech::Transcript again;
    again.covered = {{"k", 0, 10}};
    again.lines = {{3.5, 4, "k", "K", "виправлене"}};
    speech::merge_transcript(tr, again);
    CHECK(tr.lines.size() == 3 && tr.lines[0].text == "виправлене" && tr.lines[1].text == "інший");
    const auto back = speech::transcript_from_json(speech::transcript_to_json(tr));
    CHECK(back && back->lines.size() == 3 && back->lines[0].text == "виправлене" && back->covered.size() == 1 &&
          speech::covers(*back, "k", 0, 20));

    // Субтитри з текстом: репліки накладаються, коротка тримається 1.2 с; фільтр гравців
    const std::vector<speech::Line> sl = {{5.0, 5.5, "a", "A", "раз"}, {5.8, 7.0, "b", "B", "два"}};
    const std::string srt = render::make_transcript_srt(sl, {}, 4.0, 60.0);
    CHECK(srt.find("00:00:01,000 --> 00:00:01,800\nA: раз\n") != std::string::npos);
    CHECK(srt.find("00:00:01,800 --> 00:00:02,200\nA: раз\nB: два\n") != std::string::npos);
    CHECK(srt.find("00:00:02,200 --> 00:00:03,000\nB: два\n") != std::string::npos);
    const std::string only_b = render::make_transcript_srt(sl, {"b"}, 4.0, 60.0, 0.0, 0.5);   // ×0.5
    CHECK(only_b.find("A:") == std::string::npos && only_b.find("00:00:03,600 --> 00:00:06,000\nB: два\n") != std::string::npos);
}

// Уповільнення/прискорення: ланцюжки atempo, тривалість і висота тону, субтитри в часі відео
static void test_speed() {
    std::printf("[speed]\n");
    CHECK(audio::tempo_filter(1.0) == "atempo=1.000000");
    CHECK(audio::tempo_filter(0.5) == "atempo=0.500000");
    CHECK(audio::tempo_filter(0.25) == "atempo=0.500000,atempo=0.500000");
    CHECK(audio::tempo_filter(4) == "atempo=2.000000,atempo=2.000000");
    CHECK(audio::tempo_filter(0.3) == "atempo=0.500000,atempo=0.600000");
    CHECK(audio::tempo_filter(8) == "atempo=2.000000,atempo=2.000000,atempo=2.000000");
    constexpr int R = 48000;
    const double kPi = 3.14159265358979323846;
    std::vector<float> tone(2 * R);
    for (size_t i = 0; i < tone.size(); ++i) tone[i] = static_cast<float>(0.5 * std::sin(2 * kPi * 440 * i / R));
    for (double sp : {0.5, 0.25, 2.0}) {
        audio::AudioFilterChain c;
        std::string err;
        CHECK(c.open(audio::tempo_filter(sp), 1, 1, &err));
        for (size_t at = 0; at < tone.size(); at += 4096)
            c.push(0, tone.data() + at, std::min<size_t>(4096, tone.size() - at), &err);
        CHECK(c.finish(&err));
        // 2 с демо -> 2/sp с відео
        CHECK_NEAR(static_cast<double>(c.out_end() - c.out_start()) / R, 2.0 / sp, 0.05);
        // Висота тону та сама: 220 перетинів нуля вгору за пів секунди всередині
        int ups = 0;
        const int64_t a = c.out_start() + R / 8;
        for (int64_t p = a; p < a + R / 2; ++p) ups += c.out_at(p - 1)[0] < 0 && c.out_at(p)[0] >= 0;
        CHECK(std::abs(ups - 220) <= 3);
    }
    // Репліка 1.0–2.0 с демо при ×0.5 — 2.0–4.0 с відео; затримка голосу 0.5 с теж удвічі довша
    voice::SpeakerTrack a;
    voice::VoiceSegment g;
    g.start = 1 * 48000;
    g.length = 1 * 48000;
    a.segments = {g};
    CHECK(render::make_speaker_srt({{&a, "A"}}, 0, 10.0, 0.0, 0.5).find("00:00:02,000 --> 00:00:04,000\nA\n") !=
          std::string::npos);
    CHECK(render::make_speaker_srt({{&a, "A"}}, 0, 10.0, 0.5, 0.5).find("00:00:03,000 --> 00:00:05,000\nA\n") !=
          std::string::npos);
    CHECK(render::make_speaker_srt({{&a, "A"}}, 0, 10.0, 0.0, 2.0).find("00:00:00,500 --> 00:00:01,000\nA\n") !=
          std::string::npos);
    // Налаштування зберігаються
    render::RenderSettings rs;
    rs.speed = 0.25;
    rs.speed_audio = "mute";
    const auto back = render::RenderSettings::from_json(rs.to_json());
    CHECK(back.speed == 0.25 && back.speed_audio == "mute");
}

static void test_audio_filters() {
    std::printf("[audio filters]\n");
    constexpr int R = 48000;
    const double kPi = 3.14159265358979323846;
    auto sine = [&](double seconds, double freq, double amp) {
        std::vector<float> v(static_cast<size_t>(seconds * R));
        for (size_t i = 0; i < v.size(); ++i) v[i] = static_cast<float>(amp * std::sin(2 * kPi * freq * i / R));
        return v;
    };
    std::mt19937 rng(7);
    std::normal_distribution<float> gauss(0.0f, 1.0f);

    // ---- гучність BS.1770: синус 1 кГц з піком -20 дБ у моно = -23.0 LUFS ----
    {
        const auto s = sine(10, 997, 0.1);
        CHECK_NEAR(audio::integrated_loudness(s.data(), s.size()), -23.01, 0.1);
        std::vector<float> silence(R * 5, 0.0f);
        CHECK(audio::integrated_loudness(silence.data(), silence.size()) < -90);
        audio::VoiceProfile p;
        p.seconds = 10;
        p.loudness = -30;
        CHECK_NEAR(20 * std::log10(audio::level_gain(p, -18)), 12.0, 0.01);
        p.loudness = -50;
        CHECK_NEAR(20 * std::log10(audio::level_gain(p, -18)), 15.0, 0.01);   // не більше +15 дБ
        p.seconds = 0.2;
        CHECK(audio::level_gain(p) == 1.0f);   // замало мовлення — не чіпаємо
    }

    // ---- ланцюжок FFmpeg: позиції збігаються з входом ----
    {
        audio::AudioFilterChain c;
        std::string err;
        CHECK(c.open("volume=0.5", 1, 1, &err));
        const auto s = sine(1, 440, 0.8);
        for (size_t at = 0; at < s.size(); at += 1000) c.push(0, s.data() + at, std::min<size_t>(1000, s.size() - at), &err);
        CHECK(c.finish(&err));
        CHECK(c.out_start() == 0 && c.out_end() == static_cast<int64_t>(s.size()));
        bool same = true;
        for (int64_t p = 0; p < c.out_end(); p += 997) same &= std::abs(c.out_at(p)[0] - s[static_cast<size_t>(p)] * 0.5f) < 1e-5f;
        CHECK(same);
        CHECK(!c.open("такого_фільтра_немає", 1, 1, &err) && !err.empty());
    }
    // loudnorm: вихід затримується (дивиться на 3 с уперед), але після кінця — рівно стільки ж і в цілі
    {
        audio::AudioFilterChain c;
        std::string err;
        CHECK(c.open(audio::loudness_filter(-14), 1, 2, &err));
        std::vector<float> st;
        const auto s = sine(20, 300, 0.02);
        for (float v : s) {
            st.push_back(v);
            st.push_back(v);
        }
        int64_t max_lag = 0;
        for (size_t at = 0; at < s.size(); at += 4096) {
            c.push(0, st.data() + at * 2, std::min<size_t>(4096, s.size() - at), &err);
            max_lag = std::max(max_lag, c.pushed(0) - c.out_end());
        }
        CHECK(max_lag > R / 2);   // справді є затримка
        CHECK(c.finish(&err));
        CHECK(std::llabs(c.out_end() - static_cast<int64_t>(s.size())) <= 64);
        std::vector<float> left;
        for (int64_t p = 5 * R; p < std::min<int64_t>(c.out_end(), 19 * R); ++p) left.push_back(c.out_at(p)[0]);
        // Стерео з однаковими каналами: гучність = гучність одного каналу + 3 дБ
        CHECK_NEAR(audio::integrated_loudness(left.data(), left.size()) + 3.01, -14.0, 1.5);
        CHECK(audio::loudness_filter(0).empty());
    }

    // ---- гейт: фон між фразами тихішає, мова і початки слів цілі ----
    {
        std::vector<float> v(static_cast<size_t>(8 * R));
        for (auto& x : v) x = gauss(rng) * 0.001f;   // фон -60 дБ
        const auto tone = sine(1, 220, 0.14);        // "фраза" ~ -20 дБ RMS
        for (double at : {1.0, 4.0})
            for (size_t i = 0; i < tone.size(); ++i) v[static_cast<size_t>(at * R) + i] += tone[i];
        const auto prof = audio::profile_samples(v.data(), v.size());
        CHECK(prof.valid() && prof.noise_db < -55 && prof.speech_db > -25);
        const auto gp = audio::gate_for(prof);
        CHECK(gp.open_db > prof.noise_db + 5 && gp.open_db < prof.speech_db - 5 && gp.close_db < gp.open_db);
        auto in = std::make_unique<VecInput>(v);
        audio::FilteredInput f("голос", {{{in.get(), 1.0f}}}, true);
        std::string err;
        CHECK(f.open({}, &gp, &err));
        CHECK(f.available() == INT64_MAX);
        std::vector<float> out(v.size() * 2, 0.0f);
        for (size_t at = 0; at < v.size(); at += 1000) {
            const size_t n = std::min<size_t>(1000, v.size() - at);
            f.mix(static_cast<int64_t>(at), out.data() + at * 2, n, 1.0f);
            f.discard_before(static_cast<int64_t>(at + n));
        }
        CHECK(rms_db(out, 2.6, 3.8, 2) < rms_db(v, 2.6, 3.8) - 20);    // пауза: -25 дБ
        CHECK(std::abs(rms_db(out, 1.1, 1.9, 2) - rms_db(v, 1.1, 1.9)) < 0.5);   // фраза без змін
        const size_t onset = static_cast<size_t>(4.0 * R) + R / 500;   // 2 мс після початку фрази
        CHECK(std::abs(out[onset * 2] - v[onset]) < 0.1f * std::abs(v[onset]) + 1e-4f);
        // afftdn + гейт: так само, фраза ціла (запасний шумодав, коли немає моделі RNNoise;
        // сама RNNoise синус як "не мову" приглушила б)
        audio::FilteredInput f2("голос", {{{in.get(), 1.0f}}}, true);
        CHECK(f2.open("afftdn=nr=12:nf=-60:tn=1", &gp, &err));
        std::fill(out.begin(), out.end(), 0.0f);
        for (size_t at = 0; at < v.size(); at += 4096)
            f2.mix(static_cast<int64_t>(at), out.data() + at * 2, std::min<size_t>(4096, v.size() - at), 1.0f);
        CHECK(std::abs(rms_db(out, 1.1, 1.9, 2) - rms_db(v, 1.1, 1.9)) < 1.0);
        CHECK(rms_db(out, 2.6, 3.8, 2) < rms_db(v, 2.6, 3.8) - 20);
        // Початок не з нуля (експорт фрагмента): позиції ті самі
        audio::FilteredInput f3("голос", {{{in.get(), 1.0f}}}, true);
        CHECK(f3.open({}, &gp, &err));
        f3.start_at(static_cast<int64_t>(3.9 * R));
        std::vector<float> part(R * 2, 0.0f);
        f3.mix(static_cast<int64_t>(4.0 * R), part.data(), R / 2, 1.0f);
        CHECK(std::abs(part[(R / 4) * 2] - v[static_cast<size_t>(4.25 * R)]) < 1e-5f);
    }

    // ---- нейромережевий шумодав RNNoise (arnndn): модель поруч із програмою ----
    {
        CHECK(audio::filter_path_arg("C:/a b/c,d'[e];f") == R"(C\\:/a b/c\,d\\\'\[e\]\;f)");
        const auto model = audio::voice_denoise_model();
        CHECK(!model.empty());
        CHECK(audio::denoise_filter(-50).rfind("arnndn=", 0) == 0);
        if (!model.empty()) {
            // Шлях з усім, що треба екранувати в описі графа, і кирилицею
            namespace fs = std::filesystem;
            const fs::path dir = fs::temp_directory_path() / path_from_utf8("gmdr тест, [x]; 'q'");
            fs::create_directories(dir);
            const fs::path copy = dir / "m,1.rnnn";
            fs::copy_file(model, copy, fs::copy_options::overwrite_existing);
            audio::AudioFilterChain c;
            std::string err;
            CHECK(c.open("arnndn=m=" + audio::filter_path_arg(copy), 1, 1, &err));
            if (!err.empty()) std::printf("  %s\n", err.c_str());
            std::vector<float> noise(static_cast<size_t>(4 * R));
            for (auto& x : noise) x = gauss(rng) * 0.03f;   // шум -30 дБ, без мови
            for (size_t at = 0; at < noise.size(); at += 4096)
                c.push(0, noise.data() + at, std::min<size_t>(4096, noise.size() - at), &err);
            CHECK(c.finish(&err));
            CHECK(std::llabs(c.out_end() - static_cast<int64_t>(noise.size())) <= 480);
            std::vector<float> out;
            for (int64_t p = R; p < std::min<int64_t>(c.out_end(), 4 * R); ++p) out.push_back(c.out_at(p)[0]);
            CHECK(rms_db(out, 0, 3) < rms_db(noise, 1, 4) - 15);   // шум без мови глушиться
            fs::remove_all(dir);
        }
    }

    // ---- гра стихає під голоси (sidechaincompress) ----
    {
        auto game = std::make_unique<VecInput>(sine(8, 150, 0.25), 3 * R);   // "живе" джерело: є лише 3 с
        std::vector<float> talk(static_cast<size_t>(8 * R), 0.0f);
        const auto t = sine(2, 400, 0.3);
        std::copy(t.begin(), t.end(), talk.begin() + 2 * R);
        auto voice = std::make_unique<VecInput>(talk);
        audio::FilteredInput d("гра", {{{game.get(), 1.0f}}, {{voice.get(), 1.0f}}}, false);
        std::string err;
        CHECK(d.open(audio::duck_filter(), nullptr, &err));
        const int64_t a = d.available();
        CHECK(a > 3 * R - 8192 && a <= 3 * R);   // не далі, ніж є звук гри
        game->set_available(INT64_MAX);
        std::vector<float> out(static_cast<size_t>(8 * R) * 2, 0.0f);
        for (int64_t at = 0; at < 8 * R; at += 4096)
            d.mix(at, out.data() + at * 2, static_cast<size_t>(std::min<int64_t>(4096, 8 * R - at)), 1.0f);
        const double before = rms_db(out, 0.5, 1.5, 2), during = rms_db(out, 3.0, 3.8, 2), after = rms_db(out, 6.5, 7.5, 2);
        CHECK(during < before - 6);
        CHECK(std::abs(after - before) < 1.0 && std::abs(before - 20 * std::log10(0.25 / std::sqrt(2.0))) < 0.5);
    }

    // ---- змішувач: фільтр доріжки віддає рівно стільки кадрів, скільки треба, і без зсуву ----
    {
        std::vector<float> ramp(static_cast<size_t>(3 * R));
        for (size_t i = 0; i < ramp.size(); ++i) ramp[i] = static_cast<float>(i % 1000) / 2000.0f;
        std::vector<std::unique_ptr<audio::AudioInput>> inputs;
        inputs.push_back(std::make_unique<VecInput>(ramp));
        audio::AudioInput* src = inputs.back().get();
        std::vector<audio::AudioTrackPlan> tracks = {{"з фільтром", {{src, 1.0f}}, "volume=0.5"},
                                                     {"без", {{src, 1.0f}}, ""}};
        audio::AudioMixer mixer(std::move(inputs), std::move(tracks));
        std::vector<std::vector<float>> got(2);
        auto sink = [&](size_t t, const float* d, size_t n) { got[t].insert(got[t].end(), d, d + n * 2); };
        const int64_t total = 2 * R + 123;
        mixer.produce(R, sink);
        mixer.produce(total, sink);
        mixer.flush(total, sink);
        CHECK(got[0].size() == static_cast<size_t>(total) * 2 && got[1].size() == static_cast<size_t>(total) * 2);
        bool aligned = got[0].size() == got[1].size();
        for (size_t i = 0; aligned && i < got[0].size(); i += 777) aligned = std::abs(got[0][i] - got[1][i] * 0.5f) < 1e-4f;
        CHECK(aligned);
    }
}

static void test_rtx_profile() {
    std::printf("[rtx profile]\n");
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "gmdr_test_rtx" / "GarrysMod";
    fs::remove_all(root.parent_path());
    fs::create_directories(root / "garrysmod");
    fs::create_directories(root / "rtx-remix" / "logs");
    write_file_text(root / "garrysmod" / "gameinfo.txt", "GameInfo {}");
    write_file_text(root / "hl2.exe", "");
    auto g = game::gmod_from_dir(root);
    CHECK(g.has_value() && g->valid());
    if (!g) return;
    CHECK(game::is_rtx_install(*g));
    const fs::path backup = root.parent_path() / "rtx.conf.bak";

    // 1) rtx.conf не було: після рендеру файл має зникнути
    std::string err;
    CHECK(game::apply_rtx_render_profile(*g, backup, &err));
    auto conf = read_file_text(root / "rtx.conf");
    CHECK(conf && conf->find("rtx.qualityDLSS = 5") != std::string::npos);
    CHECK(conf && conf->find("rtx.shader.enableAsyncCompilation = False") != std::string::npos);   // без чорних кадрів
    CHECK(game::restore_rtx_profile(*g, backup));
    CHECK(!fs::exists(root / "rtx.conf") && !fs::exists(backup));

    // 2) свій rtx.conf із тим самим ключем: без дублів, оригінал повертається байт у байт
    const std::string original = "rtx.enableRaytracing = True\r\nrtx.qualityDLSS = 2\r\n";
    write_file_text(root / "rtx.conf", original);
    CHECK(game::apply_rtx_render_profile(*g, backup, &err));
    CHECK(game::apply_rtx_render_profile(*g, root.parent_path() / "second.bak", &err));   // двічі — один блок
    conf = read_file_text(root / "rtx.conf");
    CHECK(conf && conf->find("rtx.qualityDLSS = 2") == std::string::npos);
    CHECK(conf && conf->find("rtx.enableRaytracing = True") != std::string::npos);
    CHECK(conf && conf->find("# GMod Demo Render") == conf->rfind("# GMod Demo Render"));
    CHECK(conf && conf->find("rtx.qualityDLSS = 5") == conf->rfind("rtx.qualityDLSS = 5"));
    CHECK(game::restore_rtx_profile(*g, backup));
    CHECK(read_file_text(root / "rtx.conf").value_or("") == original);

    // 3) журнал Remix: береться останній блок, лише рядки rtx.*
    write_file_text(root / "rtx-remix" / "logs" / "remix-dxvk.log",
                    "info:  Effective RtxOption values\ninfo:    rtx.qualityDLSS = 1\n"
                    "info:  Effective RtxOption values\ninfo:    rtx.qualityDLSS = 5\n"
                    "info:    rtx.graphicsPreset = 4\ninfo:  Device created\ninfo:    rtx.fake = 1\n");
    auto eff = game::read_remix_effective_options(*g);
    CHECK(eff.size() == 2);
    CHECK(eff["rtx.qualityDLSS"] == "5" && eff["rtx.graphicsPreset"] == "4");

    // Копія гри за режимом: «Стандарт» — game_dir, RTX — rtx_game_dir або RTX-копія, вказана
    // як звичайна папка гри (старі налаштування)
    const fs::path plain = root.parent_path() / "Plain";
    fs::create_directories(plain / "garrysmod");
    write_file_text(plain / "garrysmod" / "gameinfo.txt", "GameInfo {}");
    write_file_text(plain / "hl2.exe", "");
    render::RenderSettings s;
    s.game_dir = path_to_utf8(plain);
    s.rtx_game_dir = path_to_utf8(root);
    auto pick = [&] {
        auto l = render::locate_game(s);
        return l ? l->root : fs::path();
    };
    CHECK(pick() == plain);
    s.rtx = true;
    CHECK(pick() == root);
    s.rtx_game_dir.clear();
    s.game_dir = path_to_utf8(root);
    CHECK(pick() == root);
    fs::remove_all(root.parent_path());
}

int main(int argc, char** argv) {
    set_min_log_level(LogLevel::Warn);
    add_log_sink([](LogLevel l, const std::string& s) { std::printf("  [%s] %s\n", log_level_name(l), s.c_str()); });
    media::install_ffmpeg_log_bridge(AV_LOG_ERROR);
    test_bitreader();
    test_strings_json_vdf();
    test_steam_voice_crc();
    test_lzss();
    test_tga();
    test_blender();
    test_color_conversion();
    test_subtitles_and_sizes();
    test_frame_files();
    test_frame_pipe();
    test_frame_transport_choice();
    test_job_elapsed();
    test_driver_cfg();
    test_extra_versions();
    test_derived_outputs();
    test_report_zip();
    test_dem_association();
    test_power_action();
    test_update_check();
    test_speaker_overlay();
    test_edit_package();
    test_rtx_profile();
    test_chat_and_markers();
    test_audio_filters();
    test_speed();
    test_game_audio_segments();
    test_fragmented_mp4();
    test_plan_parts();
    test_part_assembly();
    test_resume_record();
    test_speech();
    test_translate_dub();
    test_i18n();
    if (argc > 1) {
        const std::filesystem::path dir = argv[1];
        if (std::filesystem::exists(dir / "test24.dem")) test_demo(dir / "test24.dem", 24);
        if (std::filesystem::exists(dir / "test20c.dem")) test_demo(dir / "test20c.dem", 20);
        if (std::filesystem::exists(dir / "test2026.dem")) test_demo(dir / "test2026.dem", 24, true);
        if (std::filesystem::exists(dir / "test24.dem")) test_fuzz_demo(dir / "test24.dem");
        if (std::filesystem::exists(dir / "test24.dem") && std::filesystem::exists(dir / "test20c.dem")) test_demo_library(dir);
    }
    std::printf("\nПройдено: %d, провалено: %d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
