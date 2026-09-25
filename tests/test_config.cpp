// =============================================================================
//  test_config.cpp — тести моделі налаштувань: каталог, міграція, правила
//  перевірки (constraints), пресети, реєстр складників.
//
//  Середовище тут — синтетичне (full_env): жодних проб відеокарти чи пошуку гри,
//  тож результати однакові на будь-якому ПК і в CI.
// =============================================================================
#include "core/config/constraints.hpp"
#include "core/config/dependencies.hpp"
#include "core/config/formats.hpp"
#include "core/config/presets.hpp"
#include "core/config/settings_catalog.hpp"
#include "core/config/preflight.hpp"
#include "core/game/game_renderer.hpp"
#include "core/render/jobs.hpp"
#include "core/util/strings.hpp"

#include "test_check.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <set>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
}

using namespace gmdr;
using namespace gmdr::config;

namespace {

bool has_encoder(const char* name) { return avcodec_find_encoder_by_name(name) != nullptr; }

// Усе є: кодеки зі збірки FFmpeg, GPU-кодеків немає (не NVIDIA/AMD/Intel), обидві копії гри,
// whisper, ключі перекладу, OmniVoice. Тест прибирає те, що перевіряє.
EnvironmentCapabilities full_env() {
    EnvironmentCapabilities env;
    const CapabilityState yes{Availability::Available, {}};
    const CapabilityState no{Availability::Unavailable, "немає"};
    env.platform = {"windows", "Windows 11"};
    env.hardware.cpu_threads = 16;
    env.hardware.ram_bytes = 32ull << 30;
    env.hardware.gpus.push_back({"NVIDIA GeForce RTX 4070", "nvidia", 0x10de, 12ull << 30, "560.94", false});
    env.hardware.gpu_info = yes;
    for (const auto& e : video_encoders()) {
        EncoderState st;
        st.compiled = has_encoder(e.name.c_str());
        st.gpu = e.gpu;
        st.probed = e.gpu;
        st.state = st.compiled && !e.gpu ? yes : CapabilityState{Availability::Unavailable, "немає"};
        env.encoders.video[e.name] = st;
    }
    for (const auto& e : audio_encoders()) env.encoders.audio[e.name] = has_encoder(e.name.c_str()) ? yes : no;
    env.encoders.gpu_probe_done = true;
    env.media.ffmpeg_version = "libavcodec test";
    for (const char* f : {"thumbnail", "palettegen", "paletteuse", "arnndn", "afftdn", "loudnorm", "sidechaincompress", "atempo"})
        env.media.filters[f] = yes;
    for (const char* e : {"gif", "libwebp_anim", "libwebp", "mjpeg"}) env.media.extra_encoders[e] = yes;
    env.media.rnnoise_model = yes;
    for (const auto* r : game::game_renderers()) {
        GameInstallInfo g;
        g.renderer = r->id();
        g.install = {Availability::Available, "C:/Games/" + r->id()};
        g.has_64bit = true;
        g.accepted = true;
        g.driver = "installed";
        env.game.installs.push_back(g);
    }
    env.game.running = no;
    env.game.frame_pipes = yes;
    env.speech.whisper_cli = yes;
    env.speech.model = yes;
    for (const char* p : {"deepl", "google", "libre", "openai", "fake"}) env.translation.providers[p] = yes;
    env.dubbing.omnivoice = yes;
    env.dubbing.elevenlabs = yes;
    env.dubbing.nvidia_gpu = yes;
    env.filesystem.demo = yes;
    env.filesystem.mic = {Availability::NotApplicable, {}};
    env.filesystem.output_exists = no;
    env.filesystem.output_free = 500ull << 30;
    env.filesystem.game_free = 500ull << 30;
    return env;
}

render::RenderSettings base_settings() {
    render::RenderSettings s;
    s.demo_path = "C:/demos/match.dem";
    s.output_path = "C:/videos/match.mp4";
    return s;
}

ValidationContext ctx_for(ValidationContext::Purpose p, double seconds = 0) {
    ValidationContext c;
    c.purpose = p;
    if (seconds > 0) {
        c.demo_known = true;
        c.tick_interval = 1.0 / 66;
        c.last_tick = static_cast<int32_t>(seconds * 66);
        c.speakers = 2;
        c.local_speaker = 1;
    }
    return c;
}

std::string dump(const ValidationResult& r) {
    std::string out;
    for (const auto& i : r.issues) out += "    [" + i.rule + "] " + format_issue(i) + "\n";
    return out;
}

// Помилок немає (друкує, що знайшлося — щоб одразу бачити причину в журналі CI)
bool no_errors(const ValidationResult& r, const char* what) {
    if (r.executable()) return true;
    std::printf("  %s:\n%s", what, dump(r).c_str());
    return false;
}

// Копія зауваження: результат evaluate() часто тимчасовий
std::optional<Issue> issue(const ValidationResult& r, const std::string& rule) {
    if (const Issue* i = r.find(rule)) return *i;
    return std::nullopt;
}

bool has(const ValidationResult& r, const std::string& rule, Severity sev) {
    return std::any_of(r.issues.begin(), r.issues.end(), [&](const Issue& i) { return i.rule == rule && i.severity == sev; });
}

const OptionState* option(const ValidationResult& r, SettingId id, const std::string& value) {
    for (const auto& o : r.state(id).options)
        if (o.value == value) return &o;
    return nullptr;
}

void test_catalog_and_migration() {
    std::printf("[config: каталог і міграція]\n");
    // Кожне поле RenderSettings — у каталозі, ключ = ключ JSON, запис і читання збігаються
    CHECK(settings_catalog().size() == static_cast<size_t>(SettingId::Count));
    std::set<std::string> keys;
    const json::Value j = base_settings().to_json();
    for (const auto& info : settings_catalog()) {
        keys.insert(info.key);
        CHECK(find_setting(info.key) == info.id);
        CHECK(j.has(info.key));
    }
    CHECK(keys.size() == settings_catalog().size());
    render::RenderSettings s;
    CHECK(set_setting(s, SettingId::parallel_games, json::Value::number(3)));
    CHECK(s.parallel_games == 3);
    CHECK(get_setting(s, SettingId::parallel_games).as_number() == 3);
    CHECK(set_setting(s, SettingId::game_renderer, json::Value::string("rtx")));
    CHECK(s.game_renderer == "rtx");
    CHECK(!set_setting(s, SettingId::width, json::Value::string("wide")));   // тип не той
    CHECK(s.width == 1920);
    CHECK(setting_info(SettingId::deepl_key).secret);
    CHECK(!setting_info(SettingId::ui_theme).render);

    // Версія конфігурації пишеться; старі налаштування ("rtx": true) переходять у рендерер
    CHECK(j["configuration_version"].as_number() == render::kConfigurationVersion);
    auto old = json::parse(R"({"rtx": true, "parallel_games": 2, "width": 1280})");
    CHECK(old.has_value());
    const auto m = render::RenderSettings::from_json(*old);
    CHECK(m.game_renderer == "rtx");
    CHECK(m.parallel_games == 2);   // не виправляється мовчки — перевірка скаже
    CHECK(m.width == 1280);
    const auto plain = render::RenderSettings::from_json(*json::parse(R"({"rtx": false})"));
    CHECK(plain.game_renderer == "standard");
    // Нова версія з явним рендерером: старе поле не перебиває
    const auto v1 = render::RenderSettings::from_json(*json::parse(R"({"configuration_version": 1, "game_renderer": "standard", "rtx": true})"));
    CHECK(v1.game_renderer == "standard");
    const auto round = render::RenderSettings::from_json(m.to_json());
    CHECK(round.game_renderer == "rtx");
    // Невідомий рендерер зберігається як є — і перевірка каже про це з виправленням
    const auto unknown = render::RenderSettings::from_json(*json::parse(R"({"game_renderer": "vulkan-rt"})"));
    CHECK(unknown.game_renderer == "vulkan-rt");
    CHECK(&render::renderer_of(unknown) == &game::standard_renderer());
    auto r = evaluate(unknown, full_env());
    CHECK(has(r, "game.renderer", Severity::Error));
}

void test_rules_basics() {
    std::printf("[config: правила — основне]\n");
    const auto env = full_env();
    // Типові налаштування з демо — без помилок для всіх цілей
    for (auto p : {ValidationContext::Purpose::Edit, ValidationContext::Purpose::Render, ValidationContext::Purpose::TestRun,
                   ValidationContext::Purpose::QueueItem}) {
        const auto r = evaluate(base_settings(), env, ctx_for(p, 60));
        CHECK(no_errors(r, "типові налаштування"));
    }
    // Ідентифікатори правил унікальні, кожне зауваження — від відомого правила
    std::set<std::string> ids;
    for (const auto& ri : rule_list()) ids.insert(ri.id);
    CHECK(ids.size() == rule_list().size());
    CHECK(ids.size() >= 30);
    // Без демо — помилка з дією «відкрити демо»
    render::RenderSettings s = base_settings();
    s.demo_path.clear();
    auto r = evaluate(s, env);
    std::optional<Issue> i = issue(r, "source.demo");
    CHECK(i && i->severity == Severity::Error && i->action == ActionId::OpenDemo);
    for (const auto& x : r.issues) CHECK(ids.count(x.rule) == 1);
    // Порядок: спершу помилки
    s = base_settings();
    s.demo_path.clear();
    s.voice_mode = "selected";   // попередження
    r = evaluate(s, env);
    CHECK(!r.issues.empty() && r.issues.front().severity == Severity::Error);
    for (size_t k = 1; k < r.issues.size(); ++k) CHECK(r.issues[k - 1].severity >= r.issues[k].severity);
    // apply_fix не змінює вихідних налаштувань
    s = base_settings();
    s.start_tick = 500;
    s.end_tick = 100;
    r = evaluate(s, env);
    i = issue(r, "source.range");
    CHECK(i && !i->fixes.empty());
    if (i && !i->fixes.empty()) {
        const auto fixed = apply_fix(s, i->fixes.front());
        CHECK(s.start_tick == 500);
        CHECK(fixed.start_tick == 0 && fixed.end_tick == -1);
        CHECK(!evaluate(fixed, env).find("source.range"));
    }
    // Демо зникло з диска
    auto env2 = env;
    env2.filesystem.demo = {Availability::Unavailable, "немає"};
    CHECK(has(evaluate(base_settings(), env2), "source.demo", Severity::Error));
    // Початок після кінця демо (відомо після аналізу)
    s = base_settings();
    s.start_tick = 66 * 100;
    CHECK(has(evaluate(s, env, ctx_for(ValidationContext::Purpose::Edit, 60)), "source.range", Severity::Error));
}

void test_rules_renderer_parallel() {
    std::printf("[config: рендерер і паралельний рендер]\n");
    const auto env = full_env();
    using P = ValidationContext::Purpose;
    // RTX + 2 копії — помилка, не мовчазна одна копія (раніше так і було)
    render::RenderSettings s = base_settings();
    s.game_renderer = "rtx";
    s.parallel_games = 2;
    auto r = evaluate(s, env, ctx_for(P::Edit, 120));
    std::optional<Issue> i = issue(r, "parallel.limits");
    CHECK(i && i->severity == Severity::Error && i->kind == IssueKind::Renderer);
    CHECK(i && std::find(i->related.begin(), i->related.end(), SettingId::game_renderer) != i->related.end());
    CHECK(!r.executable());
    CHECK(r.state(SettingId::parallel_games).max == 1.0);
    const OptionState* o = option(r, SettingId::parallel_games, "2");
    CHECK(o && !o->available && !o->reason.empty());
    CHECK(!r.state(SettingId::parallel_games).note.empty());
    if (i && !i->fixes.empty()) {
        const auto fixed = apply_fix(s, i->fixes.front());
        CHECK(fixed.parallel_games == 1);
        CHECK(no_errors(evaluate(fixed, env, ctx_for(P::Edit, 120)), "RTX після виправлення"));
    }
    // Стандарт + 2 копії, MKV, 2 хв — справді 2
    s = base_settings();
    s.output_path = "C:/videos/match.mkv";
    s.parallel_games = 2;
    r = evaluate(s, env, ctx_for(P::Render, 120));
    CHECK(r.derived.parallel == 2 && r.derived.parallel_max == 4);
    CHECK(!r.find("parallel.limits"));
    // Тестовий прогін і пункт черги — одна копія, з поясненням (інформація, не помилка)
    r = evaluate(s, env, ctx_for(P::TestRun, 120));
    CHECK(r.derived.parallel == 1 && has(r, "parallel.limits", Severity::Info));
    // AVI не склеюється — одна копія, попередження з виправленням на MKV
    s.output_path = "C:/videos/match.avi";
    r = evaluate(s, env, ctx_for(P::Render, 120));
    CHECK(r.derived.parallel == 1);
    i = issue(r, "parallel.limits");
    CHECK(i && i->severity == Severity::Warning && i->fixes.size() == 2);
    // Короткий фрагмент — одна копія (інформація)
    s.output_path = "C:/videos/match.mp4";
    r = evaluate(s, env, ctx_for(P::Render, 10));
    CHECK(r.derived.parallel == 1 && has(r, "parallel.limits", Severity::Info));
    // Поза межами 1…4
    s.parallel_games = 9;
    CHECK(has(evaluate(s, env), "parallel.limits", Severity::Error));
    // 4 копії на 4 ГБ пам'яті — попередження про ресурси
    auto small = env;
    small.hardware.ram_bytes = 4ull << 30;
    s.parallel_games = 4;
    CHECK(has(evaluate(s, small, ctx_for(P::Render, 300)), "parallel.resources", Severity::Warning));

    // RTX: за межами екрана — не можна (варіант недоступний), вибране — інформація і виправлення
    s = base_settings();
    s.game_renderer = "rtx";
    s.game_window = "offscreen";
    r = evaluate(s, env);
    o = option(r, SettingId::game_window, "offscreen");
    CHECK(o && !o->available);
    CHECK(r.derived.window_mode == "behind");
    CHECK(has(r, "game.window", Severity::Info));
    // Поле папки гри — лише вибраного рендерера
    CHECK(r.state(SettingId::rtx_game_dir).visible);
    CHECK(!r.state(SettingId::game_dir).visible);
    CHECK(!evaluate(base_settings(), env).state(SettingId::rtx_game_dir).visible);
    // Копії RTX немає — помилка з дією «знайти гру»
    auto no_rtx = env;
    for (auto& g : no_rtx.game.installs)
        if (g.renderer == "rtx") g.install = {Availability::Unavailable, "не знайдено"};
    i = issue(evaluate(s, no_rtx), "game.install");
    CHECK(i && i->severity == Severity::Error && i->action == ActionId::DetectGame);
    // Без пошуку гри (Unknown) — мовчить: не вигадуємо «немає»
    auto unknown = env;
    unknown.game.installs.clear();
    CHECK(!evaluate(s, unknown).find("game.install"));
    // Гра вже запущена: під час редагування — попередження, перед рендером — помилка, у черзі — ні
    auto running = env;
    running.game.running = {Availability::Available, "запущено"};
    CHECK(has(evaluate(base_settings(), running, ctx_for(P::Edit)), "game.running", Severity::Warning));
    CHECK(has(evaluate(base_settings(), running, ctx_for(P::Render)), "game.running", Severity::Error));
    CHECK(!evaluate(base_settings(), running, ctx_for(P::QueueItem)).find("game.running"));
    // Ручний режим: тест і черга неможливі
    s = base_settings();
    s.manual_mode = true;
    CHECK(has(evaluate(s, env, ctx_for(P::TestRun)), "game.manual", Severity::Error));
    CHECK(has(evaluate(s, env, ctx_for(P::QueueItem)), "game.manual", Severity::Error));
    CHECK(has(evaluate(s, env, ctx_for(P::Render)), "game.manual", Severity::Info));
    CHECK(!evaluate(s, env).state(SettingId::game_window).enabled);
    // Передача кадрів каналом, де каналів немає
    auto no_pipes = env;
    no_pipes.game.frame_pipes = {Availability::Unavailable, "немає"};
    s = base_settings();
    s.frame_transport = "pipe";
    CHECK(has(evaluate(s, no_pipes), "game.frame_transport", Severity::Error));
    CHECK(!evaluate(base_settings(), env).state(SettingId::max_pending_frames).visible);
}

void test_rules_video() {
    std::printf("[config: відео]\n");
    const auto env = full_env();
    // WebM + x264 — помилка до запуску гри (раніше — після завантаження демо), з виправленнями
    render::RenderSettings s = base_settings();
    s.output_path = "C:/videos/match.webm";
    auto r = evaluate(s, env);
    std::optional<Issue> i = issue(r, "video.codec");
    CHECK(i && i->severity == Severity::Error && i->kind == IssueKind::Conflict);
    CHECK(i && !i->fixes.empty());
    const OptionState* o = option(r, SettingId::video_codec, "libx264");
    CHECK(o && !o->available);
    if (i)
        for (const auto& f : i->fixes) CHECK(!evaluate(apply_fix(s, f), env).find("video.codec"));
    // 641×361 — не мовчки 640×360: інформація з виправленням
    s = base_settings();
    s.width = 641;
    s.height = 361;
    r = evaluate(s, env);
    CHECK(r.derived.width == 640 && r.derived.height == 360);
    i = issue(r, "video.size");
    CHECK(i && i->severity == Severity::Info && !i->fixes.empty());
    // x264 12 біт — не мовчки 10: помилка з виправленням на те, що вміє кодек
    s = base_settings();
    s.bit_depth = 12;
    r = evaluate(s, env);
    i = issue(r, "video.pixel_format");
    CHECK(i && i->severity == Severity::Error);
    CHECK(r.derived.bit_depth < 12);
    o = option(r, SettingId::bit_depth, "12");
    CHECK(o && !o->available);
    if (i && !i->fixes.empty()) {
        const auto fixed = apply_fix(s, i->fixes.front());
        CHECK(fixed.bit_depth == r.derived.bit_depth);
        CHECK(!has(evaluate(fixed, env), "video.pixel_format", Severity::Error));
        // Залишається лише застереження про сумісність 10-бітного H.264
        CHECK(fixed.bit_depth == 8 || has(evaluate(fixed, env), "video.pixel_format", Severity::Info));
    }
    // Явний формат пікселів, якого кодек не вміє
    s = base_settings();
    s.pix_fmt = "rgb48le";
    CHECK(has(evaluate(s, env), "video.pixel_format", Severity::Error));
    CHECK(!evaluate(s, env).state(SettingId::bit_depth).enabled);
    // GPU-кодек не працює на цій відеокарті — помилка з переходом на процесорний
    s = base_settings();
    s.video_codec = "hevc_nvenc";
    auto failed = env;
    failed.encoders.video["hevc_nvenc"].state = {Availability::Failed, "OpenEncodeSessionEx failed"};
    i = issue(evaluate(s, failed), "video.codec");
    CHECK(i && i->severity == Severity::Error && !i->fixes.empty());
    if (i && !i->fixes.empty()) CHECK(apply_fix(s, i->fixes.front()).video_codec == "libx265");
    // Ще не перевірено: під час редагування — інформація, перед рендером — попередження
    auto unprobed = env;
    unprobed.encoders.video["hevc_nvenc"].state = {Availability::Unknown, {}};
    CHECK(has(evaluate(s, unprobed, ctx_for(ValidationContext::Purpose::Edit)), "video.codec", Severity::Info));
    CHECK(has(evaluate(s, unprobed, ctx_for(ValidationContext::Purpose::Render)), "video.codec", Severity::Warning));
    // Неправильний FPS, якість поза межами, чужий пресет
    s = base_settings();
    s.fps = "abc";
    CHECK(has(evaluate(s, env), "video.fps", Severity::Error));
    s = base_settings();
    s.quality = 99;
    CHECK(has(evaluate(s, env), "video.quality", Severity::Error));
    s = base_settings();
    s.preset = "p7";
    CHECK(has(evaluate(s, env), "video.quality", Severity::Error));
    // ProRes: якість — профіль, бітрейт і розмір файлу сховані
    s = base_settings();
    s.output_path = "C:/videos/match.mov";
    s.video_codec = "prores_ks";
    r = evaluate(s, env);
    CHECK(!r.state(SettingId::video_bitrate).visible && !r.state(SettingId::target_size_mb).visible);
    CHECK(r.state(SettingId::quality).options.size() == 6);
    // Розмір файлу занадто малий для довжини — попередження
    s = base_settings();
    s.target_size_mb = 5;
    CHECK(has(evaluate(s, env, ctx_for(ValidationContext::Purpose::Edit, 600)), "video.quality", Severity::Warning));
    // Послідовність кадрів PNG: кодек заданий форматом, шаблон імені обов'язковий
    s = base_settings();
    s.container = "png";
    s.video_codec = "png";
    i = issue(evaluate(s, env), "output.file");
    CHECK(i && i->severity == Severity::Error && !i->fixes.empty());
    if (i && !i->fixes.empty()) CHECK(apply_fix(s, i->fixes.front()).output_path.find("%06d") != std::string::npos);
    // Вихід = демо
    s = base_settings();
    s.output_path = s.demo_path;
    CHECK(has(evaluate(s, env), "output.file", Severity::Error));
    // Перезапис: під час редагування — інформація, перед рендером — попередження
    auto exists = env;
    exists.filesystem.output_exists = {Availability::Available, {}};
    CHECK(has(evaluate(base_settings(), exists, ctx_for(ValidationContext::Purpose::Render)), "output.file", Severity::Warning));
}

void test_rules_audio_outputs() {
    std::printf("[config: звук і додаткові версії]\n");
    const auto env = full_env();
    // Opus лише 48 кГц — не мовчки: помилка з виправленням
    render::RenderSettings s = base_settings();
    s.output_path = "C:/videos/match.mkv";
    s.audio_codec = "libopus";
    s.sample_rate = 44100;
    if (has_encoder("libopus")) {
        std::optional<Issue> i = issue(evaluate(s, env), "audio.codec");
        CHECK(i && i->severity == Severity::Error && !i->fixes.empty());
        if (i && !i->fixes.empty()) CHECK(apply_fix(s, i->fixes.front()).sample_rate == 48000);
    }
    // Звук вимкнено — залежні поля недоступні
    s = base_settings();
    s.audio = false;
    auto r = evaluate(s, env);
    CHECK(!r.state(SettingId::audio_codec).enabled && !r.state(SettingId::voice_mode).enabled);
    // «Вибрані» голоси без жодного вибраного — попередження (раніше — тиша у відео без пояснень)
    s = base_settings();
    s.voice_mode = "selected";
    CHECK(has(evaluate(s, env), "audio.sources", Severity::Warning));
    // Звук без джерел
    s = base_settings();
    s.game_audio = false;
    s.voice_mode = "none";
    CHECK(has(evaluate(s, env), "audio.sources", Severity::Warning));
    // Файлу мікрофона немає
    s = base_settings();
    s.mic_file = "C:/mic.wav";
    auto no_mic = env;
    no_mic.filesystem.mic = {Availability::Unavailable, "немає"};
    CHECK(has(evaluate(s, no_mic), "audio.mic", Severity::Error));

    // Додаткові версії: невідома — помилка з виправленням; з послідовністю кадрів — помилка
    s = base_settings();
    s.extra_versions = "discord,bogus";
    std::optional<Issue> i = issue(evaluate(s, env), "outputs.versions");
    CHECK(i && i->severity == Severity::Error && !i->fixes.empty());
    if (i && !i->fixes.empty()) CHECK(apply_fix(s, i->fixes.front()).extra_versions == "discord");
    s = base_settings();
    s.container = "png";
    s.video_codec = "png";
    s.output_path = "C:/videos/f/frame_%06d.png";
    s.extra_versions = "discord";
    CHECK(has(evaluate(s, env), "outputs.versions", Severity::Error));
    // Немає кодека для GIF у збірці FFmpeg — версія неможлива, варіант недоступний
    auto no_gif = env;
    no_gif.media.extra_encoders["gif"] = {Availability::Unavailable, "немає"};
    s = base_settings();
    s.extra_versions = "gif";
    r = evaluate(s, no_gif);
    CHECK(has(r, "outputs.versions", Severity::Error));
    const OptionState* o = option(r, SettingId::extra_versions, "gif");
    CHECK(o && !o->available);
    // Тестовий прогін: версії не кодуються (інформація)
    s.extra_versions = "discord";
    CHECK(has(evaluate(s, env, ctx_for(ValidationContext::Purpose::TestRun)), "outputs.versions", Severity::Info));
}

void test_rules_ai() {
    std::printf("[config: розпізнавання, переклад, озвучення]\n");
    const auto env = full_env();
    // Текст розмов без whisper-cli — попередження з дією «встановити»
    render::RenderSettings s = base_settings();
    s.subtitles_srt = true;
    s.speech_subtitles = true;
    auto no_whisper = env;
    no_whisper.speech.whisper_cli = {Availability::Unavailable, "немає whisper-cli"};
    std::optional<Issue> i = issue(evaluate(s, no_whisper), "speech.recognition");
    CHECK(i && i->severity == Severity::Warning && i->action == ActionId::InstallWhisperCli);
    auto no_model = env;
    no_model.speech.model = {Availability::Unavailable, "немає моделі"};
    i = issue(evaluate(s, no_model), "speech.recognition");
    CHECK(i && i->action == ActionId::InstallWhisperModel);
    // Переклад без ключа — попередження (рендер буде, переклад пропуститься — так і робить ядро)
    s = base_settings();
    s.translate_subtitles = true;
    s.dub_languages = "en,de";
    auto no_key = env;
    no_key.translation.providers["deepl"] = {Availability::Unavailable, "немає ключа API"};
    i = issue(evaluate(s, no_key), "ai.translator");
    CHECK(i && i->severity == Severity::Warning && i->action == ActionId::ConfigureTranslator);
    // Невідома мова — помилка з виправленням
    s.dub_languages = "en,xx";
    i = issue(evaluate(s, env), "ai.languages");
    CHECK(i && i->severity == Severity::Error && !i->fixes.empty());
    if (i && !i->fixes.empty()) CHECK(apply_fix(s, i->fixes.front()).dub_languages == "en");
    // Без мов — попередження
    s.dub_languages.clear();
    CHECK(has(evaluate(s, env), "ai.languages", Severity::Warning));
    // Озвучення без звуку — помилка
    s = base_settings();
    s.dub = true;
    s.dub_languages = "en";
    s.audio = false;
    CHECK(has(evaluate(s, env), "ai.dub", Severity::Error));
    // Локальний рушій не встановлено — попередження з дією
    s.audio = true;
    auto no_engine = env;
    no_engine.dubbing.omnivoice = {Availability::Unavailable, "не встановлено"};
    i = issue(evaluate(s, no_engine), "ai.tts");
    CHECK(i && i->severity == Severity::Warning && i->action == ActionId::InstallVoiceEngine);
    // Клонування без згоди — попередження, виправлення вимикає клонування
    s.tts_clone = true;
    i = issue(evaluate(s, env), "ai.tts");
    CHECK(i && i->action == ActionId::ConfirmVoiceConsent && !i->fixes.empty());
    s.tts_clone_ack = true;
    CHECK(!evaluate(s, env).find("ai.tts"));
    // Послідовність кадрів: озвучення — лише аудіофайлами
    s = base_settings();
    s.container = "png";
    s.video_codec = "png";
    s.output_path = "C:/videos/f/frame_%06d.png";
    s.dub = true;
    s.dub_languages = "en";
    s.dub_outputs = "tracks";
    CHECK(has(evaluate(s, env), "ai.dub", Severity::Error));
    // Поля ключів — лише вибраного сервісу
    auto r = evaluate(base_settings(), env);
    CHECK(r.state(SettingId::deepl_key).visible && !r.state(SettingId::openai_key).visible);
}

void test_graphics_api_rule() {
    std::printf("[config: графічний API вікна]\n");
    auto env = full_env();
    render::RenderSettings s = base_settings();
    s.ui_graphics_api = "d3d12";
    CHECK(!evaluate(s, env).find("app.graphics_api"));   // CLI: вікна немає — мовчить
    env.graphics.apis = {{"auto", "Auto", Availability::Available, {}},
                         {"d3d11", "Direct3D 11", Availability::Available, {}},
                         {"d3d12", "Direct3D 12", Availability::Unavailable, "потрібен Qt 6.6"}};
    std::optional<Issue> i = issue(evaluate(s, env), "app.graphics_api");
    CHECK(i && i->severity == Severity::Warning && !i->fixes.empty());
    if (i && !i->fixes.empty()) CHECK(apply_fix(s, i->fixes.front()).ui_graphics_api == "auto");
    s.ui_graphics_api = "d3d11";
    CHECK(!evaluate(s, env).find("app.graphics_api"));
    // Ще не перевірений API — не «недоступний»
    env.graphics.apis[2].state = Availability::Unknown;
    s.ui_graphics_api = "d3d12";
    CHECK(!evaluate(s, env).find("app.graphics_api"));
}

void test_presets() {
    std::printf("[config: пресети]\n");
    auto env = full_env();
    // Кожен пресет дає виконувані налаштування
    for (const auto& p : presets()) {
        const auto s = apply_preset(base_settings(), p.id, env);
        const auto r = evaluate(s, env, ctx_for(ValidationContext::Purpose::Render, 60));
        CHECK(no_errors(r, p.id.c_str()));
        CHECK(!p.label.empty() && !p.description.empty());
    }
    CHECK(find_preset("youtube-1080p60") && !find_preset("nope"));
    // 4K60: HEVC на відеокарті, лише якщо проба пройшла
    auto s = apply_preset(base_settings(), "youtube-4k60", env);
    CHECK(s.video_codec == "libx265" && s.bit_depth == 10 && s.width == 3840);
    env.encoders.video["hevc_nvenc"].state = {Availability::Available, {}};
    s = apply_preset(base_settings(), "youtube-4k60", env);
    CHECK(s.video_codec == "hevc_nvenc");
    // Discord 10 МБ — розмір файлу; ProRes — MOV
    s = apply_preset(base_settings(), "discord-10mb", env);
    CHECK(s.target_size_mb == 10 && s.width == 1280);
    s = apply_preset(base_settings(), "edit-prores", env);
    CHECK(container_of(s) == "mov" && s.video_codec == "prores_ks");
    // Невідомий пресет — без змін
    CHECK(apply_preset(base_settings(), "nope", env).to_json().dump() == base_settings().to_json().dump());

    // Зміна формату: лише шлях (кодек не підміняється мовчки), послідовність кадрів і назад
    render::RenderSettings c = base_settings();
    set_container(c, "webm");
    // Роздільники шляхів — як у системі (у Windows склеєні частини йдуть через «\\»)
    auto slashes = [](std::string p) { return replace_all(std::move(p), "\\", "/"); };
    CHECK(slashes(c.output_path) == "C:/videos/match.webm" && c.video_codec == "libx264");
    CHECK(has(evaluate(c, full_env()), "video.codec", Severity::Error));
    set_container(c, "png");
    CHECK(c.video_codec == "png");
    CHECK(slashes(c.output_path) == "C:/videos/match_frames/frame_%06d.png");
    CHECK(is_image_sequence(c));
    set_container(c, "mkv");
    CHECK(slashes(c.output_path) == "C:/videos/match.mkv");
    CHECK(replace_ext("C:/a/b.mp4", "mkv") == "C:/a/b.mkv");
    CHECK(replace_ext("", "mkv").empty());
}

void test_dependencies() {
    std::printf("[config: складники]\n");
    auto env = full_env();
    env.dubbing.omnivoice = {Availability::Unavailable, "не встановлено"};
    render::RenderSettings s = base_settings();
    auto find = [](const std::vector<DependencyStatus>& list, const std::string& id) -> const DependencyStatus* {
        for (const auto& d : list)
            if (d.id == id) return &d;
        return nullptr;
    };
    auto deps = dependencies(s, env);
    const DependencyStatus* d = find(deps, "tts.omnivoice");
    CHECK(d && d->state == DependencyState::Missing && !d->required);
    s.dub = true;
    s.dub_languages = "en";
    deps = dependencies(s, env);
    d = find(deps, "tts.omnivoice");
    CHECK(d && d->required && d->action == ActionId::InstallVoiceEngine);
    CHECK(d && format_dependency(*d).find("✗") == 0);
    d = find(deps, "game.standard");
    CHECK(d && d->required && d->state == DependencyState::Ready);
    d = find(deps, "game.rtx");
    CHECK(d && !d->required);
    d = find(deps, "whisper.cli");
    CHECK(d && d->required);
    CHECK(!find(deps, "translate.fake"));
    env.translation.providers["deepl"] = {Availability::Unavailable, "немає ключа API"};
    deps = dependencies(s, env);   // покажчик — у вектор, що живе далі
    d = find(deps, "translate.deepl");
    CHECK(d && d->state == DependencyState::NotConfigured && d->required);
}

// Властивість: виправлення справді прибирає помилку, яку пропонує виправити (перебір поєднань)
void test_fixes_converge() {
    std::printf("[config: виправлення прибирають помилку]\n");
    const auto env = full_env();
    int checked = 0;
    for (const char* ext : {"mp4", "mkv", "mov", "webm", "avi", "png"})
        for (const char* codec : {"libx264", "libx265", "libvpx-vp9", "prores_ks", "ffv1", "png", "h264_nvenc"})
            for (int depth : {8, 10, 12})
                for (int chroma : {420, 444})
                    for (const char* renderer : {"standard", "rtx"}) {
                        render::RenderSettings s = base_settings();
                        set_container(s, ext);
                        s.video_codec = codec;
                        s.bit_depth = depth;
                        s.chroma = chroma;
                        s.game_renderer = renderer;
                        s.parallel_games = 2;
                        const auto ctx = ctx_for(ValidationContext::Purpose::Render, 120);
                        const auto r = evaluate(s, env, ctx);
                        for (const auto& i : r.issues) {
                            if (i.severity != Severity::Error) continue;
                            for (const auto& f : i.fixes) {
                                const auto after = evaluate(apply_fix(s, f), env, ctx);
                                const bool gone = std::none_of(after.issues.begin(), after.issues.end(), [&](const Issue& x) {
                                    return x.rule == i.rule && x.message == i.message;
                                });
                                if (!gone)
                                    std::printf("    %s/%s/%d/%d/%s: «%s» → «%s» не допомогло\n", ext, codec, depth, chroma,
                                                renderer, i.message.c_str(), f.label.c_str());
                                CHECK(gone);
                                ++checked;
                            }
                        }
                    }
    CHECK(checked > 50);
}

} // namespace

