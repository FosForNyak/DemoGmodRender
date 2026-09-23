#include "jobs.hpp"

#include "../frames/sequence_reader.hpp"
#include "../game/audio_mute.hpp"
#include "../game/lua_driver.hpp"
#include "../game/process.hpp"
#include "../game/rtx.hpp"
#include "../media/muxer.hpp"
#include "../util/file_util.hpp"
#include "../util/log.hpp"
#include "../util/power.hpp"
#include "../util/strings.hpp"
#include "../util/thread_pool.hpp"
#include "encode_session.hpp"
#include "markers.hpp"
#include "subtitles.hpp"
#include "edit_package.hpp"
#include "versions.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <format>
#include <future>
#include <map>
#include <optional>
#include <system_error>
#include <thread>

namespace gmdr::render {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

// ================================== Job =========================================
Job::~Job() {
    kill_ = true;
    cancel_ = true;
    if (thread_.joinable()) thread_.join();
}

void Job::start() {
    if (state_ == JobState::Running) return;
    if (thread_.joinable()) thread_.join();
    cancel_ = false;
    kill_ = false;
    state_ = JobState::Running;
    started_ = Clock::now();
    ended_ns_ = 0;
    thread_ = std::thread([this] {
        try {
            run();
        } catch (const std::exception& e) {
            fail(std::string("Непередбачена помилка: ") + e.what());
        } catch (...) {
            fail("Непередбачена помилка");
        }
        mark_ended();
        if (state_ == JobState::Running) state_ = cancel_ ? JobState::Cancelled : JobState::Succeeded;
    });
}

void Job::cancel() { cancel_ = true; }
void Job::kill() {
    kill_ = true;
    cancel_ = true;
}
void Job::wait() {
    if (thread_.joinable()) thread_.join();
}

Progress Job::progress() const {
    std::lock_guard lock(mutex_);
    Progress p = progress_;
    p.elapsed = elapsed_seconds();
    return p;
}

std::string Job::error() const {
    std::lock_guard lock(mutex_);
    return error_;
}

std::string Job::result() const {
    std::lock_guard lock(mutex_);
    return result_;
}

std::string Job::report() const {
    std::lock_guard lock(mutex_);
    return report_;
}

void Job::set_report(const std::string& report) {
    std::lock_guard lock(mutex_);
    report_ = report;
}

// Після завершення лічильник "Минуло" зупиняється на тривалості завдання.
void Job::mark_ended() {
    if (ended_ns_ != 0) return;
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started_).count();
    ended_ns_ = std::max<int64_t>(1, ns);
}

double Job::elapsed_seconds() const {
    if (state_ == JobState::Idle) return 0;
    if (const int64_t ended = ended_ns_.load(); ended != 0) return static_cast<double>(ended) / 1e9;
    return std::chrono::duration<double>(Clock::now() - started_).count();
}

void Job::set_stage(const std::string& stage, double fraction) {
    {
        std::lock_guard lock(mutex_);
        progress_.stage = stage;
        if (fraction >= 0) progress_.fraction = fraction;
    }
    log_info("== {} ==", stage);
}

void Job::fail(const std::string& message) {
    {
        std::lock_guard lock(mutex_);
        error_ = message;
        progress_.stage = "Помилка";
    }
    log_error("{}", message);
    mark_ended();
    state_ = JobState::Failed;
}

void Job::succeed(const std::string& result) {
    {
        std::lock_guard lock(mutex_);
        result_ = result;
        progress_.stage = "Готово";
        progress_.fraction = 1.0;
    }
    mark_ended();
    state_ = JobState::Succeeded;
}

// ============================ Допоміжні функції ==================================
namespace {

// Після рендеру: обкладинка й анімації з готового файлу; журнал — що записано і скільки важить
void make_after_render(const RenderSettings& s, std::vector<std::string> made) {
    const auto post = make_post_versions(s.extra_versions, s.output_path);
    made.insert(made.end(), post.begin(), post.end());
    for (const auto& p : made) {
        std::error_code ec;
        const auto size = fs::file_size(path_from_utf8(p), ec);
        log_info("Додатково: {} ({})", p, ec ? "?" : format_bytes(size));
    }
}

bool make_encode_settings(const RenderSettings& s, EncodeSettings& es, std::string* error) {
    auto fps = parse_rational(s.fps);
    if (!fps || !fps->valid()) {
        if (error) *error = std::format("неправильна частота кадрів '{}'", s.fps);
        return false;
    }
    es.video.codec = s.video_codec;
    es.video.width = s.width;
    es.video.height = s.height;
    es.video.fps = AVRational{fps->num, fps->den};
    es.video.pix_fmt = s.pix_fmt;
    es.video.bit_depth = s.bit_depth;
    es.video.chroma = s.chroma;
    es.video.quality = s.quality;
    es.video.bitrate = s.video_bitrate.empty() ? 0 : parse_bitrate(s.video_bitrate).value_or(0);
    es.video.preset = s.preset;
    es.video.options = parse_key_values(s.video_options);
    es.video.scaler = s.scaler;
    es.video.accurate_color = s.accurate_color;
    es.video.threads = s.threads;
    es.video.full_range = s.full_range;
    es.video.gop_seconds = s.gop_seconds;
    es.motion_blur_samples = std::clamp(s.motion_blur, 1, 256);
    es.shutter_degrees = s.shutter;
    es.audio_enabled = s.audio;
    es.audio.codec = s.audio_codec;
    es.audio.bitrate = parse_bitrate(s.audio_bitrate).value_or(320000);
    es.audio.sample_rate = s.sample_rate;
    es.separate_tracks = s.separate_tracks;
    es.output_path = s.output_path;
    es.container = s.container;
    es.faststart = s.faststart;
    return true;
}

// Гучність кожного вибраного гравця з налаштування "key=gain; key2=gain".
std::vector<float> speaker_gains(const RenderSettings& s, const std::vector<const voice::SpeakerTrack*>& speakers) {
    std::vector<float> gains(speakers.size(), 1.0f);
    const auto kv = parse_key_values(s.voice_volumes);
    for (size_t i = 0; i < speakers.size(); ++i)
        for (const auto& [k, v] : kv)
            if (k == speakers[i]->key) gains[i] = static_cast<float>(std::clamp(parse_double(v).value_or(1.0), 0.0, 4.0));
    return gains;
}

bool denoise_speaker(const RenderSettings& s, const std::string& key) {
    if (s.voice_denoise) return true;
    for (const auto& k : split(s.voice_denoise_players, ','))
        if (trim(k) == key) return true;
    return false;
}

} // namespace

// Обробка голосу: мовлення кожного гравця аналізується заздалегідь (гучність за EBU R128,
// рівні фону і мови), щоб вирівняти гучність і поставити гейту поріг саме цього гравця.
std::vector<audio::VoiceCleanup> voice_cleanup_for(const RenderSettings& s,
                                                   const std::vector<const voice::SpeakerTrack*>& speakers,
                                                   const std::atomic<bool>& cancel) {
    std::vector<audio::VoiceCleanup> out(speakers.size());
    for (size_t i = 0; i < speakers.size() && !cancel; ++i) {
        auto& c = out[i];
        c.denoise = denoise_speaker(s, speakers[i]->key);
        if (!s.voice_level && !c.denoise) continue;
        c.profile = audio::profile_voice(*speakers[i]);
        if (s.voice_level) c.level = audio::level_gain(c.profile);
        if (c.profile.valid())
            log_info("Голос {}: {:.1f} LUFS, фон {:.0f} дБ, мова {:.0f} дБ{}", speakers[i]->display_name(),
                     c.profile.loudness, c.profile.noise_db, c.profile.speech_db,
                     s.voice_level ? std::format(", підсилення {:+.1f} дБ", 20.0 * std::log10(c.level)) : std::string());
        else
            log_info("Голос {}: замало мовлення для аналізу — без обробки", speakers[i]->display_name());
    }
    return out;
}

bool needs_voice_cleanup(const RenderSettings& s) {
    return s.voice_level || s.voice_denoise || !trim(s.voice_denoise_players).empty();
}

namespace {

// Чи це MP4/MOV (для фрагментованого запису і переупаковки).
bool is_mov_family(const std::string& path, const std::string& container) {
    const std::string c = to_lower(container);
    if (!c.empty()) return c == "mp4" || c == "mov" || c == "ipod" || c == "m4v";
    const std::string ext = to_lower(path_to_utf8(path_from_utf8(path).extension()));
    return ext == ".mp4" || ext == ".mov" || ext == ".m4v";
}

// Субтитри "хто говорить" поруч із відео (те саме ім'я, розширення .srt).
// Швидкість відео з налаштувань (1 — звичайна; 0.5 — уповільнення вдвічі)
double video_speed(const RenderSettings& s) { return s.speed > 0 ? std::clamp(s.speed, 0.1, 16.0) : 1.0; }

// Мовці для субтитрів і підписів на кадрі (вимкнені гравці — без підпису)
std::vector<SpeakerSubtitleSource> subtitle_sources(const RenderSettings& s,
                                                    const std::vector<const voice::SpeakerTrack*>& speakers) {
    const auto gains = speaker_gains(s, speakers);
    std::vector<SpeakerSubtitleSource> src;
    for (size_t i = 0; i < speakers.size(); ++i)
        if (gains[i] > 0.0f) src.push_back({speakers[i], speakers[i]->name.empty() ? speakers[i]->display_name() : speakers[i]->name});
    return src;
}

void write_speaker_subtitles(const RenderSettings& s, const std::vector<const voice::SpeakerTrack*>& speakers,
                             int64_t origin_sample, double duration) {
    if (s.output_path.find('%') != std::string::npos) return;   // послідовність зображень
    const std::string srt =
        make_speaker_srt(subtitle_sources(s, speakers), origin_sample, duration, s.voice_delay, video_speed(s));
    fs::path path = path_from_utf8(s.output_path);
    path.replace_extension(".srt");
    std::string err;
    if (write_file_text(path, srt, &err)) log_info("Субтитри «хто говорить»: {}", path_to_utf8(path));
    else log_warn("Не вдалося записати субтитри: {}", err);
}

// Підписи «хто говорить» на кадрі; nullptr — нема кого показувати або немає шрифту
std::unique_ptr<SpeakerOverlay> make_overlay(const RenderSettings& s, const std::vector<const voice::SpeakerTrack*>& speakers,
                                             int64_t origin_sample, double duration, int frame_w, int frame_h) {
    const std::string font = SpeakerOverlay::find_font();
    if (font.empty()) {
        log_warn("Підписи «хто говорить»: у системі не знайдено шрифту з кирилицею — без підписів");
        return nullptr;
    }
    auto ov = std::make_unique<SpeakerOverlay>();
    std::string err;
    if (!ov->init(SpeakerOverlay::speakers_for(subtitle_sources(s, speakers), origin_sample, duration, s.voice_delay,
                                               video_speed(s)),
                  frame_w, frame_h, font, &err)) {
        log_info("Підписи «хто говорить»: {}", err);
        return nullptr;
    }
    log_info("Підписи «хто говорить» на кадрі: {} гравц(ів)", ov->label_count());
    return ov;
}

void write_chat_subtitles(const RenderSettings& s, const demo::DemoAnalysis& a, int32_t start_tick, double duration) {
    if (s.output_path.find('%') != std::string::npos || a.tick_interval <= 0) return;
    const double vti = a.tick_interval / video_speed(s);   // тривалість тіку у відео
    const int32_t end_tick = start_tick + static_cast<int32_t>(std::ceil(duration / vti));
    const std::string srt = make_chat_srt(a.events, start_tick, end_tick, vti, duration);
    if (srt.empty()) {
        log_info("Субтитри чату: у фрагменті немає повідомлень");
        return;
    }
    fs::path path = path_from_utf8(s.output_path);
    path.replace_extension(s.subtitles_srt ? ".chat.srt" : ".srt");
    std::string err;
    if (write_file_text(path, srt, &err)) log_info("Субтитри чату: {}", path_to_utf8(path));
    else log_warn("Не вдалося записати субтитри чату: {}", err);
}

// Після рендеру: фрагментований MP4 -> звичайний (якщо просили faststart), розділи з позначок
// і перевірка результату.
void finalize_output(const RenderSettings& s, bool fragmented, int64_t frames, double seconds,
                     const std::vector<Chapter>& chapters = {}) {
    if (s.output_path.find('%') != std::string::npos) return;   // послідовність зображень
    std::vector<media::ChapterMark> marks;
    for (const auto& c : chapters) marks.push_back({c.start, c.end, c.title});
    const bool with_chapters = !marks.empty() && media::container_supports_chapters(s.output_path);
    if (!marks.empty() && !with_chapters)
        log_warn("Розділи не записано: формат {} їх не підтримує (потрібен MP4, MOV або MKV)",
                 path_to_utf8(path_from_utf8(s.output_path).extension()));
    if ((fragmented && s.faststart) || with_chapters) {
        if (fragmented && s.faststart) log_info("Переупаковую у звичайний MP4 (без перекодування)...");
        else log_info("Додаю розділи у файл (без перекодування)...");
        std::string err;
        const bool faststart = s.faststart && is_mov_family(s.output_path, s.container);
        if (!media::remux_file(s.output_path, faststart, &err, with_chapters ? &marks : nullptr))
            log_warn("Не вдалося переупакувати ({}), файл лишився як був — він теж відтворюється", err);
    }
    if (with_chapters) {
        // Таймкоди для опису на YouTube — поруч із відео
        fs::path txt = path_from_utf8(s.output_path);
        txt.replace_extension(".chapters.txt");
        write_file_text(txt, chapters_as_text(chapters), nullptr);
    }
    // Перевірка: тривалість відео і звуку, кількість кадрів
    media::MediaFileInfo info;
    std::string err;
    if (!media::probe_media_file(s.output_path, info, &err)) {
        log_warn("Не вдалося перевірити готовий файл: {}", err);
        return;
    }
    std::string problems;
    if (!info.has_video) problems += " у файлі немає відео;";
    else if (seconds > 1 && std::abs(info.video_seconds - seconds) > std::max(0.5, seconds * 0.01))
        problems += std::format(" тривалість відео {} замість {};", format_duration(info.video_seconds), format_duration(seconds));
    if (info.video_frames > 0 && frames > 0 && std::llabs(info.video_frames - frames) > 1)
        problems += std::format(" кадрів {} замість {};", info.video_frames, frames);
    if (s.audio && info.audio_streams > 0 && info.video_seconds > 1 && info.audio_seconds + 0.5 < info.video_seconds)
        problems += std::format(" звук коротший за відео ({} проти {});", format_duration(info.audio_seconds),
                                format_duration(info.video_seconds));
    if (with_chapters && info.chapters != static_cast<int>(marks.size()))
        problems += std::format(" розділів {} замість {};", info.chapters, marks.size());
    if (problems.empty())
        log_info("Перевірка файлу: відео {}{}{}, усе гаразд", format_duration(info.video_seconds),
                 info.audio_streams > 0 ? std::format(", звук {} ({} дор.)", format_duration(info.audio_seconds), info.audio_streams) : "",
                 info.chapters > 0 ? std::format(", розділів {}", info.chapters) : "");
    else
        log_warn("Перевірка файлу:{}", problems);
}

} // namespace

