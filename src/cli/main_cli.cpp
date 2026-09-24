// =============================================================================
//  main_cli.cpp — консольна версія GMod Demo Render (gmdr-cli).
//
//  Приклади:
//    gmdr-cli info  demo.dem
//    gmdr-cli voice demo.dem -o voices/
//    gmdr-cli render demo.dem -o video.mp4 --size 2560x1440 --fps 60 --codec hevc_nvenc --bit-depth 10
//    gmdr-cli render demo.dem -o film.mov --codec prores_ks --motion-blur 16 --shutter 180
//    gmdr-cli encode "C:\...\garrysmod" --prefix movie -o out.mkv --codec ffv1
//    gmdr-cli encoders --test
// =============================================================================
#include "core/demo/analysis.hpp"
#include "core/game/gmod_install.hpp"
#include "core/game/lua_driver.hpp"
#include "core/media/ffmpeg_util.hpp"
#include "core/media/video_encoder.hpp"
#include "core/dub/tts.hpp"
#include "core/dub/voice_library.hpp"
#include "core/render/dub_jobs.hpp"
#include "core/render/dubbing.hpp"
#include "core/render/jobs.hpp"
#include "core/translate/translate.hpp"
#include "core/util/secret.hpp"
#include "core/render/markers.hpp"
#include "core/render/report.hpp"
#include "core/render/settings.hpp"
#include "core/render/subtitles.hpp"
#include "core/render/versions.hpp"
#include "core/util/crash_dump.hpp"
#include "core/util/file_util.hpp"
#include "core/util/log.hpp"
#include "core/util/power.hpp"
#include "core/util/update_check.hpp"
#include "core/util/strings.hpp"
#include "core/voice/voice_decoder.hpp"
#include "core/util/i18n.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <format>
#include <iostream>
#include <map>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

using namespace gmdr;
namespace fs = std::filesystem;

static std::atomic<int> g_interrupts{0};
static void on_sigint(int) { ++g_interrupts; }
static PowerAction g_then = PowerAction::None;   // --then: що зробити після рендеру чи черги

