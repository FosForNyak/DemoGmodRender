#include "jobs.hpp"

#include "../frames/frame_pipe.hpp"
#include "../frames/sequence_reader.hpp"
#include "../game/audio_mute.hpp"
#include "../game/lua_driver.hpp"
#include "../game/process.hpp"
#include "../game/rtx.hpp"
#include "../media/muxer.hpp"
#include "../util/file_util.hpp"
#include "../util/http_download.hpp"
#include "../util/log.hpp"
#include "../util/power.hpp"
#include "../util/strings.hpp"
#include "../util/thread_pool.hpp"
#include "encode_session.hpp"
#include "frame_transport.hpp"
#include "markers.hpp"
#include "subtitles.hpp"
#include "dubbing.hpp"
#include "edit_package.hpp"
#include "versions.hpp"
#include "../util/i18n.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
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
            fail(std::string(tr("Непередбачена помилка: ")) + e.what());
        } catch (...) {
            fail(tr("Непередбачена помилка"));
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

void Job::report_progress(const std::string& stage, double fraction) {
    bool changed;
    {
        std::lock_guard lock(mutex_);
        changed = progress_.stage != stage;
        progress_.stage = stage;
        if (fraction >= 0) progress_.fraction = fraction;
    }
    if (changed) log_info("== {} ==", stage);
}

void Job::fail(const std::string& message) {
    {
        std::lock_guard lock(mutex_);
        error_ = message;
        progress_.stage = tr("Помилка");
    }
    log_error("{}", message);
    mark_ended();
    state_ = JobState::Failed;
}

void Job::succeed(const std::string& result) {
    {
        std::lock_guard lock(mutex_);
        result_ = result;
        progress_.stage = tr("Готово");
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
        log_info("{}", trf("Додатково: {} ({})", p, ec ? "?" : format_bytes(size)));
    }
}

bool make_encode_settings(const RenderSettings& s, EncodeSettings& es, std::string* error) {
    auto fps = parse_rational(s.fps);
    if (!fps || !fps->valid()) {
        if (error) *error = trf("неправильна частота кадрів '{}'", s.fps);
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
            log_info("{}", trf("Голос {}: {:.1f} LUFS, фон {:.0f} дБ, мова {:.0f} дБ{}", speakers[i]->display_name(),
                     c.profile.loudness, c.profile.noise_db, c.profile.speech_db,
                     s.voice_level ? trf(", підсилення {:+.1f} дБ", 20.0 * std::log10(c.level)) : std::string()));
        else
            log_info("{}", trf("Голос {}: замало мовлення для аналізу — без обробки", speakers[i]->display_name()));
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
// Повна копія кадру (кадри зазвичай лише переміщуються)
frames::Image copy_image(const frames::Image& s) {
    frames::Image d;
    d.allocate(s.width, s.height, s.layout);
    d.full_range = s.full_range;
    d.bt709 = s.bt709;
    d.index = s.index;
    for (int p = 0; p < s.planes(); ++p)
        for (int y = 0; y < s.plane_height(p); ++y) std::memcpy(d.row(p, y), s.row(p, y), s.row_bytes(p));
    return d;
}

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

// transcript — розпізнане мовлення: тоді в субтитрах текст розмов, а не лише імена
void write_speaker_subtitles(const RenderSettings& s, const std::vector<const voice::SpeakerTrack*>& speakers,
                             int64_t origin_sample, double duration, const speech::Transcript* transcript = nullptr) {
    if (s.output_path.find('%') != std::string::npos) return;   // послідовність зображень
    std::string srt;
    if (transcript) {
        std::vector<std::string> keys;
        for (const auto& src : subtitle_sources(s, speakers)) keys.push_back(src.track->key);
        srt = make_transcript_srt(transcript->lines, keys, static_cast<double>(origin_sample) / voice::kVoiceRate,
                                  duration, s.voice_delay, video_speed(s));
    } else {
        srt = make_speaker_srt(subtitle_sources(s, speakers), origin_sample, duration, s.voice_delay, video_speed(s));
    }
    fs::path path = path_from_utf8(s.output_path);
    path.replace_extension(".srt");
    std::string err;
    if (write_file_text(path, srt, &err))
        log_info("{}", trf("Субтитри {}: {}", transcript ? tr("з текстом розмов") : tr("«хто говорить»"), path_to_utf8(path)));
    else
        log_warn("{}", trf("Не вдалося записати субтитри: {}", err));
}

// Підписи «хто говорить» на кадрі; nullptr — нема кого показувати або немає шрифту
std::unique_ptr<SpeakerOverlay> make_overlay(const RenderSettings& s, const std::vector<const voice::SpeakerTrack*>& speakers,
                                             int64_t origin_sample, double duration, int frame_w, int frame_h) {
    const std::string font = SpeakerOverlay::find_font();
    if (font.empty()) {
        log_warn("{}", trf("Підписи «хто говорить»: у системі не знайдено шрифту з кирилицею — без підписів"));
        return nullptr;
    }
    auto ov = std::make_unique<SpeakerOverlay>();
    std::string err;
    if (!ov->init(SpeakerOverlay::speakers_for(subtitle_sources(s, speakers), origin_sample, duration, s.voice_delay,
                                               video_speed(s)),
                  frame_w, frame_h, font, &err)) {
        log_info("{}", trf("Підписи «хто говорить»: {}", err));
        return nullptr;
    }
    log_info("{}", trf("Підписи «хто говорить» на кадрі: {} гравц(ів)", ov->label_count()));
    return ov;
}

void write_chat_subtitles(const RenderSettings& s, const demo::DemoAnalysis& a, int32_t start_tick, double duration) {
    if (s.output_path.find('%') != std::string::npos || a.tick_interval <= 0) return;
    const double vti = a.tick_interval / video_speed(s);   // тривалість тіку у відео
    const int32_t end_tick = start_tick + static_cast<int32_t>(std::ceil(duration / vti));
    const std::string srt = make_chat_srt(a.events, start_tick, end_tick, vti, duration);
    if (srt.empty()) {
        log_info("{}", trf("Субтитри чату: у фрагменті немає повідомлень"));
        return;
    }
    fs::path path = path_from_utf8(s.output_path);
    path.replace_extension(s.subtitles_srt ? ".chat.srt" : ".srt");
    std::string err;
    if (write_file_text(path, srt, &err)) log_info("{}", trf("Субтитри чату: {}", path_to_utf8(path)));
    else log_warn("{}", trf("Не вдалося записати субтитри чату: {}", err));
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
        log_warn("{}", trf("Розділи не записано: формат {} їх не підтримує (потрібен MP4, MOV або MKV)",
                 path_to_utf8(path_from_utf8(s.output_path).extension())));
    if ((fragmented && s.faststart) || with_chapters) {
        if (fragmented && s.faststart) log_info("{}", trf("Переупаковую у звичайний MP4 (без перекодування)..."));
        else log_info("{}", trf("Додаю розділи у файл (без перекодування)..."));
        std::string err;
        const bool faststart = s.faststart && is_mov_family(s.output_path, s.container);
        if (!media::remux_file(s.output_path, faststart, &err, with_chapters ? &marks : nullptr))
            log_warn("{}", trf("Не вдалося переупакувати ({}), файл лишився як був — він теж відтворюється", err));
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
        log_warn("{}", trf("Не вдалося перевірити готовий файл: {}", err));
        return;
    }
    std::string problems;
    if (!info.has_video) problems += tr(" у файлі немає відео;");
    else if (seconds > 1 && std::abs(info.video_seconds - seconds) > std::max(0.5, seconds * 0.01))
        problems += trf(" тривалість відео {} замість {};", format_duration(info.video_seconds), format_duration(seconds));
    if (info.video_frames > 0 && frames > 0 && std::llabs(info.video_frames - frames) > 1)
        problems += trf(" кадрів {} замість {};", info.video_frames, frames);
    if (s.audio && info.audio_streams > 0 && info.video_seconds > 1 && info.audio_seconds + 0.5 < info.video_seconds)
        problems += trf(" звук коротший за відео ({} проти {});", format_duration(info.audio_seconds),
                                format_duration(info.video_seconds));
    if (with_chapters && info.chapters != static_cast<int>(marks.size()))
        problems += trf(" розділів {} замість {};", info.chapters, marks.size());
    if (problems.empty())
        log_info("{}", trf("Перевірка файлу: відео {}{}{}, усе гаразд", format_duration(info.video_seconds),
                 info.audio_streams > 0 ? trf(", звук {} ({} дор.)", format_duration(info.audio_seconds), info.audio_streams) : "",
                 info.chapters > 0 ? trf(", розділів {}", info.chapters) : ""));
    else
        log_warn("{}", trf("Перевірка файлу:{}", problems));
}

} // namespace

namespace {
// Коротша частина не варта ще одного запуску гри (для автотестів: GMDR_TEST_MIN_PART=секунди)
double min_part_seconds() {
    const char* e = std::getenv("GMDR_TEST_MIN_PART");
    return e ? std::max(0.5, std::atof(e)) : 20.0;
}

// Паралельний рендер: частини склеюються пакетами, тож контейнер має зберігати їх як є
bool parallel_container_ok(const RenderSettings& s) {
    if (s.output_path.find('%') != std::string::npos) return false;   // послідовність зображень
    std::string c = to_lower(s.container);
    if (c.empty()) {
        c = to_lower(path_to_utf8(path_from_utf8(s.output_path).extension()));
        if (!c.empty() && c[0] == '.') c = c.substr(1);
    }
    return c == "mp4" || c == "mov" || c == "m4v" || c == "mkv" || c == "matroska" || c == "webm";
}

// Проєкт для Premiere / DaVinci Resolve: відео, окремі WAV і позначки на одній шкалі
void write_edit_project(const RenderSettings& s, const EncodeSettings& es, int64_t frames,
                        const std::vector<EncodeSession::StemFile>& stems, const std::vector<Chapter>& markers) {
    EditProject pr;
    const fs::path out = fs::absolute(path_from_utf8(s.output_path));
    pr.name = path_to_utf8(out.stem());
    pr.video_path = path_to_utf8(out);
    pr.width = es.video.width > 0 ? es.video.width : s.width;
    pr.height = es.video.height > 0 ? es.video.height : s.height;
    pr.fps_num = es.video.fps.num;
    pr.fps_den = es.video.fps.den;
    pr.frames = frames;
    pr.video_has_audio = es.audio_enabled;
    for (const auto& st : stems) pr.stems.push_back({st.title, path_to_utf8(fs::absolute(path_from_utf8(st.path)))});
    pr.markers = markers;
    const fs::path xml = path_from_utf8(es.stems_dir) / path_from_utf8(pr.name + ".xml");
    std::string werr;
    if (write_file_text(xml, make_fcp7_xml(pr), &werr))
        log_info("{}", trf("Пакет для монтажу: {} (відкрийте в Premiere Pro або DaVinci Resolve: File → Import)", path_to_utf8(xml)));
    else
        log_warn("{}", trf("Не вдалося записати проєкт для монтажу: {}", werr));
}

// Перемістити файл (між дисками — копією)
bool move_file(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    if (fs::equivalent(from, to, ec)) return true;   // уже на місці (копія поверх себе з видаленням знищила б файл)
    fs::rename(from, to, ec);
    if (!ec) return true;
    if (!copy_file_overwrite(from, to, nullptr)) return false;
    fs::remove(from, ec);
    return true;
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
        log_info("{}", trf("Демо вже в папці гри — відтворюю на місці: {}", *rel));
        return *rel;
    }
    const fs::path dst = tmp_dir / "demo.dem";
    std::error_code lec;
    fs::create_hard_link(path_from_utf8(demo_path), dst, lec);
    if (lec) {
        const uint64_t sz = file_size_or_zero(path_from_utf8(demo_path));
        if (sz > (64ull << 20)) log_info("{}", trf("Копіюю демо ({}) у папку гри...", format_bytes(sz)));
        std::string copy_err;
        if (!copy_file_overwrite(path_from_utf8(demo_path), dst, &copy_err)) {
            if (error) *error = tr("Не вдалося скопіювати демо в папку гри: ") + copy_err;
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
    if (code == -1) return tr("невідомий");
    if (code >= 0 && code < 0x10000) return std::to_string(code);
    std::string s = std::format("0x{:08X}", u);
    switch (u) {
        case 0xC0000135u: s += tr(" — не знайдено потрібну DLL; перевірте цілісність файлів гри в Steam"); break;
        case 0xC000007Bu: s += tr(" — пошкоджений або не тієї розрядності exe/DLL; спробуйте іншу версію гри (32/64 біт)"); break;
        case 0xC0000142u: s += tr(" — не вдалося ініціалізувати DLL"); break;
        case 0xC0000005u: s += tr(" — гра аварійно завершилась (access violation)"); break;
        case 0xC0000409u: s += tr(" — гра аварійно завершилась"); break;
        case 0xC000013Au: s += tr(" — гру закрито"); break;
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
    // Звук гри рендерів, урваних збоєм програми чи ПК, лежить тут — переносимо його до їхніх тек
    // частин (дописування візьме його звідти), решту тимчасових файлів можна прибирати
    for (auto r : pending_resumes()) {
        bool changed = false;
        for (size_t j = 0; j < r.wavs.size(); ++j) {
            const fs::path src = path_from_utf8(r.wavs[j].first);
            if (!path_is_inside(src, tmp) || !fs::exists(src, ec)) continue;
            const fs::path dst = parts_dir_for(r.settings.output_path) / path_from_utf8(std::format("part0_{}.wav", j + 1));
            fs::create_directories(dst.parent_path(), ec);
            fs::remove(dst, ec);
            if (move_file(src, dst)) {
                r.wavs[j].first = path_to_utf8(dst);
                changed = true;
            }
        }
        if (changed) save_resume(r);
    }
    for (fs::directory_iterator it(tmp, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_directory()) continue;
        const fs::path bak = it->path() / "config.cfg.bak";
        if (fs::exists(bak, ec)) {
            log_warn("{}", trf("Знайдено залишки перерваного рендеру — відновлюю config.cfg гри"));
            game::restore_config(g, bak);
        }
        if (fs::exists(it->path() / "rtx.conf.bak", ec)) {
            log_warn("{}", trf("Знайдено залишки перерваного RTX-рендеру — повертаю rtx.conf"));
            game::restore_rtx_profile(g, it->path() / "rtx.conf.bak");
        }
        const std::string id = path_to_utf8(it->path().filename());
        game::remove_job_files(g, id);
        fs::remove_all(it->path(), ec);
    }
    fs::remove(tmp, ec);
    fs::remove(g.garrysmod / "data" / "gmdr" / "job.txt", ec);   // гра не запущена — нічиє завдання вже не потрібне
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
    set_stage(tr("Аналіз демо"), 0);
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
    log_info("{}", trf("Демо: карта {}, сервер «{}», записав «{}», тривалість {} ({} тіків, {:.1f} тік/с)", a->header.map_name,
             a->header.server_name, a->header.client_name, format_duration(a->duration_seconds), a->last_tick,
             1.0 / a->tick_interval));
    for (const auto& w : a->warnings) log_warn("{}", w);
    set_stage(tr("Розбір голосу"), 0.6);
    auto v = std::make_shared<voice::VoiceDecodeResult>(voice::decode_voice(
        *a, {}, [&](double f) { update([&](Progress& p) { p.fraction = 0.6 + f * 0.4; }); }, &cancel_));
    for (const auto& w : v->warnings) log_warn("{}", w);
    for (const auto& sp : v->speakers)
        log_info("{}", trf("Голос: {}{} — {:.1f} с мовлення", sp.display_name(), sp.is_local ? tr(" [це ви]") : "", sp.seconds));
    if (v->speakers.empty()) log_info("{}", trf("Голосу в демо не знайдено"));
    else if (std::none_of(v->speakers.begin(), v->speakers.end(), [](const auto& s) { return s.is_local; }))
        log_info("{}", trf("Вашого власного голосу в демо немає: сервер не пересилає гравцю його ж голос. "
                 "Щоб він записувався, перед записом демо введіть у консолі voice_loopback 1, "
                 "або додайте запис мікрофона окремим файлом."));
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
        fail(tr("Спершу відкрийте демо"));
        return;
    }
    const demo::DemoAnalysis& A = *analysis_;
    RenderSettings sel = s_;
    sel.audio = true;
    if (sel.voice_mode == "none") sel.voice_mode = "all";   // кнопка "зберегти голоси" — значить, усі
    const auto all_speakers = select_speakers(sel, *voices_);
    if (all_speakers.empty()) {
        fail(tr("У демо немає голосів для збереження"));
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
        fail(tr("Порожній відрізок часу"));
        return;
    }
    // Лише ті, хто говорив у цьому відрізку (інакше файли були б суцільною тишею)
    std::vector<const voice::SpeakerTrack*> speakers;
    for (const auto* sp : all_speakers) {
        const bool spoke = std::any_of(sp->segments.begin(), sp->segments.end(),
                                       [&](const voice::VoiceSegment& g) { return g.start < to && g.end() > from; });
        if (spoke) speakers.push_back(sp);
        else log_info("{}", trf("{} не говорив у цьому відрізку — пропускаю", sp->display_name()));
    }
    if (speakers.empty()) {
        fail(tr("У вибраному відрізку демо ніхто не говорив"));
        return;
    }
    const uint64_t wav_bytes = static_cast<uint64_t>(to - from) * 2u * speakers.size();
    const bool flac = wav_bytes > (2ull << 30);
    const char* ext = flac ? ".flac" : ".wav";
    if (flac)
        log_info("{}", trf("Голоси будуть збережені у FLAC (без втрат): у WAV вони зайняли б {}", format_bytes(wav_bytes)));

    std::error_code ec;
    fs::create_directories(dir_, ec);
    // Та сама обробка голосу, що й у відео (вирівнювання гучності, шумодав)
    std::vector<audio::VoiceCleanup> voice_fx;
    if (needs_voice_cleanup(s_)) {
        set_stage(tr("Аналіз голосу гравців"));
        voice_fx = voice_cleanup_for(s_, speakers, cancel_);
    }
    set_stage(tr("Збереження голосів"), 0);
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
                log_info("{}", trf("Збережено: {}", path_to_utf8(file)));
            } else if (!cancel_) {
                log_error("{}", trf("Не вдалося зберегти {}: {}", path_to_utf8(file), err));
            }
            std::lock_guard lock(pm);
            part[i] = 1.0;
        }));
    }
    for (auto& f : futures) f.wait();
    if (cancel_) return;   // стан "скасовано" виставить Job::start
    if (ok_count == 0) {
        fail(tr("Не вдалося зберегти жодного голосу"));
        return;
    }
    log_info("{}", trf("Збережено голосів: {} у {}. Кожен файл починається з {} демо.", ok_count.load(), path_to_utf8(dir_),
             format_duration(static_cast<double>(from) / voice::kVoiceRate)));
    succeed(path_to_utf8(dir_));
}

