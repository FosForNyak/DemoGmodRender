// =============================================================================
//  test_main.cpp — модульні тести ядра (без зовнішніх фреймворків).
//
//  Запуск:  gmdr-tests [папка_з_тестовими_даними]
//  Тести демо використовують синтетичні файли з tests/tools/make_test_demo.py
// =============================================================================
#include "core/audio/wav.hpp"
#include "core/demo/analysis.hpp"
#include "core/demo/bitreader.hpp"
#include "core/demo/string_tables.hpp"
#include "core/frames/blender.hpp"
#include "core/frames/image_decode.hpp"
#include "core/frames/tga.hpp"
#include "core/frames/sequence_reader.hpp"
#include "core/game/lua_driver.hpp"
#include "core/game/process.hpp"
#include "core/render/jobs.hpp"
#include "core/render/subtitles.hpp"
#include "core/util/file_util.hpp"
#include "core/media/ffmpeg_util.hpp"
#include "core/media/video_encoder.hpp"
#include "core/util/json.hpp"
#include "core/util/log.hpp"
#include "core/util/strings.hpp"
#include "core/util/vdf.hpp"
#include "core/voice/steam_voice.hpp"
#include "core/voice/voice_decoder.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>
#include <random>
#include <string>
#include <thread>

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
    test_job_elapsed();
    test_driver_cfg();
    if (argc > 1) {
        const std::filesystem::path dir = argv[1];
        if (std::filesystem::exists(dir / "test24.dem")) test_demo(dir / "test24.dem", 24);
        if (std::filesystem::exists(dir / "test20c.dem")) test_demo(dir / "test20c.dem", 20);
        if (std::filesystem::exists(dir / "test2026.dem")) test_demo(dir / "test2026.dem", 24, true);
        if (std::filesystem::exists(dir / "test24.dem")) test_fuzz_demo(dir / "test24.dem");
    }
    std::printf("\nПройдено: %d, провалено: %d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