static void print_usage() {
    std::puts(tr(R"(GMod Demo Render — рендер демо Garry's Mod у відео (консольна версія)

Використання:
  gmdr-cli info <demo.dem> [--json] [--chat]   інформація про демо і голоси (--chat — увесь чат)
  gmdr-cli voice <demo.dem> -o <папка>         зберегти голоси гравців у WAV
  gmdr-cli transcribe <demo.dem> [-o файл]     розпізнати мовлення гравців (whisper.cpp, локально):
                                               .txt — репліки з часом, .srt — субтитри, .json — усе
  gmdr-cli whisper [--download N]              де whisper-cli і моделі; завантажити модель N (1 — найточніша)
  gmdr-cli translate <demo.dem> --translate en,de   перекласти розпізнане мовлення в субтитри
                                               <демо>.<мова>.srt (або поруч із -o), без рендеру
  gmdr-cli voices [delete КЛЮЧ | clear]        бібліотека голосів гравців (для клонування)
  gmdr-cli voice-engine [install [--cpu] -y | check | remove]   локальний рушій озвучення
                                               OmniVoice (~5 ГБ: Python, PyTorch, модель)
  gmdr-cli render <demo.dem> [параметри]       відрендерити демо через гру
  gmdr-cli render <demo.dem> --test-run         тестовий прогін: 3 с, звіт по кроках і прогноз часу
  gmdr-cli render a.dem b.dem ... -o <папка>   черга: кілька демо підряд, гра запускається один раз
  gmdr-cli queue <список.txt> [параметри]      черга з файлу: у рядку — демо і його параметри
                                               ("match.dem" --start 5:00 --end 7:30 -o "бій.mp4")
  gmdr-cli resume [--forget] [--parallel N]    дописати рендер, урваний збоєм програми чи ПК (--forget —
                                               забути про нього)
  gmdr-cli watch <demo.dem> [--from ЧАС]       переглянути демо в грі з цього місця; клавіші в грі:
                                               F9 — початок фрагмента, F11 — кінець, F6 — позначка
  gmdr-cli encode <папка_кадрів> [параметри]   закодувати готові кадри startmovie (TGA/JPG + WAV)
  gmdr-cli encoders [--test]                   список кодеків (і перевірка GPU-кодеків)
  gmdr-cli report [-o файл.zip]                звіт про проблему: журнал, налаштування, система,
                                               консоль GMod (нічого не надсилається — лише файл)
  gmdr-cli --version                           версія програми
  gmdr-cli update                              чи вийшла нова версія (запит до GitHub Releases)
  gmdr-cli driver install|uninstall|status     драйвер у меню GMod

Відео:
  -o, --output ФАЙЛ      вихідний файл (.mp4 .mkv .mov .webm .avi ... або кадри_%06d.png)
  --size ШxВ             роздільна здатність відео (типово 1920x1080)
  --render-size ШxВ      розмір вікна гри (типово = --size; більше — суперсемплінг)
  --fps N                частота кадрів: 24, 30, 59.94, 60, 120, 240, 60000/1001 ...
  --motion-blur N        під-кадрів на кадр для розмиття руху (1 — вимкнено)
  --shutter ГРАДУСИ      кут затвора для motion blur (типово 180)
  --speed N              швидкість: 0.5 — уповільнення вдвічі, 0.25 — вчетверо, 4 — прискорення (0.1..16)
  --speed-audio stretch|mute   звук при зміні швидкості: розтягнути (висота тону та сама) або без звуку
  --codec НАЗВА          libx264, libx265, libsvtav1, libvpx-vp9, prores_ks, ffv1, png,
                         h264_nvenc, hevc_nvenc, av1_nvenc, h264_amf, hevc_amf, h264_qsv ...
  --bit-depth 8|10|12    бітність, --chroma 420|422|444, --pix-fmt ФОРМАТ
  --quality N            CRF/CQ/QP (менше — краще), --bitrate 20M, --preset ПРЕСЕТ
  --vopt ключ=значення   довільний параметр кодека (можна кілька разів)
  --scaler lanczos|bicubic|bilinear|spline   --full-range   --gop СЕКУНД   --threads N
  --accurate-color       максимальна точність кольору (повільніше)
  --target-size МБ       бітрейт під розмір файлу (напр. 10 для Discord)
  --also discord,480p,vertical,master,thumb,gif,webp   ще версії поруч з основним файлом:
                         Discord до 10 МБ, легка 480p, вертикальна 9:16, ProRes для монтажу (з тих самих
                         кадрів); обкладинка JPG, GIF і WebP (перші 15 с) — з готового відео
Звук:
  --no-audio  --acodec aac|libopus|flac|pcm_s16le|pcm_s24le|alac|libmp3lame  --abitrate 320k
  --sample-rate 48000  --no-game-audio  --game-volume 1.0  --audio-offset СЕКУНД
  --voice all|local|others|none|selected  --voice-keys steam:7656...,slot:3
  --voice-volume 1.0  --voice-delay СЕКУНД  --separate-tracks  --engine-voice
  --player-volume "steam:7656...=1.5; slot:3=0"   гучність окремих гравців (0 — вимкнути)
  --level-voices         вирівняти гучність гравців (кожного — до -18 LUFS)
  --denoise              шумодав (RNNoise) і тиша між фразами для всіх гравців; --denoise-player steam:7656...,slot:3 — для вибраних
  --duck-game            звук гри стихає, коли хтось говорить
  --loudness LUFS|off    гучність загального міксу за EBU R128 (-14 — YouTube, -23 — ТБ)
  --srt                  субтитри «хто говорить» (.srt поруч із відео)
  --speaker-overlay      підписи «хто говорить» прямо на кадрі (плашки праворуч унизу)
  --edit-package         пакет для монтажу: окремі WAV (гра, кожен гравець, мікрофон) і проєкт XML
                         для Premiere / DaVinci Resolve у теці «назва_монтаж» поруч із відео
  --chat-srt             субтитри з чатом гри (.srt; разом із --srt — .chat.srt)
  --speech-srt           у субтитрах — текст розмов (розпізнане мовлення; вмикає --srt)
  --language auto|uk|ru|en...   мова розмов для розпізнавання (типово — визначити самому)
  --whisper-cli ФАЙЛ  --whisper-model ФАЙЛ    whisper-cli і модель ggml-*.bin (типово — у теці whisper)
  --markers "1:02=Вступ; 2:30=Бій"   позначки -> розділи у MP4/MOV/MKV (типово — збережені для демо)
  --no-chapters          не записувати розділи
  --mic ФАЙЛ  --mic-offset СЕКУНД  --mic-volume 1.0
Переклад і озвучення (після рендеру; потрібне розпізнавання мовлення):
  --translate en,de,pl   мови перекладу -> субтитри <відео>.<мова>.srt
  --dub                  ще й озвучити переклад: --dub-to tracks,videos,audio (доріжки в цьому відео,
                         окреме відео на мову, окремі аудіофайли); --dub-format mp3|flac|wav|m4a
  --dub-original 0.12    гучність оригінальних голосів під озвученням (0 — прибрати)
  --publish youtube|tracks|shorts|discord|editing   готовий набір виходів під сервіс
  --translator deepl|google|libre|openai  --translator-url URL  --translator-model МОДЕЛЬ
                         (openai — будь-яка OpenAI-сумісна модель: Ollama, LM Studio, OpenAI...)
  --tts omnivoice|elevenlabs  --tts-device auto|cuda|cpu  --tts-python ФАЙЛ
  --elevenlabs-model ID  --elevenlabs-voice ID
  --clone-voices         озвучувати голосом самого гравця; лише разом із --voices-consent (ви
                         підтверджуєте, що гравці згодні на клонування їхнього голосу)
  --voice-library        накопичувати зразки голосів гравців з кожного розпізнаного демо
  Ключі — лише змінними середовища: GMDR_DEEPL_KEY, GMDR_GOOGLE_KEY, GMDR_LIBRE_KEY,
  GMDR_OPENAI_KEY, GMDR_ELEVENLABS_KEY (або збережені у вікні програми, зашифровано)
Гра:
  --game-dir ПАПКА  --game-exe ФАЙЛ  --capture tga|jpg  --jpeg-quality N
  --hide-hud  --hide-viewmodel  --exec "команда"  --launch-args "..."  --max-pending N
  --start-tick N  --end-tick N  --start ЧАС  --end ЧАС (секунди або год:хв:сек)  --keep-game-open  --manual
  --window offscreen|behind|normal   де вікно гри (типово — за межами екрана)
  --no-mute              не вимикати звук гри в мікшері Windows
  --rtx                  копія GMod RTX від RTXLauncher (її параметри запуску)
  --rtx-dir ПАПКА        папка копії GMod RTX (типово — з налаштувань RTXLauncher); вмикає --rtx
  --parallel N           рендерити фрагмент частинами в N копіях гри одночасно (2..4, -multirun)
  --frame-transport auto|pipe|files   як кадри йдуть з гри: auto — каналом, без файлів на диску
                         (якщо гра в канал не пише — файлами), pipe — лише каналом, files — файлами
Інше:
  --config ФАЙЛ.json  --save-config ФАЙЛ.json  --keep-temp  -v (детальний журнал)
  --lang en|uk|de|pl...  мова повідомлень (English, українська, Deutsch, Polski…; або змінна GMDR_LANG)
  --no-crash-safe        звичайний MP4 під час запису (типово — фрагментами, вціліє при збої)
  --then shutdown|sleep  після рендеру чи черги вимкнути ПК або сон (60 с на скасування: Ctrl+C)
  encode: --prefix ПРЕФІКС  --wav ФАЙЛ  --demo ДЕМО.dem (для голосу)
)"));
}

static std::vector<std::string> utf8_args(int argc, char** argv) {
    std::vector<std::string> out;
#ifdef _WIN32
    int n = 0;
    LPWSTR* w = CommandLineToArgvW(GetCommandLineW(), &n);
    for (int i = 0; i < n; ++i) out.push_back(wide_to_utf8(w[i]));
    LocalFree(w);
    (void)argc;
    (void)argv;
#else
    for (int i = 0; i < argc; ++i) out.emplace_back(argv[i]);
#endif
    return out;
}

struct Cli {
    std::string              command;
    std::vector<std::string> positional;
    std::map<std::string, std::vector<std::string>> opts;
    bool has(const std::string& k) const { return opts.count(k) > 0; }
    std::string get(const std::string& k, const std::string& def = {}) const {
        auto it = opts.find(k);
        return it == opts.end() || it->second.empty() ? def : it->second.back();
    }
};

// Прапорці без значення
static const char* kFlags[] = {"--json", "--chat", "--test", "--hide-hud", "--hide-viewmodel", "--keep-game-open", "--manual",
                               "--no-audio", "--no-game-audio", "--separate-tracks", "--engine-voice", "--full-range",
                               "--keep-temp", "-v", "--verbose", "--no-faststart", "--mix", "-h", "--help", "-y",
                               "--test-run", "--no-mute", "--rtx", "--srt", "--accurate-color", "--no-crash-safe",
                               "--chat-srt", "--no-chapters", "--level-voices", "--denoise", "--duck-game",
                               "--speaker-overlay", "--version", "--edit-package", "--speech-srt", "--force", "--forget",
                               "--dub", "--clone-voices", "--voices-consent", "--voice-library", "--cpu"};

static bool is_flag(const std::string& a) {
    for (const char* f : kFlags)
        if (a == f) return true;
    return false;
}

static Cli parse_cli(const std::vector<std::string>& args) {
    Cli c;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a.size() > 1 && a[0] == '-' && !(a.size() > 1 && std::isdigit(static_cast<unsigned char>(a[1])))) {
            std::string key = a == "-o" ? "--output" : a;
            if (is_flag(key)) {
                c.opts[key].push_back("1");
            } else if (i + 1 < args.size()) {
                c.opts[key].push_back(args[++i]);
            } else {
                c.opts[key].push_back("");
            }
        } else if (c.command.empty()) {
            c.command = a;
        } else {
            c.positional.push_back(a);
        }
    }
    return c;
}