int64_t bitrate_for_target_size(double target_mb, double seconds, int64_t audio_bitrate) {
    if (target_mb <= 0 || seconds <= 0) return 0;
    // 4% запасу на контейнер і коливання бітрейту
    const double total_bits = target_mb * 1000.0 * 1000.0 * 8.0 * 0.96;
    const double video = total_bits / seconds - static_cast<double>(std::max<int64_t>(0, audio_bitrate));
    return static_cast<int64_t>(std::max(150000.0, video));
}

// Шлях файлу відносно папки dir ("demos/x.dem"), якщо файл лежить у ній.
// На Windows регістр літер у шляхах не має значення (Steam пише "c:\\games", а
// провідник — "C:\\Games"), тому порівнюємо без урахування регістру.
static std::optional<std::string> relative_inside(const fs::path& file, const fs::path& dir) {
    std::error_code ec;
    fs::path f = fs::absolute(file, ec);
    if (ec) return std::nullopt;
    fs::path d = fs::absolute(dir, ec);
    if (ec) return std::nullopt;
    std::string fs_ = path_to_utf8(f.lexically_normal());
    std::string ds = path_to_utf8(d.lexically_normal());
    std::replace(fs_.begin(), fs_.end(), '\\', '/');
    std::replace(ds.begin(), ds.end(), '\\', '/');
    while (!ds.empty() && ds.back() == '/') ds.pop_back();
#ifdef _WIN32
    const bool inside = fs_.size() > ds.size() + 1 && to_lower(fs_.substr(0, ds.size())) == to_lower(ds) && fs_[ds.size()] == '/';
#else
    const bool inside = fs_.size() > ds.size() + 1 && fs_.compare(0, ds.size(), ds) == 0 && fs_[ds.size()] == '/';
#endif
    if (!inside) return std::nullopt;
    return fs_.substr(ds.size() + 1);
}

// Чи можна передати шлях у консоль гри без лапок (лише латиниця, цифри, _ - . /).
static bool safe_console_path(const std::string& p) {
    if (p.empty() || p.size() > 200) return false;
    for (unsigned char c : p)
        if (!(std::isalnum(c) || c == '_' || c == '-' || c == '.' || c == '/') || c >= 128) return false;
    return p.find("..") == std::string::npos;
}

// Гра відкриває демо лише з власної папки (шлях відносно garrysmod/). Якщо демо
// вже там (наприклад, garrysmod/demos) і шлях простий — граємо його на місці;
// інакше — жорстке посилання в tmp_dir (миттєво, без зайвого місця) або копія.
static std::optional<std::string> demo_path_for_game(const game::GModInstall& g, const std::string& demo_path,
                                                     const fs::path& tmp_dir, const std::string& id, std::string* error) {
    if (auto rel = relative_inside(path_from_utf8(demo_path), g.garrysmod); rel && safe_console_path(*rel)) {
        log_info("Демо вже в папці гри — відтворюю на місці: {}", *rel);
        return *rel;
    }
    const fs::path dst = tmp_dir / "demo.dem";
    std::error_code lec;
    fs::create_hard_link(path_from_utf8(demo_path), dst, lec);
    if (lec) {
        const uint64_t sz = file_size_or_zero(path_from_utf8(demo_path));
        if (sz > (64ull << 20)) log_info("Копіюю демо ({}) у папку гри...", format_bytes(sz));
        std::string copy_err;
        if (!copy_file_overwrite(path_from_utf8(demo_path), dst, &copy_err)) {
            if (error) *error = "Не вдалося скопіювати демо в папку гри: " + copy_err;
            return std::nullopt;
        }
    }
    return "gmdr_tmp/" + id + "/demo";
}

std::vector<const voice::SpeakerTrack*> select_speakers(const RenderSettings& s, const voice::VoiceDecodeResult& v) {
    std::vector<const voice::SpeakerTrack*> out;
    if (!s.audio || s.voice_mode == "none") return out;
    std::vector<std::string> keys = split(s.voice_selected, ',');
    for (auto& k : keys) k = trim(k);
    for (const auto& sp : v.speakers) {
        bool take = false;
        if (s.voice_mode == "all") take = true;
        else if (s.voice_mode == "local") take = sp.is_local;
        else if (s.voice_mode == "others") take = !sp.is_local;
        else if (s.voice_mode == "selected") take = std::find(keys.begin(), keys.end(), sp.key) != keys.end();
        if (take && !sp.segments.empty()) out.push_back(&sp);
    }
    return out;
}

// Код завершення гри у зрозумілому вигляді. На Windows аварійні коди — це NTSTATUS
// (0xC0000005 тощо), їх зручніше показувати в шістнадцятковому вигляді.
static std::string describe_exit_code(int code) {
    const auto u = static_cast<uint32_t>(code);
    if (code == -1) return "невідомий";
    if (code >= 0 && code < 0x10000) return std::to_string(code);
    std::string s = std::format("0x{:08X}", u);
    switch (u) {
        case 0xC0000135u: s += " — не знайдено потрібну DLL; перевірте цілісність файлів гри в Steam"; break;
        case 0xC000007Bu: s += " — пошкоджений або не тієї розрядності exe/DLL; спробуйте іншу версію гри (32/64 біт)"; break;
        case 0xC0000142u: s += " — не вдалося ініціалізувати DLL"; break;
        case 0xC0000005u: s += " — гра аварійно завершилась (access violation)"; break;
        case 0xC0000409u: s += " — гра аварійно завершилась"; break;
        case 0xC000013Au: s += " — гру закрито"; break;
        default: break;
    }
    return s;
}

std::string default_output_path(const std::string& demo_path, const std::string& ext) {
    fs::path p = path_from_utf8(demo_path);
    fs::path out = p.parent_path() / (path_to_utf8(p.stem()) + "." + ext);
    std::error_code ec;
    for (int i = 2; fs::exists(out, ec) && i < 1000; ++i)
        out = p.parent_path() / std::format("{} ({}).{}", path_to_utf8(p.stem()), i, ext);
    return path_to_utf8(out);
}

std::string detect_frame_prefix(const fs::path& dir, int64_t* count) {
    std::map<std::string, int64_t> groups;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = path_to_utf8(it->path().filename());
        const std::string lower = to_lower(name);
        if (!(ends_with_i(lower, ".tga") || ends_with_i(lower, ".jpg") || ends_with_i(lower, ".jpeg") ||
              ends_with_i(lower, ".png") || ends_with_i(lower, ".bmp")))
            continue;
        const size_t dot = name.rfind('.');
        size_t d = dot;
        while (d > 0 && std::isdigit(static_cast<unsigned char>(name[d - 1]))) --d;
        if (d == dot) continue;
        ++groups[name.substr(0, d)];
    }
    std::string best;
    int64_t best_n = 0;
    for (const auto& [p, n] : groups)
        if (n > best_n) { best = p; best_n = n; }
    if (count) *count = best_n;
    return best;
}

void recover_leftovers(const game::GModInstall& g) {
    std::error_code ec;
    const fs::path tmp = g.garrysmod / "gmdr_tmp";
    if (!fs::exists(tmp, ec)) return;
    if (!game::GameProcess::find_by_name({"gmod.exe", "hl2.exe", "gmod", "hl2_linux"}).empty()) return;
    for (fs::directory_iterator it(tmp, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_directory()) continue;
        const fs::path bak = it->path() / "config.cfg.bak";
        if (fs::exists(bak, ec)) {
            log_warn("Знайдено залишки перерваного рендеру — відновлюю config.cfg гри");
            game::restore_config(g, bak);
        }
        if (fs::exists(it->path() / "rtx.conf.bak", ec)) {
            log_warn("Знайдено залишки перерваного RTX-рендеру — повертаю rtx.conf");
            game::restore_rtx_profile(g, it->path() / "rtx.conf.bak");
        }
        const std::string id = path_to_utf8(it->path().filename());
        game::remove_job_files(g, id);
        fs::remove_all(it->path(), ec);
    }
    fs::remove(tmp, ec);
}

// =============================== AnalyzeJob =======================================
std::shared_ptr<const demo::DemoAnalysis> AnalyzeJob::analysis() const {
    std::lock_guard lock(m_);
    return analysis_;
}
std::shared_ptr<const voice::VoiceDecodeResult> AnalyzeJob::voices() const {
    std::lock_guard lock(m_);
    return voices_;
}

void AnalyzeJob::run() {
    set_stage("Аналіз демо", 0);
    std::shared_ptr<demo::DemoAnalysis> a;
    try {
        a = std::make_shared<demo::DemoAnalysis>(demo::analyze_demo(
            path_from_utf8(path_), {}, [&](double f) { update([&](Progress& p) { p.fraction = f * 0.6; }); }, &cancel_));
    } catch (const std::exception& e) {
        fail(e.what());
        return;
    }
    {
        std::lock_guard lock(m_);
        analysis_ = a;
    }
    log_info("Демо: карта {}, сервер «{}», записав «{}», тривалість {} ({} тіків, {:.1f} тік/с)", a->header.map_name,
             a->header.server_name, a->header.client_name, format_duration(a->duration_seconds), a->last_tick,
             1.0 / a->tick_interval);
    for (const auto& w : a->warnings) log_warn("{}", w);
    set_stage("Розбір голосу", 0.6);
    auto v = std::make_shared<voice::VoiceDecodeResult>(voice::decode_voice(
        *a, {}, [&](double f) { update([&](Progress& p) { p.fraction = 0.6 + f * 0.4; }); }, &cancel_));
    for (const auto& w : v->warnings) log_warn("{}", w);
    for (const auto& sp : v->speakers)
        log_info("Голос: {}{} — {:.1f} с мовлення", sp.display_name(), sp.is_local ? " [це ви]" : "", sp.seconds);
    if (v->speakers.empty()) log_info("Голосу в демо не знайдено");
    else if (std::none_of(v->speakers.begin(), v->speakers.end(), [](const auto& s) { return s.is_local; }))
        log_info("Вашого власного голосу в демо немає: сервер не пересилає гравцю його ж голос. "
                 "Щоб він записувався, перед записом демо введіть у консолі voice_loopback 1, "
                 "або додайте запис мікрофона окремим файлом.");
    {
        std::lock_guard lock(m_);
        voices_ = v;
    }
    if (cancel_) return;
    succeed(path_);
}

// ============================== ExportVoicesJob =====================================
ExportVoicesJob::ExportVoicesJob(RenderSettings s, std::shared_ptr<const demo::DemoAnalysis> analysis,
                                 std::shared_ptr<const voice::VoiceDecodeResult> voices, std::filesystem::path out_dir)
    : s_(std::move(s)), analysis_(std::move(analysis)), voices_(std::move(voices)), dir_(std::move(out_dir)) {}