// ============================ Розпізнавання мовлення ===================================
std::optional<speech::Transcript> ensure_transcript(const RenderSettings& s,
                                                    const std::vector<const voice::SpeakerTrack*>& speakers,
                                                    double from, double to, const speech::Progress& progress,
                                                    const std::atomic<bool>* cancel, std::string* error) {
    speech::Transcript stored = speech::load_transcript(s.demo_path).value_or(speech::Transcript{});
    std::vector<std::pair<const voice::SpeakerTrack*, std::string>> need;
    for (const auto& src : subtitle_sources(s, speakers)) {
        const double end = to >= 0 ? to : static_cast<double>(src.track->end_sample()) / voice::kVoiceRate;
        if (!speech::covers(stored, src.track->key, std::max(0.0, from), end)) need.push_back({src.track, src.name});
    }
    if (need.empty()) return stored;
    std::string why;
    const auto tools = speech::find_whisper(s.whisper_cli, s.whisper_model, &why);
    if (!tools) {
        if (error) *error = why;
        return std::nullopt;
    }
    log_info("{}", trf("Розпізнавання мовлення: {} гравц(ів), модель {}, мова {}", need.size(), path_to_utf8(tools->model.filename()),
             s.whisper_language.empty() ? "auto" : s.whisper_language));
    speech::Options opt;
    opt.language = s.whisper_language.empty() ? "auto" : s.whisper_language;
    opt.from = std::max(0.0, from);
    opt.to = to;
    const fs::path work = app_data_dir() / "whisper_tmp" / make_unique_id();
    auto fresh = speech::transcribe(need, *tools, opt, work, progress, cancel, error);
    std::error_code ec;
    fs::remove_all(work, ec);
    if (fs::is_empty(work.parent_path(), ec)) fs::remove(work.parent_path(), ec);
    if (!fresh) return std::nullopt;
    speech::merge_transcript(stored, *fresh);
    std::string serr;
    if (!speech::save_transcript(s.demo_path, stored, &serr)) log_warn("{}", trf("Не вдалося зберегти розшифровку: {}", serr));
    return stored;
}

TranscribeJob::TranscribeJob(RenderSettings s, std::shared_ptr<const demo::DemoAnalysis> analysis,
                             std::shared_ptr<const voice::VoiceDecodeResult> voices, bool range_only)
    : s_(std::move(s)), analysis_(std::move(analysis)), voices_(std::move(voices)), range_only_(range_only) {}

std::optional<speech::Transcript> TranscribeJob::transcript() const {
    std::lock_guard lock(tr_mutex_);
    return transcript_;
}

void TranscribeJob::run() {
    if (!analysis_ || !voices_) {
        fail(tr("Спершу відкрийте демо"));
        return;
    }
    RenderSettings sel = s_;
    sel.audio = true;
    if (sel.voice_mode == "none") sel.voice_mode = "all";
    const auto speakers = select_speakers(sel, *voices_);
    if (speakers.empty()) {
        fail(tr("У демо немає голосів гравців"));
        return;
    }
    const double ti = analysis_->tick_interval;
    const double from = range_only_ && s_.start_tick > 0 ? s_.start_tick * ti : 0.0;
    const double to = range_only_ && s_.end_tick > 0 ? s_.end_tick * ti : -1.0;
    set_stage(tr("Розпізнавання мовлення"), 0);
    std::string err;
    auto ts = ensure_transcript(sel, speakers, from, to, [&](double f, const std::string& what) { set_stage(what, f); },
                                &cancel_, &err);
    if (cancel_) {
        update([](Progress& p) { p.stage = tr("Скасовано"); });
        return;
    }
    if (!ts) {
        fail(tr("Не вдалося розпізнати мовлення: ") + err);
        return;
    }
    {
        std::lock_guard lock(tr_mutex_);
        transcript_ = ts;
    }
    log_info("{}", trf("Розшифровка: {} реплік — {}", ts->lines.size(), path_to_utf8(speech::transcript_path(s_.demo_path))));
    collect_voice_samples(sel, speakers, *ts);
    succeed(path_to_utf8(speech::transcript_path(s_.demo_path)));
}

DownloadJob::DownloadJob(std::string url, fs::path dest, std::string what)
    : url_(std::move(url)), dest_(std::move(dest)), what_(std::move(what)) {}

void DownloadJob::run() {
    set_stage(tr("Завантаження: ") + what_, 0);
    log_info("{}", trf("Завантажую {} з {}", what_, url_));
    std::string err;
    auto last = Clock::now() - std::chrono::seconds(1);
    const bool ok = download_file(
        url_, dest_,
        [&](uint64_t done, uint64_t total) {
            if (Clock::now() - last < std::chrono::milliseconds(200)) return;
            last = Clock::now();
            set_stage(trf("Завантаження: {} — {} з {}", what_, format_bytes(done),
                                  total > 0 ? format_bytes(total) : std::string("?")),
                      total > 0 ? static_cast<double>(done) / static_cast<double>(total) : 0.0);
        },
        &cancel_, &err);
    if (cancel_) {
        update([](Progress& p) { p.stage = tr("Скасовано"); });
        return;
    }
    if (!ok) {
        fail(tr("Не вдалося завантажити ") + what_ + ": " + err);
        return;
    }
    log_info("{}", trf("Завантажено: {} ({})", path_to_utf8(dest_), format_bytes(file_size_or_zero(dest_))));
    succeed(path_to_utf8(dest_));
}

// =============================== RenderJob ========================================
namespace {
// Кроки, які показуються в тестовому прогоні (і в журналі звичайного рендеру)
enum CheckIndex { kCheckLaunch, kCheckDriver, kCheckDemo, kCheckFrames, kCheckAudio, kCheckEncode, kCheckRtx, kCheckCount };
const char* const kCheckNames[kCheckCount] = {"Гру запущено", "Драйвер у меню GMod відповідає",
                                              "Демо завантажилось і грає", "Кадри надходять",
                                              "Звук гри записується", "Кодування працює", "RTX у кадрі"};
const char* const kCheckHints[kCheckCount] = {
    "Перевірте папку і версію гри на сторінці «Гра».",
    "Натисніть «Інструменти → Встановити драйвер у GMod» (GMod міг оновити menu.lua) і переконайтеся, що Steam запущено.",
    "Перевірте, що демо відкривається в самій грі (playdemo) і що у вас є карта й аддони з сервера.",
    "Гра не записує кадри: перевірте місце на диску з грою; спробуйте режим вікна «позаду інших» на сторінці «Гра».",
    "Гра не пише звук: вимкніть і ввімкніть звук у налаштуваннях GMod або вимкніть «Звук гри», щоб рендерити без нього.",
    "Кодек не працює з цими налаштуваннями: спробуйте інший кодек або формат файлу.",
    "Кадр чорний: Remix не малює, коли вікно гри за межами екрана. Виберіть на сторінці «Гра» вікно «Позаду інших вікон» "
    "або «На екрані» і перевірте, що RTX-копія гри запускається з RTXLauncher."};
} // namespace