static bool apply_options(const Cli& c, render::RenderSettings& s, const demo::DemoAnalysis* a, std::string& err) {
    auto num = [&](const char* k, auto& field) {
        if (!c.has(k)) return true;
        auto v = parse_double(c.get(k));
        if (!v) { err = trf("неправильне значення {} '{}'", k, c.get(k)); return false; }
        field = static_cast<std::remove_reference_t<decltype(field)>>(*v);
        return true;
    };
    if (c.has("--output")) s.output_path = c.get("--output");
    if (c.has("--game-dir")) s.game_dir = c.get("--game-dir");
    if (c.has("--game-exe")) s.game_exe = c.get("--game-exe");
    if (c.has("--size")) {
        auto sz = parse_size(c.get("--size"));
        if (!sz) { err = tr("неправильний --size"); return false; }
        s.width = sz->first;
        s.height = sz->second;
    }
    if (c.has("--render-size")) {
        auto sz = parse_size(c.get("--render-size"));
        if (!sz) { err = tr("неправильний --render-size"); return false; }
        s.render_width = sz->first;
        s.render_height = sz->second;
    }
    if (c.has("--fps")) s.fps = c.get("--fps");
    if (!num("--motion-blur", s.motion_blur) || !num("--shutter", s.shutter) || !num("--bit-depth", s.bit_depth) ||
        !num("--chroma", s.chroma) || !num("--quality", s.quality) || !num("--gop", s.gop_seconds) ||
        !num("--threads", s.threads) || !num("--sample-rate", s.sample_rate) || !num("--game-volume", s.game_volume) ||
        !num("--audio-offset", s.game_audio_offset) || !num("--voice-volume", s.voice_volume) ||
        !num("--voice-delay", s.voice_delay) || !num("--mic-offset", s.mic_offset) ||
        !num("--mic-volume", s.mic_volume) || !num("--jpeg-quality", s.jpeg_quality) ||
        !num("--max-pending", s.max_pending_frames) || !num("--start-tick", s.start_tick) ||
        !num("--end-tick", s.end_tick) || !num("--menu-delay", s.menu_delay) ||
        !num("--target-size", s.target_size_mb))
        return false;
    if (c.has("--codec")) s.video_codec = c.get("--codec");
    if (c.has("--pix-fmt")) s.pix_fmt = c.get("--pix-fmt");
    if (c.has("--bitrate")) s.video_bitrate = c.get("--bitrate");
    if (c.has("--preset")) s.preset = c.get("--preset");
    if (c.has("--vopt")) {
        for (const auto& v : c.opts.at("--vopt")) s.video_options += (s.video_options.empty() ? "" : "; ") + v;
    }
    if (c.has("--scaler")) s.scaler = c.get("--scaler");
    if (c.has("--full-range")) s.full_range = true;
    if (c.has("--no-audio")) s.audio = false;
    if (c.has("--acodec")) s.audio_codec = c.get("--acodec");
    if (c.has("--abitrate")) s.audio_bitrate = c.get("--abitrate");
    if (c.has("--no-game-audio")) s.game_audio = false;
    if (c.has("--voice")) s.voice_mode = c.get("--voice");
    if (c.has("--voice-keys")) {
        s.voice_selected = c.get("--voice-keys");
        if (!c.has("--voice")) s.voice_mode = "selected";
    }
    if (c.has("--separate-tracks")) s.separate_tracks = true;
    if (c.has("--engine-voice")) s.mute_engine_voice = false;
    if (c.has("--mic")) s.mic_file = c.get("--mic");
    if (c.has("--capture")) s.capture_format = c.get("--capture");
    if (c.has("--frame-transport")) {
        const std::string v = to_lower(c.get("--frame-transport"));
        if (v != "auto" && v != "pipe" && v != "files") {
            err = tr("--frame-transport: auto, pipe або files");
            return false;
        }
        s.frame_transport = v;
    }
    if (c.has("--hide-hud")) s.hide_hud = true;
    if (c.has("--hide-viewmodel")) s.hide_viewmodel = true;
    if (c.has("--exec"))
        for (const auto& v : c.opts.at("--exec")) s.extra_commands += (s.extra_commands.empty() ? "" : "\n") + v;
    if (c.has("--launch-args")) s.extra_launch_args = c.get("--launch-args");
    if (c.has("--keep-game-open")) s.quit_game_when_done = false;
    if (c.has("--manual")) s.manual_mode = true;
    if (c.has("--format")) s.container = c.get("--format");
    if (c.has("--no-faststart")) s.faststart = false;
    if (c.has("--keep-temp")) s.keep_temp_files = true;
    if (c.has("--window")) {
        const std::string w = c.get("--window");
        if (w != "offscreen" && w != "behind" && w != "normal") { err = tr("--window: offscreen, behind або normal"); return false; }
        s.game_window = w;
    }
    if (c.has("--no-mute")) s.mute_game_sound = false;
    if (c.has("--rtx")) s.rtx = true;
    if (c.has("--rtx-dir")) {
        s.rtx_game_dir = c.get("--rtx-dir");
        s.rtx = true;
    }
    if (c.has("--parallel")) {
        const auto v = parse_int(c.get("--parallel"));
        if (!v || *v < 1 || *v > 4) { err = tr("--parallel: від 1 до 4 копій гри"); return false; }
        s.parallel_games = static_cast<int>(*v);
    }
    if (c.has("--srt")) s.subtitles_srt = true;
    if (c.has("--speaker-overlay")) s.speaker_overlay = true;
    if (c.has("--edit-package")) s.edit_package = true;
    if (c.has("--speech-srt")) s.speech_subtitles = s.subtitles_srt = true;
    if (c.has("--language")) s.whisper_language = c.get("--language");
    if (c.has("--whisper-cli")) s.whisper_cli = c.get("--whisper-cli");
    if (c.has("--whisper-model")) s.whisper_model = c.get("--whisper-model");
    if (c.has("--speed")) {
        const auto v = parse_double(c.get("--speed"));
        if (!v || *v < 0.1 || *v > 16) { err = tr("--speed: від 0.1 до 16 (0.5 — удвічі повільніше, 4 — учетверо швидше)"); return false; }
        s.speed = *v;
    }
    if (c.has("--speed-audio")) {
        const std::string v = c.get("--speed-audio");
        if (v != "stretch" && v != "mute") { err = tr("--speed-audio: stretch (розтягнути) або mute (без звуку)"); return false; }
        s.speed_audio = v;
    }
    if (c.has("--accurate-color")) s.accurate_color = true;
    // ---- Переклад і озвучення ----
    if (c.has("--publish")) {
        const render::PublishTemplate* t = render::find_template(c.get("--publish"));
        if (!t) { err = tr("--publish: youtube, tracks, shorts, discord або editing"); return false; }
        render::apply_template(s, *t);
    }
    if (c.has("--translate")) {
        for (const auto& code : split(c.get("--translate"), ','))
            if (!translate::find_language(trim(code))) {
                std::string all;
                for (const auto& l : translate::languages()) all += (all.empty() ? "" : ", ") + std::string(l.code);
                err = trf("--translate: невідома мова «{}» (є: {})", trim(code), all);
                return false;
            }
        s.dub_languages = c.get("--translate");
        if (!c.has("--publish")) s.translate_subtitles = true;
    }
    if (c.has("--dub")) s.dub = true;
    if (c.has("--dub-to")) {
        for (const auto& x : split(c.get("--dub-to"), ','))
            if (trim(x) != "tracks" && trim(x) != "videos" && trim(x) != "audio") {
                err = tr("--dub-to: tracks, videos, audio (через кому)");
                return false;
            }
        s.dub_outputs = c.get("--dub-to");
    }
    if (c.has("--dub-format")) {
        const std::string f = c.get("--dub-format");
        if (f != "mp3" && f != "flac" && f != "wav" && f != "m4a") { err = tr("--dub-format: mp3, flac, wav або m4a"); return false; }
        s.dub_audio_format = f;
    }
    if (!num("--dub-original", s.dub_original_volume)) return false;
    if (c.has("--translator")) {
        if (!translate::find_provider(c.get("--translator"))) { err = tr("--translator: deepl, google, libre або openai"); return false; }
        s.translator = c.get("--translator");
    }
    if (c.has("--translator-url")) s.translator_url = c.get("--translator-url");
    if (c.has("--translator-model")) s.translator_model = c.get("--translator-model");
    if (c.has("--tts")) {
        if (!dub::find_engine(c.get("--tts"))) { err = tr("--tts: omnivoice або elevenlabs"); return false; }
        s.tts_engine = c.get("--tts");
    }
    if (c.has("--tts-device")) s.tts_device = c.get("--tts-device");
    if (c.has("--tts-python")) s.tts_python = c.get("--tts-python");
    if (c.has("--elevenlabs-model")) s.elevenlabs_model = c.get("--elevenlabs-model");
    if (c.has("--elevenlabs-voice")) s.elevenlabs_voice = c.get("--elevenlabs-voice");
    if (c.has("--clone-voices")) s.tts_clone = true;
    if (c.has("--voices-consent")) s.tts_clone_ack = true;
    if (c.has("--voice-library")) s.voice_library_auto = true;
    if (s.tts_clone && !s.tts_clone_ack)
        std::fprintf(stderr, "%s", tr("УВАГА: клонування голосів вимкнено — додайте --voices-consent, якщо гравці згодні\n"));
    // Ключі сервісів — зі змінних середовища (у командному рядку вони лишились би в історії)
    for (auto [env, field] : {std::pair{"GMDR_DEEPL_KEY", &s.deepl_key}, std::pair{"GMDR_GOOGLE_KEY", &s.google_key},
                              std::pair{"GMDR_LIBRE_KEY", &s.libre_key}, std::pair{"GMDR_OPENAI_KEY", &s.openai_key},
                              std::pair{"GMDR_ELEVENLABS_KEY", &s.elevenlabs_key}})
        if (const char* v = std::getenv(env); v && *v) *field = protect_secret(v);
    if (c.has("--no-crash-safe")) s.crash_safe = false;
    if (c.has("--player-volume")) s.voice_volumes = c.get("--player-volume");
    if (c.has("--chat-srt")) s.chat_srt = true;
    if (c.has("--no-chapters")) s.chapters = false;
    if (c.has("--also")) {
        std::string bad;
        if (!render::valid_version_ids(c.get("--also"), &bad)) {
            err = tr("--also: невідома версія '") + bad + tr("' (є discord, 480p, vertical, master, thumb, gif, webp)");
            return false;
        }
        s.extra_versions = c.get("--also");
    }
    if (c.has("--level-voices")) s.voice_level = true;
    if (c.has("--denoise")) s.voice_denoise = true;
    if (c.has("--denoise-player")) s.voice_denoise_players = c.get("--denoise-player");
    if (c.has("--duck-game")) s.duck_game = true;
    if (c.has("--loudness")) {
        const std::string v = to_lower(c.get("--loudness"));
        if (v == "off" || v == "0") {
            s.loudness_target = 0;
        } else {
            auto l = parse_double(v);
            if (!l || *l > -5 || *l < -70) { err = tr("--loudness: число LUFS від -70 до -5 (напр. -14) або off"); return false; }
            s.loudness_target = *l;
        }
    }
    if (a) {
        if (c.has("--markers")) {
            std::vector<render::Marker> list;
            for (const auto& item : split(c.get("--markers"), ';')) {
                const size_t eq = item.find('=');
                auto t = parse_timecode(trim(item.substr(0, eq)));
                if (!t) { err = trf("--markers: не розумію час у «{}»", trim(item)); return false; }
                render::add_marker(list, {static_cast<int32_t>(std::llround(*t / a->tick_interval)),
                                          eq == std::string::npos ? std::string() : trim(item.substr(eq + 1))});
            }
            s.markers = render::format_markers(list);
        }
        // Час: секунди або "год:хв:сек" / "хв:сек"
        if (c.has("--start")) {
            auto t = parse_timecode(c.get("--start"));
            if (!t) { err = tr("неправильне значення --start (приклади: 95.5, 1:35, 1:02:03)"); return false; }
            s.start_tick = static_cast<int32_t>(std::llround(*t / a->tick_interval));
        }
        if (c.has("--end")) {
            auto t = parse_timecode(c.get("--end"));
            if (!t) { err = tr("неправильне значення --end (приклади: 95.5, 1:35, 1:02:03)"); return false; }
            s.end_tick = static_cast<int32_t>(std::llround(*t / a->tick_interval));
        }
    }
    if (!parse_rational(s.fps)) { err = tr("неправильне значення --fps"); return false; }
    return true;
}