// Рендер з несумісними налаштуваннями не запускає гру: помилка налаштувань ще до підготовки гри
void test_render_preflight(const std::filesystem::path& demo) {
    std::printf("[config: перевірка перед рендером]\n");
    const auto tmp = std::filesystem::temp_directory_path() / "gmdr_preflight_test";
    std::filesystem::create_directories(tmp);
    render::RenderSettings s;
    s.demo_path = path_to_utf8(demo);
    s.output_path = path_to_utf8(tmp / "out.webm");   // WebM не бере H.264 і AAC
    s.game_dir = path_to_utf8(tmp / "no_game");
    render::RenderJob job(s, nullptr, nullptr);
    job.start();
    job.wait();
    CHECK(job.state() == render::JobState::Failed);
    CHECK(job.settings_error());
    const auto issues = job.settings_issues();
    CHECK(std::any_of(issues.begin(), issues.end(), [](const Issue& i) { return i.rule == "video.codec"; }));
    CHECK(std::any_of(issues.begin(), issues.end(), [](const Issue& i) { return i.rule == "audio.codec"; }));
    CHECK(!std::filesystem::exists(tmp / "out.webm"));
    // Тестовий прогін у ручному режимі — теж помилка налаштувань (раніше ручний режим мовчки вимикався)
    s.output_path = path_to_utf8(tmp / "out.mp4");
    s.manual_mode = true;
    render::RenderJob test(s, nullptr, nullptr, true);
    test.start();
    test.wait();
    CHECK(test.settings_error());
    // Та сама перевірка для CLI: помилки, похідні з аналізу демо
    s.manual_mode = false;
    s.output_path = path_to_utf8(tmp / "out.webm");
    config::PreflightOptions po;
    const auto pf = config::preflight(s, po);
    CHECK(!pf.result.executable());
    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
}

void test_config() {
    test_catalog_and_migration();
    test_rules_basics();
    test_rules_renderer_parallel();
    test_rules_video();
    test_rules_audio_outputs();
    test_rules_ai();
    test_graphics_api_rule();
    test_presets();
    test_dependencies();
    test_fixes_converge();
}