RenderJob::RenderJob(RenderSettings s, std::shared_ptr<const demo::DemoAnalysis> analysis,
                     std::shared_ptr<const voice::VoiceDecodeResult> voices, bool test_run,
                     std::shared_ptr<GameHandoff> handoff, bool keep_game)
    : s_(std::move(s)), analysis_(std::move(analysis)), voices_(std::move(voices)), test_run_(test_run),
      handoff_(std::move(handoff)), keep_game_(keep_game && handoff_ != nullptr) {
    update([](Progress& p) {
        p.checks.clear();
        for (int i = 0; i < kCheckCount; ++i) p.checks.push_back({tr(kCheckNames[i]), CheckItem::Pending, {}});
    });
    if (!s_.rtx) set_check(kCheckRtx, CheckItem::Skipped, tr("без RTX"));
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

std::optional<game::GModInstall> locate_game(const RenderSettings& s, std::vector<std::string>* log) {
    if (!s.rtx) {
        if (!s.game_dir.empty()) return game::gmod_from_dir(path_from_utf8(s.game_dir));
        return game::detect_gmod(log);
    }
    if (!s.rtx_game_dir.empty()) return game::gmod_from_dir(path_from_utf8(s.rtx_game_dir));
    if (!s.game_dir.empty())
        if (auto g = game::gmod_from_dir(path_from_utf8(s.game_dir)); g && g->valid() && game::is_rtx_install(*g)) return g;
    return game::detect_rtx_install(log);
}

namespace {
const std::vector<std::string> kGameProcessNames = {"gmod.exe", "hl2.exe", "gmod", "hl2_linux"};

// Папка гри за налаштуваннями (locate_game) і exe саме з неї. Також: відновлення після збою,
// перевірка, що гра не запущена, і встановлення драйвера.
std::optional<game::GModInstall> resolve_game(RenderSettings& s, bool need_driver, std::string* error) {
    std::vector<std::string> log;
    std::optional<game::GModInstall> g = locate_game(s, &log);
    for (const auto& l : log) log_debug("{}", l);
    if (s.rtx && !g) {
        if (error) *error = tr("Не знайдено копію GMod RTX від RTXLauncher. Вкажіть її папку на сторінці «Гра».");
        return std::nullopt;
    }
    if (!g || !g->valid()) {
        if (error) *error = tr("Не знайдено Garry's Mod. Вкажіть папку гри (…\\steamapps\\common\\GarrysMod) у налаштуваннях.");
        return std::nullopt;
    }
    // Вибраний exe — з іншої копії GMod (наприклад, звичайної, а рендеримо RTX): береться типовий.
    // Exe поза будь-якою копією гри (імітатор гри в тестах) лишається як є.
    if (!s.game_exe.empty()) {
        std::error_code ec;
        fs::path dir = path_from_utf8(s.game_exe).parent_path();
        for (int up = 0; up < 3 && !dir.empty(); ++up, dir = dir.parent_path()) {
            if (!fs::exists(dir / "garrysmod" / "gameinfo.txt", ec) && !fs::exists(dir / "garrysmod" / "lua", ec)) continue;
            if (to_lower(path_to_utf8(dir.lexically_normal())) != to_lower(path_to_utf8(g->root.lexically_normal()))) {
                log_info("{}", trf("Вибраний exe гри не з цієї копії — беру типовий: {}", path_to_utf8(g->default_exe())));
                s.game_exe.clear();
            }
            break;
        }
    }
    log_info("Garry's Mod: {}", path_to_utf8(g->root));
    recover_leftovers(*g);
    if (!game::GameProcess::find_by_name(kGameProcessNames).empty()) {
        if (error) *error = tr("Garry's Mod уже запущено. Закрийте гру — програма запустить її сама з потрібними параметрами.");
        return std::nullopt;
    }
    if (need_driver && game::driver_state(*g) != game::DriverState::Installed) {
        log_info("{}", trf("Встановлюю драйвер рендеру в меню GMod (один рядок у lua/menu/menu.lua)..."));
        if (!game::install_driver(*g, error)) return std::nullopt;
    }
    return g;
}
} // namespace

bool RenderJob::prepare(std::string* error) {
    // Копія гри паралельного рендеру: гру знайшов і драйвер встановив основний рендер
    gmod_ = part_ ? part_->gmod : resolve_game(s_, !s_.manual_mode, error);
    if (!gmod_) return false;

    id_ = make_unique_id();
    tmp_dir_ = gmod_->garrysmod / "gmdr_tmp" / id_;
    std::error_code ec;
    fs::create_directories(tmp_dir_, ec);
    if (ec) {
        if (error) *error = tr("Не вдалося створити тимчасову папку в папці гри: ") + ec.message();
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
            log_warn("{}", trf("Увімкнено RTX, але в папці гри немає rtx.conf чи rtx-remix — це точно копія від RTXLauncher?"));
        // Налаштування Remix для офлайн-рендеру; оригінал повернеться після рендеру (і після збою)
        rtx_backup_ = backup_dir / "rtx.conf.bak";
        std::string rerr;
        if (fs::exists(rtx_backup_, ec))
            log_info("{}", trf("rtx.conf уже налаштовано для рендеру попереднім пунктом черги"));
        else if (game::apply_rtx_render_profile(*gmod_, rtx_backup_, &rerr)) {
            log_info("{}", trf("rtx.conf: на час рендеру — повна роздільна здатність (DLAA), без генерації кадрів і заставки"));
            log_info("{}", trf("RTX: шейдери Remix компілюються до кадру, а не у фоні — кадр чекає на них, а не виходить "
                               "чорним (перша компіляція без кешу може тривати кілька хвилин)"));
        }
        else
            log_warn("{}", trf("Не вдалося змінити rtx.conf ({}) — Remix рендеритиме з вашими налаштуваннями", rerr));
    }
    const uint64_t free_space = free_disk_space(tmp_dir_);
    log_info("{}", trf("Вільно на диску з грою: {}", format_bytes(free_space)));
    if (free_space > 0 && free_space < (3ull << 30))
        log_warn("{}", trf("На диску з грою мало місця — зменште «Черга кадрів на диску» або звільніть місце"));

    if (part_ && !part_->demo_for_game.empty()) {
        demo_for_game_ = part_->demo_for_game;
    } else {
        const auto demo_for_game = demo_path_for_game(*gmod_, s_.demo_path, tmp_dir_, id_, error);
        if (!demo_for_game) return false;
        demo_for_game_ = *demo_for_game;
    }

    // Налаштування, які ми змінимо, — щоб потім повернути
    const std::vector<std::string> touched = {"host_framerate", "snd_fixed_rate", "fps_max", "mat_vsync",
                                              "snd_mute_losefocus", "net_graph",
                                              "cl_showfps", "voice_scale", "cl_drawhud",
                                              "r_drawviewmodel", "sv_cheats"};
    config_originals_ = game::read_config_values(*gmod_, touched);
    // Як кадри йдуть з гри: каналом (без файлів на диску) чи файлами
    game_exe_ = s_.game_exe.empty() ? gmod_->default_exe() : path_from_utf8(s_.game_exe);
    const TransportChoice transport = choose_frame_transport(s_, game_exe_);
    pipe_transport_ = transport.pipe;
    pipe_strict_ = transport.strict;
    if (!transport.pipe && !transport.note.empty())
        log_info("{}", trf("Кадри йтимуть файлами на диску: {}", transport.note));
    // Копія гри: config.cfg зберіг основний рендер, а завдання пишеться при запуску (по черзі з іншими копіями)
    if (part_) return true;
    config_backup_ = backup_dir / "config.cfg.bak";
    if (!fs::exists(config_backup_, ec)) game::backup_config(*gmod_, config_backup_);
    return write_game_job(std::max(0, s_.start_tick), error);
}

bool RenderJob::write_game_job(int32_t start_tick, std::string* error) {
    auto fps = parse_rational(s_.fps);
    game::DriverJob job;
    job.id = id_;
    job.demo = demo_for_game_;
    const bool jpeg = s_.capture_format == "jpg" || s_.capture_format == "jpeg";
    if (jpeg)
        job.movie_flags = {"jpeg", "jpeg_quality", std::to_string(std::clamp(s_.jpeg_quality, 1, 100)), "wav"};
    else
        job.movie_flags = {"raw"};
    // Канал для кадрів (і звуку) цього запуску гри: створюється до того, як гра дістане завдання,
    // і має ту саму назву, що й файли, — лише не в папці, а серед каналів ОС
    const std::string dir_for_game = "gmdr_tmp/" + id_;
    pipe_reader_.reset();
    if (pipe_transport_) {
        frames::PipeOptions po;
        po.dir = tmp_dir_;
        po.prefix = movie_prefix();
        po.ext = jpeg ? ".jpg" : ".tga";
        po.decode_threads = s_.threads > 0 ? std::max(1, s_.threads / 2) : 0;
        auto reader = std::make_unique<frames::FramePipeReader>(po);
        if (reader->ok()) {
            pipe_reader_ = std::move(reader);
        } else if (pipe_strict_) {
            if (error) *error = tr("Не вдалося створити канал для кадрів: ") + reader->last_error();
            return false;
        } else {
            log_warn("{}", trf("Не вдалося створити канал для кадрів ({}) — кадри йтимуть файлами на диску", reader->last_error()));
            pipe_transport_ = false;
        }
    }
    job.movie = pipe_reader_ ? frames::pipe_movie_name(dir_for_game, movie_prefix()) : dir_for_game + "/" + movie_prefix();
    // Кадр відео = 1/FPS секунди демо, помножене на швидкість (уповільнення — менший крок)
    job.host_framerate = fps->value() * std::clamp(s_.motion_blur, 1, 256) / video_speed(s_);
    job.start_tick = start_tick;
    job.end_tick = s_.end_tick;
    // Далекий фрагмент: швидко перемотуємо до точки за кілька секунд до нього (останні
    // секунди гра програє звичайно, щоб сутності й освітлення встигли оновитися).
    // RTX: денойзеру потрібна історія кадрів — розгін довший.
    if (analysis_ && analysis_->tick_interval > 0) {
        const auto ticks = [&](double sec) { return static_cast<int32_t>(std::llround(sec / analysis_->tick_interval)); };
        const int32_t seek = job.start_tick - ticks(s_.rtx ? 10.0 : 5.0);
        if (seek > ticks(30.0)) job.seek_tick = seek;
    }
    // RTX: перший кадр з трасуванням чекає, поки Remix скомпілює шейдери (без кешу — хвилини)
    if (s_.rtx) job.load_timeout = 1800;
    job.quit_when_done = keep_game_ ? false : s_.quit_game_when_done;
    job.wait_next = keep_game_;
    job.menu_delay = s_.menu_delay;
    job.hide_hud = s_.hide_hud;
    job.hide_viewmodel = s_.hide_viewmodel;
    std::string extra = s_.extra_commands;
    if (s_.manual_mode) {
        extra += tr("\necho \"[GMDR] Ручний режим: gmdr_start — почати запис, gmdr_stop — зупинити\"");
    }
    const std::string cfg = game::make_job_cfg(job, s_.mute_engine_voice, extra, config_originals_);
    // У ручному режимі завдання для драйвера не пишемо — лише конфіг з аліасами
    if (!game::write_job_files(*gmod_, job, cfg, !s_.manual_mode, error)) return false;
    job_written_ = true;
    return true;
}

void RenderJob::cleanup(bool game_closing) {
    pipe_reader_.reset();   // канал, створений для запуску гри, що так і не відбувся
    if (!gmod_) return;
    if (job_written_) game::remove_job_files(*gmod_, id_);
    if (!stray_dir_.empty()) {
        // Кадри/звук, які гра записала не в нашу тимчасову папку
        std::error_code ec;
        for (fs::directory_iterator it(stray_dir_, ec), end; !ec && it != end; it.increment(ec))
            if (starts_with_i(path_to_utf8(it->path().filename()), base_prefix())) fs::remove(it->path(), ec);
    }
    const std::vector<std::string> game_names = {"gmod.exe", "hl2.exe", "gmod", "hl2_linux"};
    // Копія гри паралельного рендеру свою гру вже дочекалась, а інші копії можуть ще працювати;
    // config.cfg поверне основний рендер, коли закриються всі
    const bool game_running = part_          ? false
                              : game_closing ? !game::GameProcess::wait_all_exited(game_names, 15000)
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
        log_info("{}", trf("Тимчасові файли залишено: {}", path_to_utf8(tmp_dir_)));
    }
}

// Що run() уже підготував (аналіз голосу, розпізнане мовлення, налаштування кодування) для
// паралельного рендеру
struct RenderJob::ParallelInput {
    std::vector<PartPlan>                   parts;
    std::vector<const voice::SpeakerTrack*> speakers;
    std::vector<audio::VoiceCleanup>        voice_fx;
    const speech::Transcript*               transcript = nullptr;
    EncodeSettings                          es;
    int32_t                                 range_start = 0, range_end = 0;
    double                                  expected_seconds = 0;
    double                                  vti = 0;   // тривалість тіку у відео
    double                                  t0 = 0;    // час демо першого кадру відео
    std::optional<PartResult>               head;      // дописування: уже записаний початок (обрізаний до ключового кадру)
};