// Показ прогресу завдання в один рядок
static int run_job(render::Job& job) {
    std::signal(SIGINT, on_sigint);
    job.start();
    int handled_interrupts = 0;
    std::string last_line;
    while (job.running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (g_interrupts > handled_interrupts) {
            handled_interrupts = g_interrupts;
            if (handled_interrupts == 1) {
                std::fprintf(stderr, "%s", tr("\nЗупиняю (ще раз Ctrl+C — перервати негайно)...\n"));
                job.cancel();
            } else {
                job.kill();
            }
        }
        const auto p = job.progress();
        std::string line = trf("[{}] {:5.1f}%  кадрів {}  відео {}", p.stage, std::max(0.0, p.fraction) * 100,
                                       p.frames, format_duration(p.video_seconds));
        if (p.demo_total > 0) line += trf("  тік {}/{}", p.demo_tick, p.demo_total);
        if (p.speed_fps > 0) line += trf("  {:.1f} к/с", p.speed_fps);
        if (p.eta >= 0) line += tr("  залишилось ") + format_duration(p.eta);
        if (p.pending_files > 0) line += trf("  у черзі {}", p.pending_files);
        if (p.game_paused) line += tr("  [гра на паузі]");
        if (line != last_line) {
            std::fprintf(stderr, "\r%-150s", line.c_str());
            std::fflush(stderr);
            last_line = line;
        }
    }
    job.wait();
    std::fprintf(stderr, "\n");
    const auto final_progress = job.progress();
    if (!job.report().empty()) std::printf("\n%s\n\n", job.report().c_str());
    else if (job.state() == render::JobState::Failed && !final_progress.checks.empty()) {
        for (const auto& ch : final_progress.checks)
            std::printf("  %s %s%s\n", ch.state == render::CheckItem::Ok ? "+" : ch.state == render::CheckItem::Skipped ? "-" : "x",
                        ch.name.c_str(), ch.detail.empty() ? "" : (" — " + ch.detail).c_str());
    }
    int rc = 1;
    switch (job.state()) {
    case render::JobState::Succeeded:
        std::printf(tr("Готово: %s\n"), job.result().c_str());
        rc = 0;
        break;
    case render::JobState::Cancelled:
        std::printf("%s", tr("Скасовано\n"));
        return 2;   // зупинили вручну — вимикати ПК не треба
    default:
        std::printf(tr("Помилка: %s\n"), job.error().c_str());
        break;
    }
    if (g_then != PowerAction::None) {
        // Хвилина, щоб передумати
        const int before = g_interrupts;
        for (int left = power_countdown_seconds(); left > 0; --left) {
            std::fprintf(stderr, tr("\rПісля рендеру: %s через %2d с (Ctrl+C — скасувати)"), power_action_name(g_then), left);
            std::fflush(stderr);
            for (int i = 0; i < 10 && g_interrupts == before; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (g_interrupts != before) {
                std::fprintf(stderr, tr("\nСкасовано: %s не буде\n"), power_action_name(g_then));
                return rc;
            }
        }
        std::fprintf(stderr, "\n");
        std::string err;
        if (!do_power_action(g_then, &err)) std::printf(tr("Не вдалося %s: %s\n"), power_action_name(g_then), err.c_str());
    }
    return rc;
}

static int cmd_info(const Cli& c) {
    if (c.positional.empty()) { print_usage(); return 1; }
    demo::DemoAnalysis a;
    try {
        a = demo::analyze_demo(path_from_utf8(c.positional[0]));
    } catch (const std::exception& e) {
        std::printf(tr("Помилка: %s\n"), e.what());
        return 1;
    }
    auto v = voice::decode_voice(a);
    if (c.has("--json")) {
        json::Value j = json::Value::object();
        j.set("map", json::Value::string(a.header.map_name));
        j.set("server", json::Value::string(a.header.server_name));
        j.set("client", json::Value::string(a.header.client_name));
        j.set("gamemode", json::Value::string(a.server_info.gamemode));
        j.set("ticks", json::Value::number(a.last_tick));
        j.set("tick_interval", json::Value::number(a.tick_interval));
        j.set("duration", json::Value::number(a.duration_seconds));
        j.set("packets", json::Value::number(a.packets_total));
        j.set("packets_failed", json::Value::number(a.packets_failed));
        j.set("variant", json::Value::string(a.variant.describe()));
        json::Value sp = json::Value::array();
        for (const auto& s : v.speakers) {
            json::Value o = json::Value::object();
            o.set("key", json::Value::string(s.key));
            o.set("name", json::Value::string(s.name));
            o.set("steamid64", json::Value::string(std::to_string(s.steamid64)));
            o.set("slot", json::Value::number(s.slot));
            o.set("local", json::Value::boolean(s.is_local));
            o.set("seconds", json::Value::number(s.seconds));
            o.set("packets", json::Value::number(s.packets));
            sp.push(o);
        }
        j.set("speakers", sp);
        json::Value ev = json::Value::array();
        for (const auto& e : a.events) {
            json::Value o = json::Value::object();
            o.set("tick", json::Value::number(e.tick));
            o.set("time", json::Value::number(a.tick_to_seconds(e.tick)));
            o.set("kind", json::Value::string(demo::event_kind_name(e.kind)));
            o.set("slot", json::Value::number(e.slot));
            o.set("who", json::Value::string(e.who));
            o.set("text", json::Value::string(e.text));
            if (!e.channel.empty()) o.set("channel", json::Value::string(e.channel));
            ev.push(o);
        }
        j.set("events", ev);
        std::printf("%s\n", j.dump().c_str());
        return 0;
    }
    std::printf(tr("Файл:        %s\n"), c.positional[0].c_str());
    std::printf(tr("Карта:       %s\n"), a.header.map_name.c_str());
    std::printf(tr("Сервер:      %s\n"), a.header.server_name.c_str());
    std::printf(tr("Записав:     %s (слот %d)\n"), a.header.client_name.c_str(), a.local_slot);
    std::printf(tr("Режим гри:   %s\n"), a.server_info.gamemode.c_str());
    std::printf(tr("Тривалість:  %s (%d тіків, %.2f тік/с)\n"), format_duration(a.duration_seconds).c_str(), a.last_tick,
                1.0 / a.tick_interval);
    std::printf(tr("Протокол:    демо %d, мережа %d, варіант [%s]\n"), a.header.demo_protocol, a.header.network_protocol,
                a.variant.describe().c_str());
    std::printf(tr("Пакетів:     %d (не розібрано повністю: %d)\n"), a.packets_total, a.packets_failed);
    std::printf(tr("Гравців:     %zu, голосовий кодек: %s\n"), a.players.size(), a.voice_codec.c_str());
    for (const auto& w : a.warnings) std::printf("  ! %s\n", w.c_str());
    std::printf(tr("\nГолоси (%zu):\n"), v.speakers.size());
    for (const auto& s : v.speakers)
        std::printf(tr("  %-20s %-40s %6.1f с  %s\n"), s.key.c_str(), s.display_name().c_str(), s.seconds,
                    s.is_local ? tr("<- це ви") : "");
    if (v.speakers.empty()) std::printf("%s", tr("  (немає)\n"));
    for (const auto& w : v.warnings) std::printf("  ! %s\n", w.c_str());
    std::printf(tr("\nЧат і події: %zu повідомлень чату, %zu від сервера, %zu входів, %zu виходів\n"),
                a.count_events(demo::DemoEventKind::Chat), a.count_events(demo::DemoEventKind::Server),
                a.count_events(demo::DemoEventKind::Join), a.count_events(demo::DemoEventKind::Leave));
    if (c.has("--chat")) std::printf("%s", demo::format_chat_log(a.events, a.tick_interval).c_str());
    return 0;
}

static int cmd_watch(const Cli& c) {
    if (c.positional.empty()) {
        std::puts(tr("Використання: gmdr-cli watch <demo.dem> [--from ЧАС] [--game-dir ПАПКА] [--game-exe ФАЙЛ] [--rtx]"));
        return 1;
    }
    render::RenderSettings s;
    const fs::path cfg = app_data_dir() / "gmdr_settings.json";
    if (fs::exists(cfg)) render::load_settings(s, path_to_utf8(cfg), nullptr);
    std::shared_ptr<demo::DemoAnalysis> a;
    try {
        demo::AnalyzeOptions o;
        o.collect_voice = false;
        a = std::make_shared<demo::DemoAnalysis>(demo::analyze_demo(path_from_utf8(c.positional[0]), o));
    } catch (const std::exception& e) {
        std::printf(tr("Помилка: %s\n"), e.what());
        return 1;
    }
    s.demo_path = c.positional[0];
    std::string err;
    if (!apply_options(c, s, a.get(), err)) {
        std::printf(tr("Помилка: %s\n"), err.c_str());
        return 1;
    }
    int32_t from = std::max(0, s.start_tick);
    if (c.has("--from")) {
        auto t = parse_timecode(c.get("--from"));
        if (!t) {
            std::puts(tr("Неправильне значення --from (приклади: 95.5, 1:35, 1:02:03)"));
            return 1;
        }
        from = static_cast<int32_t>(std::llround(*t / a->tick_interval));
    }
    render::WatchJob job(s, a, from);
    const int rc = run_job(job);
    // Позначки з гри: початок/кінець — підказка для рендеру, позначки — у файл позначок демо
    const auto marks = job.take_marks();
    const fs::path store = app_data_dir() / "gmdr_markers.json";
    auto saved = render::load_demo_markers(store, s.demo_path);
    int32_t mstart = -1, mend = -1;
    for (const auto& m : marks) {
        const double t = m.tick * static_cast<double>(a->tick_interval);
        std::printf(tr("  %-8s %s (тік %d)\n"), m.kind.c_str(), format_timecode(t).c_str(), m.tick);
        if (m.kind == "start") mstart = m.tick;
        else if (m.kind == "end") mend = m.tick;
        else render::add_marker(saved, {m.tick, tr("Позначка з гри")});
    }
    if (!marks.empty()) render::save_demo_markers(store, s.demo_path, saved, nullptr);
    if (mstart >= 0 || mend >= 0)
        std::printf(tr("Рендер цього фрагмента: gmdr-cli render \"%s\"%s%s\n"), s.demo_path.c_str(),
                    mstart >= 0 ? std::format(" --start-tick {}", mstart).c_str() : "",
                    mend >= 0 ? std::format(" --end-tick {}", mend).c_str() : "");
    return rc;
}

static int cmd_voice(const Cli& c) {
    if (c.positional.empty() || !c.has("--output")) {
        std::puts(tr("Використання: gmdr-cli voice <demo.dem> -o <папка> [--voice all|local|others] [--voice-keys ...]\n"
                  "                      [--start СЕКУНД] [--end СЕКУНД] [--level-voices] [--denoise]"));
        return 1;
    }
    auto a = std::make_shared<demo::DemoAnalysis>();
    try {
        *a = demo::analyze_demo(path_from_utf8(c.positional[0]));
    } catch (const std::exception& e) {
        std::printf(tr("Помилка: %s\n"), e.what());
        return 1;
    }
    auto v = std::make_shared<voice::VoiceDecodeResult>(voice::decode_voice(*a));
    render::RenderSettings s;
    s.demo_path = c.positional[0];
    s.voice_mode = "all";
    std::string err;
    if (!apply_options(c, s, a.get(), err)) {
        std::printf(tr("Помилка: %s\n"), err.c_str());
        return 1;
    }
    render::ExportVoicesJob job(s, a, v, path_from_utf8(c.get("--output")));
    const int r = run_job(job);
    if (r == 0) std::printf("%s", tr("Кожен файл починається з того самого моменту демо — у програмі монтажу кладіть їх на початок.\n"));
    return r;
}

// Налаштування за замовчуванням: --config або збережені програмою (без шляху виходу).
static bool load_base_settings(const Cli& c, render::RenderSettings& s, std::string& err) {
    const fs::path default_cfg = app_data_dir() / "gmdr_settings.json";
    if (c.has("--config")) return render::load_settings(s, c.get("--config"), &err);
    if (fs::exists(default_cfg)) {
        render::load_settings(s, path_to_utf8(default_cfg), nullptr);
        s.output_path.clear();
    }
    return true;
}

// Розпізнати мовлення гравців і показати/зберегти репліки
static int cmd_transcribe(const Cli& c) {
    if (c.positional.empty()) {
        std::puts(tr("Використання: gmdr-cli transcribe <demo.dem> [-o файл.txt|.srt|.json] [--language uk]\n"
                  "                      [--start ЧАС] [--end ЧАС] [--voice-keys ...] [--force]\n"
                  "                      [--whisper-cli ФАЙЛ] [--whisper-model ФАЙЛ]\n"
                  "Потрібні whisper-cli і модель (тека whisper поруч із програмою). --force — розпізнати заново."));
        return 1;
    }
    auto a = std::make_shared<demo::DemoAnalysis>();
    try {
        *a = demo::analyze_demo(path_from_utf8(c.positional[0]));
    } catch (const std::exception& e) {
        std::printf(tr("Помилка: %s\n"), e.what());
        return 1;
    }
    auto v = std::make_shared<voice::VoiceDecodeResult>(voice::decode_voice(*a));
    render::RenderSettings s;
    std::string err;
    load_base_settings(c, s, err);   // мова і шляхи whisper — як у програмі
    s.demo_path = c.positional[0];
    s.voice_mode = "all";
    s.start_tick = 0;
    s.end_tick = -1;
    if (!apply_options(c, s, a.get(), err)) {
        std::printf(tr("Помилка: %s\n"), err.c_str());
        return 1;
    }
    if (c.has("--force")) {
        std::error_code ec;
        fs::remove(speech::transcript_path(s.demo_path), ec);
    }
    const bool range = c.has("--start") || c.has("--end");
    render::TranscribeJob job(s, a, v, range);
    const int rc = run_job(job);
    const auto ts = job.transcript();
    if (rc != 0 || !ts) return rc;
    const double ti = a->tick_interval;
    const double from = range && s.start_tick > 0 ? s.start_tick * ti : 0.0;
    const double to = range && s.end_tick > 0 ? s.end_tick * ti : 1e18;
    std::vector<speech::Line> lines;
    for (const auto& l : ts->lines)
        if (l.start >= from && l.start < to) lines.push_back(l);
    std::string text;
    for (const auto& l : lines) text += std::format("[{}] {}: {}\n", format_duration(l.start), l.speaker, l.text);
    if (!c.has("--output")) {
        std::fputs(text.c_str(), stdout);
        return 0;
    }
    const fs::path out = path_from_utf8(c.get("--output"));
    const std::string ext = to_lower(path_to_utf8(out.extension()));
    std::string body = text;
    if (ext == ".srt") {
        const double end = to < 1e17 ? to : a->last_tick * ti;
        body = render::make_transcript_srt(lines, {}, from, end - from);
    } else if (ext == ".json") {
        speech::Transcript part = *ts;
        part.lines = lines;
        body = speech::transcript_to_json(part);
    }
    if (!write_file_text(out, body, &err)) {
        std::printf(tr("Не вдалося записати %s: %s\n"), path_to_utf8(out).c_str(), err.c_str());
        return 1;
    }
    std::printf(tr("Реплік: %zu — %s\n"), lines.size(), path_to_utf8(out).c_str());
    return 0;
}

// Перекласти розпізнане мовлення демо в субтитри (без рендеру)
static int cmd_translate(const Cli& c) {
    if (c.positional.empty() || !c.has("--translate")) {
        std::puts(tr("Використання: gmdr-cli translate <demo.dem> --translate en,de [-o відео.mp4] [--start ЧАС] [--end ЧАС]\n"
                  "                      [--translator deepl|google|libre|openai] [--language uk]\n"
                  "Субтитри <демо>.<мова>.srt (з -o — поруч із тим файлом). Ключ — змінною GMDR_DEEPL_KEY тощо."));
        return 1;
    }
    auto a = std::make_shared<demo::DemoAnalysis>();
    try {
        *a = demo::analyze_demo(path_from_utf8(c.positional[0]));
    } catch (const std::exception& e) {
        std::printf(tr("Помилка: %s\n"), e.what());
        return 1;
    }
    auto v = std::make_shared<voice::VoiceDecodeResult>(voice::decode_voice(*a));
    render::RenderSettings s;
    std::string err;
    load_base_settings(c, s, err);   // сервіс перекладу й ключі — як у програмі
    s.demo_path = c.positional[0];
    s.output_path.clear();
    s.voice_mode = "all";
    s.start_tick = 0;
    s.end_tick = -1;
    if (!apply_options(c, s, a.get(), err)) {
        std::printf(tr("Помилка: %s\n"), err.c_str());
        return 1;
    }
    render::TranslateJob job(s, a, v, c.has("--start") || c.has("--end"));
    return run_job(job);
}

// Бібліотека голосів гравців
static int cmd_voices(const Cli& c) {
    const std::string sub = c.positional.empty() ? "list" : c.positional[0];
    if (sub == "delete" && c.positional.size() > 1) {
        if (!dub::delete_profile(c.positional[1])) {
            std::printf(tr("Не вдалося видалити %s\n"), c.positional[1].c_str());
            return 1;
        }
        std::printf(tr("Видалено: %s\n"), c.positional[1].c_str());
        return 0;
    }
    if (sub == "clear") {
        if (!c.has("-y")) {
            std::puts(tr("Буде видалено всі зразки голосів. Підтвердіть: gmdr-cli voices clear -y"));
            return 1;
        }
        std::error_code ec;
        fs::remove_all(dub::voices_dir(), ec);
        std::puts(tr("Бібліотеку голосів очищено"));
        return 0;
    }
    const auto list = dub::list_profiles();
    std::printf(tr("Бібліотека голосів: %s\n"), path_to_utf8(dub::voices_dir()).c_str());
    if (list.empty()) std::puts(tr("  порожньо (зразки додаються з розпізнаних демо, коли увімкнено --voice-library)"));
    for (const auto& p : list)
        std::printf(tr("  %-24s %-28s зразків %2zu, %5.1f с%s\n"), p.key.c_str(), p.name().c_str(), p.samples.size(), p.total_seconds(),
                    p.elevenlabs_voice_id.empty() ? "" : tr(", є клон ElevenLabs"));
    return 0;
}

// Локальний рушій озвучення
static int cmd_voice_engine(const Cli& c) {
    const std::string sub = c.positional.empty() ? "status" : c.positional[0];
    render::RenderSettings s;
    std::string err;
    load_base_settings(c, s, err);
    if (c.has("--tts-python")) s.tts_python = c.get("--tts-python");
    if (c.has("--tts-device")) s.tts_device = c.get("--tts-device");
    if (sub == "install") {
        const bool cuda = !c.has("--cpu") && dub::has_nvidia_gpu();
        if (!c.has("-y")) {
            std::printf(tr("Буде завантажено близько %s (Python 3.12, PyTorch%s, OmniVoice і модель) у\n  %s\n"
                           "Нічого в системі не змінюється. Підтвердіть: gmdr-cli voice-engine install -y%s\n"),
                        cuda ? "5 ГБ" : "2 ГБ", cuda ? " з CUDA" : "", path_to_utf8(dub::engine_dir()).c_str(), cuda ? "" : " --cpu");
            return 1;
        }
        render::VoiceEngineJob job(render::VoiceEngineJob::Action::Install, cuda, s);
        return run_job(job);
    }
    if (sub == "check") {
        render::VoiceEngineJob job(render::VoiceEngineJob::Action::Check, false, s);
        return run_job(job);
    }
    if (sub == "remove") {
        if (!dub::remove_engine(&err)) {
            std::printf(tr("Не вдалося видалити: %s\n"), err.c_str());
            return 1;
        }
        std::puts(tr("Рушій озвучення видалено"));
        return 0;
    }
    const auto py = dub::engine_python(render::tts_config(s));
    std::printf(tr("Рушій озвучення (OmniVoice): %s\n"), py ? path_to_utf8(*py).c_str() : tr("не встановлено"));
    std::printf(tr("GPU NVIDIA: %s\n"), dub::has_nvidia_gpu() ? tr("є") : tr("немає (буде повільно, на процесорі)"));
    if (!py) std::puts(tr("Встановити: gmdr-cli voice-engine install -y"));
    return py ? 0 : 1;
}

// Стан розпізнавання мовлення і завантаження моделі
static int cmd_whisper(const Cli& c) {
    const auto& models = speech::known_models();
    if (c.has("--download")) {
        const std::string want = c.get("--download");
        const speech::ModelInfo* m = nullptr;
        for (size_t i = 0; i < models.size(); ++i)
            if (want == std::to_string(i + 1) || iequals(want, models[i].file)) m = &models[i];
        if (!m) {
            std::printf(tr("Невідома модель «%s» — див. список: gmdr-cli whisper\n"), want.c_str());
            return 1;
        }
        render::DownloadJob job(speech::model_url(m->file), speech::models_download_dir() / m->file,
                                std::string(tr("модель ")) + m->file);
        return run_job(job);
    }
    std::string why;
    const auto tools = speech::find_whisper("", "", &why);
    if (tools) std::printf(tr("whisper-cli: %s\nМодель:      %s\n"), path_to_utf8(tools->cli).c_str(), path_to_utf8(tools->model).c_str());
    else std::printf(tr("Розпізнавання недоступне: %s\n"), why.c_str());
    std::printf(tr("\nМоделі (завантажити: gmdr-cli whisper --download N; тека %s):\n"),
                path_to_utf8(speech::models_download_dir()).c_str());
    for (size_t i = 0; i < models.size(); ++i) {
        std::error_code ec;
        const bool have = fs::exists(speech::models_download_dir() / models[i].file, ec);
        std::printf(tr("  %zu. %-30s %5d МБ  %s%s\n"), i + 1, models[i].file, models[i].size_mb, models[i].label,
                    have ? tr("  [є]") : "");
    }
    return tools ? 0 : 1;
}

// Рядок файлу черги -> аргументи (лапки групують, як у командному рядку).
static std::vector<std::string> split_command_line(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool in_quotes = false, has = false;
    for (char ch : line) {
        if (ch == '"') {
            in_quotes = !in_quotes;
            has = true;
        } else if (!in_quotes && (ch == ' ' || ch == '\t')) {
            if (has) out.push_back(cur);
            cur.clear();
            has = false;
        } else {
            cur += ch;
            has = true;
        }
    }
    if (has) out.push_back(cur);
    return out;
}

// Звіт про проблему: ZIP з журналом, налаштуваннями, відомостями про систему і консоллю GMod
static int cmd_report(const Cli& c) {
    const fs::path out = c.has("--output") ? path_from_utf8(c.get("--output"))
                                           : fs::current_path() / path_from_utf8(render::default_report_name());
    render::ReportInput in;
    const fs::path settings = app_data_dir() / "gmdr_settings.json";
    std::error_code ec;
    if (fs::exists(settings, ec)) in.settings_path = path_to_utf8(settings);
    std::vector<std::string> contents;
    std::string err;
    if (!render::make_problem_report(out, in, &contents, &err)) {
        std::printf(tr("Не вдалося створити звіт: %s\n"), err.c_str());
        return 1;
    }
    std::printf(tr("Звіт: %s\n"), path_to_utf8(out).c_str());
    for (const auto& n : contents) std::printf("  %s\n", n.c_str());
    std::puts(tr("Шлях до профілю Windows у текстах замінено на %USERPROFILE%. Перегляньте архів перед тим, як надіслати."));
    return 0;
}

// Черга: "render a.dem b.dem ..." (demos) або "queue список.txt" (рядок — демо і його параметри;
// параметри з командного рядка — для всіх). Гра запускається один раз.
// Дописати рендер, урваний збоєм програми чи ПК (найновіший)
static int cmd_resume(const Cli& c) {
    auto list = render::pending_resumes();
    if (list.empty()) {
        std::printf("%s", tr("Урваних рендерів, які можна дописати, немає\n"));
        return 0;
    }
    render::ResumeRecord r = list.front();
    const double fps = parse_rational(r.settings.fps).value_or(Rational{60, 1}).value();
    std::printf(tr("Урваний рендер: %s\n  демо: %s\n  записано ≈ %s з %s\n"), r.settings.output_path.c_str(),
                r.settings.demo_path.c_str(), format_duration(static_cast<double>(r.frames) / fps).c_str(),
                format_duration(r.seconds).c_str());
    std::fflush(stdout);   // до журналу рендеру, що йде в stderr
    if (c.has("--forget")) {
        render::forget_resume(r.id);
        std::printf("%s", tr("Запис про урваний рендер прибрано (частковий файл лишився як був)\n"));
        return 0;
    }
    if (c.has("--parallel")) {
        const auto v = parse_int(c.get("--parallel"));
        if (!v || *v < 1 || *v > 4) {
            std::printf("%s\n", tr("--parallel: від 1 до 4 копій гри"));
            return 1;
        }
        r.settings.parallel_games = static_cast<int>(*v);
    }
    render::RenderJob job(r.settings, nullptr, nullptr);
    job.set_resume(r);
    return run_job(job);
}

static int cmd_queue(const Cli& common, const std::vector<std::string>& demos) {
    std::vector<Cli> lines;
    if (!demos.empty()) {
        for (const auto& d : demos) {
            Cli item;
            item.command = "render";
            item.positional = {d};
            lines.push_back(item);
        }
    } else {
        if (common.positional.empty()) {
            std::puts(tr("Використання: gmdr-cli queue <список.txt> [спільні параметри]\n"
                      "  Рядок списку: демо і його параметри, напр.\n"
                      "    \"C:\\demos\\match.dem\" --start 5:00 --end 7:30 -o \"D:\\video\\бій.mp4\"\n"
                      "  Порожні рядки і рядки з # пропускаються."));
            return 1;
        }
        auto text = read_file_text(path_from_utf8(common.positional[0]));
        if (!text) {
            std::printf(tr("Не вдалося прочитати %s\n"), common.positional[0].c_str());
            return 1;
        }
        if (text->rfind("\xEF\xBB\xBF", 0) == 0) text->erase(0, 3);   // BOM
        for (const auto& raw : split(replace_all(*text, "\r", ""), '\n')) {
            const std::string line = trim(raw);
            if (line.empty() || line[0] == '#') continue;
            auto args = split_command_line(line);
            args.insert(args.begin(), {"gmdr-cli", "render"});
            lines.push_back(parse_cli(args));
        }
    }
    // -o для кількох демо — папка
    fs::path out_dir;
    if (common.has("--output")) {
        const std::string o = common.get("--output");
        std::error_code ec;
        if (fs::is_directory(path_from_utf8(o), ec) || o.ends_with('\\') || o.ends_with('/')) {
            out_dir = path_from_utf8(o);
        } else {
            std::puts(tr("Для черги -o має бути папкою (куди класти відео); файл для окремого пункту — -o у його рядку."));
            return 1;
        }
    }
    std::vector<render::RenderSettings> items;
    std::map<std::string, int> used_outputs;
    for (size_t i = 0; i < lines.size(); ++i) {
        Cli merged = common;
        merged.opts.erase("--output");
        merged.positional.clear();
        for (const auto& [k, v] : lines[i].opts) {
            if (k == "--vopt" || k == "--exec") merged.opts[k].insert(merged.opts[k].end(), v.begin(), v.end());
            else merged.opts[k] = v;
        }
        if (lines[i].positional.empty()) {
            std::printf(tr("Пункт %zu: не вказано демо\n"), i + 1);
            return 1;
        }
        const std::string demo_path = lines[i].positional[0];
        render::RenderSettings s;
        std::string err;
        if (!load_base_settings(merged, s, err)) {
            std::printf(tr("Не вдалося прочитати конфіг: %s\n"), err.c_str());
            return 1;
        }
        std::shared_ptr<demo::DemoAnalysis> analysis;
        try {
            analysis = std::make_shared<demo::DemoAnalysis>(demo::analyze_demo(path_from_utf8(demo_path)));
        } catch (const std::exception& e) {
            std::printf(tr("Пункт %zu (%s): %s\n"), i + 1, demo_path.c_str(), e.what());
            return 1;
        }
        s.demo_path = demo_path;
        s.markers = render::format_markers(render::load_demo_markers(app_data_dir() / "gmdr_markers.json", demo_path));
        if (!apply_options(merged, s, analysis.get(), err)) {
            std::printf(tr("Пункт %zu: %s\n"), i + 1, err.c_str());
            return 1;
        }
        if (s.output_path.empty()) {
            const std::string ext = s.container.empty() ? "mp4" : s.container;
            s.output_path = out_dir.empty() ? render::default_output_path(demo_path, ext)
                                            : path_to_utf8(out_dir / path_from_utf8(path_to_utf8(
                                                  path_from_utf8(demo_path).stem()) + "." + ext));
        }
        // Те саме демо кілька разів без свого -o — різні файли
        const int k = ++used_outputs[to_lower(s.output_path)];
        if (k > 1) {
            fs::path p = path_from_utf8(s.output_path);
            s.output_path = path_to_utf8(p.parent_path() / path_from_utf8(std::format(
                "{}_{}{}", path_to_utf8(p.stem()), k, path_to_utf8(p.extension()))));
        }
        std::printf("%zu. %s  %s → %s\n", i + 1, path_to_utf8(path_from_utf8(demo_path).filename()).c_str(),
                    s.start_tick > 0 || s.end_tick > 0
                        ? std::format("{}–{}", format_duration(std::max(0, s.start_tick) * analysis->tick_interval),
                                      s.end_tick > 0 ? format_duration(s.end_tick * analysis->tick_interval) : tr("кінець")).c_str()
                        : tr("усе демо"),
                    s.output_path.c_str());
        items.push_back(std::move(s));
    }
    if (items.empty()) {
        std::puts(tr("Черга порожня"));
        return 1;
    }
    render::QueueJob job(std::move(items));
    return run_job(job);
}

static int cmd_encoders(const Cli& c) {
    const bool test = c.has("--test");
    std::printf("%-22s %-10s %-9s %s\n", tr("Кодек"), tr("Формат"), tr("Тип"), test ? tr("Перевірка") : tr("Опис"));
    for (const auto& e : media::list_encoders(true)) {
        std::string status;
        if (test && e.hardware) {
            media::VideoEncoderSettings vs;
            vs.codec = e.name;
            vs.width = 1280;
            vs.height = 720;
            media::VideoEncoder enc;
            std::string err;
            status = enc.open(vs, 1280, 720, false, &err) ? tr("ПРАЦЮЄ") : tr("недоступний");
        }
        std::printf("%-22s %-10s %-9s %s\n", e.name.c_str(), e.codec_name.c_str(),
                    e.hardware ? ("GPU/" + e.vendor).c_str() : "CPU", test ? status.c_str() : e.long_name.c_str());
    }
    return 0;
}

static int cmd_driver(const Cli& c) {
    const std::string action = c.positional.empty() ? "status" : c.positional[0];
    std::optional<game::GModInstall> g = c.has("--game-dir") ? game::gmod_from_dir(path_from_utf8(c.get("--game-dir")))
                                                              : game::detect_gmod();
    if (!g) {
        std::puts(tr("Garry's Mod не знайдено — вкажіть --game-dir"));
        return 1;
    }
    std::printf("Garry's Mod: %s\n", path_to_utf8(g->root).c_str());
    for (const auto& e : g->executables) std::printf("  %s\n", path_to_utf8(e).c_str());
    std::string err;
    if (action == "install") {
        if (!game::install_driver(*g, &err)) { std::printf(tr("Помилка: %s\n"), err.c_str()); return 1; }
        std::puts(tr("Драйвер встановлено"));
    } else if (action == "uninstall") {
        if (!game::uninstall_driver(*g, &err)) { std::printf(tr("Помилка: %s\n"), err.c_str()); return 1; }
        std::puts(tr("Драйвер видалено"));
    } else {
        const auto st = game::driver_state(*g);
        std::printf(tr("Драйвер: %s\n"), st == game::DriverState::Installed ? tr("встановлено")
                                     : st == game::DriverState::Outdated ? tr("застарів (переустановіть)") : tr("не встановлено"));
    }
    return 0;
}

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    install_crash_handler(app_data_dir());
    const auto args = utf8_args(argc, argv);
    Cli c = parse_cli(args);
    // Мова повідомлень: --lang en|de|…, змінна GMDR_LANG; типово — українська (як і в скриптах)
    {
        std::string lang = c.get("--lang");
        if (lang.empty())
            if (const char* e = std::getenv("GMDR_LANG")) lang = e;
        set_ui_language(lang.empty() ? "uk" : lang);
    }
    if (c.command == "--version" || c.command == "version" || c.has("--version")) {
        std::puts("GMod Demo Render " GMDR_VERSION);
        return 0;
    }
    if (c.command.empty() || c.has("-h") || c.has("--help")) {
        print_usage();
        return c.command.empty() ? 1 : 0;
    }
    if (c.has("--then")) {
        const auto a = parse_power_action(to_lower(c.get("--then")));
        if (!a) {
            std::puts(tr("--then: shutdown (вимкнути ПК), sleep (сон) або none"));
            return 1;
        }
        g_then = *a;
    }
    const bool verbose = c.has("-v") || c.has("--verbose");
    set_min_log_level(verbose ? LogLevel::Debug : LogLevel::Info);
    add_log_sink([](LogLevel l, const std::string& msg) {
        const char* prefix = l == LogLevel::Error ? tr("ПОМИЛКА: ") : l == LogLevel::Warn ? tr("УВАГА: ") : "";
        std::fprintf(stderr, "\r%s%-100s\n", prefix, msg.c_str());
    });
    media::install_ffmpeg_log_bridge(verbose ? AV_LOG_INFO : AV_LOG_ERROR);

    // Якщо першим аргументом одразу дали .dem (перетягнули файл на exe) — рендер
    if (ends_with_i(c.command, ".dem")) {
        c.positional.insert(c.positional.begin(), c.command);
        c.command = "render";
    }

    if (c.command == "info") return cmd_info(c);
    if (c.command == "report") return cmd_report(c);
    if (c.command == "update") {
        std::string err;
        auto r = fetch_latest_release(kUpdateRepo, &err);
        if (!r) {
            std::printf(tr("Не вдалося перевірити оновлення: %s\n"), err.c_str());
            return 1;
        }
        if (compare_versions(r->version, GMDR_VERSION) > 0)
            std::printf(tr("Є нова версія %s (у вас %s): %s\n"), r->version.c_str(), GMDR_VERSION, r->url.c_str());
        else
            std::printf(tr("У вас остання версія (%s)\n"), GMDR_VERSION);
        return 0;
    }
    if (c.command == "voice") return cmd_voice(c);
    if (c.command == "transcribe") return cmd_transcribe(c);
    if (c.command == "whisper") return cmd_whisper(c);
    if (c.command == "translate") return cmd_translate(c);
    if (c.command == "voices") return cmd_voices(c);
    if (c.command == "voice-engine") return cmd_voice_engine(c);
    if (c.command == "encoders") return cmd_encoders(c);
    if (c.command == "driver") return cmd_driver(c);

    if (c.command == "watch") return cmd_watch(c);
    if (c.command == "resume") return cmd_resume(c);
    if (c.command == "queue") return cmd_queue(c, {});
    if (c.command == "render" && c.positional.size() > 1) return cmd_queue(c, c.positional);

    if (c.command == "render" || c.command == "encode") {
        if (c.positional.empty()) {
            print_usage();
            return 1;
        }
        render::RenderSettings s;
        std::string err;
        if (!load_base_settings(c, s, err)) {
            std::printf(tr("Не вдалося прочитати конфіг: %s\n"), err.c_str());
            return 1;
        }
        std::shared_ptr<demo::DemoAnalysis> analysis;
        std::shared_ptr<voice::VoiceDecodeResult> voices;
        const std::string demo_path = c.command == "render" ? c.positional[0] : c.get("--demo");
        if (!demo_path.empty()) {
            try {
                analysis = std::make_shared<demo::DemoAnalysis>(demo::analyze_demo(path_from_utf8(demo_path)));
            } catch (const std::exception& e) {
                std::printf(tr("Помилка: %s\n"), e.what());
                return 1;
            }
            voices = std::make_shared<voice::VoiceDecodeResult>(voice::decode_voice(*analysis));
            s.demo_path = demo_path;
            s.markers = render::format_markers(render::load_demo_markers(app_data_dir() / "gmdr_markers.json", demo_path));
            std::printf(tr("Демо: %s, %s, голосів: %zu\n"), analysis->header.map_name.c_str(),
                        format_duration(analysis->duration_seconds).c_str(), voices->speakers.size());
        }
        if (!apply_options(c, s, analysis.get(), err)) {
            std::printf(tr("Помилка: %s\n"), err.c_str());
            return 1;
        }
        if (c.has("--save-config")) render::save_settings(s, c.get("--save-config"), nullptr);
        if (c.command == "render") {
            render::RenderJob job(s, analysis, voices, c.has("--test-run"));
            return run_job(job);
        }
        render::EncodeFramesJob job(s, path_from_utf8(c.positional[0]), c.get("--prefix"),
                                    c.has("--wav") ? path_from_utf8(c.get("--wav")) : fs::path(), analysis, voices);
        return run_job(job);
    }
    std::printf(tr("Невідома команда '%s'\n\n"), c.command.c_str());
    print_usage();
    return 1;
}