void ExportVoicesJob::run() {
    if (!analysis_ || !voices_) {
        fail("Спершу відкрийте демо");
        return;
    }
    const demo::DemoAnalysis& A = *analysis_;
    RenderSettings sel = s_;
    sel.audio = true;
    if (sel.voice_mode == "none") sel.voice_mode = "all";   // кнопка "зберегти голоси" — значить, усі
    const auto all_speakers = select_speakers(sel, *voices_);
    if (all_speakers.empty()) {
        fail("У демо немає голосів для збереження");
        return;
    }
    // Відрізок: фрагмент з налаштувань або все демо
    const double spt = static_cast<double>(A.tick_interval) * voice::kVoiceRate;
    const int64_t from = s_.start_tick > 0 ? static_cast<int64_t>(std::llround(s_.start_tick * spt)) : 0;
    int64_t to = s_.end_tick > 0 ? static_cast<int64_t>(std::llround(s_.end_tick * spt))
                                 : static_cast<int64_t>(std::llround(A.last_tick * spt));
    for (const auto* sp : all_speakers)
        if (s_.end_tick <= 0) to = std::max(to, sp->end_sample());
    if (to <= from) {
        fail("Порожній відрізок часу");
        return;
    }
    // Лише ті, хто говорив у цьому відрізку (інакше файли були б суцільною тишею)
    std::vector<const voice::SpeakerTrack*> speakers;
    for (const auto* sp : all_speakers) {
        const bool spoke = std::any_of(sp->segments.begin(), sp->segments.end(),
                                       [&](const voice::VoiceSegment& g) { return g.start < to && g.end() > from; });
        if (spoke) speakers.push_back(sp);
        else log_info("{} не говорив у цьому відрізку — пропускаю", sp->display_name());
    }
    if (speakers.empty()) {
        fail("У вибраному відрізку демо ніхто не говорив");
        return;
    }
    const uint64_t wav_bytes = static_cast<uint64_t>(to - from) * 2u * speakers.size();
    const bool flac = wav_bytes > (2ull << 30);
    const char* ext = flac ? ".flac" : ".wav";
    if (flac)
        log_info("Голоси будуть збережені у FLAC (без втрат): у WAV вони зайняли б {}", format_bytes(wav_bytes));

    std::error_code ec;
    fs::create_directories(dir_, ec);
    // Та сама обробка голосу, що й у відео (вирівнювання гучності, шумодав)
    std::vector<audio::VoiceCleanup> voice_fx;
    if (needs_voice_cleanup(s_)) {
        set_stage("Аналіз голосу гравців");
        voice_fx = voice_cleanup_for(s_, speakers, cancel_);
    }
    set_stage("Збереження голосів", 0);
    std::vector<double> part(speakers.size(), 0.0);
    std::mutex pm;
    std::atomic<int> ok_count{0};
    ThreadPool pool(std::min<unsigned>(std::max(1u, std::thread::hardware_concurrency()),
                                       static_cast<unsigned>(speakers.size())));
    std::vector<std::future<void>> futures;
    for (size_t i = 0; i < speakers.size(); ++i) {
        futures.push_back(pool.submit([&, i] {
            const auto* sp = speakers[i];
            const fs::path file =
                dir_ / path_from_utf8(sanitize_filename(sp->name + "_" + std::to_string(sp->steamid64)) + ext);
            // Оброблений голос: VoiceInput (позиції = семпли демо) за шумодавом і гейтом
            std::unique_ptr<audio::AudioInput> processed;
            float level = 1.0f;
            if (i < voice_fx.size()) {
                const auto& cl = voice_fx[i];
                level = cl.level;
                auto vi = std::make_unique<audio::VoiceInput>(sp, 0, 0.0);
                if (cl.denoise && cl.profile.valid()) {
                    auto f = std::make_unique<audio::FilteredInput>(
                        vi->name(), std::vector<std::vector<audio::TrackSource>>{{{vi.get(), 1.0f}}}, true);
                    const audio::GateParams gp = audio::gate_for(cl.profile);
                    std::string ferr;
                    if (f->open(audio::denoise_filter(cl.profile.noise_db), &gp, &ferr) || f->open({}, &gp, &ferr)) {
                        f->start_at(from);
                        f->own(std::move(vi));
                        processed = std::move(f);
                    }
                }
                if (!processed) processed = std::move(vi);
            }
            std::vector<float> stereo;
            voice::VoiceFill fill;
            if (processed)
                fill = [&](int64_t pos, size_t n, float* out) {
                    stereo.assign(n * 2, 0.0f);
                    processed->mix(pos, stereo.data(), n, level);
                    for (size_t k = 0; k < n; ++k) out[k] += stereo[k * 2];
                    processed->discard_before(pos + static_cast<int64_t>(n));
                };
            std::string err;
            const bool ok = voice::export_speaker_audio(*sp, file, from, to, &err, &cancel_, [&](double f) {
                std::lock_guard lock(pm);
                part[i] = f;
                double sum = 0;
                for (double x : part) sum += x;
                update([&](Progress& p) { p.fraction = sum / static_cast<double>(part.size()); });
            }, fill);
            if (ok) {
                ++ok_count;
                log_info("Збережено: {}", path_to_utf8(file));
            } else if (!cancel_) {
                log_error("Не вдалося зберегти {}: {}", path_to_utf8(file), err);
            }
            std::lock_guard lock(pm);
            part[i] = 1.0;
        }));
    }
    for (auto& f : futures) f.wait();
    if (cancel_) return;   // стан "скасовано" виставить Job::start
    if (ok_count == 0) {
        fail("Не вдалося зберегти жодного голосу");
        return;
    }
    log_info("Збережено голосів: {} у {}. Кожен файл починається з {} демо.", ok_count.load(), path_to_utf8(dir_),
             format_duration(static_cast<double>(from) / voice::kVoiceRate));
    succeed(path_to_utf8(dir_));
}

// =============================== RenderJob ========================================
namespace {
// Кроки, які показуються в тестовому прогоні (і в журналі звичайного рендеру)
enum CheckIndex { kCheckLaunch, kCheckDriver, kCheckDemo, kCheckFrames, kCheckAudio, kCheckEncode, kCheckRtx, kCheckCount };
const char* const kCheckNames[kCheckCount] = {"Гру запущено", "Драйвер у меню GMod відповідає",
                                              "Демо завантажилось і грає", "Кадри надходять",
                                              "Звук гри записується", "Кодування працює", "RTX у кадрі"};
const char* const kCheckHints[kCheckCount] = {
    "Перевірте папку і версію гри на вкладці «Гра».",
    "Натисніть «Інструменти → Встановити драйвер у GMod» (GMod міг оновити menu.lua) і переконайтеся, що Steam запущено.",
    "Перевірте, що демо відкривається в самій грі (playdemo) і що у вас є карта й аддони з сервера.",
    "Гра не записує кадри: перевірте місце на диску з грою; спробуйте режим вікна «позаду інших» на вкладці «Гра».",
    "Гра не пише звук: вимкніть і ввімкніть звук у налаштуваннях GMod або вимкніть «Звук гри», щоб рендерити без нього.",
    "Кодек не працює з цими налаштуваннями: спробуйте інший кодек або формат файлу.",
    "Кадр чорний: Remix не малює, коли вікно гри за межами екрана. Виберіть на вкладці «Гра» вікно «Позаду інших вікон» "
    "або «На екрані» і перевірте, що RTX-копія гри запускається з RTXLauncher."};
} // namespace

RenderJob::RenderJob(RenderSettings s, std::shared_ptr<const demo::DemoAnalysis> analysis,
                     std::shared_ptr<const voice::VoiceDecodeResult> voices, bool test_run,
                     std::shared_ptr<GameHandoff> handoff, bool keep_game)
    : s_(std::move(s)), analysis_(std::move(analysis)), voices_(std::move(voices)), test_run_(test_run),
      handoff_(std::move(handoff)), keep_game_(keep_game && handoff_ != nullptr) {
    update([](Progress& p) {
        p.checks.clear();
        for (int i = 0; i < kCheckCount; ++i) p.checks.push_back({kCheckNames[i], CheckItem::Pending, {}});
    });
    if (!s_.rtx) set_check(kCheckRtx, CheckItem::Skipped, "без RTX");
}

bool RenderJob::can_show_game() const {
    return running() && game::window_mode_from_string(s_.game_window) != game::WindowMode::Normal;
}

void RenderJob::set_check(int index, CheckItem::State state, const std::string& detail) {
    bool changed = false;
    update([&](Progress& p) {
        if (index < 0 || static_cast<size_t>(index) >= p.checks.size()) return;
        auto& c = p.checks[static_cast<size_t>(index)];
        if (c.state == state && (detail.empty() || c.detail == detail || state == CheckItem::Ok)) return;
        if (c.state == CheckItem::Ok && state == CheckItem::Pending) return;
        c.state = state;
        if (!detail.empty()) c.detail = detail;
        changed = true;
    });
    if (changed && state == CheckItem::Ok)
        log_debug("Перевірка: {} — так{}", kCheckNames[index], detail.empty() ? "" : " (" + detail + ")");
}

namespace {
const std::vector<std::string> kGameProcessNames = {"gmod.exe", "hl2.exe", "gmod", "hl2_linux"};

// Папка гри за налаштуваннями. RTX: потрібна копія від RTXLauncher — якщо вказано звичайну
// гру, береться RTX-копія (s.game_exe тоді скидається: exe звичайної гри там не підходить).
// Також: відновлення після збою, перевірка, що гра не запущена, і встановлення драйвера.
std::optional<game::GModInstall> resolve_game(RenderSettings& s, bool need_driver, std::string* error) {
    std::optional<game::GModInstall> g;
    if (!s.game_dir.empty()) g = game::gmod_from_dir(path_from_utf8(s.game_dir));
    else if (!s.rtx) {
        std::vector<std::string> log;
        g = game::detect_gmod(&log);
        for (const auto& l : log) log_debug("{}", l);
    }
    if (s.rtx && (!g || !game::is_rtx_install(*g))) {
        std::vector<std::string> log;
        if (auto rtx = game::detect_rtx_install(&log)) {
            if (g) log_info("RTX: замість звичайної гри використовую копію від RTXLauncher");
            g = rtx;
            s.game_exe.clear();
            for (const auto& l : log) log_info("{}", l);
        } else if (!g) {
            if (error) *error = "Не знайдено копію GMod RTX від RTXLauncher. Вкажіть її папку на вкладці «Гра».";
            return std::nullopt;
        }
    }
    if (!g || !g->valid()) {
        if (error) *error = "Не знайдено Garry's Mod. Вкажіть папку гри (…\\steamapps\\common\\GarrysMod) у налаштуваннях.";
        return std::nullopt;
    }
    log_info("Garry's Mod: {}", path_to_utf8(g->root));
    recover_leftovers(*g);
    if (!game::GameProcess::find_by_name(kGameProcessNames).empty()) {
        if (error) *error = "Garry's Mod уже запущено. Закрийте гру — програма запустить її сама з потрібними параметрами.";
        return std::nullopt;
    }
    if (need_driver && game::driver_state(*g) != game::DriverState::Installed) {
        log_info("Встановлюю драйвер рендеру в меню GMod (один рядок у lua/menu/menu.lua)...");
        if (!game::install_driver(*g, error)) return std::nullopt;
    }
    return g;
}
} // namespace