void RenderJob::run() {
    KeepAwake keep_awake;   // ПК не засне посеред рендеру
    if (part_) set_thread_log_prefix(trf("[частина {}] ", part_->plan.index + 1));
    // Запис для дописування після збою: звичайне завершення (готово, помилка, скасування) його
    // прибирає, лишається він лише після збою програми чи ПК. Дописування своє прибирає саме, коли вдасться.
    struct ResumeGuard {
        std::string id;
        ~ResumeGuard() { forget_resume(id); }
    } resume_guard;
    std::string err;
    // ---- 1. Аналіз ----
    if (!analysis_) {
        set_stage(tr("Аналіз демо"), 0);
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
        set_stage(tr("Розбір голосу"));
        voices_ = std::make_shared<voice::VoiceDecodeResult>(voice::decode_voice(A, {}, {}, &cancel_));
    }
    std::vector<const voice::SpeakerTrack*> speakers;
    if (voices_) speakers = select_speakers(s_, *voices_);
    std::vector<audio::VoiceCleanup> voice_fx;
    if (!part_ && !speakers.empty() && needs_voice_cleanup(s_)) {   // копії гри звук не міксують
        set_stage(tr("Аналіз голосу гравців"));
        voice_fx = voice_cleanup_for(s_, speakers, cancel_);
    }

    // Повний відрізок (для прогнозу в тестовому прогоні) і відрізок цього запуску
    const int32_t full_start = std::max(0, s_.start_tick);
    const int32_t full_end = s_.end_tick > 0 ? std::min(s_.end_tick, A.last_tick) : A.last_tick;
    const double vspeed = video_speed(s_);
    const double vti = A.tick_interval / vspeed;   // тривалість тіку у відео (з уповільненням — довша)
    if (std::abs(vspeed - 1.0) > 1e-6)
        log_info("{}", trf("Швидкість ×{:g}: {}", vspeed, vspeed < 1 ? tr("уповільнення") : tr("прискорення")));
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
        log_info("{}", trf("Тестовий прогін: {:.0f} с з початку фрагмента ({}), файл {}", kTestSeconds,
                 format_duration(full_start * static_cast<double>(A.tick_interval)), s_.output_path));
    }
    if (!s_.audio || !s_.game_audio) set_check(kCheckAudio, CheckItem::Skipped, tr("звук гри вимкнено"));
    if (s_.manual_mode) set_check(kCheckDriver, CheckItem::Skipped, tr("ручний режим"));

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
    const double expected_seconds = part_ ? part_->plan.frames * es.video.fps.den / static_cast<double>(es.video.fps.num)
                                          : std::max(0, range_end - range_start) * vti;
    if (s_.target_size_mb > 0 && expected_seconds > 0) {
        const double size_seconds = part_ ? part_->size_seconds : test_run_ ? full_seconds : expected_seconds;
        es.video.bitrate = bitrate_for_target_size(s_.target_size_mb, size_seconds, s_.audio ? es.audio.bitrate : 0);
        log_info("{}", trf("Цільовий розмір {:.0f} МБ на {}: бітрейт відео {:.2f} Мбіт/с", s_.target_size_mb,
                 format_duration(size_seconds), es.video.bitrate / 1e6));
    }
    // Додаткові версії (Discord, вертикальна...) — у тестовому прогоні не потрібні
    if (!test_run_) es.extras = make_extra_outputs(s_.extra_versions, es, part_ ? part_->size_seconds : expected_seconds);
    if (part_) {
        // Копія гри пише лише відео своєї частини (і частини додаткових версій); звук, субтитри
        // й решта — після склеювання
        es.audio_enabled = false;
        es.faststart = false;
        for (size_t i = 0; i < es.extras.size(); ++i) {
            const std::string ext = path_to_utf8(path_from_utf8(es.extras[i].output_path).extension());
            es.extras[i].output_path =
                path_to_utf8(part_->dir / path_from_utf8(std::format("part{}_v{}{}", part_->plan.index + 1, i + 1, ext)));
        }
    }
    // Пакет для монтажу: окремі WAV у теці "назва_монтаж" поруч із відео (проєкт XML — після рендеру)
    if (s_.edit_package && !test_run_ && s_.output_path.find('%') == std::string::npos) {
        const fs::path out = path_from_utf8(s_.output_path);
        es.stems_dir = path_to_utf8(out.parent_path() / path_from_utf8(path_to_utf8(out.stem()) + "_монтаж"));
    }
    if (test_run_ && !trim(s_.extra_versions).empty()) log_info("{}", trf("Тестовий прогін: додаткові версії не кодуються"));
    // Озвучення перекладу кладеться на звук гри без голосів: окремі WAV джерел — у тимчасову теку
    if (es.stems_dir.empty() && !test_run_ && !part_ && dub_needs_stems(s_) && s_.output_path.find('%') == std::string::npos)
        es.stems_dir = path_to_utf8(dub_work_dir(s_) / "stems");
    update([&](Progress& p) {
        p.range_start = range_start;
        p.range_end = range_end;
        p.demo_total = A.last_tick;
        p.expected_seconds = expected_seconds;
    });

    // Текст розмов у субтитрах і переклад: розпізнати мовлення фрагмента до запуску гри (якщо ще ні)
    std::optional<speech::Transcript> transcript;
    const bool want_transcript = (s_.speech_subtitles && s_.subtitles_srt) || translation_requested(s_);
    if (translation_requested(s_) && !test_run_ && speakers.empty())
        log_warn("{}", trf("Переклад пропущено: у відео немає голосів гравців"));
    if (want_transcript && !test_run_ && !part_ && !speakers.empty()) {
        set_stage(tr("Розпізнавання мовлення"), 0);
        transcript = ensure_transcript(s_, speakers, range_start * static_cast<double>(A.tick_interval) - 1,
                                       range_end * static_cast<double>(A.tick_interval) + 1,
                                       [&](double f, const std::string& what) { set_stage(tr("Розпізнавання мовлення: ") + what, f); },
                                       &cancel_, &err);
        if (cancel_) {
            update([](Progress& p) { p.stage = tr("Скасовано"); });
            return;
        }
        if (!transcript) {
            if (s_.speech_subtitles && s_.subtitles_srt)
                log_warn("{}", trf("Мовлення не розпізнано ({}) — у субтитрах будуть лише імена", err));
            if (translation_requested(s_)) log_warn("{}", trf("Переклад пропущено: мовлення не розпізнано ({})", err));
        }
    }

    // ---- Дописування рендеру, урваного збоєм програми чи ПК ----
    if (resume_) {
        ParallelInput in;
        const double ti0 = static_cast<double>(A.tick_interval);
        const double fps_v = es.video.fps.num / static_cast<double>(es.video.fps.den);
        const double frame_dt = vspeed / fps_v;
        const fs::path out = path_from_utf8(s_.output_path);
        const fs::path pdir = parts_dir_for(s_.output_path);
        const fs::path head = resume_head_path(s_.output_path);
        std::error_code ec;
        fs::create_directories(pdir, ec);
        // Частковий файл — у теку частин (якщо дописування вже починалось, він там)
        if (fs::exists(out, ec)) {
            fs::remove(head, ec);
            if (!move_file(out, head)) {
                fail(trf("Не вдалося перенести частково записане відео {} у {}", s_.output_path, path_to_utf8(head)));
                return;
            }
        }
        if (!fs::exists(head, ec)) {
            forget_resume(resume_->id);
            fail(trf("Не знайдено частково записаного відео {} — дописувати нема що", s_.output_path));
            return;
        }
        std::string kerr;
        const auto keys = media::keyframe_frames(path_to_utf8(head), es.video.fps, &kerr);
        // Обрізаємо на останньому ключовому кадрі (далі група кадрів могла не дописатись). Краще —
        // на ключовому, що припадає рівно на тік: тоді кадри дописаної частини лягають на ту саму
        // сітку без зсуву (якщо такий знайдеться не далі 10 с від останнього)
        int64_t keep = keys.empty() ? -1 : keys.back();
        if (const int64_t m = tick_aligned_period(ti0, frame_dt, 600); m > 0 && keep > 0) {
            for (auto it = keys.rbegin(); it != keys.rend() && *it >= keep - std::llround(fps_v * 10); ++it)
                if (*it > 0 && *it % m == 0) {
                    keep = *it;
                    break;
                }
        }
        in.t0 = resume_->video_t0;
        const int64_t total = std::llround((range_end * ti0 - in.t0) / frame_dt);
        if (keep > 0) {
            // WAV зірваного сеансу — до частин (стару тимчасову папку гри зараз буде прибрано)
            PartResult h;
            h.video = path_to_utf8(head);
            h.frames = keep;
            h.complete = true;
            for (size_t j = 0; j < resume_->wavs.size(); ++j) {
                const fs::path src = path_from_utf8(resume_->wavs[j].first);
                const fs::path dst = pdir / path_from_utf8(std::format("part0_{}.wav", j + 1));
                if (fs::exists(src, ec)) move_file(src, dst);   // уже перенесений recover_leftovers — на місці
                if (fs::exists(dst, ec)) h.wavs.push_back({dst, resume_->wavs[j].second});
            }
            if (h.wavs.empty() && s_.audio && s_.game_audio)
                log_warn("{}", trf("Звук гри зірваного рендеру не знайдено — у дописаному відео до {} звуку гри не буде",
                                   format_duration(static_cast<double>(keep) / fps_v)));
            in.head = h;
            log_info("{}", trf("Дописую урваний рендер: уже є {} ({} кадрів до останнього ключового), лишилось {}",
                               format_duration(static_cast<double>(keep) / fps_v), keep,
                               format_duration(static_cast<double>(std::max<int64_t>(0, total - keep)) / fps_v)));
        } else {
            log_warn("{}", trf("У частково записаному відео не знайдено цілих кадрів ({}) — рендерю фрагмент заново", kerr));
        }
        const bool par_ok = s_.parallel_games > 1 && !s_.rtx && !s_.manual_mode;
        in.parts = plan_parts_range(in.t0, std::max<int64_t>(0, keep), total, range_end, ti0, frame_dt,
                                    par_ok ? std::min(s_.parallel_games, 4) : 1, std::llround(fps_v * min_part_seconds()));
        in.speakers = speakers;
        in.voice_fx = voice_fx;
        in.transcript = transcript ? &*transcript : nullptr;
        in.es = es;
        in.range_start = range_start;
        in.range_end = range_end;
        in.expected_seconds = std::max(0.0, static_cast<double>(total)) / fps_v;
        in.vti = vti;
        run_parallel(in);
        return;
    }

    // ---- Паралельний рендер кількома копіями гри ----
    if (!part_ && s_.parallel_games > 1) {
        std::string why;
        if (test_run_) why = tr("тестовий прогін");
        else if (handoff_) why = tr("пункт черги");
        else if (s_.manual_mode) why = tr("ручний режим");
        else if (s_.rtx) why = tr("RTX");
        else if (!parallel_container_ok(s_)) why = tr("формат файлу — лише MP4, MOV, MKV або WebM");
        ParallelInput in;
        if (why.empty()) {
            const double fps_v = es.video.fps.num / static_cast<double>(es.video.fps.den);
            in.parts = plan_parts(range_start, range_end, static_cast<double>(A.tick_interval), vspeed / fps_v,
                                  std::min(s_.parallel_games, 4), std::llround(fps_v * min_part_seconds()));
            if (in.parts.size() < 2) why = trf("фрагмент коротший за {:.0f} с", 2 * min_part_seconds());
        }
        if (why.empty()) {
            in.speakers = speakers;
            in.voice_fx = voice_fx;
            in.transcript = transcript ? &*transcript : nullptr;
            in.es = es;
            in.range_start = range_start;
            in.range_end = range_end;
            in.expected_seconds = expected_seconds;
            in.vti = vti;
            in.t0 = range_start * static_cast<double>(A.tick_interval);
            run_parallel(in);
            return;
        }
        if (!test_run_) log_info("{}", trf("Паралельний рендер не використовується ({}) — рендерить одна копія гри", why));
    }

    // ---- 2. Підготовка гри ----
    set_stage(test_run_ ? tr("Тестовий прогін: підготовка гри") : tr("Підготовка гри"), 0);
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
    set_stage(tr("Запуск Garry's Mod"));
    game::WindowMode window_mode = s_.manual_mode ? game::WindowMode::Normal : game::window_mode_from_string(s_.game_window);
    if (s_.rtx && window_mode == game::WindowMode::Offscreen) {
        // Перевірено: за межами екрана Remix віддає чорні кадри, а на моніторі (навіть позаду вікон) — з RTX
        window_mode = game::WindowMode::Behind;
        log_info("{}", trf("RTX: вікно гри буде позаду інших вікон, а не за межами екрана (інакше Remix не малює)"));
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
    if (part_) args.push_back("-multirun");   // кілька копій гри одночасно
    // Запуск гри. Копія паралельного рендеру пише своє завдання лише під замком черги запусків
    // і чекає, поки драйвер її гри його забере, — інакше дві копії взяли б те саме job.txt.
    auto launch_game = [&](int32_t job_tick) -> std::unique_ptr<game::GameProcess> {
        std::unique_lock<std::mutex> gate;
        if (part_) {
            gate = std::unique_lock(part_->gate->m);
            if (!write_game_job(job_tick, &err)) return nullptr;
        }
        log_info("{}", trf("Команда запуску: {}", game::format_command_line(exe, args)));
        auto p = game::GameProcess::launch(exe, args, gmod_->root, {{"SteamAppId", "4000"}, {"SteamGameId", "4000"}}, &err,
                                           window_mode != game::WindowMode::Normal);
        if (p && part_) {
            const auto t0 = Clock::now();
            while (game::job_file_pending(*gmod_, id_) && p->running() && !kill_ && Clock::now() - t0 < std::chrono::minutes(3))
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return p;
    };
    if (handoff_ && handoff_->proc && handoff_->proc->running() && handoff_->launch_sig != launch_sig) {
        log_info("{}", trf("Цей пункт черги потребує інших параметрів запуску гри (розмір вікна, RTX...) — перезапускаю гру"));
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
        log_info("{}", trf("Гра вже запущена (PID {}) — наступний пункт черги без перезапуску", proc->pid()));
    } else {
        proc = launch_game(std::max(0, s_.start_tick));
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
    auto t_launch = Clock::now();
    if (s_.manual_mode)
        log_info("{}", trf("РУЧНИЙ РЕЖИМ: у грі увімкніть демо, перемотайте до потрібного місця і введіть у консолі gmdr_start "
                 "(зупинити — gmdr_stop). Програма кодуватиме кадри на льоту."));
    else if (window_mode == game::WindowMode::Offscreen)
        log_info("{}", trf("Гра запускається у фоні (вікно за межами екрана). Подивитися на неї — кнопка «Показати гру»."));
    else if (window_mode == game::WindowMode::Behind)
        log_info("{}", trf("Гра запускається позаду інших вікон. Не клацайте у вікні гри під час рендеру."));
    else
        log_info("{}", trf("Гра запускається... Не згортайте і не клацайте у вікні гри під час рендеру."));

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
                    log_info("{}", trf("Звук Garry's Mod вимкнено в мікшері Windows на час рендеру (у відео звук буде)"));
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
                log_info("{}", trf("Вікно гри показано"));
            } else {
                proc->place_window(effective_mode);
                log_info("{}", trf("Вікно гри знову приховано"));
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
    so.live = true;
    so.delete_after_read = !s_.keep_temp_files;
    so.decode_threads = s_.threads > 0 ? std::max(1, s_.threads / 2) : 0;
    // Кадри поточного запуску гри: канал, створений разом із його завданням (write_game_job), або
    // файли в папці dir
    uint64_t pipe_bytes = 0;   // скільки прийшло каналом за попередні запуски гри
    int64_t pipe_frames = 0;
    auto open_reader = [&](const fs::path& dir) -> std::unique_ptr<frames::FrameSource> {
        if (pipe_reader_) return std::move(pipe_reader_);
        so.dir = dir;
        so.prefix = movie_prefix();
        return std::make_unique<frames::FrameSequenceReader>(so);
    };
    std::unique_ptr<frames::FrameSource> reader_ptr = open_reader(tmp_dir_);
    frames::FrameSource* rp = reader_ptr.get();
    auto pipe_of = [&] { return dynamic_cast<frames::FramePipeReader*>(rp); };
    auto replace_reader = [&](std::unique_ptr<frames::FrameSource> next) {
        if (auto* pr = pipe_of()) {
            pipe_bytes += pr->bytes_received();
            pipe_frames += pr->delivered();
        }
        reader_ptr = std::move(next);
        rp = reader_ptr.get();
    };
    fs::path frames_dir = tmp_dir_;
    bool pipe_announced = false;
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

    auto fatal = [&](const std::string& msg, const std::string& own_hint = {}) {
        unmute_game();
        if (proc->suspended()) proc->resume();
        if (!s_.manual_mode) game::request_cancel(*gmod_, id_);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        if (proc->running()) proc->terminate();
        session.abort();
        rp->set_producer_done();
        replace_reader(nullptr);   // канал (і WAV, що він дописує) — закрити до прибирання тимчасової папки
        session_ptr.reset();
        cleanup();
        // Перший крок, що не пройшов, — позначаємо з підказкою
        std::string hint;
        update([&](Progress& p) {
            for (size_t i = 0; i < p.checks.size(); ++i)
                if (p.checks[i].state == CheckItem::Pending) {
                    p.checks[i].state = CheckItem::Failed;
                    p.checks[i].detail = msg.substr(0, msg.find('\n'));
                    hint = own_hint.empty() ? tr(kCheckHints[i]) : own_hint;
                    break;
                }
        });
        std::string full = msg;
        if (!hint.empty() && full.find(hint) == std::string::npos) full += tr("\n\nЩо робити: ") + hint;
        auto tail = game::console_log_tail(*gmod_, 15);
        if (!tail.empty()) {
            full += tr("\n\nОстанні рядки консолі гри:");
            for (const auto& l : tail) full += "\n  " + l;
        }
        fail(full);
    };

    // ---- Продовження після збою гри ----
    // Гра впала чи зависла посеред запису. Кодер живе в нашому процесі, тож гра перезапускається
    // з місця, де урвалися кадри, а кадри й звук дописуються в той самий файл — без склейки і
    // перекодування. Час кожного під-кадру відомий у часі демо, тож новий запуск вирівнюється
    // по ньому: зайві під-кадри на початку відкидаються.
    constexpr int kMaxRestarts = 3;
    const double ti = static_cast<double>(A.tick_interval);
    const double sub_dt = vspeed / (parse_rational(s_.fps)->value() * std::clamp(s_.motion_blur, 1, 256));   // с демо на під-кадр
    // Сторож: скільки чекати на кадри, перш ніж вважати гру завислою (RTX-кадр рендериться довше)
    const auto watchdog = std::chrono::seconds([&] {
        const char* e = std::getenv("GMDR_TEST_WATCHDOG");   // для автотестів
        return e ? std::max(1, std::atoi(e)) : s_.rtx ? 90 : 30;
    }());
    int restarts = 0;
    double cpu_sample = -1;            // процесорний час гри на момент останнього заміру (сторож)
    auto cpu_sample_t = Clock::now();
    bool game_busy = false, busy_logged = false;
    ResumeRecord resume_rec;           // запис для дописування після збою (id порожній — не пишемо)
    auto last_resume_save = Clock::now() - std::chrono::seconds(60);
    // Час демо першого кадру відео: тік, з якого гра почала запис, а в частині паралельного
    // рендеру — місце цієї частини в сітці кадрів усього фрагмента
    double video_t0 = range_start * ti;
    const double record_end_t = part_ ? part_->plan.first_time + static_cast<double>(part_->plan.frames) * sub_dt * es.motion_blur_samples
                                      : range_end * ti;
    const int64_t part_subframes = part_ ? part_->plan.frames * es.motion_blur_samples : INT64_MAX;
    int64_t pushed = 0;                // під-кадрів передано в кодер (з усіх запусків гри)
    bool resync = false;               // чекаємо перший кадр після перезапуску
    int32_t resume_tick = -1;
    int64_t drop_subframes = 0;        // зайві під-кадри на початку нового запуску
    double segment_video_start = 0;    // з якої секунди відео пише поточний запуск гри
    bool gave_up = false;              // гру не вдалося відновити — зберігаємо відрендерене
    auto next_demo_time = [&] { return video_t0 + static_cast<double>(pushed) * sub_dt; };
    auto can_restart = [&] {
        if (test_run_ || s_.manual_mode || cancel_ || kill_ || !session.started() || restarts >= kMaxRestarts) return false;
        return next_demo_time() < record_end_t - 2 * sub_dt;   // інакше фрагмент уже записано
    };
    auto restart_game = [&](const std::string& why) -> bool {
        ++restarts;
        if (proc->suspended()) proc->resume();
        if (proc->running()) proc->terminate();
        if (part_) proc->wait(15000);   // інші копії гри не чіпаємо
        else game::GameProcess::wait_all_exited(kGameProcessNames, 15000);
        // Дочитуємо цілі кадри, що встигли записатися (урваний останній читач відкине)
        rp->set_producer_done();
        for (const auto until = Clock::now() + std::chrono::seconds(30); Clock::now() < until;) {
            const auto w = rp->next_for(img, 200);
            if (w == frames::FrameSource::Wait::End) break;
            if (w != frames::FrameSource::Wait::Frame) continue;
            if (resync) continue;   // запуск, що впав до першого кадру: час його кадрів невідомий
            if (drop_subframes > 0) {
                --drop_subframes;
                continue;
            }
            if (!session.push_subframe(std::move(img), &err)) break;   // помилку кодера побачить основний цикл
            ++pushed;
        }
        resume_tick = std::max(range_start, static_cast<int32_t>(std::floor(next_demo_time() / ti)) - 3);
        log_warn("{}", trf("{} Перезапускаю гру і продовжую з {} відео (тік {}) — спроба {} з {}", why,
                 format_duration(session.video_seconds()), resume_tick, restarts, kMaxRestarts));
        ++segment_;
        if (!part_ && !write_game_job(resume_tick, &err)) {
            log_warn("{}", trf("Не вдалося записати завдання для гри: {}", err));
            return false;
        }
        auto p2 = launch_game(resume_tick);
        if (!p2) {
            log_warn("{}", trf("Не вдалося перезапустити гру: {}", err));
            return false;
        }
        replace_reader(open_reader(frames_dir));   // канал нового запуску створено разом із його завданням
        proc = std::move(p2);
        apply_process_tweaks(*proc);
        t_launch = Clock::now();
        last_frame_t = Clock::now();
        cpu_sample = -1;
        st.reset();
        recording_seen = false;
        recording_since = {};
        relocated = frames_dir != tmp_dir_;
        window_placed = false;
        attached_after_relaunch = false;
        resync = true;
        drop_subframes = 0;
        return true;
    };

    // ---- Канал не спрацював: кадри файлами ----
    // Рушій не пише в канал (ця збірка гри інакше обробляє назву для startmovie). Кодування ще не
    // почалось, тож гра просто запускається знову з того самого місця, а кадри йдуть файлами, як
    // раніше. Програма це запам'ятовує: наступні рендери з цією грою одразу пишуть файли.
    auto fall_back_to_files = [&](const std::string& why) -> bool {
        remember_pipe_failure(game_exe_, why);
        pipe_transport_ = false;
        log_warn("{}", trf("{} Перезапускаю гру — кадри йтимуть файлами на диску (з цією грою так буде й надалі).", why));
        if (proc->suspended()) proc->resume();
        if (proc->running()) proc->terminate();
        if (part_) proc->wait(15000);
        else game::GameProcess::wait_all_exited(kGameProcessNames, 15000);
        ++segment_;   // нові імена файлів: від каналу нічого не лишилось, але так надійніше
        const int32_t tick = std::max(0, s_.start_tick);
        if (!part_ && !write_game_job(tick, &err)) {
            log_warn("{}", trf("Не вдалося записати завдання для гри: {}", err));
            return false;
        }
        auto p2 = launch_game(tick);
        if (!p2) {
            log_warn("{}", trf("Не вдалося перезапустити гру: {}", err));
            return false;
        }
        frames_dir = tmp_dir_;
        replace_reader(open_reader(frames_dir));
        proc = std::move(p2);
        apply_process_tweaks(*proc);
        t_launch = Clock::now();
        last_frame_t = Clock::now();
        cpu_sample = -1;
        st.reset();
        producer_done = false;
        recording_seen = false;
        recording_since = {};
        relocated = false;
        window_placed = false;
        attached_after_relaunch = false;
        return true;
    };

    for (;;) {
        if (kill_) {
            log_warn("{}", trf("Рендер перервано — закриваю гру"));
            unmute_game();
            if (proc->running()) proc->terminate();
            session.abort();
            rp->set_producer_done();
            replace_reader(nullptr);
            session_ptr.reset();
            cleanup();
            update([](Progress& p) { p.stage = tr("Скасовано"); });
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
                log_info("{}", trf("Зупиняю запис (буде збережено вже відрендерене)..."));
                game::request_cancel(*gmod_, id_);
            }
        }

        // ---- Стан гри ----
        const auto now = Clock::now();
        if (now - last_poll > std::chrono::milliseconds(200)) {
            last_poll = now;
            if (!s_.manual_mode) {
                if (auto ns = game::read_status(*gmod_, id_)) {
                    if (!st || st->state != ns->state) log_info("{}", trf("Гра: стан «{}» {}", ns->state, ns->message));
                    st = ns;
                    set_check(kCheckDriver, CheckItem::Ok);
                }
                if (st) {
                    // "waiting" — черга: запис закінчено, гра чекає наступне завдання
                    const bool finished = st->state == "done" || st->state == "quit" || st->state == "waiting";
                    if (st->state == "recording" || st->state == "stopping" || finished) {
                        recording_seen = true;
                        set_check(kCheckDemo, CheckItem::Ok, trf("тік {}", st->start_tick >= 0 ? st->start_tick : st->tick));
                    }
                    if (finished) producer_done = true;
                    if (st->state == "error") {
                        std::string hint;
                        if (st->message.find("не запустилося") != std::string::npos ||
                            st->message.find("не завантажилось") != std::string::npos)
                            hint = trf("\nЯкщо демо записане на сервері, перевірте, що у вас є карта «{}» і "
                                               "потрібний контент (зайдіть на той сервер або підпишіться на карту в "
                                               "Workshop) і що демо відкривається в самій грі (playdemo).",
                                               A.header.map_name);
                        fatal(tr("Гра повідомила про помилку: ") + st->message + hint);
                        return;
                    }
                } else if (!proc->suspended() && now - t_launch > std::chrono::seconds(240)) {
                    fatal(tr("Драйвер у меню GMod не відповідає. Можливо, файл lua/menu/menu.lua було оновлено — "
                          "спробуйте «Встановити драйвер» ще раз."));
                    return;
                }
            }
            if (!proc->running()) {
                if (!part_ && !recording_seen && !attached_after_relaunch && now - t_launch < std::chrono::seconds(90)) {
                    // Гра могла перезапуститися через Steam — шукаємо новий процес (не дочірній CEF)
                    auto pids = game::GameProcess::find_by_name({"gmod.exe", "hl2.exe", "gmod", "hl2_linux"}, true);
                    if (!pids.empty()) {
                        if (auto p2 = game::GameProcess::attach(pids.front(), &err)) {
                            log_info("{}", trf("Гра перезапустилася (PID {}) — продовжую", pids.front()));
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
                // Гра закрилася, не дописавши фрагмент, — упала. Перезапускаємо з того самого місця.
                const bool game_finished = st && (st->state == "done" || st->state == "quit" || st->state == "waiting" ||
                                                  st->state == "stopping");
                if (!game_finished && can_restart() &&
                    restart_game(trf("Гра закрилася посеред запису (код {}).",
                                             describe_exit_code(proc->exit_code().value_or(-1)))))
                    continue;
                if (!recording_seen && rp->delivered() == 0 && !s_.manual_mode && !session.started()) {
                    fatal(trf("Гра закрилася до початку запису (код {}). Перевірте, що Steam запущено і демо "
                                      "відкривається в грі вручну.", describe_exit_code(proc->exit_code().value_or(-1))));
                    return;
                }
                if (!game_finished && session.started() && !cancel_ && !s_.manual_mode && !gave_up && pushed < part_subframes) {
                    gave_up = true;
                    log_warn("{}", trf("Гра закрилася посеред запису — зберігаю вже відрендерене"));
                }
                producer_done = true;
            }
            // Поки гру призупинено (кодер наздоганяє), її вікно не чіпаємо
            if (proc->running() && !proc->suspended())
                manage_window_and_sound(producer_done || (st && (st->state == "stopping" || st->state == "done")));
        }
        if (producer_done) {
            rp->set_producer_done();
            session.game_audio_finished();
        }

        // ---- Якщо гра пише кадри не туди, куди ми чекаємо, — шукаємо їх ----
        if (recording_seen && recording_since == Clock::time_point{}) recording_since = Clock::now();
        const auto recording_for = recording_since == Clock::time_point{} ? Clock::duration::zero() : Clock::now() - recording_since;
        // Канал: гра вже відрендерила кілька кадрів запису (лічильник драйвера), а в канал не прийшло
        // нічого. Сам час тут не ознака: перший кадр з RTX може рендеритись довго.
        const bool via_pipe = pipe_of() != nullptr;
        const bool frames_missing = via_pipe ? st && st->frames >= 5 && (recording_for > std::chrono::seconds(3) || producer_done)
                                             : recording_for > std::chrono::seconds(8) || producer_done;
        if (!relocated && recording_since != Clock::time_point{} && !rp->saw_any_file() && frames_missing) {
            relocated = true;
            const std::vector<fs::path> candidates = {gmod_->garrysmod, gmod_->garrysmod / "gmdr_tmp", gmod_->root};
            for (const auto& dir : candidates) {
                std::error_code ec;
                bool found = false;
                for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
                    if (starts_with_i(path_to_utf8(it->path().filename()), movie_prefix())) found = true;
                if (!found) continue;
                log_warn("{}", trf("Гра записує кадри в іншу папку ({}) — переключаюся на неї", path_to_utf8(dir)));
                if (via_pipe) {
                    // Рушій відкинув назву каналу і пише звичайні файли — з цією грою далі одразу файлами
                    remember_pipe_failure(game_exe_, tr("гра записала кадри файлами, а не в канал"));
                    pipe_transport_ = false;
                }
                frames_dir = dir;
                stray_dir_ = dir;
                so.dir = dir;
                so.prefix = movie_prefix();
                replace_reader(std::make_unique<frames::FrameSequenceReader>(so));
                break;
            }
            if (auto* pr = pipe_of(); pr && !cancel_ && !kill_) {
                // Що сказала гра: рядки консолі цього запуску про запис фільму (від останнього
                // «Started recording movie») і все, де є назва кадрів цього завдання. console.log
                // гра лише дописує, тож раніші рядки — з попередніх запусків, їх не показуємо.
                const auto lines = game::console_log_tail(*gmod_, 400);
                size_t from = lines.size();
                for (size_t i = lines.size(); i-- > 0;)
                    if (to_lower(lines[i]).find("started recording movie") != std::string::npos) {
                        from = i;
                        break;
                    }
                const std::string ours = to_lower(movie_prefix());
                for (size_t i = 0; i < lines.size(); ++i) {
                    const std::string low = to_lower(lines[i]);
                    if (low.find(ours) != std::string::npos ||
                        (i >= from && (low.find("movie") != std::string::npos || low.find("snapshot") != std::string::npos)))
                        log_warn("{}", trf("Консоль гри: {}", lines[i]));
                }
                // Чи дійшла гра до каналу взагалі: якщо ні — рушій не зміг відкрити "файл" за такою назвою
                const std::string why = pr->frame_opens() == 0
                    ? tr("Гра не записала в канал жодного кадру: рушій жодного разу не відкрив канал за назвою, яку дала програма.")
                    : trf("Гра не записала в канал жодного кадру (відкривала канал {} раз, але нічого не записала).", pr->frame_opens());
                if (!pipe_strict_ && fall_back_to_files(why)) continue;
                fatal(why + (pipe_strict_ ? tr(" Вибрано лише канал (--frame-transport pipe), тож на файли рендер не переходить.") : ""),
                      pipe_strict_ ? tr("З цією грою канал не працює — запустіть рендер без --frame-transport pipe: тоді кадри підуть файлами.")
                                   : std::string());
                return;
            }
            if (!rp->saw_any_file() && frames_dir == tmp_dir_)
                log_warn("{}", trf("Гра вже записує, але кадрів ще немає. Якщо так і лишиться — перевірте консоль гри (garrysmod/console.log)"));
        }

        // ---- Кадри ----
        if (rp->skipped() > 60 && rp->skipped() > rp->delivered()) {
            fatal(tr("Не вдалося прочитати кадри, які записує гра: ") + rp->last_error());
            return;
        }
        // "Чекали на гру": у черзі нічого готового (останній файл на диску ще дописується),
        // а next_for() чекає, поки гра запише наступний кадр
        const bool starving = recording_seen && !producer_done && !proc->suspended() && rp->pending_files() <= 1;
        const auto wait_t0 = Clock::now();
        const auto w = rp->next_for(img, 40);
        if (starving) wait_ms += std::chrono::duration<double, std::milli>(Clock::now() - wait_t0).count();
        if (w == frames::FrameSource::Wait::End) break;
        if (w == frames::FrameSource::Wait::Frame) {
            recording_seen = true;
            last_frame_t = Clock::now();
            if (auto* pr = pipe_of(); pr && !pipe_announced) {
                // Звук рушій пише так само, як кадри (на Windows — теж у канал): WAV створюється разом
                // із початком запису, а далі дописується з кожним кадром. Поки кодування не почалось,
                // гра встигає записати ще з десяток кадрів (їх приймає канал) — а з ними і звук.
                if (s_.audio && s_.game_audio && !pr->audio_flowing()) {
                    for (const auto t0 = Clock::now(); !pr->audio_flowing() && Clock::now() - t0 < std::chrono::seconds(3);)
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    if (!pr->audio_flowing() && !pipe_strict_ && !session.started()) {
                        img.release();
                        if (fall_back_to_files(tr("Кадри йдуть каналом, а звук гри — ні."))) continue;
                        fatal(tr("Не вдалося перезапустити гру, щоб писати кадри файлами."));
                        return;
                    }
                }
                pipe_announced = true;
                if (pipe_strict_) forget_pipe_failure(game_exe_);
                log_info("{}", trf("Кадри йдуть з гри напряму в програму (канал), без файлів на диску"));
                update([](Progress& p) { p.frames_via_pipe = true; });
            }
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
                    else log_warn("{}", trf("Не вдалося дізнатися тік початку запису — голос може бути трохи зсунутий"));
                }
                log_info("{}", trf("Запис почався: тік {}, кадри гри {}x{}", first_tick, img.width, img.height));
                video_start_tick = first_tick;
                set_check(kCheckDemo, CheckItem::Ok, trf("тік {}", first_tick));
                set_check(kCheckFrames, CheckItem::Ok, std::format("{}×{}, {}{}", img.width, img.height,
                                                                   frames::is_yuv(img.layout) ? "JPEG" : "TGA",
                                                                   pipe_of() ? tr(", каналом") : ""));
                first_frame_t = Clock::now();
                if (img.width != rw || img.height != rh)
                    log_warn("{}", trf("Гра рендерить {}x{} замість {}x{} (обмеження монітора?) — кадри буде масштабовано до {}x{}",
                             img.width, img.height, rw, rh, s_.width, s_.height));
                AudioSourcesSpec spec;
                spec.game_audio = s_.game_audio;
                spec.game_wav = frames_dir / path_from_utf8(movie_prefix() + ".wav");
                spec.game_wav_live = true;
                spec.game_offset = s_.game_audio_offset;
                spec.game_gain = static_cast<float>(s_.game_volume);
                spec.voices = speakers;
                spec.voice_gains = speaker_gains(s_, speakers);
                video_t0 = part_ ? part_->plan.first_time : first_tick * ti;
                spec.voice_origin_sample = static_cast<int64_t>(std::llround(video_t0 * media::kMixRate));
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
                    fatal(tr("Не вдалося почати кодування: ") + err);
                    return;
                }
                // Запис для дописування після збою: лише коли частковий файл переживе збій
                // (MKV/WebM, або MP4/MOV фрагментами); з RTX дописування ще не перевірене
                if (!test_run_ && !s_.manual_mode && !s_.rtx && !part_ && parallel_container_ok(s_) &&
                    (!is_mov_family(s_.output_path, s_.container) || es.crash_safe)) {
                    resume_rec.id = id_;
                    resume_rec.settings = s_;
                    resume_rec.video_t0 = video_t0;
                    resume_rec.seconds = expected_seconds;
                    resume_rec.wavs = {{path_to_utf8(spec.game_wav), first_tick * ti}};
                    resume_guard.id = id_;
                }
                if (s_.subtitles_srt && !speakers.empty()) {
                    // Субтитри пишемо одразу: відрізок і голоси вже відомі
                    write_speaker_subtitles(s_, speakers, spec.voice_origin_sample, expected_seconds,
                                            s_.speech_subtitles && transcript ? &*transcript : nullptr);
                }
                if (s_.speaker_overlay && !speakers.empty())
                    session.set_overlay(make_overlay(s_, speakers, spec.voice_origin_sample, expected_seconds, img.width,
                                                     img.height));
                if (part_) {
                    // Частина паралельного рендеру: гра почала на цілому тіку, а перший кадр частини —
                    // трохи далі (зайві під-кадри відкидаються) або, якщо гра почала пізніше, повторюється
                    std::lock_guard lock(part_mutex_);
                    part_result_.wavs.push_back({frames_dir / path_from_utf8(movie_prefix() + ".wav"), first_tick * ti});
                    const int64_t shift = std::llround((video_t0 - first_tick * ti) / sub_dt);
                    drop_subframes = std::max<int64_t>(0, shift);
                    if (shift < 0) {
                        log_warn("{}", trf("Гра почала на {} під-кадр(ів) пізніше за початок частини — повторюю перший кадр", -shift));
                        for (int64_t i = 0; i < -shift && session.push_subframe(copy_image(img), &err); ++i) ++pushed;
                    }
                }
                set_stage(test_run_ ? tr("Тестовий прогін: рендер") : tr("Рендер"));
                update([&](Progress& p) {
                    p.video_desc = session.video_description();
                    p.audio_desc = session.audio_description();
                });
            } else if (resync) {
                // Перший кадр після перезапуску гри: з якого тіку вона почала цього разу
                resync = false;
                const auto t0 = Clock::now();
                while ((!st || st->start_tick < 0) && Clock::now() - t0 < std::chrono::seconds(5)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    if (auto ns = game::read_status(*gmod_, id_)) st = ns;
                }
                int32_t seg_tick = resume_tick;
                if (st && st->start_tick >= 0) seg_tick = st->start_tick;
                else log_warn("{}", trf("Не вдалося дізнатися тік, з якого гра знову почала запис, — беру {}", resume_tick));
                const double seg_t = seg_tick * ti;   // час демо першого кадру цього запуску
                const int64_t shift = std::llround((next_demo_time() - seg_t) / sub_dt);
                drop_subframes = std::max<int64_t>(0, shift);
                if (shift < 0) {
                    // Гра почала пізніше, ніж треба: повторюємо перший кадр, щоб звук і голоси не зсунулись
                    log_warn("{}", trf("Після перезапуску гра почала на {} під-кадр(ів) пізніше — повторюю перший кадр", -shift));
                    for (int64_t i = 0; i < -shift && session.push_subframe(copy_image(img), &err); ++i) ++pushed;
                }
                session.game_audio_new_segment(frames_dir / path_from_utf8(movie_prefix() + ".wav"), seg_t - video_t0);
                if (!resume_rec.id.empty()) {
                    resume_rec.wavs.push_back({path_to_utf8(frames_dir / path_from_utf8(movie_prefix() + ".wav")), seg_t});
                    save_resume(resume_rec);
                }
                if (part_) {
                    std::lock_guard lock(part_mutex_);
                    part_result_.wavs.push_back({frames_dir / path_from_utf8(movie_prefix() + ".wav"), seg_t});
                }
                segment_video_start = session.video_seconds();
                log_info("{}", trf("Гра знову записує з тіку {} — відео продовжується з {} без шва{}", seg_tick,
                         format_duration(segment_video_start),
                         drop_subframes > 0 ? trf(" (зайвих під-кадрів на початку: {})", drop_subframes) : ""));
            }
            if (drop_subframes > 0) {
                --drop_subframes;
                img.release();
            } else if (pushed >= part_subframes) {
                img.release();   // частину вже записано — це кадри наступної частини
            } else if (!session.push_subframe(std::move(img), &err)) {
                fatal(tr("Помилка кодування: ") + err);
                return;
            } else if (++pushed == part_subframes) {
                log_info("{}", trf("Частину записано ({} кадрів) — зупиняю цю копію гри", part_->plan.frames));
                game::request_cancel(*gmod_, id_);
                if (proc->suspended()) proc->resume();
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
                        set_check(kCheckRtx, CheckItem::Failed, trf("кадр на {:.0f}% чорний", share * 100));
                        log_warn("{}", trf("RTX: кадр майже чорний ({:.0f}%) — схоже, Remix не малює. {}", share * 100, tr(kCheckHints[kCheckRtx])));
                    } else {
                        set_check(kCheckRtx, CheckItem::Ok);
                    }
                }
            }
            if (s_.game_audio && s_.audio && !part_) {   // копія гри звук не міксує — WAV забере склеювання
                if (session.game_audio_opened()) set_check(kCheckAudio, CheckItem::Ok);
                else if (!wav_warned && session.video_seconds() - segment_video_start > std::min(5.0, kTestSeconds * 0.8)) {
                    wav_warned = true;
                    log_warn("{}", trf("Гра не записує звук (немає WAV) — звук гри буде тишею"));
                    set_check(kCheckAudio, CheckItem::Failed, tr("гра не створила WAV"));
                    session.game_audio_finished();
                }
            }
        } else {
            if (session.started() && !session.pump_audio(&err)) {
                fatal(tr("Помилка кодування звуку: ") + err);
                return;
            }
        }

        // ---- Сторож: гра перестала віддавати кадри ----
        if (proc->suspended() || !recording_seen || producer_done) last_frame_t = Clock::now();
        const auto silent = Clock::now() - last_frame_t;
        // Кадрів немає, але гра витрачає процесорний час — вона зайнята, а не зависла: з RTX Remix
        // компілює шейдери (кадр чекає на них), або кадр просто дуже важкий. Сторож тоді чекає — до межі.
        if (Clock::now() - cpu_sample_t > std::chrono::seconds(2)) {
            const double c = proc->suspended() ? -1 : proc->cpu_seconds();
            const double dt = std::chrono::duration<double>(Clock::now() - cpu_sample_t).count();
            game_busy = cpu_sample >= 0 && c >= 0 && (c - cpu_sample) / dt >= 0.25;   // ≥ чверті ядра
            cpu_sample = c;
            cpu_sample_t = Clock::now();
        }
        const bool busy_wait = game_busy && silent < (s_.rtx ? std::chrono::minutes(20) : std::chrono::minutes(5));
        if (busy_wait && silent > std::chrono::seconds(15) && !busy_logged) {
            busy_logged = true;
            log_info("{}", trf("Гра вже {} с не віддає кадрів, але працює{} — чекаю",
                               std::chrono::duration_cast<std::chrono::seconds>(silent).count(),
                               s_.rtx ? tr(" (Remix компілює шейдери?)") : ""));
        }
        if (silent < std::chrono::seconds(2)) busy_logged = false;   // кадри пішли знову
        if (!window_fallback && effective_mode == game::WindowMode::Offscreen && silent > std::chrono::seconds(20)) {
            window_fallback = true;
            effective_mode = game::WindowMode::Behind;
            last_frame_t = Clock::now();
            log_warn("{}", trf("Гра за межами екрана не віддає кадрів — повертаю вікно на екран (позаду інших вікон). "
                     "Якщо так буде щоразу, виберіть на сторінці «Гра» режим «позаду інших вікон»."));
            proc->show_window_front();
            proc->place_window(game::WindowMode::Behind);
        } else if (busy_wait) {
            // зайнята — не чіпаємо
        } else if (silent > watchdog && can_restart()) {
            if (restart_game(trf("Гра вже {} с не віддає нових кадрів (зависла?).",
                                         std::chrono::duration_cast<std::chrono::seconds>(silent).count())))
                continue;
            log_warn("{}", trf("Зберігаю вже відрендерене"));
            gave_up = true;
            if (proc->running()) proc->terminate();
            producer_done = true;
        } else if (silent > std::chrono::seconds(120)) {
            if (session.started() && !test_run_ && !s_.manual_mode) {
                log_warn("{}", trf("Гра вже 2 хвилини не віддає нових кадрів (зависла?) — закриваю її і зберігаю вже відрендерене"));
                gave_up = true;
                if (proc->running()) proc->terminate();
                producer_done = true;
            } else {
                fatal(tr("Гра вже 2 хвилини не віддає нових кадрів (зависла?). Спробуйте ще раз або зменште роздільну "
                      "здатність."));
                return;
            }
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
                    log_warn("{}", trf("Закінчується місце на диску (вільно: з грою {}, для відео {}) — гру призупинено. "
                             "Звільніть місце, і рендер продовжиться сам, або натисніть «Зупинити».",
                             format_bytes(free_tmp), format_bytes(free_out)));
                }
            } else if (disk_low && now_ok) {
                disk_low = false;
                log_info("{}", trf("Місця на диску знову достатньо — продовжую"));
            }
        }

        // ---- Зворотний тиск: не даємо кадрам заполонити диск ----
        // (каналом кадри на диск не йдуть: гра сама чекає на записі, поки програма не звільнить пам'ять)
        const int64_t pending = rp->pending_files();
        const bool queue_high = !pipe_of() && pending > std::max(8, s_.max_pending_frames);
        const bool queue_low = pending <= std::max(2, s_.max_pending_frames / 3);
        if (!proc->suspended() && proc->running() && (queue_high || (disk_low && !cancel_))) {
            if (proc->suspend()) log_debug("Гра на паузі: {}", disk_low ? "мало місця на диску" : trf("кодер не встигає ({} кадрів у черзі)", pending));
        } else if (proc->suspended() && ((queue_low && !disk_low) || cancel_)) {
            proc->resume();
        }

        // Запис для дописування — раз на 3 с (скільки кадрів уже є)
        if (!resume_rec.id.empty() && session.started() && Clock::now() - last_resume_save > std::chrono::seconds(3)) {
            last_resume_save = Clock::now();
            resume_rec.frames = session.frames_encoded();
            save_resume(resume_rec);
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
                    if (!session.started() && st->state == "loading") p.stage = tr("Завантаження демо в грі");
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
                p.game_restarts = restarts;
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
    // Канал закривається раніше, ніж WAV звуку гри, який він дописував, знадобиться далі
    const int64_t skipped_frames = rp->skipped();
    replace_reader(nullptr);
    if (proc->suspended()) proc->resume();
    unmute_game();
    set_stage(tr("Завершення файлу"));
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
            update([](Progress& p) { p.stage = tr("Скасовано"); });
            return;
        }
        set_check(kCheckFrames, CheckItem::Failed, tr("жодного кадру"));
        fail(std::string(tr("Гра не записала жодного кадру. Перевірте журнал і консоль гри (garrysmod/console.log).\n\nЩо робити: ")) +
             tr(kCheckHints[kCheckFrames]));
        return;
    }
    const auto encode_end_t = Clock::now();
    if (!session.finish(&err)) {
        if (proc->running()) proc->terminate();
        session_ptr.reset();
        cleanup();
        fail(tr("Не вдалося завершити файл: ") + err);
        return;
    }
    const bool hand_over = keep_game_ && proc->running();   // черга: гра чекає наступне завдання
    if (!hand_over && s_.quit_game_when_done && proc->running()) {
        log_info("{}", trf("Чекаю, поки гра закриється..."));
        if (!proc->wait(30000)) {
            log_warn("{}", trf("Гра не закрилась сама — закриваю примусово"));
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
    if (part_) {
        // Копія гри: звук гри — з тимчасової папки до частин (її зараз буде прибрано), решту
        // (звук, субтитри, розділи) зробить склеювання
        PartResult r;
        {
            std::lock_guard lock(part_mutex_);
            r.wavs = part_result_.wavs;
        }
        r.frames = frames_done;
        // Остання частина кінчається там, де й фрагмент, — гра може не дописати кількох під-кадрів
        // останнього кадру (як і в звичайному рендері); частина ціла, якщо є всі її кадри
        r.complete = pushed >= part_subframes || frames_done >= part_->plan.frames;
        r.video = s_.output_path;
        for (const auto& x : es.extras)
            r.extras.push_back(std::find(extras_done.begin(), extras_done.end(), x.output_path) != extras_done.end() ? x.output_path : "");
        for (size_t i = 0; i < r.wavs.size(); ++i) {
            const fs::path dst = part_->dir / path_from_utf8(std::format("part{}_{}.wav", part_->plan.index + 1, i + 1));
            if (move_file(r.wavs[i].first, dst)) r.wavs[i].first = dst;
            else log_warn("{}", trf("Звук гри цієї частини не знайдено ({})", path_to_utf8(r.wavs[i].first)));
        }
        std::erase_if(r.wavs, [&](const auto& w) {
            std::error_code wec;
            return !fs::exists(w.first, wec);
        });
        {
            std::lock_guard lock(part_mutex_);
            part_result_ = r;
        }
        cleanup(false);
        log_info("{}", trf("Частину готово: {} кадрів, {}", frames_done, format_duration(secs)));
        succeed(s_.output_path);
        return;
    }
    if (hand_over) {
        handoff_->proc = std::move(proc);
        handoff_->waiting_id = id_;
    }
    cleanup(!hand_over && s_.quit_game_when_done);
    if (skipped_frames > 0) log_warn("{}", trf("Пропущено кадрів: {}", skipped_frames));
    if (pipe_frames > 0)
        log_info("{}", trf("Каналом прийнято {} кадрів гри ({}) — жоден кадр не записувався на диск", pipe_frames,
                           format_bytes(pipe_bytes)));
    const auto chapters =
        s_.chapters ? chapters_for_range(parse_markers(s_.markers), video_start_tick,
                                         video_start_tick + static_cast<int32_t>(std::llround(secs / vti)), vti)
                    : std::vector<Chapter>{};
    finalize_output(s_, fragmented, frames_done, secs, chapters);
    if (!test_run_) make_after_render(s_, extras_done);
    if (s_.edit_package && !es.stems_dir.empty())
        write_edit_project(s_, es, frames_done, stems,
                           chapters_for_range(parse_markers(s_.markers), video_start_tick,
                                              video_start_tick + static_cast<int32_t>(std::llround(secs / vti)), vti));
    if (s_.chat_srt && frames_done > 0) write_chat_subtitles(s_, A, video_start_tick, secs);
    if (frames_done > 0) translate_after_render(stems, speakers, transcript ? &*transcript : nullptr, video_t0, secs, es.audio);
    if (s_.rtx) {
        // Звірка з журналом Remix: чи прийняв він налаштування для рендеру
        const auto eff = game::read_remix_effective_options(*gmod_);
        for (const auto& [k, v] : game::rtx_render_profile_values()) {
            auto it = eff.find(k);
            if (it == eff.end()) continue;   // Remix пише лише значення, відмінні від типових
            if (it->second != v)
                log_warn("{}", trf("Remix: {} = {} замість {} — налаштування користувача в меню Remix мають вищий пріоритет", k,
                         it->second, v));
            else
                log_debug("Remix прийняв {} = {}", k, v);
        }
    }
    log_info("{}", trf("Готово! {} — {} кадрів, {}, {}", s_.output_path, frames_done, format_duration(secs),
             format_bytes(file_size_or_zero(path_from_utf8(s_.output_path)))));
    if (cancel_) log_info("{}", trf("Запис зупинено достроково — збережено відрендерену частину"));
    if (gave_up)
        log_warn("{}", trf("Гра {}впала — збережено відрендерене до {} ({:.0f}% фрагмента). Решту можна дорендерити окремо: "
                 "початок фрагмента — тік {}", restarts > 0 ? trf("після {} перезапуск(ів) знову ", restarts) : "",
                 format_duration(secs), expected_seconds > 0 ? std::min(100.0, secs / expected_seconds * 100) : 0.0,
                 video_start_tick + static_cast<int32_t>(std::llround(secs / vti))));
    else if (restarts > 0)
        log_info("{}", trf("Гру перезапускали після збою: {} раз(и) — відео продовжено з того самого місця", restarts));

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
        std::string head = all_ok ? tr("Усе працює.") : tr("Є проблеми — дивіться кроки нижче.");
        rep = head + "\n\n" + rep;
        rep += trf("\nШвидкість: {:.1f} кадр/с відео", fps_speed);
        if (es.motion_blur_samples > 1) rep += trf(" ({:.0f} під-кадрів гри за секунду)", fps_speed * es.motion_blur_samples);
        if (prog.stat_game_wait >= 0) rep += trf("; програма чекала на гру {:.0f}% часу", prog.stat_game_wait * 100);
        rep += trf("\nЕтапи на кадр: читання {:.1f} мс, декодування {:.1f} мс, колір {:.1f} мс, кодування {:.1f} мс",
                           prog.stat_read_ms, prog.stat_decode_ms, pstats.convert_ms, pstats.encode_ms);
        if (fps_speed > 0 && full_seconds > 0) {
            const double est_s = full_frames / fps_speed;
            const double bytes_done = static_cast<double>(file_size_or_zero(path_from_utf8(s_.output_path)));
            const double est_bytes = secs > 0 ? bytes_done / secs * full_seconds : 0;
            rep += trf("\n\nВесь вибраний фрагмент ({}): рендер ≈ {}, файл ≈ {}", format_duration(full_seconds),
                               format_duration(est_s), format_bytes(static_cast<uint64_t>(est_bytes)));
            if (est_s > 6 * 3600) rep += tr("\nЦе дуже довго — розгляньте менший фрагмент, нижчу роздільну здатність або GPU-кодек.");
        }
        rep += tr("\n\nТестове відео: ") + s_.output_path;
        set_report(rep);
        log_info("{}", trf("Тестовий прогін: {}", head));
    }
    succeed(s_.output_path);
}

