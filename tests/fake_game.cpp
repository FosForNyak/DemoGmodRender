// =============================================================================
//  fake_game.cpp — ІМІТАТОР Garry's Mod для тестування конвеєра без гри.
//
//  Поводиться як GMod + наш Lua-драйвер:
//   * читає garrysmod/data/gmdr/job.txt і конфіг garrysmod/cfg/gmdr/job_<id>.cfg
//   * пише статус garrysmod/data/gmdr/status_<id>.txt
//   * "рендерить" кадри TGA (як startmovie) і звук WAV, що росте під час запису
//   * кожну цілу секунду — білий спалах у кадрі і "біп" 1 кГц у звуці
//     (так перевіряється синхронізація звуку і відео)
// =============================================================================
#include "core/audio/wav.hpp"
#include "core/demo/demo_file.hpp"
#include "core/frames/tga.hpp"
#include "core/media/ffmpeg_util.hpp"
#include "core/util/file_util.hpp"
#include "core/util/json.hpp"
#include "core/util/strings.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <thread>

using namespace gmdr;
namespace fs = std::filesystem;

static void write_status(const fs::path& p, const std::string& state, int tick, int total, int start_tick, int last_tick,
                         const std::string& msg = "") {
    json::Value j = json::Value::object();
    j.set("state", json::Value::string(state));
    j.set("message", json::Value::string(msg));
    j.set("tick", json::Value::number(tick));
    j.set("total", json::Value::number(total));
    j.set("start_tick", json::Value::number(start_tick));
    j.set("last_tick", json::Value::number(last_tick));
    j.set("playing", json::Value::boolean(state == "recording" || state == "loading"));
    write_file_atomic(p, j.dump(), nullptr);
}


// JPEG через FFmpeg (як startmovie з параметром jpeg)
static std::vector<uint8_t> encode_jpeg(const frames::Image& img, int quality) {
    using namespace gmdr::media;
    static CodecCtxPtr ctx;
    static SwsContext* sws = nullptr;
    static FramePtr frame;
    if (!ctx) {
        const AVCodec* c = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
        ctx.reset(avcodec_alloc_context3(c));
        ctx->width = img.width;
        ctx->height = img.height;
        ctx->pix_fmt = AV_PIX_FMT_YUVJ420P;
        ctx->time_base = {1, 25};
        ctx->flags |= AV_CODEC_FLAG_QSCALE;
        ctx->global_quality = FF_QP2LAMBDA * std::max(2, 31 - quality * 30 / 100);
        ctx->thread_count = 1;
        ctx->color_range = AVCOL_RANGE_JPEG;
        ctx->strict_std_compliance = FF_COMPLIANCE_UNOFFICIAL;
        const int r = avcodec_open2(ctx.get(), c, nullptr);
        if (r < 0) std::fprintf(stderr, "fake_game: mjpeg: %s\n", av_error_string(r).c_str());
        sws = sws_getContext(img.width, img.height, AV_PIX_FMT_BGR24, img.width, img.height, AV_PIX_FMT_YUVJ420P,
                             SWS_BICUBIC, nullptr, nullptr, nullptr);
        frame = make_frame();
        frame->format = AV_PIX_FMT_YUVJ420P;
        frame->width = img.width;
        frame->height = img.height;
        av_frame_get_buffer(frame.get(), 32);
    }
    const uint8_t* src[1] = {img.row(0, 0)};
    const int stride[1] = {img.stride[0]};
    sws_scale(sws, src, stride, 0, img.height, frame->data, frame->linesize);
    frame->quality = ctx->global_quality;
    static int64_t pts = 0;
    frame->pts = pts++;
    frame->color_range = AVCOL_RANGE_JPEG;
    const int r = avcodec_send_frame(ctx.get(), frame.get());
    if (r < 0) std::fprintf(stderr, "fake_game: mjpeg send: %s\n", av_error_string(r).c_str());
    PacketPtr pkt = make_packet();
    std::vector<uint8_t> out;
    if (avcodec_receive_packet(ctx.get(), pkt.get()) == 0) out.assign(pkt->data, pkt->data + pkt->size);
    return out;
}