bool RenderJob::prepare(std::string* error) {
    gmod_ = resolve_game(s_, !s_.manual_mode, error);
    if (!gmod_) return false;

    id_ = make_unique_id();
    tmp_dir_ = gmod_->garrysmod / "gmdr_tmp" / id_;
    std::error_code ec;
    fs::create_directories(tmp_dir_, ec);
    if (ec) {
        if (error) *error = "Не вдалося створити тимчасову папку в папці гри: " + ec.message();
        return false;
    }
    // Первинні копії config.cfg і rtx.conf. У черзі вони одні на всю чергу: поки гра відкрита,
    // наступні пункти не мають копіювати вже змінений файл.
    fs::path backup_dir = tmp_dir_;
    if (handoff_) {
        if (handoff_->dir.empty()) handoff_->dir = gmod_->garrysmod / "gmdr_tmp" / ("queue_" + make_unique_id());
        fs::create_directories(handoff_->dir, ec);
        backup_dir = handoff_->dir;
        handoff_->gmod = gmod_;
        handoff_->job_ids.push_back(id_);
    }
    if (s_.rtx) {
        if (!game::is_rtx_install(*gmod_))
            log_warn("Увімкнено RTX, але в папці гри немає rtx.conf чи rtx-remix — це точно копія від RTXLauncher?");
        // Налаштування Remix для офлайн-рендеру; оригінал повернеться після рендеру (і після збою)
        rtx_backup_ = backup_dir / "rtx.conf.bak";
        std::string rerr;
        if (fs::exists(rtx_backup_, ec))
            log_info("rtx.conf уже налаштовано для рендеру попереднім пунктом черги");
        else if (game::apply_rtx_render_profile(*gmod_, rtx_backup_, &rerr))
            log_info("rtx.conf: на час рендеру — повна роздільна здатність (DLAA), без генерації кадрів і заставки");
        else
            log_warn("Не вдалося змінити rtx.conf ({}) — Remix рендеритиме з вашими налаштуваннями", rerr);
    }
    const uint64_t free_space = free_disk_space(tmp_dir_);
    log_info("Вільно на диску з грою: {}", format_bytes(free_space));
    if (free_space > 0 && free_space < (3ull << 30))
        log_warn("На диску з грою мало місця — зменште «Черга кадрів на диску» або звільніть місце");

    const auto demo_for_game = demo_path_for_game(*gmod_, s_.demo_path, tmp_dir_, id_, error);
    if (!demo_for_game) return false;

    // Налаштування, які ми змінимо, — щоб потім повернути
    const std::vector<std::string> touched = {"host_framerate", "snd_fixed_rate", "fps_max", "mat_vsync",
                                              "snd_mute_losefocus", "net_graph",
                                              "cl_showfps", "voice_scale", "cl_drawhud",
                                              "r_drawviewmodel", "sv_cheats"};
    auto originals = game::read_config_values(*gmod_, touched);
    config_backup_ = backup_dir / "config.cfg.bak";
    if (!fs::exists(config_backup_, ec)) game::backup_config(*gmod_, config_backup_);

    auto fps = parse_rational(s_.fps);
    game::DriverJob job;
    job.id = id_;
    job.demo = *demo_for_game;
    job.movie = "gmdr_tmp/" + id_ + "/" + movie_prefix();
    if (s_.capture_format == "jpg" || s_.capture_format == "jpeg")
        job.movie_flags = {"jpeg", "jpeg_quality", std::to_string(std::clamp(s_.jpeg_quality, 1, 100)), "wav"};
    else
        job.movie_flags = {"raw"};
    // Кадр відео = 1/FPS секунди демо, помножене на швидкість (уповільнення — менший крок)
    job.host_framerate = fps->value() * std::clamp(s_.motion_blur, 1, 256) / video_speed(s_);
    job.start_tick = std::max(0, s_.start_tick);
    job.end_tick = s_.end_tick;
    // Далекий фрагмент: швидко перемотуємо до точки за кілька секунд до нього (останні
    // секунди гра програє звичайно, щоб сутності й освітлення встигли оновитися).
    // RTX: денойзеру потрібна історія кадрів — розгін довший.
    if (analysis_ && analysis_->tick_interval > 0) {
        const auto ticks = [&](double sec) { return static_cast<int32_t>(std::llround(sec / analysis_->tick_interval)); };
        const int32_t seek = job.start_tick - ticks(s_.rtx ? 10.0 : 5.0);
        if (seek > ticks(30.0)) job.seek_tick = seek;
    }
    job.quit_when_done = keep_game_ ? false : s_.quit_game_when_done;
    job.wait_next = keep_game_;
    job.menu_delay = s_.menu_delay;
    job.hide_hud = s_.hide_hud;
    job.hide_viewmodel = s_.hide_viewmodel;
    std::string extra = s_.extra_commands;
    if (s_.manual_mode) {
        extra += "\necho \"[GMDR] Ручний режим: gmdr_start — почати запис, gmdr_stop — зупинити\"";
    }
    const std::string cfg = game::make_job_cfg(job, s_.mute_engine_voice, extra, originals);
    // У ручному режимі завдання для драйвера не пишемо — лише конфіг з аліасами
    if (!game::write_job_files(*gmod_, job, cfg, !s_.manual_mode, error)) return false;
    job_written_ = true;
    return true;
}

void RenderJob::cleanup(bool game_closing) {
    if (!gmod_) return;
    if (job_written_) game::remove_job_files(*gmod_, id_);
    if (!stray_dir_.empty()) {
        // Кадри/звук, які гра записала не в нашу тимчасову папку
        std::error_code ec;
        for (fs::directory_iterator it(stray_dir_, ec), end; !ec && it != end; it.increment(ec))
            if (starts_with_i(path_to_utf8(it->path().filename()), movie_prefix())) fs::remove(it->path(), ec);
    }
    const std::vector<std::string> game_names = {"gmod.exe", "hl2.exe", "gmod", "hl2_linux"};
    const bool game_running = game_closing ? !game::GameProcess::wait_all_exited(game_names, 15000)
                                           : !game::GameProcess::find_by_name(game_names).empty();
    std::error_code ec;
    if (!game_running) {
        if (!config_backup_.empty() && fs::exists(config_backup_, ec)) {
            game::restore_config(*gmod_, config_backup_);
            if (handoff_) fs::remove(config_backup_, ec);   // наступний пункт черги зробить нову копію
        }
        if (!rtx_backup_.empty() && fs::exists(rtx_backup_, ec)) {
            game::restore_rtx_profile(*gmod_, rtx_backup_);
            if (handoff_) fs::remove(rtx_backup_, ec);
        }
    }
    // Пункт черги віддає відкриту гру наступному: запис уже закінчено, тож свою тимчасову
    // папку можна прибрати, хоч гра й працює
    if (!s_.keep_temp_files && !tmp_dir_.empty() && (!game_running || keep_game_)) {
        fs::remove_all(tmp_dir_, ec);
        std::error_code ec2;
        if (fs::is_empty(tmp_dir_.parent_path(), ec2)) fs::remove(tmp_dir_.parent_path(), ec2);
    } else if (!tmp_dir_.empty()) {
        log_info("Тимчасові файли залишено: {}", path_to_utf8(tmp_dir_));
    }
}