// ======================= Переклад і озвучення після рендеру =========================
void RenderJob::translate_after_render(const std::vector<EncodeSession::StemFile>& stems,
                                       const std::vector<const voice::SpeakerTrack*>& speakers,
                                       const speech::Transcript* transcript, double t0, double seconds,
                                       const media::AudioEncoderSettings& audio) {
    // Тимчасові окремі WAV для озвучення прибираються за будь-якого результату
    struct TempGuard {
        const RenderSettings& s;
        ~TempGuard() {
            std::error_code ec;
            if (!s.keep_temp_files) fs::remove_all(dub_work_dir(s), ec);
        }
    } temp_guard{s_};
    if (test_run_ || part_ || seconds <= 0) return;
    if (transcript) collect_voice_samples(s_, speakers, *transcript);
    if (!translation_requested(s_) || !transcript || cancel_) return;
    DubSource src;
    src.lines = transcript->lines;
    for (const auto& x : subtitle_sources(s_, speakers)) src.keys.push_back(x.track->key);
    src.speakers = speakers;
    src.origin = t0;
    src.duration = seconds;
    src.stems = stems;
    src.main_audio = audio;
    const auto made = make_translations(s_, src, [&](const std::string& what, double f) { report_progress(tr("Переклад і озвучення: ") + what, f); },
                                        &cancel_);
    if (!made.empty()) log_info("{}", trf("Переклад і озвучення: файлів — {}", made.size()));
}