int main(int argc, char** argv) {
    int w = 640, h = 360;
    double max_fps = 0;   // обмеження швидкості "гри" (0 — без обмеження)
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-w" && i + 1 < argc) w = std::atoi(argv[++i]);
        else if (a == "-h" && i + 1 < argc) h = std::atoi(argv[++i]);
        else if (a == "-fakefps" && i + 1 < argc) max_fps = std::atof(argv[++i]);
    }
    const fs::path gm = fs::current_path() / "garrysmod";
    std::ofstream con(gm / "console.log");
    con << "FakeGMod: старт " << w << "x" << h << "\n";
    auto job_text = read_file_text(gm / "data" / "gmdr" / "job.txt");
    if (!job_text) {
        con << "FakeGMod: немає завдання, виходжу\n";
        std::fprintf(stderr, "fake_game: немає job.txt\n");
        return 3;
    }
    fs::remove(gm / "data" / "gmdr" / "job.txt");
    auto job = json::parse(*job_text);
    const std::string id = (*job)["id"].as_string();
    const fs::path status = gm / "data" / "gmdr" / ("status_" + id + ".txt");
    const fs::path cancel = gm / "data" / "gmdr" / ("cancel_" + id + ".txt");
    const std::string movie = (*job)["movie"].as_string();
    const double rate = (*job)["host_framerate"].as_number();
    const int start_req = static_cast<int>((*job)["start_tick"].as_int());
    const int end_req = static_cast<int>((*job)["end_tick"].as_int(-1));
    const bool jpeg = (*job)["movie_flags"][0].as_string() == "jpeg";

    // Перевіряємо, що конфіг існує і містить потрібні аліаси
    auto cfg = read_file_text(gm / "cfg" / "gmdr" / ("job_" + id + ".cfg"));
    if (!cfg || cfg->find("alias gmdr_start") == std::string::npos || cfg->find("host_framerate") == std::string::npos) {
        write_status(status, "error", 0, 0, -1, -1, "немає конфігу завдання");
        return 4;
    }
    const std::string demo_rel = (*job)["demo"].as_string();
    // Як у рушії (Q_DefaultExtension): ".dem" додається, лише якщо розширення немає
    const bool has_ext = demo_rel.size() > 4 && demo_rel.compare(demo_rel.size() - 4, 4, ".dem") == 0;
    demo::DemoFile df(gm / (has_ext ? demo_rel : demo_rel + ".dem"));
    const int total = df.header().playback_ticks;
    const double ti = total > 0 ? df.header().playback_time / total : 1.0 / 66.0;

    write_status(status, "menu", 0, total, -1, -1);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    write_status(status, "loading", 0, total, -1, -1);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    const int start_tick = std::max(start_req, 3);   // "завантаження" з'їдає кілька тіків
    const int end_tick = end_req > 0 ? std::min(end_req, total) : total;
    // FAKE_STRIP_DIR=1 — імітувати рушій, що ігнорує папку в назві фільму (пише в garrysmod/)
    const bool strip = std::getenv("FAKE_STRIP_DIR") != nullptr;
    const fs::path movie_path = strip ? gm / fs::path(movie).filename() : gm / movie;
    audio::WavWriter wav;
    wav.open(fs::path(movie_path.string() + ".wav"), 44100, 2, audio::WavWriter::Format::Int16);
    frames::Image img;
    img.allocate(w, h, frames::PixelLayout::BGR24);
    int64_t frame = 0;
    double audio_carry = 0;
    int64_t audio_pos = 0;
    int last_tick = start_tick;
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<float> abuf;
    con << "FakeGMod: запис, host_framerate " << rate << "\n";
    for (;;) {
        const double t = frame / rate;                           // час від початку запису
        const int tick = start_tick + static_cast<int>(t / ti);
        if (tick >= end_tick || fs::exists(cancel)) break;
        last_tick = tick;
        // ---- Кадр ----
        const bool flash = std::floor(t) != std::floor((frame - 1) / rate) && frame > 0;
        const int shift = static_cast<int>(t * 200) % w;
        for (int y = 0; y < h; ++y) {
            uint8_t* row = img.row(0, y);
            for (int x = 0; x < w; ++x) {
                const int xs = (x + shift) % w;
                uint8_t v = flash ? 255 : static_cast<uint8_t>(40 + (xs * 120) / w);
                row[x * 3 + 0] = v;
                row[x * 3 + 1] = flash ? 255 : static_cast<uint8_t>((y * 150) / h);
                row[x * 3 + 2] = flash ? 255 : 30;
            }
        }
        const std::string name = std::format("{}{:04d}.{}", movie_path.string(), frame, jpeg ? "jpg" : "tga");
        auto bytes = jpeg ? encode_jpeg(img, 90) : frames::encode_tga(img, true, false);
        {
            std::ofstream f(name, std::ios::binary);
            f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        // ---- Звук: 44100/rate семплів на кадр ----
        audio_carry += 44100.0 / rate;
        const int n = static_cast<int>(audio_carry);
        audio_carry -= n;
        abuf.assign(static_cast<size_t>(n) * 2, 0.0f);
        for (int i = 0; i < n; ++i) {
            const double ts = static_cast<double>(audio_pos + i) / 44100.0;
            float v = 0.05f * static_cast<float>(std::sin(2 * 3.14159265358979323846 * 220 * ts));   // тихий фон
            if (ts >= 1.0 && std::fmod(ts, 1.0) < 0.05) v = 0.5f * static_cast<float>(std::sin(2 * 3.14159265358979323846 * 1000 * ts));
            abuf[static_cast<size_t>(i) * 2] = v;
            abuf[static_cast<size_t>(i) * 2 + 1] = v;
        }
        wav.write(abuf.data(), static_cast<size_t>(n));
        audio_pos += n;
        ++frame;
        if (frame % 5 == 0) write_status(status, "recording", tick, total, start_tick, last_tick);
        if (max_fps > 0) {
            const auto target = t0 + std::chrono::duration<double>(frame / max_fps);
            std::this_thread::sleep_until(target);
        }
    }
    wav.close();
    write_status(status, "stopping", last_tick, total, start_tick, last_tick);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    write_status(status, "done", last_tick, total, start_tick, last_tick);
    con << "FakeGMod: записано " << frame << " кадрів\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    write_status(status, "quit", last_tick, total, start_tick, last_tick);
    return 0;
}