void RenderJob::run() {
    KeepAwake keep_awake;   // ПК не засне посеред рендеру
    std::string err;
    // ---- 1. Аналіз ----
    if (!analysis_) {
        set_stage("Аналіз демо", 0);
        try {
            analysis_ = std::make_shared<demo::DemoAnalysis>(demo::analyze_demo(path_from_utf8(s_.demo_path), {}, {}, &cancel_));
        } catch (const std::exception& e) {
            fail(e.what());
            return;
        }
    }
    const demo::DemoAnalysis& A = *analysis_;
    const bool need_voices = s_.audio && s_.voice_mode != "none";
    if (need_voices && !voices_) {
        set_stage("Розбір голосу");
        voices_ = std::make_shared<voice::VoiceDecodeResult>(voice::decode_voice(A, {}, {}, &cancel_));
    }
    std::vector<const voice::SpeakerTrack*> speakers;
    if (voices_) speakers = select_speakers(s_, *voices_);
    std::vector<audio::VoiceCleanup> voice_fx;
    if (!speakers.empty() && needs_voice_cleanup(s_)) {
        set_stage("Аналіз голосу гравців");
        voice_fx = voice_cleanup_for(s_, speakers, cancel_);
    }

    // Повний відрізок (для прогнозу в тестовому прогоні) і відрізок цього запуску
    const int32_t full_start = std::max(0, s_.start_tick);
    const int32_t full_end = s_.end_tick > 0 ? std::min(s_.end_tick, A.last_tick) : A.last_tick;
    const double vspeed = video_speed(s_);
    const double vti = A.tick_interval / vspeed;   // тривалість тіку у відео (з уповільненням — довша)
    if (std::abs(vspeed - 1.0) > 1e-6)
        log_info("Швидкість ×{:g}: {}", vspeed, vspeed < 1 ? "уповільнення" : "прискорення");
    const double full_seconds = std::max(0, full_end - full_start) * vti;
    if (test_run_) {
        const int32_t test_ticks = static_cast<int32_t>(std::llround(kTestSeconds / vti));
        s_.end_tick = std::min(A.last_tick, full_start + test_ticks);
        s_.manual_mode = false;
        s_.quit_game_when_done = true;
        s_.subtitles_srt = false;
        s_.chat_srt = false;
        s_.markers.clear();
        s_.keep_temp_files = false;
        // Тимчасовий файл у папці програми (послідовність зображень — у MKV)
        std::string ext = to_lower(path_to_utf8(path_from_utf8(s_.output_path).extension()));
        if (ext.size() > 1) ext = ext.substr(1);
        if (ext.empty() || s_.output_path.find('%') != std::string::npos) ext = "mkv";
        if (s_.video_codec == "png" || s_.video_codec == "tiff" || s_.video_codec == "bmp" || s_.video_codec == "mjpeg")
            s_.video_codec = "libx264";
        s_.output_path = path_to_utf8(app_data_dir() / ("gmdr_test_run." + ext));
        std::error_code ec;
        fs::remove(path_from_utf8(s_.output_path), ec);
        log_info("Тестовий прогін: {:.0f} с з початку фрагмента ({}), файл {}", kTestSeconds,
                 format_duration(full_start * static_cast<double>(A.tick_interval)), s_.output_path);
    }
    if (!s_.audio || !s_.game_audio) set_check(kCheckAudio, CheckItem::Skipped, "звук гри вимкнено");
    if (s_.manual_mode) set_check(kCheckDriver, CheckItem::Skipped, "ручний режим");

    if (s_.output_path.empty()) s_.output_path = default_output_path(s_.demo_path, "mp4");
    EncodeSettings es;
    if (!make_encode_settings(s_, es, &err)) {
        fail(err);
        return;
    }
    // Розмір вікна гри
    int rw = s_.render_width > 0 ? s_.render_width : s_.width;
    int rh = s_.render_height > 0 ? s_.render_height : s_.height;
    const int32_t range_start = std::max(0, s_.start_tick);
    const int32_t range_end = s_.end_tick > 0 ? std::min(s_.end_tick, A.last_tick) : A.last_tick;
    const double expected_seconds = std::max(0, range_end - range_start) * vti;
    if (s_.target_size_mb > 0 && expected_seconds > 0) {
        const double size_seconds = test_run_ ? full_seconds : expected_seconds;
        es.video.bitrate = bitrate_for_target_size(s_.target_size_mb, size_seconds, s_.audio ? es.audio.bitrate : 0);
        log_info("Цільовий розмір {:.0f} МБ на {}: бітрейт відео {:.2f} Мбіт/с", s_.target_size_mb,
                 format_duration(size_seconds), es.video.bitrate / 1e6);
    }
    // Додаткові версії (Discord, вертикальна...) — у тестовому прогоні не потрібні
    if (!test_run_) es.extras = make_extra_outputs(s_.extra_versions, es, expected_seconds);
    // Пакет для монтажу: окремі WAV у теці "назва_монтаж" поруч із відео (проєкт XML — після рендеру)
    if (s_.edit_package && !test_run_ && s_.output_path.find('%') == std::string::npos) {
        const fs::path out = path_from_utf8(s_.output_path);
        es.stems_dir = path_to_utf8(out.parent_path() / path_from_utf8(path_to_utf8(out.stem()) + "_монтаж"));
    }
    else if (!trim(s_.extra_versions).empty()) log_info("Тестовий прогін: додаткові версії не кодуються");
    update([&](Progress& p) {
        p.range_start = range_start;
        p.range_end = range_end;
        p.demo_total = A.last_tick;
        p.expected_seconds = expected_seconds;
    });

    // ---- 2. Підготовка гри ----
    set_stage(test_run_ ? "Тестовий прогін: підготовка гри" : "Підготовка гри", 0);
    if (!prepare(&err)) {
        cleanup(false);   // гру ще не запускали
        set_check(kCheckLaunch, CheckItem::Failed, err);
        fail(err);
        return;
    }
    if (cancel_) {
        cleanup(false);
        return;
    }

    // ---- 3. Запуск ----
    set_stage("Запуск Garry's Mod");
    game::WindowMode window_mode = s_.manual_mode ? game::WindowMode::Normal : game::window_mode_from_string(s_.game_window);
    if (s_.rtx && window_mode == game::WindowMode::Offscreen) {
        // Перевірено: за межами екрана Remix віддає чорні кадри, а на моніторі (навіть позаду вікон) — з RTX
        window_mode = game::WindowMode::Behind;
        log_info("RTX: вікно гри буде позаду інших вікон, а не за межами екрана (інакше Remix не малює)");
    }
    fs::path exe = s_.game_exe.empty() ? gmod_->default_exe() : path_from_utf8(s_.game_exe);
    std::vector<std::string> args;
    const std::string exe_name = to_lower(path_to_utf8(exe.filename()));
    if (exe_name.rfind("hl2", 0) == 0) args.insert(args.end(), {"-game", "garrysmod"});
    args.insert(args.end(), {"-novid", "-windowed", "-noborder", "-w", std::to_string(rw), "-h", std::to_string(rh)});
    if (window_mode == game::WindowMode::Offscreen) {
        // Одразу створити вікно за межами екрана (якщо рушій не зрозуміє -x/-y, пересунемо його самі)
        const auto [x, y] = game::GameProcess::offscreen_position();
        args.insert(args.end(), {"-x", std::to_string(x), "-y", std::to_string(y)});
    }
    if (s_.rtx) {
        // Так гру запускає сам RTXLauncher; файли гри в копії пропатчені, тож VAC вимкнено (-insecure)
        for (const char* a : {"-dxlevel", "90", "-nod3d9ex", "+mat_disable_d3d9ex", "1", "-insecure"}) args.push_back(a);
    }
    // Параметри запуску без конфігу завдання: якщо в пункту черги вони інші, гру треба перезапустити
    const std::string launch_sig = game::format_command_line(exe, args) + " | " + s_.extra_launch_args + " | " + s_.game_window;
    args.insert(args.end(), {"-condebug", "+exec", "gmdr/job_" + id_ + ".cfg"});
    for (const auto& a : split(s_.extra_launch_args, ' ')) args.push_back(trim(a));
    if (handoff_ && handoff_->proc && handoff_->proc->running() && handoff_->launch_sig != launch_sig) {
        log_info("Цей пункт черги потребує інших параметрів запуску гри (розмір вікна, RTX...) — перезапускаю гру");
        game::request_cancel(*gmod_, handoff_->waiting_id);
        if (!handoff_->proc->wait(30000)) handoff_->proc->terminate();
        handoff_->proc.reset();
        game::GameProcess::wait_all_exited({"gmod.exe", "hl2.exe", "gmod", "hl2_linux"}, 15000);
        game::remove_job_files(*gmod_, handoff_->waiting_id);
    }
    if (handoff_) handoff_->launch_sig = launch_sig;
    std::unique_ptr<game::GameProcess> proc;
    if (handoff_ && handoff_->proc && handoff_->proc->running()) {
        // Черга: гра вже запущена попереднім пунктом, драйвер чекає це завдання
        proc = std::move(handoff_->proc);
        log_info("Гра вже запущена (PID {}) — наступний пункт черги без перезапуску", proc->pid());
    } else {
        log_info("Команда запуску: {}", game::format_command_line(exe, args));
        proc = game::GameProcess::launch(exe, args, gmod_->root, {{"SteamAppId", "4000"}, {"SteamGameId", "4000"}}, &err,
                                         window_mode != game::WindowMode::Normal);
    }
    if (!proc) {
        cleanup();
        set_check(kCheckLaunch, CheckItem::Failed, err);
        fail(err);
        return;
    }
    set_check(kCheckLaunch, CheckItem::Ok, std::format("PID {}", proc->pid()));
    auto apply_process_tweaks = [&](game::GameProcess& p) {
        if (s_.high_priority) p.set_high_priority(true);
        if (window_mode != game::WindowMode::Normal) {
            if (p.disable_background_throttling())
                log_debug("Для гри вимкнено EcoQoS і ігнорування таймера у фоні (Windows 11)");
        }
    };
    apply_process_tweaks(*proc);
    const auto t_launch = Clock::now();
    if (s_.manual_mode)
        log_info("РУЧНИЙ РЕЖИМ: у грі увімкніть демо, перемотайте до потрібного місця і введіть у консолі gmdr_start "
                 "(зупинити — gmdr_stop). Програма кодуватиме кадри на льоту.");
    else if (window_mode == game::WindowMode::Offscreen)
        log_info("Гра запускається у фоні (вікно за межами екрана). Подивитися на неї — кнопка «Показати гру».");
    else if (window_mode == game::WindowMode::Behind)
        log_info("Гра запускається позаду інших вікон. Не клацайте у вікні гри під час рендеру.");
    else
        log_info("Гра запускається... Не згортайте і не клацайте у вікні гри під час рендеру.");

    // ---- Вікно гри і звук у мікшері Windows ----
    bool window_placed = false, showing_game = false;
    game::WindowMode effective_mode = window_mode;
    auto last_window_fix = Clock::now() - std::chrono::seconds(10);
    const bool restore_mute = game::game_audio_mute_pending();   // звук лишився вимкненим після збою
    bool muted = false;
    auto last_mute_try = Clock::now() - std::chrono::seconds(10);
    auto unmute_game = [&]() {
        if (!muted && !restore_mute) return;
        if (game::set_process_audio_mute(proc->pid(), false) > 0 || !muted) {
            if (muted) log_debug("Звук гри в мікшері Windows увімкнено назад");
            game::mark_game_audio_muted(false);
            muted = false;
        }
    };
    auto manage_window_and_sound = [&](bool recording_finished) {
        const auto now = Clock::now();
        // Звук: вимикаємо, щойно в гри з'явиться аудіосесія (і повторюємо — сесія може змінитися)
        if (now - last_mute_try > std::chrono::seconds(2)) {
            last_mute_try = now;
            if (s_.mute_game_sound && !recording_finished) {
                if (game::set_process_audio_mute(proc->pid(), true) > 0 && !muted) {
                    muted = true;
                    game::mark_game_audio_muted(true);
                    log_info("Звук Garry's Mod вимкнено в мікшері Windows на час рендеру (у відео звук буде)");
                }
            } else if (muted || (restore_mute && game::game_audio_mute_pending())) {
                unmute_game();
            }
        }
        // Вікно: розміщуємо, коли з'явиться, і перевіряємо, чи гра його не повернула
        const bool want_show = show_game_.load();
        if (want_show != showing_game) {
            showing_game = want_show;
            if (want_show) {
                proc->show_window_front();
                log_info("Вікно гри показано");
            } else {
                proc->place_window(effective_mode);
                log_info("Вікно гри знову приховано");
            }
            update([&](Progress& p) { p.game_hidden = !want_show && effective_mode != game::WindowMode::Normal; });
        }
        if (!showing_game && effective_mode != game::WindowMode::Normal &&
            now - last_window_fix > std::chrono::milliseconds(window_placed ? 3000 : 300) &&
            now - t_launch < std::chrono::minutes(10)) {
            last_window_fix = now;
            if (proc->place_window(effective_mode) && !window_placed) {
                window_placed = true;
                update([&](Progress& p) { p.game_hidden = true; });
            }
        }
    };

    // ---- 4. Конвеєр ----
    frames::SequenceOptions so;
    so.dir = tmp_dir_;
    so.prefix = movie_prefix();
    so.live = true;
    so.delete_after_read = !s_.keep_temp_files;
    so.decode_threads = s_.threads > 0 ? std::max(1, s_.threads / 2) : 0;
    auto reader_ptr = std::make_unique<frames::FrameSequenceReader>(so);
    frames::FrameSequenceReader* rp = reader_ptr.get();
    fs::path frames_dir = tmp_dir_;
    Clock::time_point recording_since{};
    bool relocated = false;
    ThreadPool pool(s_.threads > 0 ? static_cast<unsigned>(s_.threads) : 0);
    es.crash_safe = s_.crash_safe && !test_run_;
    // unique_ptr — щоб закрити файли (напр. WAV гри) ДО прибирання тимчасової папки
    auto session_ptr = std::make_unique<EncodeSession>(es, &pool, preview_.get());
    EncodeSession& session = *session_ptr;

    std::optional<game::DriverStatus> st;
    auto last_poll = Clock::now() - std::chrono::seconds(1);
    auto last_progress = Clock::now();
    auto last_disk_check = Clock::now() - std::chrono::seconds(10);
    bool producer_done = false, cancel_sent = false, recording_seen = false, wav_warned = false;
    bool attached_after_relaunch = false, disk_low = false, disk_warned = false, window_fallback = false;
    bool rtx_checked = false;
    int32_t video_start_tick = range_start;   // тік першого кадру відео (для розділів і субтитрів чату)
    int64_t last_frames = 0;
    auto last_speed_t = Clock::now();
    auto last_frame_t = Clock::now();
    Clock::time_point first_frame_t{};
    double speed = 0, wait_ms = 0;
    frames::Image img;

    auto fatal = [&](const std::string& msg) {
        unmute_game();
        if (proc->suspended()) proc->resume();
        if (!s_.manual_mode) game::request_cancel(*gmod_, id_);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        if (proc->running()) proc->terminate();
        session.abort();
        rp->set_producer_done();
        session_ptr.reset();
        cleanup();
        // Перший крок, що не пройшов, — позначаємо з підказкою
        std::string hint;
        update([&](Progress& p) {
            for (size_t i = 0; i < p.checks.size(); ++i)
                if (p.checks[i].state == CheckItem::Pending) {
                    p.checks[i].state = CheckItem::Failed;
                    p.checks[i].detail = msg.substr(0, msg.find('\n'));
                    hint = kCheckHints[i];
                    break;
                }
        });
        std::string full = msg;
        if (!hint.empty() && full.find(hint) == std::string::npos) full += "\n\nЩо робити: " + hint;
        auto tail = game::console_log_tail(*gmod_, 15);
        if (!tail.empty()) {
            full += "\n\nОстанні рядки консолі гри:";
            for (const auto& l : tail) full += "\n  " + l;
        }
        fail(full);
    };

    for (;;) {
        if (kill_) {
            log_warn("Рендер перервано — закриваю гру");
            unmute_game();
            if (proc->running()) proc->terminate();
            session.abort();
            rp->set_producer_done();
            session_ptr.reset();
            cleanup();
            update([](Progress& p) { p.stage = "Скасовано"; });
            return;
        }
        if (cancel_ && !cancel_sent) {
            cancel_sent = true;
            if (proc->suspended()) proc->resume();
            if (s_.manual_mode || !recording_seen) {
                producer_done = true;
                if (!recording_seen && proc->running()) {
                    unmute_game();
                    proc->terminate();
                }
            } else {
                log_info("Зупиняю запис (буде збережено вже відрендерене)...");
                game::request_cancel(*gmod_, id_);
            }
        }

        // ---- Стан гри ----
        const auto now = Clock::now();
        if (now - last_poll > std::chrono::milliseconds(200)) {
            last_poll = now;
            if (!s_.manual_mode) {
                if (auto ns = game::read_status(*gmod_, id_)) {
                    if (!st || st->state != ns->state) log_info("Гра: стан «{}» {}", ns->state, ns->message);
                    st = ns;
                    set_check(kCheckDriver, CheckItem::Ok);
                }
                if (st) {
                    // "waiting" — черга: запис закінчено, гра чекає наступне завдання
                    const bool finished = st->state == "done" || st->state == "quit" || st->state == "waiting";
                    if (st->state == "recording" || st->state == "stopping" || finished) {
                        recording_seen = true;
                        set_check(kCheckDemo, CheckItem::Ok, std::format("тік {}", st->start_tick >= 0 ? st->start_tick : st->tick));
                    }
                    if (finished) producer_done = true;
                    if (st->state == "error") {
                        std::string hint;
                        if (st->message.find("не запустилося") != std::string::npos ||
                            st->message.find("не завантажилось") != std::string::npos)
                            hint = std::format("\nЯкщо демо записане на сервері, перевірте, що у вас є карта «{}» і "
                                               "потрібний контент (зайдіть на той сервер або підпишіться на карту в "
                                               "Workshop) і що демо відкривається в самій грі (playdemo).",
                                               A.header.map_name);
                        fatal("Гра повідомила про помилку: " + st->message + hint);
                        return;
                    }
                } else if (!proc->suspended() && now - t_launch > std::chrono::seconds(240)) {
                    fatal("Драйвер у меню GMod не відповідає. Можливо, файл lua/menu/menu.lua було оновлено — "
                          "спробуйте «Встановити драйвер» ще раз.");
                    return;
                }
            }
            if (!proc->running()) {
                if (!recording_seen && !attached_after_relaunch && now - t_launch < std::chrono::seconds(90)) {
                    // Гра могла перезапуститися через Steam — шукаємо новий процес (не дочірній CEF)
                    auto pids = game::GameProcess::find_by_name({"gmod.exe", "hl2.exe", "gmod", "hl2_linux"}, true);
                    if (!pids.empty()) {
                        if (auto p2 = game::GameProcess::attach(pids.front(), &err)) {
                            log_info("Гра перезапустилася (PID {}) — продовжую", pids.front());
                            proc = std::move(p2);
                            apply_process_tweaks(*proc);
                            window_placed = false;
                            attached_after_relaunch = true;
                            continue;
                        }
                    }
                    if (now - t_launch < std::chrono::seconds(15)) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                        continue;   // даємо Steam час перезапустити гру
                    }
                }
                if (!recording_seen && rp->delivered() == 0 && !s_.manual_mode) {
                    fatal(std::format("Гра закрилася до початку запису (код {}). Перевірте, що Steam запущено і демо "
                                      "відкривається в грі вручну.", describe_exit_code(proc->exit_code().value_or(-1))));
                    return;
                }
                producer_done = true;
            }
            if (proc->running()) manage_window_and_sound(producer_done || (st && (st->state == "stopping" || st->state == "done")));
        }
        if (producer_done) {
            rp->set_producer_done();
            session.game_audio_finished();
        }

        // ---- Якщо гра пише кадри не туди, куди ми чекаємо, — шукаємо їх ----
        if (recording_seen && recording_since == Clock::time_point{}) recording_since = Clock::now();
        if (!relocated && recording_since != Clock::time_point{} && !rp->saw_any_file() &&
            (Clock::now() - recording_since > std::chrono::seconds(8) || producer_done)) {
            relocated = true;
            const std::vector<fs::path> candidates = {gmod_->garrysmod, gmod_->garrysmod / "gmdr_tmp", gmod_->root};
            for (const auto& dir : candidates) {
                std::error_code ec;
                bool found = false;
                for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
                    if (starts_with_i(path_to_utf8(it->path().filename()), movie_prefix())) found = true;
                if (!found) continue;
                log_warn("Гра записує кадри в іншу папку ({}) — переключаюся на неї", path_to_utf8(dir));
                frames_dir = dir;
                stray_dir_ = dir;
                so.dir = dir;
                reader_ptr = std::make_unique<frames::FrameSequenceReader>(so);
                rp = reader_ptr.get();
                break;
            }
            if (!rp->saw_any_file() && frames_dir == tmp_dir_)
                log_warn("Гра вже записує, але кадрів ще немає. Якщо так і лишиться — перевірте консоль гри (garrysmod/console.log)");
        }

        // ---- Кадри ----
        if (rp->skipped() > 60 && rp->skipped() > rp->delivered()) {
            fatal("Не вдалося прочитати кадри, які записує гра: " + rp->last_error());
            return;
        }
        // "Чекали на гру": у черзі нічого готового (останній файл на диску ще дописується),
        // а next_for() чекає, поки гра запише наступний кадр
        const bool starving = recording_seen && !producer_done && !proc->suspended() && rp->pending_files() <= 1;
        const auto wait_t0 = Clock::now();
        const auto w = rp->next_for(img, 40);
        if (starving) wait_ms += std::chrono::duration<double, std::milli>(Clock::now() - wait_t0).count();
        if (w == frames::FrameSequenceReader::Wait::End) break;
        if (w == frames::FrameSequenceReader::Wait::Frame) {
            recording_seen = true;
            last_frame_t = Clock::now();
            if (!session.started()) {
                // Чекаємо на тік першого кадру (потрібен для синхронізації голосу)
                int32_t first_tick = std::max(0, s_.start_tick);
                if (!s_.manual_mode) {
                    const auto t0 = Clock::now();
                    while ((!st || st->start_tick < 0) && Clock::now() - t0 < std::chrono::seconds(5)) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        if (auto ns = game::read_status(*gmod_, id_)) st = ns;
                    }
                    if (st && st->start_tick >= 0) first_tick = st->start_tick;
                    else log_warn("Не вдалося дізнатися тік початку запису — голос може бути трохи зсунутий");
                }
                log_info("Запис почався: тік {}, кадри гри {}x{}", first_tick, img.width, img.height);
                video_start_tick = first_tick;
                set_check(kCheckDemo, CheckItem::Ok, std::format("тік {}", first_tick));
                set_check(kCheckFrames, CheckItem::Ok, std::format("{}×{}, {}", img.width, img.height,
                                                                   frames::is_yuv(img.layout) ? "JPEG" : "TGA"));
                first_frame_t = Clock::now();
                if (img.width != rw || img.height != rh)
                    log_warn("Гра рендерить {}x{} замість {}x{} (обмеження монітора?) — кадри буде масштабовано до {}x{}",
                             img.width, img.height, rw, rh, s_.width, s_.height);
                AudioSourcesSpec spec;
                spec.game_audio = s_.game_audio;
                spec.game_wav = frames_dir / path_from_utf8(movie_prefix() + ".wav");
                spec.game_wav_live = true;
                spec.game_offset = s_.game_audio_offset;
                spec.game_gain = static_cast<float>(s_.game_volume);
                spec.voices = speakers;
                spec.voice_gains = speaker_gains(s_, speakers);
                spec.voice_origin_sample =
                    static_cast<int64_t>(std::llround(first_tick * static_cast<double>(A.tick_interval) * media::kMixRate));
                spec.voice_delay = s_.voice_delay;
                spec.voice_gain = static_cast<float>(s_.voice_volume);
                spec.mic_file = s_.mic_file.empty() ? fs::path() : path_from_utf8(s_.mic_file);
                spec.mic_offset = s_.mic_offset;
                spec.mic_gain = static_cast<float>(s_.mic_volume);
                spec.voice_cleanup = voice_fx;
                spec.duck_game = s_.duck_game;
                spec.loudness_target = s_.loudness_target;
                spec.speed = vspeed;
                spec.speed_mute = s_.speed_audio == "mute";
                if (!session.begin(img.width, img.height, spec, &err)) {
                    fatal("Не вдалося почати кодування: " + err);
                    return;
                }
                if (s_.subtitles_srt && !speakers.empty()) {
                    // Субтитри пишемо одразу: відрізок і голоси вже відомі
                    write_speaker_subtitles(s_, speakers, spec.voice_origin_sample, expected_seconds);
                }
                if (s_.speaker_overlay && !speakers.empty())
                    session.set_overlay(make_overlay(s_, speakers, spec.voice_origin_sample, expected_seconds, img.width,
                                                     img.height));
                set_stage(test_run_ ? "Тестовий прогін: рендер" : "Рендер");
                update([&](Progress& p) {
                    p.video_desc = session.video_description();
                    p.audio_desc = session.audio_description();
                });
            }
            if (!session.push_subframe(std::move(img), &err)) {
                fatal("Помилка кодування: " + err);
                return;
            }
            if (session.frames_encoded() > 0) set_check(kCheckEncode, CheckItem::Ok, session.video_description());
            // RTX: чи є трасування в кадрі. Чорний кадр (лише HUD) — Remix не малює.
            if (s_.rtx && !rtx_checked && session.video_seconds() >= 1.0) {
                PreviewFrame pf;
                if (preview_->get_if_newer(0, pf) && !pf.rgba.empty()) {
                    rtx_checked = true;
                    size_t dark = 0;
                    const size_t n = pf.rgba.size() / 4;
                    for (size_t i = 0; i < n; ++i) {
                        const uint8_t* q = &pf.rgba[i * 4];
                        if (std::max({q[0], q[1], q[2]}) < 14) ++dark;
                    }
                    const double share = n ? static_cast<double>(dark) / static_cast<double>(n) : 1.0;
                    if (share > 0.9) {
                        set_check(kCheckRtx, CheckItem::Failed, std::format("кадр на {:.0f}% чорний", share * 100));
                        log_warn("RTX: кадр майже чорний ({:.0f}%) — схоже, Remix не малює. {}", share * 100, kCheckHints[kCheckRtx]);
                    } else {
                        set_check(kCheckRtx, CheckItem::Ok);
                    }
                }
            }
            if (s_.game_audio && s_.audio) {
                if (session.game_audio_opened()) set_check(kCheckAudio, CheckItem::Ok);
                else if (!wav_warned && session.video_seconds() > std::min(5.0, kTestSeconds * 0.8)) {
                    wav_warned = true;
                    log_warn("Гра не записує звук (немає WAV) — звук гри буде тишею");
                    set_check(kCheckAudio, CheckItem::Failed, "гра не створила WAV");
                    session.game_audio_finished();
                }
            }
        } else {
            if (session.started() && !session.pump_audio(&err)) {
                fatal("Помилка кодування звуку: " + err);
                return;
            }
        }

        // ---- Сторож: гра перестала віддавати кадри ----
        if (proc->suspended() || !recording_seen || producer_done) last_frame_t = Clock::now();
        const auto silent = Clock::now() - last_frame_t;
        if (!window_fallback && effective_mode == game::WindowMode::Offscreen && silent > std::chrono::seconds(20)) {
            window_fallback = true;
            effective_mode = game::WindowMode::Behind;
            last_frame_t = Clock::now();
            log_warn("Гра за межами екрана не віддає кадрів — повертаю вікно на екран (позаду інших вікон). "
                     "Якщо так буде щоразу, виберіть на вкладці «Гра» режим «позаду інших вікон».");
            proc->show_window_front();
            proc->place_window(game::WindowMode::Behind);
        } else if (silent > std::chrono::seconds(120)) {
            fatal("Гра вже 2 хвилини не віддає нових кадрів (зависла?). Уже закодоване відео збережено не буде — "
                  "спробуйте ще раз або зменште роздільну здатність.");
            return;
        }

        // ---- Місце на диску: призупиняємо гру до того, як диск заповниться ----
        if (Clock::now() - last_disk_check > std::chrono::seconds(2)) {
            last_disk_check = Clock::now();
            uint64_t free_tmp = free_disk_space(frames_dir);
            const fs::path out_dir = path_from_utf8(s_.output_path).parent_path();
            uint64_t free_out = free_disk_space(out_dir.empty() ? fs::path(".") : out_dir);
            // Для автотестів: GMDR_TEST_LOW_DISK=N — перші N секунд запису "місця майже немає"
            static const double test_low_disk = [] {
                const char* e = std::getenv("GMDR_TEST_LOW_DISK");
                return e ? std::atof(e) : 0.0;
            }();
            if (test_low_disk > 0 && recording_since != Clock::time_point{} &&
                std::chrono::duration<double>(Clock::now() - recording_since).count() < test_low_disk)
                free_tmp = free_out = 100ull << 20;
            const uint64_t low = 1ull << 30, ok = 3ull << 29;   // 1 ГіБ — пауза, 1.5 ГіБ — продовжити
            const bool now_low = (free_tmp > 0 && free_tmp < low) || (free_out > 0 && free_out < low);
            const bool now_ok = (free_tmp == 0 || free_tmp > ok) && (free_out == 0 || free_out > ok);
            if (now_low && !disk_low) {
                disk_low = true;
                if (!disk_warned) {
                    disk_warned = true;
                    log_warn("Закінчується місце на диску (вільно: з грою {}, для відео {}) — гру призупинено. "
                             "Звільніть місце, і рендер продовжиться сам, або натисніть «Зупинити».",
                             format_bytes(free_tmp), format_bytes(free_out));
                }
            } else if (disk_low && now_ok) {
                disk_low = false;
                log_info("Місця на диску знову достатньо — продовжую");
            }
        }

        // ---- Зворотний тиск: не даємо кадрам заполонити диск ----
        const int64_t pending = rp->pending_files();
        const bool queue_high = pending > std::max(8, s_.max_pending_frames);
        const bool queue_low = pending <= std::max(2, s_.max_pending_frames / 3);
        if (!proc->suspended() && proc->running() && (queue_high || (disk_low && !cancel_))) {
            if (proc->suspend()) log_debug("Гра на паузі: {}", disk_low ? "мало місця на диску" : std::format("кодер не встигає ({} кадрів у черзі)", pending));
        } else if (proc->suspended() && ((queue_low && !disk_low) || cancel_)) {
            proc->resume();
        }

        // ---- Прогрес ----
        if (Clock::now() - last_progress > std::chrono::milliseconds(150)) {
            last_progress = Clock::now();
            const double dt = std::chrono::duration<double>(last_progress - last_speed_t).count();
            if (dt >= 1.0) {
                const double inst = (session.frames_encoded() - last_frames) / dt;
                speed = speed <= 0 ? inst : speed * 0.7 + inst * 0.3;
                last_frames = session.frames_encoded();
                last_speed_t = last_progress;
            }
            const auto rs = rp->stats();
            const auto ps = session.stats();
            const double rec_ms = recording_since == Clock::time_point{}
                                      ? 0
                                      : std::chrono::duration<double, std::milli>(Clock::now() - recording_since).count();
            update([&](Progress& p) {
                if (st) {
                    p.demo_tick = st->tick;
                    p.demo_total = st->total > 0 ? st->total : p.demo_total;
                    p.driver_state = st->state;
                    if (!session.started() && st->state == "loading") p.stage = "Завантаження демо в грі";
                }
                p.subframes = session.subframes_in();
                p.frames = session.frames_encoded();
                p.video_seconds = session.video_seconds();
                p.pending_files = pending;
                p.pending_bytes = rp->pending_bytes();
                p.bytes_written = session.bytes_written();
                p.game_running = proc->running();
                p.game_paused = proc->suspended();
                p.disk_low = disk_low;
                p.speed_fps = speed;
                if (rs.frames > 0) {
                    p.stat_read_ms = rs.read_ms / static_cast<double>(rs.frames);
                    p.stat_decode_ms = rs.decode_ms / static_cast<double>(rs.frames);
                }
                p.stat_blend_ms = ps.blend_ms;
                p.stat_convert_ms = ps.convert_ms;
                p.stat_encode_ms = ps.encode_ms;
                p.stat_audio_ms = ps.audio_ms;
                if (rec_ms > 1000) p.stat_game_wait = std::clamp(wait_ms / rec_ms, 0.0, 1.0);
                if (expected_seconds > 0 && session.started()) {
                    p.fraction = std::clamp(session.video_seconds() / expected_seconds, 0.0, 1.0);
                    const double fps_v = es.video.fps.num / static_cast<double>(es.video.fps.den);
                    const double remaining_frames = std::max(0.0, (expected_seconds - session.video_seconds()) * fps_v);
                    p.eta = speed > 0.01 ? remaining_frames / speed : -1;
                }
            });
        }
    }

    // ---- 5. Завершення ----
    if (proc->suspended()) proc->resume();
    unmute_game();
    set_stage("Завершення файлу");
    session.game_audio_finished();
    if (!session.started()) {
        const bool hand_over = keep_game_ && proc->running();   // черга: гра піде наступному пункту
        const bool leave_open = hand_over || (proc->running() && !s_.quit_game_when_done);
        if (proc->running() && !leave_open) proc->terminate();
        if (hand_over) {
            handoff_->proc = std::move(proc);
            handoff_->waiting_id = id_;
        }
        session_ptr.reset();
        cleanup(!leave_open);
        if (cancel_) {
            update([](Progress& p) { p.stage = "Скасовано"; });
            return;
        }
        set_check(kCheckFrames, CheckItem::Failed, "жодного кадру");
        fail(std::string("Гра не записала жодного кадру. Перевірте журнал і консоль гри (garrysmod/console.log).\n\nЩо робити: ") +
             kCheckHints[kCheckFrames]);
        return;
    }
    const auto encode_end_t = Clock::now();
    if (!session.finish(&err)) {
        if (proc->running()) proc->terminate();
        session_ptr.reset();
        cleanup();
        fail("Не вдалося завершити файл: " + err);
        return;
    }
    const bool hand_over = keep_game_ && proc->running();   // черга: гра чекає наступне завдання
    if (!hand_over && s_.quit_game_when_done && proc->running()) {
        log_info("Чекаю, поки гра закриється...");
        if (!proc->wait(30000)) {
            log_warn("Гра не закрилась сама — закриваю примусово");
            proc->terminate();
        }
    }
    const double secs = session.video_seconds();
    const int64_t frames_done = session.frames_encoded();
    const PipelineStats pstats = session.stats();
    const std::vector<std::string> extras_done = session.finished_extras();
    const auto stems = session.stem_files();
    const bool fragmented = es.crash_safe && is_mov_family(s_.output_path, s_.container);
    session_ptr.reset();
    if (hand_over) {
        handoff_->proc = std::move(proc);
        handoff_->waiting_id = id_;
    }
    cleanup(!hand_over && s_.quit_game_when_done);
    if (rp->skipped() > 0) log_warn("Пропущено кадрів: {}", rp->skipped());
    const auto chapters =
        s_.chapters ? chapters_for_range(parse_markers(s_.markers), video_start_tick,
                                         video_start_tick + static_cast<int32_t>(std::llround(secs / vti)), vti)
                    : std::vector<Chapter>{};
    finalize_output(s_, fragmented, frames_done, secs, chapters);
    if (!test_run_) make_after_render(s_, extras_done);
    if (!es.stems_dir.empty()) {
        // Проєкт для Premiere / DaVinci Resolve: відео, окремі WAV і позначки на одній шкалі
        EditProject pr;
        const fs::path out = fs::absolute(path_from_utf8(s_.output_path));
        pr.name = path_to_utf8(out.stem());
        pr.video_path = path_to_utf8(out);
        pr.width = es.video.width > 0 ? es.video.width : s_.width;
        pr.height = es.video.height > 0 ? es.video.height : s_.height;
        pr.fps_num = es.video.fps.num;
        pr.fps_den = es.video.fps.den;
        pr.frames = frames_done;
        pr.video_has_audio = es.audio_enabled;
        for (const auto& st : stems) pr.stems.push_back({st.title, path_to_utf8(fs::absolute(path_from_utf8(st.path)))});
        pr.markers = chapters_for_range(parse_markers(s_.markers), video_start_tick,
                                        video_start_tick + static_cast<int32_t>(std::llround(secs / vti)), vti);
        const fs::path xml = path_from_utf8(es.stems_dir) / path_from_utf8(pr.name + ".xml");
        std::string werr;
        if (write_file_text(xml, make_fcp7_xml(pr), &werr))
            log_info("Пакет для монтажу: {} (відкрийте в Premiere Pro або DaVinci Resolve: File → Import)", path_to_utf8(xml));
        else
            log_warn("Не вдалося записати проєкт для монтажу: {}", werr);
    }
    if (s_.chat_srt && frames_done > 0) write_chat_subtitles(s_, A, video_start_tick, secs);
    if (s_.rtx) {
        // Звірка з журналом Remix: чи прийняв він налаштування для рендеру
        const auto eff = game::read_remix_effective_options(*gmod_);
        for (const auto& [k, v] : game::rtx_render_profile_values()) {
            auto it = eff.find(k);
            if (it == eff.end()) continue;   // Remix пише лише значення, відмінні від типових
            if (it->second != v)
                log_warn("Remix: {} = {} замість {} — налаштування користувача в меню Remix мають вищий пріоритет", k,
                         it->second, v);
            else
                log_debug("Remix прийняв {} = {}", k, v);
        }
    }
    log_info("Готово! {} — {} кадрів, {}, {}", s_.output_path, frames_done, format_duration(secs),
             format_bytes(file_size_or_zero(path_from_utf8(s_.output_path))));
    if (cancel_) log_info("Запис зупинено достроково — збережено відрендерену частину");

    if (test_run_) {
        // Звіт тестового прогону: кроки, швидкість і прогноз для всього фрагмента
        const double work_s = first_frame_t == Clock::time_point{}
                                  ? 0
                                  : std::chrono::duration<double>(encode_end_t - first_frame_t).count();
        const double fps_speed = work_s > 0 ? frames_done / work_s : 0;
        const double fps_v = es.video.fps.num / static_cast<double>(es.video.fps.den);
        const double full_frames = full_seconds * fps_v;
        const auto prog = progress();
        bool all_ok = true;
        std::string rep;
        for (const auto& c : prog.checks) {
            const char* mark = c.state == CheckItem::Ok ? "✓" : c.state == CheckItem::Skipped ? "–" : "✗";
            if (c.state == CheckItem::Failed || c.state == CheckItem::Pending) all_ok = false;
            rep += std::format("  {} {}{}\n", mark, c.name, c.detail.empty() ? "" : " — " + c.detail);
        }
        std::string head = all_ok ? "Усе працює." : "Є проблеми — дивіться кроки нижче.";
        rep = head + "\n\n" + rep;
        rep += std::format("\nШвидкість: {:.1f} кадр/с відео", fps_speed);
        if (es.motion_blur_samples > 1) rep += std::format(" ({:.0f} під-кадрів гри за секунду)", fps_speed * es.motion_blur_samples);
        if (prog.stat_game_wait >= 0) rep += std::format("; програма чекала на гру {:.0f}% часу", prog.stat_game_wait * 100);
        rep += std::format("\nЕтапи на кадр: читання {:.1f} мс, декодування {:.1f} мс, колір {:.1f} мс, кодування {:.1f} мс",
                           prog.stat_read_ms, prog.stat_decode_ms, pstats.convert_ms, pstats.encode_ms);
        if (fps_speed > 0 && full_seconds > 0) {
            const double est_s = full_frames / fps_speed;
            const double bytes_done = static_cast<double>(file_size_or_zero(path_from_utf8(s_.output_path)));
            const double est_bytes = secs > 0 ? bytes_done / secs * full_seconds : 0;
            rep += std::format("\n\nВесь вибраний фрагмент ({}): рендер ≈ {}, файл ≈ {}", format_duration(full_seconds),
                               format_duration(est_s), format_bytes(static_cast<uint64_t>(est_bytes)));
            if (est_s > 6 * 3600) rep += "\nЦе дуже довго — розгляньте менший фрагмент, нижчу роздільну здатність або GPU-кодек.";
        }
        rep += "\n\nТестове відео: " + s_.output_path;
        set_report(rep);
        log_info("Тестовий прогін: {}", head);
    }
    succeed(s_.output_path);
}