// ========================== Паралельний рендер ==============================
// Кілька копій гри (-multirun) рендерять свої частини фрагмента одночасно, кожна — у свій файл
// поруч із відео. Потім частини склеюються пакетами без перекодування, а звук (гра з WAV кожної
// частини, голоси, мікрофон, обробка) міксується заново на всю довжину — без швів.
void RenderJob::run_parallel(const ParallelInput& in) {
    const demo::DemoAnalysis& A = *analysis_;
    const size_t n = in.parts.size();
    const auto t_start = Clock::now();
    std::string err;
    std::error_code ec;
    const int64_t head_frames = in.head ? in.head->frames : 0;
    if (n > 1)
        log_info("{}", trf("Паралельний рендер: копій гри — {}, кожна рендерить ≈ {} відео", n,
                           format_duration((in.expected_seconds - head_frames * in.es.video.fps.den /
                                                                      static_cast<double>(in.es.video.fps.num)) /
                                           static_cast<double>(n))));

    // ---- Гра: знайти, драйвер, копія config.cfg — один раз на всі копії ----
    std::optional<std::string> demo_for_game;
    if (n > 0) {
        set_stage(tr("Підготовка гри"), 0);
        gmod_ = resolve_game(s_, true, &err);
        if (!gmod_) {
            fail(err);
            return;
        }
        id_ = make_unique_id();
        tmp_dir_ = gmod_->garrysmod / "gmdr_tmp" / id_;
        fs::create_directories(tmp_dir_, ec);
        if (ec) {
            fail(tr("Не вдалося створити тимчасову папку в папці гри: ") + ec.message());
            return;
        }
        config_backup_ = tmp_dir_ / "config.cfg.bak";
        game::backup_config(*gmod_, config_backup_);
        demo_for_game = demo_path_for_game(*gmod_, s_.demo_path, tmp_dir_, id_, &err);
        if (!demo_for_game) {
            cleanup(false);
            fail(err);
            return;
        }
    }
    // Частини — поруч із відео (там і так має бути місце на все відео); при дописуванні там уже
    // лежить записаний початок і його звук
    const fs::path out = path_from_utf8(s_.output_path);
    const fs::path parts_dir = parts_dir_for(s_.output_path);
    if (!in.head) fs::remove_all(parts_dir, ec);
    fs::create_directories(parts_dir, ec);
    if (ec) {
        cleanup(false);
        fail(trf("Не вдалося створити теку для частин {}: {}", path_to_utf8(parts_dir), ec.message()));
        return;
    }
    auto remove_parts = [&] {
        if (s_.keep_temp_files) {
            log_info("{}", trf("Частини залишено: {}", path_to_utf8(parts_dir)));
            return;
        }
        std::error_code rec;
        fs::remove_all(parts_dir, rec);
    };
    // Дописування не вдалося повністю: відео частин уже склеєне у файл, а звук гри лишаємо —
    // з ним можна буде дописати ще раз
    auto remove_part_videos = [&] {
        std::error_code rec;
        for (fs::directory_iterator it(parts_dir, rec), end; !rec && it != end; it.increment(rec))
            if (!ends_with_i(path_to_utf8(it->path().filename()), ".wav")) fs::remove(it->path(), rec);
    };

    // ---- Копії гри: кожна — RenderJob у режимі частини ----
    auto gate = std::make_shared<LaunchGate>();
    const int cores = s_.threads > 0 ? s_.threads : static_cast<int>(std::max(2u, std::thread::hardware_concurrency()));
    const std::string ext = path_to_utf8(out.extension());
    std::vector<std::shared_ptr<RenderJob>> jobs;
    for (const auto& plan : in.parts) {
        RenderSettings cs = s_;
        cs.start_tick = plan.start_tick;
        cs.end_tick = plan.end_tick;
        cs.parallel_games = 1;
        cs.output_path = path_to_utf8(parts_dir / path_from_utf8(std::format("part{}{}", plan.index + 1, ext)));
        cs.game_dir = path_to_utf8(gmod_->root);
        cs.subtitles_srt = cs.chat_srt = cs.chapters = cs.edit_package = cs.speech_subtitles = false;
        cs.markers.clear();
        cs.crash_safe = false;
        cs.quit_game_when_done = true;
        cs.threads = std::max(1, cores / static_cast<int>(n));   // ядра — порівну між копіями
        auto job = std::make_shared<RenderJob>(cs, analysis_, voices_);
        PartSpec ps;
        ps.plan = plan;
        ps.count = static_cast<int>(n);
        ps.size_seconds = in.expected_seconds;
        ps.dir = parts_dir;
        ps.gate = gate;
        ps.gmod = gmod_;
        ps.demo_for_game = demo_for_game.value_or(std::string());
        job->set_part(std::move(ps));
        if (plan.index == 0) job->share_preview(preview_);   // у прев'ю — перша частина
        job->set_show_game(show_game_);
        jobs.push_back(std::move(job));
    }
    if (n > 0) set_stage(n > 1 ? trf("Запуск копій гри: {}", n) : tr("Запуск Garry's Mod"), 0);
    for (auto& j : jobs) j->start();

    const double fps_v = in.es.video.fps.num / static_cast<double>(in.es.video.fps.den);
    const int64_t total_frames = in.parts.back().first_frame + in.parts.back().frames;
    bool shown = show_game_;
    for (;;) {
        bool any = false;
        for (auto& j : jobs) any = any || j->running();
        if (!any) break;
        if (kill_) {
            for (auto& j : jobs) j->kill();
        } else if (cancel_) {
            for (auto& j : jobs) j->cancel();
        }
        if (show_game_ != shown) {
            shown = show_game_;
            for (auto& j : jobs) j->set_show_game(shown);
        }
        std::vector<Progress> ps;
        for (auto& j : jobs) ps.push_back(j->progress());
        update([&](Progress& p) {
            const Progress& first = ps.front();
            int64_t frames = head_frames, subframes = 0, pending = 0, bytes = 0;
            double speed = 0;
            int restarts = 0, recording = 0, paused = 0;
            bool running = false, disk_low = false;
            for (const auto& q : ps) {
                frames += q.frames;
                subframes += q.subframes;
                pending += q.pending_files;
                bytes += q.bytes_written;
                speed += std::max(0.0, q.speed_fps);
                restarts += q.game_restarts;
                recording += q.frames > 0 ? 1 : 0;
                paused += q.game_paused ? 1 : 0;
                running = running || q.game_running;
                disk_low = disk_low || q.disk_low;
            }
            p.stage = n == 1 ? first.stage
                      : recording == 0 ? trf("{} (копій гри: {})", first.stage, n) : trf("Рендер — копій гри: {}", n);
            p.frames = frames;
            p.subframes = subframes;
            p.video_seconds = static_cast<double>(frames) / fps_v;
            p.fraction = total_frames > 0 ? std::clamp(static_cast<double>(frames) / static_cast<double>(total_frames), 0.0, 1.0) : 0.0;
            p.speed_fps = speed;
            p.eta = speed > 0.01 ? static_cast<double>(total_frames - frames) / speed : -1;
            p.pending_files = pending;
            p.bytes_written = bytes;
            p.game_restarts = restarts;
            p.game_running = running;
            p.game_paused = running && paused == static_cast<int>(n);
            p.disk_low = disk_low;
            p.game_hidden = first.game_hidden;
            p.demo_tick = first.demo_tick;
            p.driver_state = first.driver_state;
            p.video_desc = first.video_desc;
            p.stat_read_ms = first.stat_read_ms;
            p.stat_decode_ms = first.stat_decode_ms;
            p.stat_blend_ms = first.stat_blend_ms;
            p.stat_convert_ms = first.stat_convert_ms;
            p.stat_encode_ms = first.stat_encode_ms;
            p.stat_game_wait = first.stat_game_wait;
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
    for (auto& j : jobs) j->wait();
    const auto t_rendered = Clock::now();

    // ---- Які частини склеювати: підряд від початку, до першої незавершеної включно ----
    std::vector<PartResult> res;
    for (auto& j : jobs) res.push_back(j->part_result());
    std::vector<size_t> use;
    for (size_t k = 0; k < n; ++k) {
        if (res[k].frames <= 0) break;
        use.push_back(k);
        if (!res[k].complete) break;
    }
    const bool whole = use.size() == n && (n == 0 || res[n - 1].complete);
    size_t failed_part = n;   // перша незавершена частина (для підказки)
    for (size_t k = 0; k < n; ++k)
        if (!res[k].complete) {
            failed_part = k;
            break;
        }
    std::string first_error;
    for (size_t k = 0; k < n; ++k) {
        if (jobs[k]->state() != JobState::Failed) continue;
        const std::string e = jobs[k]->error();
        if (first_error.empty()) first_error = e;
        log_warn("{}", trf("Частина {} не вдалася: {}", k + 1, e.substr(0, e.find('\n'))));
    }
    // Копії гри закриваються самі; config.cfg повертаємо, коли закриються всі
    set_stage(tr("Закриваю гру"));
    cleanup(true);
    if (kill_) {
        if (!in.head) remove_parts();   // дописування: записаний початок лишається — можна спробувати ще
        update([](Progress& p) { p.stage = tr("Скасовано"); });
        return;
    }
    if (use.empty() && !in.head) {
        remove_parts();
        if (cancel_) {
            update([](Progress& p) { p.stage = tr("Скасовано"); });
            return;
        }
        fail(first_error.empty() ? tr("Жодна копія гри не записала своєї частини — дивіться журнал") : first_error);
        return;
    }

    // ---- Склеювання: відео — пакетами, звук — заново на всю довжину ----
    set_stage(tr("Склеювання частин"), 0);
    EncodeSettings fes = in.es;
    fes.crash_safe = false;
    if (in.head) {
        // Записаний до збою початок — до останнього ключового кадру
        fes.video_parts.push_back(in.head->video);
        fes.video_part_frames.push_back(in.head->frames);
        if (!fes.extras.empty()) {
            log_warn("{}", trf("Додаткові версії при дописуванні не робляться (їхні файли збою не пережили) — "
                               "зробіть їх окремим рендером"));
            fes.extras.clear();
        }
    }
    for (size_t k : use) {
        fes.video_parts.push_back(res[k].video);
        if (in.head) fes.video_part_frames.push_back(0);
    }
    for (size_t i = 0; i < fes.extras.size();) {
        std::vector<std::string> v;
        for (size_t k : use)
            if (i < res[k].extras.size() && !res[k].extras[i].empty()) v.push_back(res[k].extras[i]);
        if (v.size() == use.size()) {
            fes.extras[i++].video_parts = std::move(v);
        } else {
            log_warn("{}", trf("Додаткову версію «{}» пропущено: вона не вдалася в одній із частин", fes.extras[i].label));
            fes.extras.erase(fes.extras.begin() + static_cast<ptrdiff_t>(i));
        }
    }
    const double t0 = in.t0;   // час демо першого кадру відео
    AudioSourcesSpec spec;
    if (in.head)
        for (const auto& [wav, t] : in.head->wavs) spec.game_segments.push_back({wav, t - t0});
    for (size_t k : use)
        for (const auto& [wav, t] : res[k].wavs) spec.game_segments.push_back({wav, t - t0});
    spec.game_audio = s_.game_audio && !spec.game_segments.empty();
    if (s_.game_audio && s_.audio && spec.game_segments.empty()) log_warn("{}", trf("Копії гри не записали звуку — звук гри буде тишею"));
    if (!spec.game_segments.empty()) spec.game_wav = spec.game_segments.front().first;
    spec.game_read_ahead = 30;
    spec.game_offset = s_.game_audio_offset;
    spec.game_gain = static_cast<float>(s_.game_volume);
    spec.voices = in.speakers;
    spec.voice_gains = speaker_gains(s_, in.speakers);
    spec.voice_origin_sample = static_cast<int64_t>(std::llround(t0 * media::kMixRate));
    spec.voice_delay = s_.voice_delay;
    spec.voice_gain = static_cast<float>(s_.voice_volume);
    spec.mic_file = s_.mic_file.empty() ? fs::path() : path_from_utf8(s_.mic_file);
    spec.mic_offset = s_.mic_offset;
    spec.mic_gain = static_cast<float>(s_.mic_volume);
    spec.voice_cleanup = in.voice_fx;
    spec.duck_game = s_.duck_game;
    spec.loudness_target = s_.loudness_target;
    spec.speed = video_speed(s_);
    spec.speed_mute = s_.speed_audio == "mute";
    auto session = std::make_unique<EncodeSession>(fes, nullptr);
    const bool ok = session->begin(s_.width, s_.height, spec, &err) &&
                    session->copy_video(&kill_, [&](double f) {
                        update([&](Progress& p) {
                            p.fraction = f;
                            p.bytes_written = session->bytes_written();   // не розмір останньої частини
                        });
                    }, &err) &&
                    session->finish(&err);
    if (!ok) {
        session->abort();
        session.reset();
        std::error_code rec;
        fs::remove(out, rec);
        if (kill_) {
            if (!in.head) remove_parts();
            update([](Progress& p) { p.stage = tr("Скасовано"); });
            return;
        }
        log_warn("{}", trf("Частини лишились у {}", path_to_utf8(parts_dir)));
        fail(tr("Не вдалося склеїти частини: ") + err);
        return;
    }
    const double secs = session->video_seconds();
    const int64_t frames_done = session->frames_encoded();
    const std::vector<std::string> extras_done = session->finished_extras();
    const auto stems = session->stem_files();
    session.reset();
    std::error_code sec;
    if (const auto sz = fs::file_size(out, sec); !sec) update([&](Progress& p) { p.bytes_written = static_cast<int64_t>(sz); });
    if (resume_ && !whole && !cancel_) {
        // Дописати вдалося не все: запис лишається, а в ньому — звук гри всіх частин
        ResumeRecord rec = *resume_;
        rec.frames = frames_done;
        rec.wavs.clear();
        for (const auto& [wav, t] : spec.game_segments) rec.wavs.push_back({path_to_utf8(wav), t + t0});
        save_resume(rec);
        remove_part_videos();
    } else {
        remove_parts();
        if (resume_) forget_resume(resume_->id);
    }

    // ---- Субтитри, розділи, перевірка, обкладинка — як після звичайного рендеру ----
    const int32_t start_tick = static_cast<int32_t>(std::llround(t0 / static_cast<double>(A.tick_interval)));
    const int32_t end_tick = start_tick + static_cast<int32_t>(std::llround(secs / in.vti));
    if (s_.subtitles_srt && !in.speakers.empty())
        write_speaker_subtitles(s_, in.speakers, spec.voice_origin_sample, secs, s_.speech_subtitles ? in.transcript : nullptr);
    const auto markers = chapters_for_range(parse_markers(s_.markers), start_tick, end_tick, in.vti);
    finalize_output(s_, false, frames_done, secs, s_.chapters ? markers : std::vector<Chapter>{});
    make_after_render(s_, extras_done);
    if (s_.edit_package && !fes.stems_dir.empty()) write_edit_project(s_, fes, frames_done, stems, markers);
    if (s_.chat_srt && frames_done > 0) write_chat_subtitles(s_, A, start_tick, secs);
    if (frames_done > 0) translate_after_render(stems, in.speakers, in.transcript, t0, secs, fes.audio);
    const auto t_end = Clock::now();
    log_info("{}", trf("Готово! {} — {} кадрів, {}, {}", s_.output_path, frames_done, format_duration(secs),
                       format_bytes(file_size_or_zero(out))));
    if (n > 1)
        log_info("{}", trf("Паралельний рендер: копій гри — {}, рендер {}, склеювання {}", n,
                           format_duration(std::chrono::duration<double>(t_rendered - t_start).count()),
                           format_duration(std::chrono::duration<double>(t_end - t_rendered).count())));
    if (in.head && whole) log_info("{}", trf("Урваний рендер дописано — відео склеєно без перекодування"));
    if (cancel_) log_info("{}", trf("Запис зупинено достроково — збережено відрендерену частину"));
    else if (!whole)
        log_warn("{}", trf("Збережено перші {} відео з {}: копія гри з частиною {} не впоралась. Решту можна дорендерити "
                           "окремо: початок фрагмента — тік {}", format_duration(secs), format_duration(in.expected_seconds),
                           failed_part + 1, end_tick));
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
        fail(tr("Черга порожня"));
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
        log_info("{}", trf("==== Черга: пункт {} з {} — {} ====", i + 1, n, title));
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
            p.stage = trf("{} з {} · {}", i + 1, n, p.stage);
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
            log_error("{}", trf("Черга: пункт {} не вдався — {}", i + 1, r.error.substr(0, r.error.find('\n'))));
            if (i + 1 < n && !cancel_) log_info("{}", trf("Черга: переходжу до наступного пункту"));
        }
        std::lock_guard lock(m_);
        results_[i] = r;
        job_.reset();
    }

    // Гра могла лишитися відкритою (чергу скасовано посеред неї): просимо драйвер закрити її
    if (handoff->proc && handoff->proc->running()) {
        set_stage(tr("Закриваю гру"));
        log_info("{}", trf("Черга закінчилась — закриваю гру"));
        if (handoff->gmod && !handoff->waiting_id.empty()) game::request_cancel(*handoff->gmod, handoff->waiting_id);
        if (!handoff->proc->wait(30000)) {
            log_warn("{}", trf("Гра не закрилась сама — закриваю примусово"));
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
    std::string rep = trf("Готово {} з {}:\n", ok, n);
    {
        std::lock_guard lock(m_);
        for (size_t i = 0; i < n; ++i) {
            const auto& r = results_[i];
            const char* mark = r.state == JobState::Succeeded ? "✓" : r.state == JobState::Failed ? "✗"
                             : r.state == JobState::Cancelled ? "–" : "·";
            std::string what = r.state == JobState::Succeeded ? r.output
                             : r.state == JobState::Failed    ? r.error.substr(0, r.error.find('\n'))
                             : r.state == JobState::Cancelled ? tr("скасовано")
                                                              : tr("не почато");
            rep += std::format("  {} {}. {} — {}\n", mark, i + 1,
                               path_to_utf8(path_from_utf8(items_[i].demo_path).filename()), what);
        }
    }
    set_report(rep);
    log_info("{}", trf("Черга: {}", rep));
    if (cancel_) return;   // стан "скасовано" виставить Job::start
    if (ok == 0) {
        fail(tr("Жоден пункт черги не вдався.\n\n") + rep);
        return;
    }
    succeed(trf("{} з {} відео готово", ok, n));
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
    set_stage(tr("Підготовка гри"), -1);
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

    set_stage(tr("Запуск Garry's Mod"), -1);
    const fs::path exe = s_.game_exe.empty() ? g->default_exe() : path_from_utf8(s_.game_exe);
    std::vector<std::string> args;
    if (to_lower(path_to_utf8(exe.filename())).rfind("hl2", 0) == 0) args.insert(args.end(), {"-game", "garrysmod"});
    args.push_back("-novid");
    if (s_.rtx)
        for (const char* a : {"-dxlevel", "90", "-nod3d9ex", "+mat_disable_d3d9ex", "1", "-insecure"}) args.push_back(a);
    args.insert(args.end(), {"-condebug", "+exec", "gmdr/job_" + id + ".cfg"});
    for (const auto& a : split(s_.extra_launch_args, ' ')) args.push_back(trim(a));
    log_info("{}", trf("Команда запуску: {}", game::format_command_line(exe, args)));
    auto proc = game::GameProcess::launch(exe, args, g->root, {{"SteamAppId", "4000"}, {"SteamGameId", "4000"}}, &err);
    if (!proc) {
        cleanup();
        fail(err);
        return;
    }
    log_info("{}", trf("Перегляд у грі з {}. Клавіші в грі: F9 — початок фрагмента, F11 — кінець, F6 — позначка. "
             "Закрийте гру, коли закінчите.", format_duration(from_tick_ * ti)));

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
            const char* what = marks[i].kind == "start" ? tr("початок фрагмента") : marks[i].kind == "end" ? tr("кінець фрагмента") : tr("позначка");
            log_info("{}", trf("У грі позначено {}: {} (тік {})", what, format_duration(marks[i].tick * ti), marks[i].tick));
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
            log_info("{}", trf("Закриваю гру..."));
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
                if (st->state == "watching") log_info("{}", trf("Демо грає. Коли закінчите, просто закрийте гру."));
                else if (st->state == "done") log_info("{}", trf("Демо закінчилось — можна закривати гру"));
                else if (st->state == "error") log_warn("{}", trf("Гра: {}", st->message));
            }
            update([&](Progress& p) {
                p.demo_tick = st->tick;
                p.demo_total = st->total;
                p.game_running = true;
                p.fraction = st->total > 0 ? std::clamp(static_cast<double>(st->tick) / st->total, 0.0, 1.0) : -1.0;
                p.stage = st->state == "watching" ? trf("Перегляд у грі: {}", format_duration(st->tick * ti))
                          : st->state == "done"   ? std::string(tr("Демо закінчилось — закрийте гру"))
                                                  : std::string(tr("Гра завантажує демо"));
            });
        }
        poll_marks();
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    poll_marks();
    update([](Progress& p) { p.game_running = false; });
    game::GameProcess::wait_all_exited(kGameProcessNames, 15000);
    cleanup();
    succeed(tr("Перегляд завершено"));
}

EncodeFramesJob::EncodeFramesJob(RenderSettings s, fs::path frames_dir, std::string prefix, fs::path wav_path,
                                 std::shared_ptr<const demo::DemoAnalysis> analysis,
                                 std::shared_ptr<const voice::VoiceDecodeResult> voices)
    : s_(std::move(s)), dir_(std::move(frames_dir)), prefix_(std::move(prefix)), wav_(std::move(wav_path)),
      analysis_(std::move(analysis)), voices_(std::move(voices)) {}

void EncodeFramesJob::run() {
    KeepAwake keep_awake;
    std::string err;
    set_stage(tr("Пошук кадрів"), 0);
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
    log_info("{}", trf("Кадри: {}{}####.*, звук: {}", path_to_utf8(dir_), prefix.empty() ? "" : "/" + prefix,
             wav.empty() ? tr("немає") : path_to_utf8(wav)));
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
        set_stage(tr("Аналіз голосу гравців"));
        voice_fx = voice_cleanup_for(s_, speakers, cancel_);
    }

    set_stage(tr("Кодування"));
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
                fail(tr("Не вдалося почати кодування: ") + err);
                return;
            }
            session.game_audio_finished();   // файл WAV уже повний
            if (s_.subtitles_srt && !speakers.empty()) {
                // Готові кадри: розшифровку беремо лише збережену (розпізнавати тут нема коли)
                const auto tr = s_.speech_subtitles ? speech::load_transcript(s_.demo_path) : std::nullopt;
                write_speaker_subtitles(s_, speakers, spec.voice_origin_sample, total_out / fps_v, tr ? &*tr : nullptr);
            }
            update([&](Progress& p) {
                p.video_desc = session.video_description();
                p.audio_desc = session.audio_description();
            });
        }
        if (!session.push_subframe(std::move(img), &err)) {
            session.abort();
            fail(tr("Помилка кодування: ") + err);
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
        fail(tr("У папці не знайдено кадрів (TGA/JPG/PNG)"));
        return;
    }
    set_stage(tr("Завершення файлу"));
    if (!session.finish(&err)) {
        fail(tr("Не вдалося завершити файл: ") + err);
        return;
    }
    const int64_t frames_done = session.frames_encoded();
    const double secs = session.video_seconds();
    const std::vector<std::string> extras_done = session.finished_extras();
    finalize_output(s_, es.crash_safe && is_mov_family(s_.output_path, s_.container), frames_done, secs);
    make_after_render(s_, extras_done);
    log_info("{}", trf("Готово! {} — {} кадрів, {}", s_.output_path, frames_done, format_duration(secs)));
    succeed(s_.output_path);
}

} // namespace gmdr::render