// ============================== Черга рендерів ======================================
QueueJob::QueueJob(std::vector<RenderSettings> items) : items_(std::move(items)) {}

void QueueJob::set_show_game(bool show) {
    show_game_ = show;
    std::lock_guard lock(m_);
    if (job_) job_->set_show_game(show);
}

std::vector<QueueJob::ItemResult> QueueJob::results() const {
    std::lock_guard lock(m_);
    return results_;
}

void QueueJob::run() {
    KeepAwake keep_awake;
    const size_t n = items_.size();
    if (n == 0) {
        fail("Черга порожня");
        return;
    }
    auto handoff = std::make_shared<GameHandoff>();
    {
        std::lock_guard lock(m_);
        results_.assign(n, {});
    }
    int ok = 0;
    for (size_t i = 0; i < n && !cancel_; ++i) {
        current_ = static_cast<int>(i);
        const RenderSettings& s = items_[i];
        const std::string title = path_to_utf8(path_from_utf8(s.output_path.empty() ? s.demo_path : s.output_path).filename());
        log_info("==== Черга: пункт {} з {} — {} ====", i + 1, n, title);
        // Гру лишаємо відкритою для всіх, крім останнього пункту
        auto job = std::make_shared<RenderJob>(s, nullptr, nullptr, false, handoff, i + 1 < n);
        job->share_preview(preview_);
        job->set_show_game(show_game_);
        {
            std::lock_guard lock(m_);
            job_ = job;
        }
        const auto t0 = Clock::now();
        job->start();
        while (job->running()) {
            if (kill_) job->kill();
            else if (cancel_) job->cancel();
            Progress p = job->progress();
            p.stage = std::format("{} з {} · {}", i + 1, n, p.stage);
            p.fraction = (static_cast<double>(i) + std::clamp(p.fraction, 0.0, 1.0)) / static_cast<double>(n);
            update([&](Progress& q) { q = p; });
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        job->wait();
        ItemResult r;
        r.state = job->state();
        r.output = job->result();
        r.error = job->error();
        r.seconds = std::chrono::duration<double>(Clock::now() - t0).count();
        if (r.state == JobState::Succeeded) {
            ++ok;
        } else if (r.state == JobState::Failed) {
            log_error("Черга: пункт {} не вдався — {}", i + 1, r.error.substr(0, r.error.find('\n')));
            if (i + 1 < n && !cancel_) log_info("Черга: переходжу до наступного пункту");
        }
        std::lock_guard lock(m_);
        results_[i] = r;
        job_.reset();
    }

    // Гра могла лишитися відкритою (чергу скасовано посеред неї): просимо драйвер закрити її
    if (handoff->proc && handoff->proc->running()) {
        set_stage("Закриваю гру");
        log_info("Черга закінчилась — закриваю гру");
        if (handoff->gmod && !handoff->waiting_id.empty()) game::request_cancel(*handoff->gmod, handoff->waiting_id);
        if (!handoff->proc->wait(30000)) {
            log_warn("Гра не закрилась сама — закриваю примусово");
            handoff->proc->terminate();
        }
    }
    handoff->proc.reset();
    if (handoff->gmod) {
        const game::GModInstall& g = *handoff->gmod;
        std::error_code ec;
        const bool game_running = !game::GameProcess::wait_all_exited({"gmod.exe", "hl2.exe", "gmod", "hl2_linux"}, 15000);
        if (!game_running && !handoff->dir.empty()) {
            // Первинні копії, які лишились (гру закрито не останнім пунктом)
            if (fs::exists(handoff->dir / "config.cfg.bak", ec)) game::restore_config(g, handoff->dir / "config.cfg.bak");
            if (fs::exists(handoff->dir / "rtx.conf.bak", ec)) game::restore_rtx_profile(g, handoff->dir / "rtx.conf.bak");
            fs::remove_all(handoff->dir, ec);
            std::error_code ec2;
            if (fs::is_empty(handoff->dir.parent_path(), ec2)) fs::remove(handoff->dir.parent_path(), ec2);
        }
        // Драйвер, поки чекав, міг знову записати стан уже прибраного завдання
        if (!game_running)
            for (const auto& id : handoff->job_ids) game::remove_job_files(g, id);
    }

    // Підсумок
    std::string rep = std::format("Готово {} з {}:\n", ok, n);
    {
        std::lock_guard lock(m_);
        for (size_t i = 0; i < n; ++i) {
            const auto& r = results_[i];
            const char* mark = r.state == JobState::Succeeded ? "✓" : r.state == JobState::Failed ? "✗"
                             : r.state == JobState::Cancelled ? "–" : "·";
            std::string what = r.state == JobState::Succeeded ? r.output
                             : r.state == JobState::Failed    ? r.error.substr(0, r.error.find('\n'))
                             : r.state == JobState::Cancelled ? "скасовано"
                                                              : "не почато";
            rep += std::format("  {} {}. {} — {}\n", mark, i + 1,
                               path_to_utf8(path_from_utf8(items_[i].demo_path).filename()), what);
        }
    }
    set_report(rep);
    log_info("Черга: {}", rep);
    if (cancel_) return;   // стан "скасовано" виставить Job::start
    if (ok == 0) {
        fail("Жоден пункт черги не вдався.\n\n" + rep);
        return;
    }
    succeed(std::format("{} з {} відео готово", ok, n));
}

// ============================= EncodeFramesJob =====================================
// ============================== Перегляд у грі ==============================
WatchJob::WatchJob(RenderSettings s, std::shared_ptr<const demo::DemoAnalysis> analysis, int32_t from_tick)
    : s_(std::move(s)), analysis_(std::move(analysis)), from_tick_(std::max(0, from_tick)) {}

std::vector<game::DriverMark> WatchJob::take_marks() {
    std::lock_guard lock(marks_mutex_);
    return std::exchange(marks_, {});
}

void WatchJob::run() {
    std::string err;
    set_stage("Підготовка гри", -1);
    auto g = resolve_game(s_, true, &err);
    if (!g) {
        fail(err);
        return;
    }
    const std::string id = make_unique_id();
    const fs::path tmp = g->garrysmod / "gmdr_tmp" / id;
    std::error_code ec;
    fs::create_directories(tmp, ec);
    auto cleanup = [&]() {
        game::remove_job_files(*g, id);
        std::error_code e2;
        fs::remove_all(tmp, e2);
        if (fs::is_empty(tmp.parent_path(), e2)) fs::remove(tmp.parent_path(), e2);
    };
    const auto demo = demo_path_for_game(*g, s_.demo_path, tmp, id, &err);
    if (!demo) {
        cleanup();
        fail(err);
        return;
    }
    const double ti = analysis_ && analysis_->tick_interval > 0 ? analysis_->tick_interval : 1.0 / 66.0;
    game::DriverJob job;
    job.id = id;
    job.demo = *demo;
    job.mode = "watch";
    job.host_framerate = 0;
    job.start_tick = from_tick_;
    // Перемотуємо трохи раніше вибраного місця, щоб сцена встигла з'явитися
    const int32_t seek = from_tick_ - static_cast<int32_t>(std::llround(2.0 / ti));
    if (seek > 0) job.seek_tick = seek;
    job.quit_when_done = false;
    job.menu_delay = s_.menu_delay;
    job.tick_interval = ti;
    if (!game::write_job_files(*g, job, game::make_job_cfg(job, false, {}, {}), true, &err)) {
        cleanup();
        fail(err);
        return;
    }

    set_stage("Запуск Garry's Mod", -1);
    const fs::path exe = s_.game_exe.empty() ? g->default_exe() : path_from_utf8(s_.game_exe);
    std::vector<std::string> args;
    if (to_lower(path_to_utf8(exe.filename())).rfind("hl2", 0) == 0) args.insert(args.end(), {"-game", "garrysmod"});
    args.push_back("-novid");
    if (s_.rtx)
        for (const char* a : {"-dxlevel", "90", "-nod3d9ex", "+mat_disable_d3d9ex", "1", "-insecure"}) args.push_back(a);
    args.insert(args.end(), {"-condebug", "+exec", "gmdr/job_" + id + ".cfg"});
    for (const auto& a : split(s_.extra_launch_args, ' ')) args.push_back(trim(a));
    log_info("Команда запуску: {}", game::format_command_line(exe, args));
    auto proc = game::GameProcess::launch(exe, args, g->root, {{"SteamAppId", "4000"}, {"SteamGameId", "4000"}}, &err);
    if (!proc) {
        cleanup();
        fail(err);
        return;
    }
    log_info("Перегляд у грі з {}. Клавіші в грі: F9 — початок фрагмента, F11 — кінець, F6 — позначка. "
             "Закрийте гру, коли закінчите.", format_duration(from_tick_ * ti));

    const auto t_launch = Clock::now();
    Clock::time_point cancel_at{};
    bool cancel_sent = false, relaunched = false;
    size_t marks_seen = 0;
    std::string last_state;
    auto poll_marks = [&]() {
        const auto marks = game::read_marks(*g, id);
        if (marks.size() <= marks_seen) return;
        std::lock_guard lock(marks_mutex_);
        for (size_t i = marks_seen; i < marks.size(); ++i) {
            marks_.push_back(marks[i]);
            const char* what = marks[i].kind == "start" ? "початок фрагмента" : marks[i].kind == "end" ? "кінець фрагмента" : "позначка";
            log_info("У грі позначено {}: {} (тік {})", what, format_duration(marks[i].tick * ti), marks[i].tick);
        }
        marks_seen = marks.size();
    };
    for (;;) {
        if (kill_) {
            proc->terminate();
            break;
        }
        if (cancel_ && !cancel_sent) {
            cancel_sent = true;
            cancel_at = Clock::now();
            log_info("Закриваю гру...");
            game::request_cancel(*g, id);
        }
        if (cancel_sent && Clock::now() - cancel_at > std::chrono::seconds(15) && proc->running()) proc->terminate();
        if (!proc->running()) {
            // Гра могла перезапуститися через Steam
            if (!relaunched && !cancel_sent && Clock::now() - t_launch < std::chrono::seconds(90)) {
                auto pids = game::GameProcess::find_by_name(kGameProcessNames, true);
                if (!pids.empty()) {
                    if (auto p2 = game::GameProcess::attach(pids.front(), &err)) {
                        proc = std::move(p2);
                        relaunched = true;
                        continue;
                    }
                }
                if (Clock::now() - t_launch < std::chrono::seconds(15)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    continue;
                }
            }
            break;
        }
        if (auto st = game::read_status(*g, id)) {
            if (st->state != last_state) {
                last_state = st->state;
                if (st->state == "watching") log_info("Демо грає. Коли закінчите, просто закрийте гру.");
                else if (st->state == "done") log_info("Демо закінчилось — можна закривати гру");
                else if (st->state == "error") log_warn("Гра: {}", st->message);
            }
            update([&](Progress& p) {
                p.demo_tick = st->tick;
                p.demo_total = st->total;
                p.game_running = true;
                p.fraction = st->total > 0 ? std::clamp(static_cast<double>(st->tick) / st->total, 0.0, 1.0) : -1.0;
                p.stage = st->state == "watching" ? std::format("Перегляд у грі: {}", format_duration(st->tick * ti))
                          : st->state == "done"   ? std::string("Демо закінчилось — закрийте гру")
                                                  : std::string("Гра завантажує демо");
            });
        }
        poll_marks();
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    poll_marks();
    update([](Progress& p) { p.game_running = false; });
    game::GameProcess::wait_all_exited(kGameProcessNames, 15000);
    cleanup();
    succeed("Перегляд завершено");
}

EncodeFramesJob::EncodeFramesJob(RenderSettings s, fs::path frames_dir, std::string prefix, fs::path wav_path,
                                 std::shared_ptr<const demo::DemoAnalysis> analysis,
                                 std::shared_ptr<const voice::VoiceDecodeResult> voices)
    : s_(std::move(s)), dir_(std::move(frames_dir)), prefix_(std::move(prefix)), wav_(std::move(wav_path)),
      analysis_(std::move(analysis)), voices_(std::move(voices)) {}

void EncodeFramesJob::run() {
    KeepAwake keep_awake;
    std::string err;
    set_stage("Пошук кадрів", 0);
    int64_t count = 0;
    std::string prefix = prefix_;
    if (prefix.empty()) prefix = detect_frame_prefix(dir_, &count);
    std::error_code ec;
    fs::path wav = wav_;
    if (wav.empty()) {
        if (fs::exists(dir_ / path_from_utf8(prefix + ".wav"), ec)) wav = dir_ / path_from_utf8(prefix + ".wav");
        else
            for (fs::directory_iterator it(dir_, ec), end; !ec && it != end; it.increment(ec))
                if (ends_with_i(path_to_utf8(it->path().filename()), ".wav")) {
                    wav = it->path();
                    break;
                }
    }
    log_info("Кадри: {}{}####.*, звук: {}", path_to_utf8(dir_), prefix.empty() ? "" : "/" + prefix,
             wav.empty() ? "немає" : path_to_utf8(wav));
    if (s_.output_path.empty()) s_.output_path = path_to_utf8(dir_ / "video.mp4");   // шлях у UTF-8
    EncodeSettings es;
    if (!make_encode_settings(s_, es, &err)) {
        fail(err);
        return;
    }
    es.crash_safe = s_.crash_safe;
    frames::SequenceOptions so;
    so.dir = dir_;
    so.prefix = prefix;
    so.live = false;
    so.delete_after_read = false;
    int64_t total_files = 0;
    for (fs::directory_iterator it(dir_, ec), end; !ec && it != end; it.increment(ec))
        if (frames::FrameSequenceReader::parse_index(path_to_utf8(it->path().filename()), prefix, so.extensions) >= 0)
            ++total_files;
    const int64_t total_out = std::max<int64_t>(1, total_files / std::max(1, es.motion_blur_samples));
    const double fps_v = es.video.fps.num / static_cast<double>(es.video.fps.den);
    if (s_.target_size_mb > 0)
        es.video.bitrate = bitrate_for_target_size(s_.target_size_mb, total_out / fps_v, s_.audio ? es.audio.bitrate : 0);
    es.extras = make_extra_outputs(s_.extra_versions, es, total_out / fps_v);
    frames::FrameSequenceReader reader(so);
    ThreadPool pool(s_.threads > 0 ? static_cast<unsigned>(s_.threads) : 0);
    EncodeSession session(es, &pool, preview_.get());
    std::vector<const voice::SpeakerTrack*> speakers;
    if (voices_) speakers = select_speakers(s_, *voices_);
    std::vector<audio::VoiceCleanup> voice_fx;
    if (!speakers.empty() && needs_voice_cleanup(s_)) {
        set_stage("Аналіз голосу гравців");
        voice_fx = voice_cleanup_for(s_, speakers, cancel_);
    }

    set_stage("Кодування");
    auto last_progress = Clock::now();
    frames::Image img;
    for (;;) {
        if (cancel_) break;
        const auto w = reader.next_for(img, 100);
        if (w == frames::FrameSequenceReader::Wait::End) break;
        if (w != frames::FrameSequenceReader::Wait::Frame) continue;
        if (!session.started()) {
            AudioSourcesSpec spec;
            spec.game_audio = s_.game_audio && !wav.empty();
            spec.game_wav = wav;
            spec.game_wav_live = false;
            spec.game_offset = s_.game_audio_offset;
            spec.game_gain = static_cast<float>(s_.game_volume);
            spec.voices = speakers;
            spec.voice_gains = speaker_gains(s_, speakers);
            if (analysis_)
                spec.voice_origin_sample = static_cast<int64_t>(
                    std::llround(std::max(0, s_.start_tick) * static_cast<double>(analysis_->tick_interval) * media::kMixRate));
            spec.voice_delay = s_.voice_delay;
            spec.voice_gain = static_cast<float>(s_.voice_volume);
            spec.mic_file = s_.mic_file.empty() ? fs::path() : path_from_utf8(s_.mic_file);
            spec.mic_offset = s_.mic_offset;
            spec.mic_gain = static_cast<float>(s_.mic_volume);
            spec.voice_cleanup = voice_fx;
            spec.duck_game = s_.duck_game;
            spec.loudness_target = s_.loudness_target;
            if (!session.begin(img.width, img.height, spec, &err)) {
                fail("Не вдалося почати кодування: " + err);
                return;
            }
            session.game_audio_finished();   // файл WAV уже повний
            if (s_.subtitles_srt && !speakers.empty())
                write_speaker_subtitles(s_, speakers, spec.voice_origin_sample, total_out / fps_v);
            update([&](Progress& p) {
                p.video_desc = session.video_description();
                p.audio_desc = session.audio_description();
            });
        }
        if (!session.push_subframe(std::move(img), &err)) {
            session.abort();
            fail("Помилка кодування: " + err);
            return;
        }
        if (Clock::now() - last_progress > std::chrono::milliseconds(150)) {
            last_progress = Clock::now();
            const auto rs = reader.stats();
            const auto ps = session.stats();
            update([&](Progress& p) {
                p.frames = session.frames_encoded();
                p.subframes = session.subframes_in();
                p.video_seconds = session.video_seconds();
                p.expected_seconds = total_out / fps_v;
                p.fraction = std::clamp(static_cast<double>(session.frames_encoded()) / total_out, 0.0, 1.0);
                p.bytes_written = session.bytes_written();
                const double el = elapsed_seconds();
                p.speed_fps = el > 0 ? session.frames_encoded() / el : 0;
                p.eta = p.speed_fps > 0 ? (total_out - session.frames_encoded()) / p.speed_fps : -1;
                if (rs.frames > 0) {
                    p.stat_read_ms = rs.read_ms / static_cast<double>(rs.frames);
                    p.stat_decode_ms = rs.decode_ms / static_cast<double>(rs.frames);
                }
                p.stat_blend_ms = ps.blend_ms;
                p.stat_convert_ms = ps.convert_ms;
                p.stat_encode_ms = ps.encode_ms;
                p.stat_audio_ms = ps.audio_ms;
            });
        }
    }
    if (!session.started()) {
        fail("У папці не знайдено кадрів (TGA/JPG/PNG)");
        return;
    }
    set_stage("Завершення файлу");
    if (!session.finish(&err)) {
        fail("Не вдалося завершити файл: " + err);
        return;
    }
    const int64_t frames_done = session.frames_encoded();
    const double secs = session.video_seconds();
    const std::vector<std::string> extras_done = session.finished_extras();
    finalize_output(s_, es.crash_safe && is_mov_family(s_.output_path, s_.container), frames_done, secs);
    make_after_render(s_, extras_done);
    log_info("Готово! {} — {} кадрів, {}", s_.output_path, frames_done, format_duration(secs));
    succeed(s_.output_path);
}

} // namespace gmdr::render
