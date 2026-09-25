// =============================================================================
//  rules.cpp — правила перевірки налаштувань (див. constraints.hpp).
//
//  Кожне правило — невелика функція: читає налаштування, середовище і похідні
//  величини, додає зауваження (помилка / попередження / інформація) з поясненням
//  і виправленнями, і позначає стан налаштувань (сховати, вимкнути, які значення
//  доступні). Правила згруповано за темами; порядок — як у списку rules().
//
//  Нове правило: функція тут + рядок у rules() + тест у tests/test_config.cpp.
// =============================================================================
#include "constraints_internal.hpp"
#include "formats.hpp"

#include "../dub/dub_mix.hpp"
#include "../dub/tts.hpp"
#include "../game/game_renderer.hpp"
#include "../media/ffmpeg_util.hpp"
#include "../media/muxer.hpp"
#include "../media/video_encoder.hpp"
#include "../render/dubbing.hpp"
#include "../render/jobs.hpp"
#include "../render/versions.hpp"
#include "../translate/translate.hpp"
#include "../util/file_util.hpp"
#include "../util/i18n.hpp"
#include "../util/strings.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>
#include <set>

extern "C" {
#include <libavfilter/avfilter.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
}

namespace gmdr::config::detail {

namespace {

using S = SettingId;
using Sev = Severity;
using K = IssueKind;

std::string fmt_bytes_gb(uint64_t b) { return std::format("{:.1f}", static_cast<double>(b) / (1024.0 * 1024 * 1024)); }

// Контекстна серйозність: під час редагування — м'якше (ще не запускали), перед виконанням — як є
Sev when_executing(const RuleContext& c, Sev exec, Sev edit) { return c.executing() ? exec : edit; }

void range_check(RuleContext& c, S id, double v, double lo, double hi, double fix_to, const std::string& what) {
    auto& st = c.state(id);
    st.min = lo;
    st.max = hi;
    if (v < lo || v > hi || !std::isfinite(v)) {
        auto& i = c.add(Sev::Error, K::Range, id, trf("{}: {} — поза межами {}…{}", what, v, lo, hi));
        i.fixes.push_back(fix(trf("Поставити {}", fix_to), {change(id, num(fix_to))}));
    }
}

// Параметр кодека FFmpeg, у який іде пресет швидкості (media::VideoEncoder::open)
const char* preset_option(const std::string& codec) {
    if (codec == "libaom-av1") return "cpu-used";
    if (codec == "librav1e") return "speed";
    if (codec == "libvpx-vp9" || codec == "libvpx") return "deadline";
    if (codec.find("_amf") != std::string::npos) return "quality";
    return "preset";
}

// Чи прийме кодек це значення параметра: true/false — FFmpeg перелічує значення (іменовані константи
// чи числовий діапазон), nullopt — довільний рядок, перевірить лише сам кодек
std::optional<bool> ffmpeg_accepts(const std::string& codec, const char* option, const std::string& value) {
    const AVCodec* c = avcodec_find_encoder_by_name(codec.c_str());
    if (!c || !c->priv_class) return std::nullopt;
    const AVClass* cls = c->priv_class;
    const AVOption* o = av_opt_find(&cls, option, nullptr, 0, AV_OPT_SEARCH_FAKE_OBJ);
    if (!o) return false;
    if (o->type == AV_OPT_TYPE_STRING) return std::nullopt;
    if (o->unit)
        for (const AVOption* k = nullptr; (k = av_opt_next(&cls, k));)
            if (k->type == AV_OPT_TYPE_CONST && k->unit && std::string(k->unit) == o->unit && value == k->name) return true;
    if (const auto v = parse_double(value)) return *v >= o->min && *v <= o->max;
    return false;
}

// Назва без пояснення після тире: «H.264 (x264) — найсумісніший» → «H.264 (x264)»
std::string short_label(std::string label) {
    const size_t dash = label.find(" — ");
    if (dash != std::string::npos) label.resize(dash);
    return label;
}

// Пояснення зі стану середовища («немає в цій збірці FFmpeg») — окремим реченням
std::string reason(const std::string& detail) { return detail.empty() ? detail : trf("Причина: {}.", detail); }

std::string codec_label(const std::string& name) {
    // У GPU-кодеків після тире — виробник (H.264 — NVIDIA NVENC), його лишаємо
    if (const VideoEncoderInfo* e = find_video_encoder(name)) return e->gpu ? tr(e->label.c_str()) : short_label(tr(e->label.c_str()));
    return name;
}

std::string audio_label(const std::string& name) {
    if (const AudioEncoderInfo* e = find_audio_encoder(name)) return short_label(tr(e->label.c_str()));
    return name;
}

// ============================== Джерело ==============================

void r_source_demo(RuleContext& c) {
    if (c.s.demo_path.empty()) {
        auto& i = c.add(Sev::Error, K::Dependency, S::demo_path, tr("Демо не відкрито."),
                        tr("Відкрийте файл .dem — з нього рендериться відео."));
        i.action = ActionId::OpenDemo;
        return;
    }
    if (c.env.filesystem.demo.state == Availability::Unavailable) {
        auto& i = c.add(Sev::Error, K::Availability, S::demo_path, trf("Файлу демо немає: {}", c.s.demo_path),
                        tr("Його перенесли чи видалили — відкрийте демо знову."));
        i.action = ActionId::OpenDemo;
    }
}

void r_source_range(RuleContext& c) {
    const auto whole = fix(tr("Увесь запис"), {change(S::start_tick, num(0)), change(S::end_tick, num(-1))});
    if (c.s.start_tick < 0) {
        auto& i = c.add(Sev::Error, K::Range, S::start_tick, tr("Початок фрагмента від'ємний."));
        i.fixes.push_back(whole);
    }
    if (c.s.end_tick > 0 && c.s.start_tick >= c.s.end_tick) {
        auto& i = c.add(Sev::Error, K::Conflict, S::end_tick, tr("Кінець фрагмента не пізніше за початок."), {},
                        {S::start_tick});
        i.fixes.push_back(whole);
        return;
    }
    if (!c.ctx.demo_known) return;
    c.state(S::start_tick).min = 0;
    c.state(S::start_tick).max = std::max(0, c.ctx.last_tick - 1);
    c.state(S::end_tick).min = -1;
    c.state(S::end_tick).max = c.ctx.last_tick;
    if (c.s.start_tick >= c.ctx.last_tick) {
        auto& i = c.add(Sev::Error, K::Range, S::start_tick, tr("Початок фрагмента — після кінця демо."));
        i.fixes.push_back(whole);
    } else if (c.d().video_frames <= 0 && c.d().fps > 0) {
        auto& i = c.add(Sev::Error, K::Range, S::end_tick, tr("Фрагмент коротший за один кадр відео."), {}, {S::start_tick});
        i.fixes.push_back(whole);
    }
}

// ============================== Гра і рендерер ==============================

void r_game_renderer(RuleContext& c) {
    const game::GameRenderer* r = game::find_game_renderer(c.s.game_renderer);
    auto& st = c.state(S::game_renderer);
    for (const auto* x : game::game_renderers()) {
        OptionState o;
        o.value = x->id();
        o.label = x->label();
        const GameInstallInfo* inst = c.env.game.find(x->id());
        if (inst && inst->install.state == Availability::Unavailable) {
            o.warning = true;
            o.reason = inst->install.detail;
        }
        st.options.push_back(std::move(o));
    }
    if (!r) {
        auto& i = c.add(Sev::Error, K::Availability, S::game_renderer, trf("Невідомий рендерер гри «{}».", c.s.game_renderer));
        i.fixes.push_back(fix(game::standard_renderer().label(), {change(S::game_renderer, str(game::standard_renderer().id()))}));
    }
    // Папка копії гри: видно лише поле вибраного рендерера
    const std::string& dir_key = render::renderer_of(c.s).traits().dir_setting;
    for (const auto* x : game::game_renderers()) {
        const std::string& key = x->traits().dir_setting;
        if (key == dir_key) continue;
        if (auto id = find_setting(key)) c.hide(*id, trf("для рендерера «{}»", x->label()));
    }
}

void r_game_install(RuleContext& c) {
    const game::GameRenderer& R = render::renderer_of(c.s);
    const GameInstallInfo* inst = c.env.game.find(R.id());
    const S dir = find_setting(R.traits().dir_setting).value_or(S::game_dir);
    if (!inst || inst->install.state == Availability::Unknown) return;   // не перевіряли (CLI без пошуку)
    if (inst->install.state != Availability::Available) {
        std::string why = R.install_url().empty() ? std::string() : trf("Де взяти: {}", R.install_url());
        auto& i = c.add(Sev::Error, K::External, dir, R.not_found_message(), why, {S::game_renderer});
        i.action = ActionId::DetectGame;
        if (inst->from_settings) i.fixes.push_back(fix(tr("Шукати автоматично"), {change(dir, str(""))}));
        return;
    }
    if (R.traits().needs_64bit && !inst->has_64bit)
        c.add(Sev::Warning, K::Renderer, dir, trf("{} потребує 64-бітної гри (гілка x86-64) — у цій папці її немає.", R.label()));
    if (!inst->accepted)
        c.add(Sev::Warning, K::Renderer, dir,
              trf("Схоже, це не копія для «{}»: у папці гри немає того, що їй потрібно.", R.label()),
              R.description());
    if (inst->driver == "outdated")
        c.add(Sev::Info, K::Consequence, dir, tr("Драйвер рендеру в грі застарів — програма оновить його перед рендером."));
}

void r_game_running(RuleContext& c) {
    // У черзі гру свідомо лишає відкритою попередній пункт
    if (c.ctx.purpose == ValidationContext::Purpose::QueueItem) return;
    if (c.env.game.running.state != Availability::Available) return;
    auto& i = c.add(when_executing(c, Sev::Error, Sev::Warning), K::Runtime, S::Count,
                    tr("Garry's Mod уже запущено."), tr("Закрийте гру — програма запустить її сама з потрібними параметрами."));
    i.action = ActionId::CloseGame;
}

void r_game_window(RuleContext& c) {
    const game::GameRenderer& R = render::renderer_of(c.s);
    auto& st = c.state(S::game_window);
    const std::pair<const char*, const char*> modes[] = {{"offscreen", N_("За межами екрана (гра не видна)")},
                                                         {"behind", N_("Позаду інших вікон")},
                                                         {"normal", N_("На екрані (як раніше)")}};
    for (const auto& [v, l] : modes) {
        OptionState o{v, tr(l)};
        if (std::string(v) == "offscreen" && !R.traits().offscreen_window) {
            o.available = false;
            o.reason = trf("{}: за межами екрана кадри чорні", R.label());
        }
        st.options.push_back(std::move(o));
    }
    if (c.s.manual_mode) {
        c.disable(S::game_window, tr("у ручному режимі гра на екрані"));
        return;
    }
    if (c.s.game_window != "offscreen" && c.s.game_window != "behind" && c.s.game_window != "normal") {
        auto& i = c.add(Sev::Error, K::Range, S::game_window, trf("Невідоме положення вікна гри «{}».", c.s.game_window));
        i.fixes.push_back(fix(tr("Позаду інших вікон"), {change(S::game_window, str("behind"))}));
    } else if (c.s.game_window == "offscreen" && !R.traits().offscreen_window) {
        auto& i = c.add(Sev::Info, K::Renderer, S::game_window,
                        trf("{}: вікно гри буде позаду інших, а не за межами екрана.", R.label()),
                        tr("За межами екрана Remix віддає чорні кадри."), {S::game_renderer});
        i.fixes.push_back(fix(tr("Позаду інших вікон"), {change(S::game_window, str("behind"))}));
        st.note = trf("Обмежено: {}", R.label());
    }
}

void r_game_render_size(RuleContext& c) {
    if ((c.s.render_width > 0) != (c.s.render_height > 0)) {
        auto& i = c.add(Sev::Warning, K::Conflict, S::render_width, tr("Розмір вікна гри задано лише наполовину — буде як відео."),
                        {}, {S::render_height});
        i.fixes.push_back(fix(tr("Як відео"), {change(S::render_width, num(0)), change(S::render_height, num(0))}));
    }
    if (c.s.render_width > 16384 || c.s.render_height > 16384) {
        auto& i = c.add(Sev::Error, K::Range, S::render_width, tr("Вікно гри більше за 16384 пікселі."), {}, {S::render_height});
        i.fixes.push_back(fix(tr("Як відео"), {change(S::render_width, num(0)), change(S::render_height, num(0))}));
    } else if (c.d().game_width > c.d().width && c.d().game_width > 0) {
        c.add(Sev::Info, K::Consequence, S::render_width,
              trf("Гра рендерить {}×{}, кадри зменшуються до {}×{} (згладжування).", c.d().game_width, c.d().game_height,
                  c.d().width, c.d().height),
              tr("Вікно, більше за монітор, гра може не дозволити — тоді кадри просто масштабуються."));
    }
}

void r_game_capture(RuleContext& c) {
    if (c.s.capture_format != "tga" && c.s.capture_format != "jpg") {
        auto& i = c.add(Sev::Error, K::Range, S::capture_format, trf("Невідомий формат кадрів гри «{}».", c.s.capture_format));
        i.fixes.push_back(fix(tr("TGA (без втрат)"), {change(S::capture_format, str("tga"))}));
    }
    auto& st = c.state(S::capture_format);
    st.options = {{"tga", tr("TGA (без втрат)")}, {"jpg", tr("JPEG (менше диску)")}};
    if (c.s.capture_format != "jpg") c.hide(S::jpeg_quality, tr("лише для JPEG"));
    else {
        range_check(c, S::jpeg_quality, c.s.jpeg_quality, 50, 100, 95, tr("Якість JPEG"));
        c.add(Sev::Info, K::Consequence, S::capture_format, tr("Кадри з гри — JPEG: менше даних, але з втратами якості."));
    }
}

void r_game_transport(RuleContext& c) {
    const std::string& t = c.s.frame_transport;
    auto& st = c.state(S::frame_transport);
    st.options = {{"auto", tr("напряму, без файлів")}, {"files", tr("файлами на диску")}, {"pipe", tr("лише каналом")}};
    if (c.env.game.frame_pipes.state == Availability::Unavailable)
        for (auto& o : st.options)
            if (o.value != "files") {
                o.available = false;
                o.reason = c.env.game.frame_pipes.detail;
            }
    if (t != "auto" && t != "pipe" && t != "files") {
        auto& i = c.add(Sev::Error, K::Range, S::frame_transport, trf("Невідомий спосіб передачі кадрів «{}».", t));
        i.fixes.push_back(fix(tr("напряму, без файлів"), {change(S::frame_transport, str("auto"))}));
    } else if (t == "pipe" && c.env.game.frame_pipes.state == Availability::Unavailable) {
        auto& i = c.add(Sev::Error, K::Platform, S::frame_transport, tr("Передача кадрів каналом тут недоступна."),
                        reason(c.env.game.frame_pipes.detail));
        i.fixes.push_back(fix(tr("напряму, без файлів"), {change(S::frame_transport, str("auto"))}));
    }
    if (c.s.manual_mode) c.disable(S::frame_transport, tr("у ручному режимі кадри йдуть файлами"));
    if (t != "files") c.hide(S::max_pending_frames, tr("каналом кадри на диску не накопичуються"));
    else range_check(c, S::max_pending_frames, c.s.max_pending_frames, 16, 600, 90, tr("Черга кадрів на диску"));
}

void r_game_manual(RuleContext& c) {
    if (!c.s.manual_mode) return;
    using P = ValidationContext::Purpose;
    if (c.ctx.purpose == P::TestRun) {
        auto& i = c.add(Sev::Error, K::Conflict, S::manual_mode, tr("У ручному режимі тестовий прогін неможливий."),
                        tr("Запис вмикаєте ви самі (gmdr_start / gmdr_stop у консолі гри)."));
        i.fixes.push_back(fix(tr("Вимкнути ручний режим"), {change(S::manual_mode, flag(false))}));
    } else if (c.ctx.purpose == P::QueueItem) {
        auto& i = c.add(Sev::Error, K::Conflict, S::manual_mode, tr("Ручний режим не можна поставити в чергу."));
        i.fixes.push_back(fix(tr("Вимкнути ручний режим"), {change(S::manual_mode, flag(false))}));
    } else {
        c.add(Sev::Info, K::Consequence, S::manual_mode,
              tr("Ручний режим: запис вмикаєте ви самі (gmdr_start / gmdr_stop у консолі гри); тестовий прогін і черга недоступні."));
    }
}

void r_game_misc(RuleContext& c) {
    range_check(c, S::menu_delay, c.s.menu_delay, 0, 120, 3, tr("Затримка меню"));
    if (c.env.filesystem.game_free > 0 && c.env.filesystem.game_free < (3ull << 30))
        c.add(Sev::Warning, K::Resource, S::max_pending_frames,
              trf("На диску з грою вільно лише {} ГБ.", fmt_bytes_gb(c.env.filesystem.game_free)),
              tr("Якщо кадри підуть файлами, тимчасові кадри можуть заповнити диск — звільніть місце."));
}

// ============================== Паралельний рендер ==============================

void r_parallel(RuleContext& c) {
    const game::GameRenderer& R = render::renderer_of(c.s);
    const int max = c.d().parallel_max;
    auto& st = c.state(S::parallel_games);
    st.min = 1;
    st.max = max;
    for (int n = 1; n <= 4; ++n) {
        OptionState o{std::to_string(n), std::format("{}×", n)};
        if (n > max) {
            o.available = false;
            o.reason = max == 1 ? trf("{} рендерить однією копією гри", R.label())
                                : trf("{} рендерить не більше {} копій гри", R.label(), max);
        }
        o.recommended = n == 1;
        st.options.push_back(std::move(o));
    }
    if (max < 4) st.note = trf("Обмежено: {}", R.label());
    if (c.s.parallel_games < 1 || c.s.parallel_games > 4) {
        auto& i = c.add(Sev::Error, K::Range, S::parallel_games, trf("Копій гри: {} — можна від 1 до 4.", c.s.parallel_games));
        i.fixes.push_back(fix(std::format("{}×", std::clamp(c.s.parallel_games, 1, max)),
                              {change(S::parallel_games, num(std::clamp(c.s.parallel_games, 1, max)))}));
        return;
    }
    if (c.s.parallel_games > max) {
        auto& i = c.add(Sev::Error, K::Renderer, S::parallel_games,
                        trf("Паралельний рендер: {}× недоступно.", c.s.parallel_games),
                        max == 1 ? trf("Вибраний рендерер гри ({}) рендерить лише однією копією гри одночасно.", R.label())
                                 : trf("Вибраний рендерер гри ({}) рендерить не більше {} копій гри одночасно.", R.label(), max),
                        {S::game_renderer});
        i.fixes.push_back(fix(std::format("{}×", max), {change(S::parallel_games, num(max))}));
        return;
    }
    if (c.s.parallel_games > 1 && c.d().parallel == 1) {
        // Рендер піде однією копією: чому — у похідних
        const bool actionable = c.s.manual_mode || c.d().parallel_reason == tr("формат файлу — лише MP4, MOV, MKV або WebM");
        auto& i = c.add(actionable ? Sev::Warning : Sev::Info, K::Consequence, S::parallel_games,
                        trf("Рендерить одна копія гри: {}.", c.d().parallel_reason));
        i.fixes.push_back(fix("1×", {change(S::parallel_games, num(1))}));
        if (c.d().parallel_reason == tr("формат файлу — лише MP4, MOV, MKV або WebM") && !c.d().image_sequence)
            i.fixes.push_back(fix(tr("Формат MKV"), {change(S::container, str("mkv")),
                                                    change(S::output_path, str(replace_ext(c.s.output_path, "mkv")))}));
    }
}

void r_parallel_resources(RuleContext& c) {
    const int n = c.d().parallel;
    if (n < 2) return;
    constexpr uint64_t per_copy = 1536ull << 20;   // ≈1,5 ГБ пам'яті на копію гри (виміряно на тестовому ПК)
    const uint64_t ram = c.env.hardware.ram_bytes;
    if (ram > 0 && per_copy * static_cast<uint64_t>(n) + (2ull << 30) > ram)
        c.add(Sev::Warning, K::Resource, S::parallel_games,
              trf("{} копії гри можуть не вміститися в пам'ять ({} ГБ).", n, fmt_bytes_gb(ram)),
              tr("Кожна копія займає ≈1–2 ГБ пам'яті; забракне — гра впаде або все сповільниться."));
    uint64_t vram = 0;
    for (const auto& g : c.env.hardware.gpus)
        if (!g.software) vram = std::max(vram, g.vram_bytes);
    if (vram > 0 && per_copy * static_cast<uint64_t>(n) > vram)
        c.add(Sev::Warning, K::Resource, S::parallel_games,
              trf("{} копії гри можуть не вміститися у відеопам'ять ({} ГБ).", n, fmt_bytes_gb(vram)),
              tr("Кожна копія займає ≈1–2 ГБ відеопам'яті."));
}

// ============================== Відео ==============================

void r_video_size(RuleContext& c) {
    auto& st = c.state(S::width);
    for (const auto& r : resolutions()) st.options.push_back({std::format("{}x{}", r.w, r.h), tr(r.label.c_str())});
    const auto clamp_fix = fix(tr("У межах 16…16384"), {change(S::width, num(std::clamp(c.s.width, 16, 16384))),
                                                        change(S::height, num(std::clamp(c.s.height, 16, 16384)))});
    for (S id : {S::width, S::height}) {
        c.state(id).min = 16;
        c.state(id).max = 16384;
    }
    if (c.s.width < 16 || c.s.width > 16384 || c.s.height < 16 || c.s.height > 16384) {
        auto& i = c.add(Sev::Error, K::Range, S::width, trf("Розмір кадру {}×{} — поза межами 16…16384.", c.s.width, c.s.height),
                        {}, {S::height});
        i.fixes.push_back(clamp_fix);
        return;
    }
    if (c.d().width != c.s.width || c.d().height != c.s.height) {
        auto& i = c.add(Sev::Info, K::Consequence, S::width,
                        trf("Кадр буде {}×{}: формат пікселів {} вимагає парних розмірів.", c.d().width, c.d().height, c.d().pix_fmt),
                        {}, {S::height, S::video_codec});
        i.fixes.push_back(fix(std::format("{}×{}", c.d().width, c.d().height),
                              {change(S::width, num(c.d().width)), change(S::height, num(c.d().height))}));
    }
    if (static_cast<int64_t>(c.d().width) * c.d().height > 3840LL * 2160 * 2)
        c.add(Sev::Warning, K::Resource, S::width, tr("Кадр більший за 8K: гра може не створити таке вікно, а рендер буде дуже довгим."));
}

void r_video_fps(RuleContext& c) {
    auto& st = c.state(S::fps);
    for (const auto& f : frame_rates()) st.options.push_back({f, f});
    if (c.d().fps <= 0) {
        auto& i = c.add(Sev::Error, K::Range, S::fps, trf("Неправильна частота кадрів «{}».", c.s.fps),
                        tr("Приклади: 60, 59.94, 60000/1001."));
        i.fixes.push_back(fix("60", {change(S::fps, str("60"))}));
    } else if (c.d().fps > 1000) {
        auto& i = c.add(Sev::Error, K::Range, S::fps, trf("Частота кадрів {} — більше за 1000.", c.s.fps));
        i.fixes.push_back(fix("240", {change(S::fps, str("240"))}));
    }
}

void r_video_motion(RuleContext& c) {
    range_check(c, S::motion_blur, c.s.motion_blur, 1, 256, 1, tr("Розмиття руху"));
    if (c.s.motion_blur <= 1) c.hide(S::shutter, tr("лише з розмиттям руху"));
    else range_check(c, S::shutter, c.s.shutter, 1, 360, 180, tr("Кут затвора"));
    range_check(c, S::speed, c.s.speed, 0.1, 16, 1, tr("Швидкість відео"));
    auto& sp = c.state(S::speed);
    for (double v : speeds()) sp.options.push_back({std::format("{:g}", v), std::format("×{:g}", v)});
    if (std::abs(c.s.speed - 1.0) < 1e-6) {
        c.hide(S::speed_audio, tr("лише при зміні швидкості"));
    } else if (c.s.speed_audio != "stretch" && c.s.speed_audio != "mute") {
        auto& i = c.add(Sev::Error, K::Range, S::speed_audio, trf("Невідомий режим звуку «{}».", c.s.speed_audio));
        i.fixes.push_back(fix(tr("розтягнути"), {change(S::speed_audio, str("stretch"))}));
    } else if (c.s.speed_audio == "mute") {
        c.add(Sev::Info, K::Consequence, S::speed_audio, tr("Відео буде без звуку (зміна швидкості, звук вимкнено)."), {},
              {S::speed});
    }
    c.state(S::speed_audio).options = {{"stretch", tr("розтягнути")}, {"mute", tr("без звуку")}};
    // Навантаження: скільки кадрів має відрендерити гра
    const Derived& d = c.d();
    if (d.fps > 0 && (d.subframes > 1 || d.speed < 1)) {
        std::string msg = trf("Гра рендеритиме {:.0f} кадрів/с часу демо ({} на кожен кадр відео).", d.game_fps,
                              d.subframes / d.speed >= 10 ? std::format("{:.0f}", d.subframes / d.speed)
                                                          : std::format("{:.2g}", d.subframes / d.speed));
        if (d.game_frames > 0) msg += " " + trf("Усього кадрів гри: {}.", d.game_frames);
        c.add(Sev::Info, K::Consequence, S::motion_blur, msg, {}, {S::fps, S::speed});
    }
    if (d.subframes > 32 || (d.subframes > 8 && static_cast<int64_t>(d.width) * d.height >= 3840LL * 2160))
        c.add(Sev::Warning, K::Resource, S::motion_blur,
              trf("{} під-кадрів на кадр{} — рендер буде у {} разів довшим.", d.subframes,
                  static_cast<int64_t>(d.width) * d.height >= 3840LL * 2160 ? tr(" у 4K") : "", d.subframes),
              tr("8–16 під-кадрів зазвичай досить для плавного розмиття."));
}

void r_video_container(RuleContext& c) {
    auto& st = c.state(S::container);
    for (const auto& ci : containers()) {
        OptionState o{ci.ext, tr(ci.label.c_str())};
        const std::string probe = "x." + ci.ext;
        if (!av_guess_format(nullptr, probe.c_str(), nullptr)) {
            o.available = false;
            o.reason = tr("немає в цій збірці FFmpeg");
        }
        st.options.push_back(std::move(o));
    }
    if (!find_container(c.d().container)) {
        auto& i = c.add(Sev::Error, K::Availability, S::container, trf("Невідомий формат файлу «{}».", c.d().container));
        i.fixes.push_back(fix("MP4", {change(S::container, str("")), change(S::output_path, str(replace_ext(c.s.output_path, "mp4")))}));
    }
}

// Кодек, який підійде для контейнера (для виправлень)
std::string codec_for_container(const RuleContext& c, const std::string& ext) {
    const ContainerInfo* ci = find_container(ext);
    if (ci && !ci->image_codec.empty()) return ci->image_codec;
    for (const char* cand : {"libx264", "libvpx-vp9", "libsvtav1", "prores_ks", "ffv1"})
        if (media::container_supports(ext, cand) > 0 && c.env.video_encoder(cand).ok()) return cand;
    return {};
}

void r_video_codec(RuleContext& c) {
    const std::string& ext = c.d().container;
    const ContainerInfo* ci = find_container(ext);
    const bool image_seq = ci && !ci->image_codec.empty();
    auto& st = c.state(S::video_codec);
    for (const auto& e : video_encoders()) {
        const bool image_codec = e.name == "tiff" || e.name == "bmp" || e.name == "mjpeg";
        if (image_seq ? e.name != ci->image_codec : image_codec) continue;
        OptionState o{e.name, tr(e.label.c_str())};
        o.group = e.gpu ? tr("Відеокарта (GPU)") : tr("Процесор (CPU)");
        const CapabilityState cap = c.env.video_encoder(e.name);
        if (cap.state == Availability::Unavailable || cap.state == Availability::Failed) {
            o.available = false;
            o.reason = cap.detail;
        } else if (cap.state == Availability::Unknown || cap.state == Availability::Checking) {
            o.warning = e.gpu;
            o.reason = e.gpu ? tr("перевіряється на цій відеокарті...") : std::string();
        }
        if (o.available && !image_seq && media::container_supports(ext, e.name) == 0) {
            o.available = false;
            o.reason = trf("не для .{}", ext);
        }
        st.options.push_back(std::move(o));
    }
    if (c.d().image_sequence && !image_seq) return;   // шлях з % без відомого формату — окреме правило
    // Послідовність кадрів: кодек заданий форматом
    if (image_seq) {
        if (c.s.video_codec != ci->image_codec) {
            auto& i = c.add(Sev::Error, K::Conflict, S::video_codec,
                            trf("Кадри .{} кодуються лише кодеком {}.", ext, ci->image_codec), {}, {S::container});
            i.fixes.push_back(fix(codec_label(ci->image_codec), {change(S::video_codec, str(ci->image_codec))}));
        }
        return;
    }
    const CapabilityState cap = c.env.video_encoder(c.s.video_codec);
    const std::string cpu = cpu_equivalent(c.s.video_codec);
    auto cpu_fix = [&](Issue& i) {
        if (!cpu.empty() && cpu != c.s.video_codec && c.env.video_encoder(cpu).ok())
            i.fixes.push_back(fix(codec_label(cpu), {change(S::video_codec, str(cpu)), change(S::preset, str("")),
                                                     change(S::quality, num(-1))}));
        else if (const std::string alt = codec_for_container(c, ext); !alt.empty() && alt != c.s.video_codec)
            i.fixes.push_back(fix(codec_label(alt), {change(S::video_codec, str(alt)), change(S::preset, str("")),
                                                     change(S::quality, num(-1))}));
    };
    if (cap.state == Availability::Unavailable) {
        auto& i = c.add(Sev::Error, K::Availability, S::video_codec, trf("Відеокодек {} недоступний.", codec_label(c.s.video_codec)),
                        reason(cap.detail));
        cpu_fix(i);
        return;
    }
    if (cap.state == Availability::Failed) {
        auto& i = c.add(Sev::Error, K::Availability, S::video_codec,
                        trf("{} не працює на цій відеокарті.", codec_label(c.s.video_codec)), reason(cap.detail));
        cpu_fix(i);
        return;
    }
    if ((cap.state == Availability::Unknown || cap.state == Availability::Checking) && find_video_encoder(c.s.video_codec) &&
        find_video_encoder(c.s.video_codec)->gpu) {
        c.add(when_executing(c, Sev::Warning, Sev::Info), K::Runtime, S::video_codec,
              trf("{} ще не перевірено на цій відеокарті.", codec_label(c.s.video_codec)),
              tr("Якщо кодек не відкриється, рендер зупиниться на першому кадрі — спершу зробіть тестовий прогін."));
    }
    if (media::container_supports(ext, c.s.video_codec) == 0) {
        auto& i = c.add(Sev::Error, K::Conflict, S::video_codec,
                        trf("Формат .{} не підтримує кодек {}.", ext, codec_label(c.s.video_codec)), {}, {S::container});
        if (media::container_supports("mkv", c.s.video_codec) > 0)
            i.fixes.push_back(fix(tr("Формат MKV"), {change(S::container, str("")),
                                                    change(S::output_path, str(replace_ext(c.s.output_path, "mkv")))}));
        if (const std::string alt = codec_for_container(c, ext); !alt.empty())
            i.fixes.push_back(fix(codec_label(alt), {change(S::video_codec, str(alt)), change(S::preset, str("")),
                                                     change(S::quality, num(-1))}));
    }
}

void r_video_pixel_format(RuleContext& c) {
    const AVCodec* codec = avcodec_find_encoder_by_name(c.s.video_codec.c_str());
    if (!codec) return;
    // Які бітність і субдискретизація справді вийдуть (той самий вибір, що зробить кодер)
    auto effective = [&](int depth, int chroma) {
        const AVPixelFormat f = media::choose_pix_fmt(codec, depth, chroma, "auto");
        return std::pair<int, int>{media::pix_fmt_bit_depth(f), media::pix_fmt_chroma(f)};
    };
    auto& bd = c.state(S::bit_depth);
    for (int depth : {8, 10, 12}) {
        OptionState o{std::to_string(depth), trf("{} біт", depth)};
        if (effective(depth, c.s.chroma).first < depth) {
            o.available = false;
            o.reason = trf("{} не вміє {} біт", codec_label(c.s.video_codec), depth);
        }
        bd.options.push_back(std::move(o));
    }
    auto& ch = c.state(S::chroma);
    for (const auto& [v, l] : {std::pair{420, "4:2:0"}, std::pair{422, "4:2:2"}, std::pair{444, "4:4:4"}}) {
        OptionState o{std::to_string(v), l};
        const auto [d8, c8] = effective(8, v);
        const auto [d10, c10] = effective(10, v);
        if (c8 < v && c10 < v) {
            o.available = false;
            o.reason = trf("{} не вміє {}", codec_label(c.s.video_codec), l);
        }
        (void)d8;
        (void)d10;
        ch.options.push_back(std::move(o));
    }
    if (c.s.bit_depth != 8 && c.s.bit_depth != 10 && c.s.bit_depth != 12) {
        auto& i = c.add(Sev::Error, K::Range, S::bit_depth, trf("Бітність {} — можна 8, 10 або 12.", c.s.bit_depth));
        i.fixes.push_back(fix(tr("8 біт"), {change(S::bit_depth, num(8))}));
    }
    if (c.s.chroma != 420 && c.s.chroma != 422 && c.s.chroma != 444) {
        auto& i = c.add(Sev::Error, K::Range, S::chroma, trf("Субдискретизація {} — можна 420, 422 або 444.", c.s.chroma));
        i.fixes.push_back(fix("4:2:0", {change(S::chroma, num(420))}));
    }
    // Явний формат пікселів
    const bool forced = !c.s.pix_fmt.empty() && c.s.pix_fmt != "auto";
    if (forced) {
        const AVPixelFormat f = av_get_pix_fmt(c.s.pix_fmt.c_str());
        const auto supported = media::codec_pix_fmts(codec);
        if (f == AV_PIX_FMT_NONE) {
            auto& i = c.add(Sev::Error, K::Range, S::pix_fmt, trf("Невідомий формат пікселів «{}».", c.s.pix_fmt));
            i.fixes.push_back(fix(tr("Автоматично"), {change(S::pix_fmt, str("auto"))}));
        } else if (!supported.empty() && std::find(supported.begin(), supported.end(), f) == supported.end()) {
            auto& i = c.add(Sev::Error, K::Conflict, S::pix_fmt,
                            trf("{} не підтримує формат пікселів {}.", codec_label(c.s.video_codec), c.s.pix_fmt), {},
                            {S::video_codec});
            i.fixes.push_back(fix(tr("Автоматично"), {change(S::pix_fmt, str("auto"))}));
        }
        c.disable(S::bit_depth, tr("задано формат пікселів"));
        c.disable(S::chroma, tr("задано формат пікселів"));
        return;
    }
    // Раніше кодер мовчки брав найближчий формат — тепер це видно до рендеру
    if ((c.s.bit_depth == 8 || c.s.bit_depth == 10 || c.s.bit_depth == 12) && c.d().bit_depth < c.s.bit_depth) {
        auto& i = c.add(Sev::Error, K::Conflict, S::bit_depth,
                        trf("{} не вміє {} біт — вийшло б {}.", codec_label(c.s.video_codec), c.s.bit_depth, c.d().bit_depth), {},
                        {S::video_codec});
        i.fixes.push_back(fix(trf("{} біт", c.d().bit_depth), {change(S::bit_depth, num(c.d().bit_depth))}));
    }
    if ((c.s.chroma == 422 || c.s.chroma == 444) && c.d().chroma < c.s.chroma) {
        auto& i = c.add(Sev::Error, K::Conflict, S::chroma,
                        trf("{} не вміє {} — вийшло б {}.", codec_label(c.s.video_codec),
                            c.s.chroma == 444 ? "4:4:4" : "4:2:2", c.d().chroma == 422 ? "4:2:2" : "4:2:0"),
                        {}, {S::video_codec});
        i.fixes.push_back(fix(c.d().chroma == 422 ? "4:2:2" : "4:2:0", {change(S::chroma, num(c.d().chroma))}));
    }
    if (c.d().bit_depth > 8 && find_video_encoder(c.s.video_codec) && find_video_encoder(c.s.video_codec)->family == "h264")
        c.add(Sev::Info, K::Consequence, S::bit_depth, tr("10-бітний H.264 відтворюють не всі програвачі і браузери."));
}

void r_video_quality(RuleContext& c) {
    const media::QualityInfo qi = media::quality_info_for(c.s.video_codec);
    auto& q = c.state(S::quality);
    if (qi.param.empty()) {
        c.hide(S::quality, tr("кодек без керування якістю (без втрат)"));
        c.hide(S::video_bitrate, tr("кодек без керування якістю (без втрат)"));
        c.hide(S::target_size_mb, tr("кодек без керування якістю (без втрат)"));
    } else {
        q.min = qi.min;
        q.max = qi.max;
        q.note = qi.param;
        if (qi.param == "profile") {
            const char* names[] = {"0 — Proxy", "1 — LT", "2 — Standard", "3 — HQ", "4 — 4444", "5 — 4444 XQ"};
            for (int k = 0; k < 6; ++k) q.options.push_back({std::to_string(k), names[k]});
            c.hide(S::video_bitrate, tr("ProRes: якість задає профіль"));
            c.hide(S::target_size_mb, tr("ProRes: якість задає профіль"));
        }
        if (c.s.quality != -1 && (c.s.quality < qi.min || c.s.quality > qi.max)) {
            auto& i = c.add(Sev::Error, K::Range, S::quality,
                            trf("Якість {} — для {} можна {}…{}.", c.s.quality, codec_label(c.s.video_codec), qi.min, qi.max), {},
                            {S::video_codec});
            i.fixes.push_back(fix(tr("Типова"), {change(S::quality, num(-1))}));
        }
        if (qi.param != "profile" && (c.s.target_size_mb > 0 || !c.s.video_bitrate.empty()))
            c.disable(S::quality, c.s.target_size_mb > 0 ? tr("задано розмір файлу") : tr("задано бітрейт"));
    }
    // Бітрейт
    if (!c.s.video_bitrate.empty() && !parse_bitrate(c.s.video_bitrate)) {
        auto& i = c.add(Sev::Error, K::Range, S::video_bitrate, trf("Неправильний бітрейт «{}».", c.s.video_bitrate),
                        tr("Приклади: 20M, 8000k."));
        i.fixes.push_back(fix(tr("За якістю"), {change(S::video_bitrate, str(""))}));
    }
    // Розмір файлу
    if (c.s.target_size_mb < 0 || c.s.target_size_mb > 100000) {
        auto& i = c.add(Sev::Error, K::Range, S::target_size_mb, trf("Розмір файлу {} МБ — поза межами 1…100000.", c.s.target_size_mb));
        i.fixes.push_back(fix(tr("За якістю"), {change(S::target_size_mb, num(0))}));
    } else if (c.s.target_size_mb > 0 && qi.param != "profile" && !qi.param.empty()) {
        if (!c.s.video_bitrate.empty()) c.disable(S::video_bitrate, tr("задано розмір файлу"));
        if (c.d().video_bitrate > 0 && c.d().video_bitrate < 800000)
            c.add(Sev::Warning, K::Consequence, S::target_size_mb,
                  trf("Під {:.0f} МБ виходить лише {:.2f} Мбіт/с — якість буде низькою.", c.s.target_size_mb,
                      c.d().video_bitrate / 1e6),
                  tr("Зменште роздільну здатність, FPS або довжину фрагмента."), {S::width, S::fps});
        else if (c.d().video_seconds <= 0)
            c.add(Sev::Info, K::Consequence, S::target_size_mb, tr("Бітрейт розрахується під довжину фрагмента."));
    }
    // Пресет швидкості (ProRes його не має: якість — профіль)
    auto& p = c.state(S::preset);
    if (qi.param == "profile" || qi.presets.empty()) {
        c.hide(S::preset, qi.param == "profile" ? tr("ProRes: якість задає профіль") : tr("кодек без пресетів швидкості"));
    } else {
        for (const auto& x : qi.presets) p.options.push_back({x.substr(0, x.find(' ')), x});
        const bool known = c.s.preset.empty() ||
                           std::any_of(qi.presets.begin(), qi.presets.end(),
                                       [&](const std::string& x) { return x.substr(0, x.find(' ')) == c.s.preset; });
        if (!known) {
            // Не з нашого списку: де FFmpeg перелічує значення — перевіряємо точно, інакше лише застереження
            std::optional<bool> accepted = ffmpeg_accepts(c.s.video_codec, preset_option(c.s.video_codec), c.s.preset);
            // x264/x265: рядок, але список пресетів у них вичерпний
            if (!accepted && (c.s.video_codec == "libx264" || c.s.video_codec == "libx264rgb" || c.s.video_codec == "libx265"))
                accepted = false;
            if (accepted == false) {
                auto& i = c.add(Sev::Error, K::Conflict, S::preset,
                                trf("Пресет «{}» не підходить для {}.", c.s.preset, codec_label(c.s.video_codec)), {},
                                {S::video_codec});
                i.fixes.push_back(fix(tr("Типовий"), {change(S::preset, str(""))}));
            } else if (!accepted) {
                auto& i = c.add(when_executing(c, Sev::Warning, Sev::Info), K::Runtime, S::preset,
                                trf("Пресет «{}» не з відомого списку для {} — кодек може його не прийняти.", c.s.preset,
                                    codec_label(c.s.video_codec)),
                                tr("Якщо кодек не відкриється, рендер зупиниться на першому кадрі — спершу зробіть тестовий прогін."),
                                {S::video_codec});
                i.fixes.push_back(fix(tr("Типовий"), {change(S::preset, str(""))}));
            }
        }
    }
    // ProRes 4:4:4 — лише профілі 4444 і 4444 XQ (кодер інакше підняв би профіль сам)
    if (qi.param == "profile" && c.d().chroma == 444 && c.s.quality >= 0 && c.s.quality < 4) {
        auto& i = c.add(Sev::Error, K::Conflict, S::quality,
                        trf("ProRes 4:4:4 — лише профілі 4444 і 4444 XQ, а вибрано {}.", c.s.quality), {}, {S::chroma});
        i.fixes.push_back(fix("4 — 4444", {change(S::quality, num(4))}));
        i.fixes.push_back(fix("4:2:2", {change(S::chroma, num(422))}));
    }
}

void r_video_advanced(RuleContext& c) {
    range_check(c, S::gop_seconds, c.s.gop_seconds, 0, 60, 2, tr("Ключовий кадр кожні"));
    range_check(c, S::threads, c.s.threads, 0, 256, 0, tr("Потоків CPU"));
    auto& sc = c.state(S::scaler);
    for (const char* x : {"lanczos", "bicubic", "spline", "bilinear", "area"}) sc.options.push_back({x, x});
    if (std::none_of(sc.options.begin(), sc.options.end(), [&](const OptionState& o) { return o.value == c.s.scaler; })) {
        auto& i = c.add(Sev::Error, K::Range, S::scaler, trf("Невідомий фільтр масштабування «{}».", c.s.scaler));
        i.fixes.push_back(fix("lanczos", {change(S::scaler, str("lanczos"))}));
    }
    for (const auto& part : split(c.s.video_options, ';')) {
        const std::string t = trim(part);
        if (!t.empty() && (t.find('=') == std::string::npos || t.front() == '=')) {
            c.add(Sev::Warning, K::Range, S::video_options, trf("Параметр кодека «{}» — без значення (потрібно ключ=значення).", t));
            break;
        }
    }
    const ContainerInfo* ci = find_container(c.d().container);
    if (!ci || !ci->mov_family) {
        c.hide(S::faststart, tr("лише для MP4 і MOV"));
        c.hide(S::crash_safe, tr("лише для MP4 і MOV"));
    }
}

// ============================== Звук ==============================

void r_audio_enabled(RuleContext& c) {
    const S dependents[] = {S::audio_codec,   S::audio_bitrate,  S::sample_rate,     S::game_audio,     S::game_volume,
                            S::game_audio_offset, S::voice_mode, S::voice_volume,    S::voice_delay,    S::voice_level,
                            S::voice_denoise, S::duck_game,      S::loudness_target, S::mic_file,       S::mic_offset,
                            S::mic_volume,    S::separate_tracks};
    if (!c.s.audio) {
        for (S id : dependents) c.disable(id, tr("звук вимкнено"));
        return;
    }
    if (c.d().image_sequence) {
        for (S id : {S::audio_codec, S::audio_bitrate, S::separate_tracks}) c.disable(id, tr("кадри — окремими файлами"));
        c.add(Sev::Info, K::Consequence, S::audio, tr("Послідовність кадрів: звук буде окремим файлом audio.wav поруч."));
    }
}

void r_audio_codec(RuleContext& c) {
    if (!c.s.audio || c.d().image_sequence) return;
    const std::string& ext = c.d().container;
    auto& st = c.state(S::audio_codec);
    for (const auto& e : audio_encoders()) {
        OptionState o{e.name, tr(e.label.c_str())};
        const CapabilityState cap = c.env.audio_encoder(e.name);
        if (!cap.ok()) {
            o.available = false;
            o.reason = cap.detail;
        } else if (media::container_supports(ext, e.name) == 0) {
            o.available = false;
            o.reason = trf("не для .{}", ext);
        }
        st.options.push_back(std::move(o));
    }
    auto alt = [&]() -> std::string {
        for (const char* cand : {"aac", "libopus", "flac", "pcm_s16le"})
            if (c.env.audio_encoder(cand).ok() && media::container_supports(ext, cand) > 0) return cand;
        return {};
    };
    const CapabilityState cap = c.env.audio_encoder(c.s.audio_codec);
    if (!cap.ok()) {
        auto& i = c.add(Sev::Error, K::Availability, S::audio_codec, trf("Аудіокодек {} недоступний.", audio_label(c.s.audio_codec)),
                        reason(cap.detail));
        if (const std::string a = alt(); !a.empty()) i.fixes.push_back(fix(audio_label(a), {change(S::audio_codec, str(a))}));
        return;
    }
    if (media::container_supports(ext, c.s.audio_codec) == 0) {
        auto& i = c.add(Sev::Error, K::Conflict, S::audio_codec,
                        trf("Формат .{} не підтримує аудіокодек {}.", ext, audio_label(c.s.audio_codec)), {}, {S::container});
        if (const std::string a = alt(); !a.empty()) i.fixes.push_back(fix(audio_label(a), {change(S::audio_codec, str(a))}));
    }
    // Бітрейт — лише для стиснення з втратами
    if (audio_lossless(c.s.audio_codec)) c.hide(S::audio_bitrate, tr("звук без втрат"));
    else {
        auto& b = c.state(S::audio_bitrate);
        for (const char* r : {"96k", "128k", "160k", "192k", "256k", "320k", "384k", "512k"}) b.options.push_back({r, r});
        if (!parse_bitrate(c.s.audio_bitrate) || parse_bitrate(c.s.audio_bitrate).value_or(0) <= 0) {
            auto& i = c.add(Sev::Error, K::Range, S::audio_bitrate, trf("Неправильний бітрейт звуку «{}».", c.s.audio_bitrate));
            i.fixes.push_back(fix("320k", {change(S::audio_bitrate, str("320k"))}));
        }
    }
    // Частота: кодек може вміти не всі (Opus — лише 48 кГц)
    const AVCodec* ac = avcodec_find_encoder_by_name(c.s.audio_codec.c_str());
    const auto rates = ac ? media::codec_sample_rates(ac) : std::vector<int>{};
    auto& sr = c.state(S::sample_rate);
    for (int r : {44100, 48000, 96000}) {
        OptionState o{std::to_string(r), trf("{} Гц", r)};
        if (!rates.empty() && std::find(rates.begin(), rates.end(), r) == rates.end()) {
            o.available = false;
            o.reason = trf("{} не вміє {} Гц", audio_label(c.s.audio_codec), r);
        }
        sr.options.push_back(std::move(o));
    }
    if (!rates.empty() && std::find(rates.begin(), rates.end(), c.s.sample_rate) == rates.end()) {
        int best = rates.front();
        for (int r : rates)
            if (std::abs(r - 48000) < std::abs(best - 48000)) best = r;
        auto& i = c.add(Sev::Error, K::Conflict, S::sample_rate,
                        trf("{} не підтримує {} Гц.", audio_label(c.s.audio_codec), c.s.sample_rate), {}, {S::audio_codec});
        i.fixes.push_back(fix(trf("{} Гц", best), {change(S::sample_rate, num(best))}));
    }
}

void r_audio_sources(RuleContext& c) {
    if (!c.s.audio) return;
    auto& vm = c.state(S::voice_mode);
    vm.options = {{"all", tr("Усі гравці")}, {"local", tr("Лише мій голос")}, {"others", tr("Усі, крім мене")},
                  {"selected", tr("Вибрані (галочки в таблиці нижче)")}, {"none", tr("Без голосів")}};
    const std::string& m = c.s.voice_mode;
    if (m != "all" && m != "local" && m != "others" && m != "selected" && m != "none") {
        auto& i = c.add(Sev::Error, K::Range, S::voice_mode, trf("Невідомий вибір голосів «{}».", m));
        i.fixes.push_back(fix(tr("Усі гравці"), {change(S::voice_mode, str("all"))}));
    }
    const bool voices = m != "none";
    if (!c.s.game_audio && !voices && c.s.mic_file.empty()) {
        auto& i = c.add(Sev::Warning, K::Consequence, S::audio, tr("Звук увімкнено, але джерел немає — доріжка буде тихою."), {},
                        {S::game_audio, S::voice_mode});
        i.fixes.push_back(fix(tr("Звук гри"), {change(S::game_audio, flag(true))}));
        i.fixes.push_back(fix(tr("Без звуку"), {change(S::audio, flag(false))}));
    }
    if (m == "selected" && trim(c.s.voice_selected).empty()) {
        auto& i = c.add(Sev::Warning, K::Consequence, S::voice_selected, tr("Вибір голосів — «Вибрані», але жодного гравця не вибрано."),
                        tr("У відео не буде голосів."), {S::voice_mode});
        i.fixes.push_back(fix(tr("Усі гравці"), {change(S::voice_mode, str("all"))}));
    }
    if (m != "selected") c.hide(S::voice_selected, tr("лише для «Вибрані»"));
    if (c.ctx.demo_known && voices && c.ctx.speakers == 0)
        c.add(Sev::Info, K::Consequence, S::voice_mode, tr("Голосового чату в демо немає."));
    else if (c.ctx.demo_known && m == "local" && c.ctx.local_speaker == 0)
        c.add(Sev::Info, K::Consequence, S::voice_mode,
              tr("Вашого голосу в демо немає: під час запису не було voice_loopback 1."));
    // Обробка голосу має сенс лише з голосами; приглушення гри — лише зі звуком гри
    if (!voices) {
        for (S id : {S::voice_volume, S::voice_delay, S::voice_level, S::voice_denoise}) c.disable(id, tr("голоси вимкнено"));
        if (c.s.voice_level || c.s.voice_denoise)
            c.add(Sev::Info, K::Consequence, S::voice_level, tr("Обробка голосу нічого не змінить: голоси вимкнено."), {},
                  {S::voice_mode});
    }
    if (!c.s.game_audio) {
        c.disable(S::game_volume, tr("звук гри вимкнено"));
        c.disable(S::game_audio_offset, tr("звук гри вимкнено"));
    }
    if (!c.s.game_audio || (!voices && c.s.mic_file.empty())) {
        c.disable(S::duck_game, !c.s.game_audio ? tr("звук гри вимкнено") : tr("немає голосів"));
        if (c.s.duck_game)
            c.add(Sev::Info, K::Consequence, S::duck_game, tr("Приглушення гри нічого не змінить: немає звуку гри або голосів."));
    }
    // Шумодав: нейромережа RNNoise або запасний afftdn
    if ((c.s.voice_denoise || !trim(c.s.voice_denoise_players).empty()) && voices &&
        c.env.media.rnnoise_model.state == Availability::Unavailable)
        c.add(Sev::Info, K::Availability, S::voice_denoise, c.env.media.rnnoise_model.detail);
}

void r_audio_levels(RuleContext& c) {
    if (!c.s.audio) return;
    range_check(c, S::game_volume, c.s.game_volume, 0, 4, 1, tr("Гучність гри"));
    range_check(c, S::voice_volume, c.s.voice_volume, 0, 4, 1, tr("Гучність голосу"));
    range_check(c, S::mic_volume, c.s.mic_volume, 0, 4, 1, tr("Гучність мікрофона"));
    range_check(c, S::game_audio_offset, c.s.game_audio_offset, -5, 5, 0, tr("Зсув звуку гри"));
    range_check(c, S::voice_delay, c.s.voice_delay, -5, 5, 0, tr("Затримка голосу"));
    auto& lt = c.state(S::loudness_target);
    for (double v : loudness_targets())
        lt.options.push_back({std::format("{:g}", v), v == 0 ? tr("Не змінювати") : std::format("{:g} LUFS", v)});
    if (c.s.loudness_target != 0 && (c.s.loudness_target < -70 || c.s.loudness_target > -5)) {
        auto& i = c.add(Sev::Error, K::Range, S::loudness_target, trf("Гучність {} LUFS — поза межами −70…−5.", c.s.loudness_target));
        i.fixes.push_back(fix(tr("Не змінювати"), {change(S::loudness_target, num(0))}));
    } else if (c.s.loudness_target != 0 && c.env.media.filters.count("loudnorm") &&
               !c.env.media.filters.at("loudnorm").ok()) {
        auto& i = c.add(Sev::Error, K::Availability, S::loudness_target, tr("Вирівнювання гучності недоступне (немає фільтра loudnorm)."));
        i.fixes.push_back(fix(tr("Не змінювати"), {change(S::loudness_target, num(0))}));
    }
}

void r_audio_mic(RuleContext& c) {
    if (c.s.mic_file.empty()) {
        c.disable(S::mic_offset, tr("файл мікрофона не вибрано"));
        c.disable(S::mic_volume, tr("файл мікрофона не вибрано"));
        return;
    }
    if (!c.s.audio) return;
    if (c.env.filesystem.mic.state == Availability::Unavailable) {
        auto& i = c.add(Sev::Error, K::Availability, S::mic_file, trf("Файлу мікрофона немає: {}", c.s.mic_file));
        i.fixes.push_back(fix(tr("Без мікрофона"), {change(S::mic_file, str(""))}));
    }
}

void r_audio_editing(RuleContext& c) {
    if (c.s.edit_package && !c.s.audio) {
        auto& i = c.add(Sev::Warning, K::Dependency, S::edit_package, tr("Пакет для монтажу без звуку буде без доріжок."), {},
                        {S::audio});
        i.fixes.push_back(fix(tr("Увімкнути звук"), {change(S::audio, flag(true))}));
    }
    if ((c.s.edit_package || c.s.separate_tracks) && c.d().image_sequence)
        c.add(Sev::Info, K::Consequence, S::edit_package, tr("Для послідовності кадрів окремі доріжки й пакет для монтажу не робляться."));
}

// ============================== Субтитри і розпізнавання ==============================

bool speech_needed(const render::RenderSettings& s) {
    return (s.subtitles_srt && s.speech_subtitles) || s.translate_subtitles || s.dub;
}

void r_speech(RuleContext& c) {
    if (!c.s.subtitles_srt) c.hide(S::speech_subtitles, tr("лише разом із субтитрами «хто говорить»"));
    if (c.s.speech_subtitles && !c.s.subtitles_srt) {
        auto& i = c.add(Sev::Warning, K::Dependency, S::speech_subtitles, tr("Текст розмов потребує субтитрів «хто говорить»."), {},
                        {S::subtitles_srt});
        i.fixes.push_back(fix(tr("Увімкнути субтитри"), {change(S::subtitles_srt, flag(true))}));
    }
    if (!speech_needed(c.s)) return;
    // Ланцюжок: голоси → розпізнавання → переклад → озвучення
    std::vector<std::string> needs;
    if (c.s.subtitles_srt && c.s.speech_subtitles) needs.push_back(tr("текст у субтитрах"));
    if (c.s.translate_subtitles) needs.push_back(tr("переклад субтитрів"));
    if (c.s.dub) needs.push_back(tr("озвучення"));
    std::string what;
    for (const auto& n : needs) what += (what.empty() ? "" : ", ") + n;
    const S main = c.s.speech_subtitles && c.s.subtitles_srt ? S::speech_subtitles : c.s.dub ? S::dub : S::translate_subtitles;
    if (c.env.speech.whisper_cli.state == Availability::Unavailable) {
        auto& i = c.add(Sev::Warning, K::External, main, trf("Розпізнавання мовлення недоступне — буде пропущено: {}.", what),
                        c.env.speech.whisper_cli.detail);
        i.action = ActionId::InstallWhisperCli;
    } else if (c.env.speech.model.state == Availability::Unavailable) {
        auto& i = c.add(Sev::Warning, K::External, main,
                        trf("Модель розпізнавання не встановлено — буде пропущено: {}.", what), c.env.speech.model.detail);
        i.action = ActionId::InstallWhisperModel;
    }
    if (c.ctx.demo_known && c.ctx.speakers == 0)
        c.add(Sev::Info, K::Consequence, main, trf("Голосового чату в демо немає — нічого розпізнавати ({}).", what));
}

void r_subtitles_voices(RuleContext& c) {
    if (c.ctx.demo_known && c.ctx.speakers == 0 && (c.s.subtitles_srt || c.s.speaker_overlay))
        c.add(Sev::Info, K::Consequence, c.s.subtitles_srt ? S::subtitles_srt : S::speaker_overlay,
              tr("Голосового чату в демо немає — субтитрів і підписів «хто говорить» не буде."));
}

// ============================== Переклад і озвучення ==============================

void r_ai_languages(RuleContext& c) {
    const bool wanted = c.s.translate_subtitles || c.s.dub;
    if (!wanted) {
        for (S id : {S::translator, S::translator_url, S::translator_model, S::deepl_key, S::google_key, S::libre_key, S::openai_key})
            c.disable(id, tr("переклад вимкнено"));
    }
    auto& st = c.state(S::dub_languages);
    for (const auto& l : translate::languages()) {
        OptionState o{l.code, l.native};
        if (!translate::supports(c.s.translator, l.code)) {
            o.warning = true;
            o.reason = tr("вибраний сервіс не перекладає на цю мову");
        } else if (c.s.dub && !dub::engine_supports(c.s.tts_engine, c.s.elevenlabs_model, l.code)) {
            o.warning = true;
            o.reason = tr("вибраний рушій не озвучує цю мову (лише субтитри)");
        }
        st.options.push_back(std::move(o));
    }
    std::vector<std::string> unknown;
    for (const auto& raw : split(c.s.dub_languages, ',')) {
        const std::string code = trim(raw);
        if (!code.empty() && !translate::find_language(code)) unknown.push_back(code);
    }
    if (!unknown.empty()) {
        auto& i = c.add(Sev::Error, K::Range, S::dub_languages, trf("Невідомі мови перекладу: {}.", join(unknown, ", ")));
        i.fixes.push_back(fix(tr("Прибрати невідомі"), {change(S::dub_languages, str(join(render::dub_language_list(c.s), ",")))}));
    }
    if (!wanted) return;
    const auto langs = render::dub_language_list(c.s);
    if (langs.empty()) {
        c.add(Sev::Warning, K::Dependency, S::dub_languages, tr("Не вибрано жодної мови — перекладати нема на що."), {},
              {S::translate_subtitles, S::dub});
        return;
    }
    std::vector<std::string> unsupported;
    for (const auto& l : langs)
        if (!translate::supports(c.s.translator, l)) unsupported.push_back(l);
    if (!unsupported.empty())
        c.add(Sev::Warning, K::Conflict, S::dub_languages,
              trf("Сервіс перекладу не перекладає на: {} — ці мови буде пропущено.", join(unsupported, ", ")), {}, {S::translator});
}

void r_ai_translator(RuleContext& c) {
    const translate::ProviderInfo* p = translate::find_provider(c.s.translator);
    auto& st = c.state(S::translator);
    for (const auto& x : translate::providers()) {
        if (std::string(x.id) == "fake" && c.s.translator != "fake") continue;   // лише для автотестів
        OptionState o{x.id, tr(x.label)};
        auto it = c.env.translation.providers.find(x.id);
        if (it != c.env.translation.providers.end() && !it->second.ok()) {
            o.warning = true;
            o.reason = it->second.detail;
        }
        st.options.push_back(std::move(o));
    }
    if (!p) {
        auto& i = c.add(Sev::Error, K::Range, S::translator, trf("Невідомий сервіс перекладу «{}».", c.s.translator));
        i.fixes.push_back(fix("DeepL", {change(S::translator, str("deepl"))}));
        return;
    }
    // Поля лише вибраного сервісу
    if (!p->uses_url) c.hide(S::translator_url, tr("для цього сервісу не потрібно"));
    if (!p->uses_model) c.hide(S::translator_model, tr("для цього сервісу не потрібно"));
    for (const auto& x : translate::providers()) {
        const std::string key = std::string(x.id) + "_key";
        if (auto id = find_setting(key); id && std::string(x.id) != p->id) c.hide(*id, tr("для іншого сервісу"));
    }
    if (!(c.s.translate_subtitles || c.s.dub) || render::dub_language_list(c.s).empty()) return;
    auto it = c.env.translation.providers.find(p->id);
    if (it != c.env.translation.providers.end() && it->second.state == Availability::Unavailable) {
        const S key = find_setting(std::string(p->id) + "_key").value_or(S::translator);
        auto& i = c.add(Sev::Warning, K::Dependency, key,
                        trf("{}: {} — переклад буде пропущено.", tr(p->label), it->second.detail), {}, {S::translator});
        i.action = ActionId::ConfigureTranslator;
    }
}

void r_ai_dub(RuleContext& c) {
    if (!c.s.dub) {
        for (S id : {S::dub_outputs, S::dub_audio_format, S::dub_original_volume, S::tts_engine, S::tts_device, S::tts_python,
                     S::tts_clone, S::elevenlabs_key, S::elevenlabs_model, S::elevenlabs_voice})
            c.disable(id, tr("озвучення вимкнено"));
        return;
    }
    if (!c.s.audio) {
        auto& i = c.add(Sev::Error, K::Dependency, S::dub, tr("Озвучення потребує звуку у відео."), {}, {S::audio});
        i.fixes.push_back(fix(tr("Увімкнути звук"), {change(S::audio, flag(true))}));
        i.fixes.push_back(fix(tr("Без озвучення"), {change(S::dub, flag(false))}));
    }
    // Куди озвучення
    std::vector<std::string> outs;
    bool bad = false;
    for (const auto& raw : split(c.s.dub_outputs, ',')) {
        const std::string o = trim(raw);
        if (o.empty()) continue;
        if (o != "tracks" && o != "videos" && o != "audio") bad = true;
        else outs.push_back(o);
    }
    auto& st = c.state(S::dub_outputs);
    st.options = {{"tracks", tr("Доріжки в цьому відео (з мітками мови)")},
                  {"videos", tr("Окреме відео для кожної мови")},
                  {"audio", tr("Окремі аудіофайли")}};
    if (c.d().image_sequence)
        for (auto& o : st.options)
            if (o.value != "audio") {
                o.available = false;
                o.reason = tr("кадри — окремими файлами");
            }
    if (bad || outs.empty()) {
        auto& i = c.add(Sev::Error, K::Range, S::dub_outputs, tr("Не вибрано, куди озвучення (доріжки, окремі відео чи аудіофайли)."));
        i.fixes.push_back(fix(tr("Доріжки в цьому відео (з мітками мови)"), {change(S::dub_outputs, str("tracks"))}));
    } else if (c.d().image_sequence && std::any_of(outs.begin(), outs.end(), [](const std::string& o) { return o != "audio"; })) {
        auto& i = c.add(Sev::Error, K::Conflict, S::dub_outputs, tr("Послідовність кадрів: озвучення — лише окремими аудіофайлами."), {},
                        {S::container});
        i.fixes.push_back(fix(tr("Окремі аудіофайли"), {change(S::dub_outputs, str("audio"))}));
    }
    auto& fmt = c.state(S::dub_audio_format);
    for (const auto& f : dub::audio_formats()) fmt.options.push_back({f.id, f.id});
    if (std::find(outs.begin(), outs.end(), "audio") == outs.end()) c.hide(S::dub_audio_format, tr("лише для окремих аудіофайлів"));
    else if (std::none_of(dub::audio_formats().begin(), dub::audio_formats().end(),
                          [&](const dub::AudioFormat& f) { return c.s.dub_audio_format == f.id; })) {
        auto& i = c.add(Sev::Error, K::Range, S::dub_audio_format, trf("Невідомий формат аудіофайлів «{}».", c.s.dub_audio_format));
        i.fixes.push_back(fix("mp3", {change(S::dub_audio_format, str("mp3"))}));
    }
    range_check(c, S::dub_original_volume, c.s.dub_original_volume, 0, 1, 0.12, tr("Оригінальні голоси"));
}

void r_ai_tts(RuleContext& c) {
    auto& st = c.state(S::tts_engine);
    for (const auto& e : dub::engines()) {
        if (std::string(e.id) == "fake" && c.s.tts_engine != "fake") continue;   // лише для автотестів
        OptionState o{e.id, std::string(e.id) == "omnivoice" ? tr("Локально (OmniVoice)") : std::string(e.label)};
        if (std::string(e.id) == "omnivoice" && !c.env.dubbing.omnivoice.ok()) {
            o.warning = true;
            o.reason = c.env.dubbing.omnivoice.detail;
        } else if (std::string(e.id) == "elevenlabs" && !c.env.dubbing.elevenlabs.ok()) {
            o.warning = true;
            o.reason = c.env.dubbing.elevenlabs.detail;
        }
        st.options.push_back(std::move(o));
    }
    if (c.s.tts_engine != "omnivoice") {
        c.hide(S::tts_device, tr("лише для локального рушія"));
        c.hide(S::tts_python, tr("лише для локального рушія"));
    }
    if (c.s.tts_engine != "elevenlabs")
        for (S id : {S::elevenlabs_key, S::elevenlabs_model, S::elevenlabs_voice}) c.hide(id, tr("лише для ElevenLabs"));
    c.state(S::tts_device).options = {{"auto", tr("Авто")}, {"cuda", "GPU (CUDA)"}, {"cpu", tr("Процесор")}};
    if (c.env.dubbing.nvidia_gpu.state == Availability::Unavailable)
        for (auto& o : c.state(S::tts_device).options)
            if (o.value == "cuda") {
                o.available = false;
                o.reason = c.env.dubbing.nvidia_gpu.detail;
            }
    // Згода на клонування — без неї клонування вимкнене (так і рендерить ядро)
    if (c.s.tts_clone && !c.s.tts_clone_ack) {
        auto& i = c.add(Sev::Warning, K::Dependency, S::tts_clone,
                        tr("Клонування голосів вимкнено: не підтверджено згоду гравців."),
                        tr("Голос — особисті дані: клоном можна озвучувати лише з дозволу гравця."), {S::tts_clone_ack});
        i.action = ActionId::ConfirmVoiceConsent;
        i.fixes.push_back(fix(tr("Без клонування"), {change(S::tts_clone, flag(false))}));
    }
    if (c.s.voice_library_auto && !c.s.tts_clone_ack) {
        auto& i = c.add(Sev::Warning, K::Dependency, S::voice_library_auto,
                        tr("Бібліотека голосів не поповнюється: не підтверджено згоду гравців."), {}, {S::tts_clone_ack});
        i.action = ActionId::ConfirmVoiceConsent;
    }
    if (!c.s.dub) return;
    const dub::EngineInfo* e = dub::find_engine(c.s.tts_engine);
    if (!e) {
        auto& i = c.add(Sev::Error, K::Range, S::tts_engine, trf("Невідомий рушій озвучення «{}».", c.s.tts_engine));
        i.fixes.push_back(fix(tr("Локально (OmniVoice)"), {change(S::tts_engine, str("omnivoice"))}));
        return;
    }
    if (c.s.tts_engine == "omnivoice" && c.env.dubbing.omnivoice.state == Availability::Unavailable) {
        auto& i = c.add(Sev::Warning, K::External, S::tts_engine, tr("Локальний рушій озвучення не встановлено — озвучення буде пропущено."),
                        tr("OmniVoice встановлюється кнопкою «Встановити» (близько 2–5 ГБ)."), {S::dub});
        i.action = ActionId::InstallVoiceEngine;
    }
    if (c.s.tts_engine == "elevenlabs" && c.env.dubbing.elevenlabs.state == Availability::Unavailable) {
        auto& i = c.add(Sev::Warning, K::Dependency, S::elevenlabs_key, tr("ElevenLabs: немає ключа API — озвучення буде пропущено."), {},
                        {S::tts_engine});
        i.action = ActionId::ConfigureElevenLabs;
    }
    std::vector<std::string> unsupported;
    for (const auto& l : render::dub_language_list(c.s))
        if (!dub::engine_supports(c.s.tts_engine, c.s.elevenlabs_model, l)) unsupported.push_back(l);
    if (!unsupported.empty())
        c.add(Sev::Warning, K::Conflict, S::tts_engine,
              trf("Рушій не озвучує: {} — для цих мов будуть лише субтитри.", join(unsupported, ", ")), {}, {S::dub_languages});
    if (c.s.tts_engine == "omnivoice" && c.s.tts_device == "cuda" && c.env.dubbing.nvidia_gpu.state == Availability::Unavailable) {
        auto& i = c.add(Sev::Warning, K::Availability, S::tts_device, tr("GPU NVIDIA не знайдено — CUDA недоступна."));
        i.fixes.push_back(fix(tr("Авто"), {change(S::tts_device, str("auto"))}));
    }
}

// ============================== Вихід ==============================

void r_output(RuleContext& c) {
    if (c.s.output_path.empty()) {
        if (!c.s.demo_path.empty())
            c.add(Sev::Info, K::Consequence, S::output_path, tr("Файл результату не вибрано — відео буде поруч із демо."));
        return;
    }
    if (!c.s.demo_path.empty() &&
        to_lower(path_to_utf8(path_from_utf8(c.s.output_path).lexically_normal())) ==
            to_lower(path_to_utf8(path_from_utf8(c.s.demo_path).lexically_normal()))) {
        auto& i = c.add(Sev::Error, K::Conflict, S::output_path, tr("Файл результату — це саме демо."));
        i.fixes.push_back(fix(tr("Поруч із демо"), {change(S::output_path, str(replace_ext(c.s.demo_path, c.d().container)))}));
        return;
    }
    const ContainerInfo* ci = find_container(c.d().container);
    const bool seq_format = ci && !ci->image_codec.empty();
    const bool pattern = c.s.output_path.find('%') != std::string::npos;
    if (seq_format && !pattern) {
        auto& i = c.add(Sev::Error, K::Conflict, S::output_path, tr("Для послідовності кадрів потрібен шаблон імені з номером (%06d)."),
                        {}, {S::container});
        const fs::path p = path_from_utf8(c.s.output_path);
        const std::string fixed =
            path_to_utf8(p.parent_path() / (path_to_utf8(p.stem()) + "_frames") / ("frame_%06d." + ci->ext));
        i.fixes.push_back(fix(fixed, {change(S::output_path, str(fixed))}));
    }
    if (c.env.filesystem.output_exists.state == Availability::Available)
        c.add(when_executing(c, Sev::Warning, Sev::Info), K::Consequence, S::output_path,
              trf("Файл уже є і буде перезаписаний: {}", c.s.output_path));
    const uint64_t free = c.env.filesystem.output_free;
    if (free > 0 && c.d().estimated_bytes > 0 && c.d().estimated_bytes > free)
        c.add(Sev::Warning, K::Resource, S::output_path,
              trf("Орієнтовно файл займе {} ГБ, а на диску вільно {} ГБ.", fmt_bytes_gb(c.d().estimated_bytes), fmt_bytes_gb(free)));
    else if (free > 0 && free < (1ull << 30))
        c.add(Sev::Warning, K::Resource, S::output_path, trf("На диску результату вільно лише {} ГБ.", fmt_bytes_gb(free)));
    // Розділи з позначок
    if (c.s.chapters && !trim(c.s.markers).empty() && ci && !ci->chapters)
        c.add(Sev::Info, K::Consequence, S::chapters, trf("Формат .{} не зберігає розділи — буде лише .chapters.txt.", ci->ext), {},
              {S::container});
}

// ============================== Додаткові версії ==============================

void r_outputs(RuleContext& c) {
    auto& st = c.state(S::extra_versions);
    // Що потрібно кожній версії з FFmpeg
    auto needs = [&](const std::string& id) -> std::vector<std::pair<std::string, bool>> {
        auto enc = [&](const char* n) { return std::pair<std::string, bool>{n, c.env.video_encoder(n).ok()}; };
        auto extra = [&](const char* n) {
            auto it = c.env.media.extra_encoders.find(n);
            return std::pair<std::string, bool>{n, it != c.env.media.extra_encoders.end() && it->second.ok()};
        };
        auto filter = [&](const char* n) {
            auto it = c.env.media.filters.find(n);
            return std::pair<std::string, bool>{n, it != c.env.media.filters.end() && it->second.ok()};
        };
        if (id == "discord" || id == "480p" || id == "vertical") return {enc("libx264")};
        if (id == "master") return {enc("prores_ks")};
        if (id == "thumb") return {extra("mjpeg"), filter("thumbnail")};
        if (id == "gif") return {extra("gif"), filter("palettegen"), filter("paletteuse")};
        if (id == "webp") {
            auto a = extra("libwebp_anim"), b = extra("libwebp");
            return {a.second ? a : b};
        }
        return {};
    };
    auto missing = [&](const std::string& id) {
        std::string m;
        for (const auto& [n, ok] : needs(id))
            if (!ok) m += (m.empty() ? "" : ", ") + n;
        return m;
    };
    for (const auto& v : render::version_presets()) {
        OptionState o{v.id, v.label};
        o.reason = v.hint;
        if (const std::string m = missing(v.id); !m.empty()) {
            o.available = false;
            o.reason = trf("немає в цій збірці FFmpeg: {}", m);
        } else if (c.d().image_sequence) {
            o.available = false;
            o.reason = tr("основний вихід — послідовність кадрів");
        }
        st.options.push_back(std::move(o));
    }
    const auto& ids = c.d().extra_outputs;
    if (ids.empty()) return;
    auto without = [&](const std::string& drop) {
        std::string out;
        for (const auto& x : ids)
            if (x != drop) out += (out.empty() ? "" : ",") + x;
        return out;
    };
    std::string bad;
    if (!render::valid_version_ids(c.s.extra_versions, &bad)) {
        auto& i = c.add(Sev::Error, K::Range, S::extra_versions, trf("Невідома додаткова версія «{}».", bad));
        i.fixes.push_back(fix(trf("Прибрати «{}»", bad), {change(S::extra_versions, str(without(bad)))}));
    }
    if (c.d().image_sequence) {
        auto& i = c.add(Sev::Error, K::Conflict, S::extra_versions,
                        tr("Додаткові версії не робляться, коли основний вихід — послідовність кадрів."), {}, {S::container});
        i.fixes.push_back(fix(tr("Без додаткових версій"), {change(S::extra_versions, str(""))}));
        return;
    }
    for (const auto& id : ids)
        if (const std::string m = missing(id); !m.empty()) {
            const auto v = std::find_if(render::version_presets().begin(), render::version_presets().end(),
                                         [&](const render::VersionPreset& p) { return p.id == id; });
            const std::string label = v != render::version_presets().end() ? v->label : id;
            auto& i = c.add(Sev::Error, K::Availability, S::extra_versions,
                            trf("Версія «{}» неможлива: немає в цій збірці FFmpeg ({}).", label, m));
            i.fixes.push_back(fix(trf("Прибрати «{}»", label), {change(S::extra_versions, str(without(id)))}));
        }
    if (c.ctx.purpose == ValidationContext::Purpose::TestRun)
        c.add(Sev::Info, K::Consequence, S::extra_versions, tr("Тестовий прогін: додаткові версії не кодуються."));
    const bool discord = std::find(ids.begin(), ids.end(), "discord") != ids.end();
    if (discord && c.d().video_seconds > 0) {
        const int64_t br = render::bitrate_for_target_size(9.5, c.d().video_seconds, 128000);
        if (br < 500000)
            c.add(Sev::Warning, K::Consequence, S::extra_versions,
                  trf("Версія Discord: {} у 10 МБ — лише {:.2f} Мбіт/с, якість буде низькою.",
                      format_duration(c.d().video_seconds), br / 1e6));
    }
    int encoded = 0;
    for (const auto& id : ids)
        if (id == "discord" || id == "480p" || id == "vertical" || id == "master") ++encoded;
    if (encoded >= 3)
        c.add(Sev::Info, K::Resource, S::extra_versions,
              trf("{} додаткові версії кодуються одночасно з основним файлом — рендер буде повільнішим.", encoded));
}

// ============================== Вікно програми ==============================

void r_app_graphics(RuleContext& c) {
    const auto& apis = c.env.graphics.apis;
    if (apis.empty()) return;   // перевіряє лише вікно (Qt)
    auto& st = c.state(S::ui_graphics_api);
    for (const auto& a : apis) {
        OptionState o{a.id, a.label};
        if (a.state != Availability::Available) {
            o.available = false;
            o.reason = a.detail;
        }
        st.options.push_back(std::move(o));
    }
    const auto it = std::find_if(apis.begin(), apis.end(), [&](const GraphicsApiInfo& a) { return a.id == c.s.ui_graphics_api; });
    if (it == apis.end() || it->state != Availability::Available) {
        auto& i = c.add(Sev::Warning, K::Platform, S::ui_graphics_api,
                        trf("Графічний API вікна «{}» тут недоступний — вікно малюватиме автоматично вибраний.", c.s.ui_graphics_api),
                        it != apis.end() ? reason(it->detail) : std::string());
        i.fixes.push_back(fix(tr("Автоматично"), {change(S::ui_graphics_api, str("auto"))}));
    }
}

} // namespace

const std::vector<Rule>& rules() {
    static const std::vector<Rule> list = {
        {"source.demo", N_("Демо відкрито і файл є"), r_source_demo},
        {"source.range", N_("Фрагмент у межах демо"), r_source_range},
        {"game.renderer", N_("Рендерер гри відомий; видно поле папки саме його копії"), r_game_renderer},
        {"game.install", N_("Копія гри для рендерера знайдена і підходить йому"), r_game_install},
        {"game.running", N_("Гра не запущена окремо"), r_game_running},
        {"game.window", N_("Вікно гри там, де рендерер малює"), r_game_window},
        {"game.render_size", N_("Розмір вікна гри"), r_game_render_size},
        {"game.capture", N_("Формат кадрів гри"), r_game_capture},
        {"game.frame_transport", N_("Передача кадрів з гри"), r_game_transport},
        {"game.manual", N_("Ручний режим: без тестового прогону і черги"), r_game_manual},
        {"game.misc", N_("Затримка меню і місце на диску з грою"), r_game_misc},
        {"parallel.limits", N_("Копій гри не більше, ніж дозволяє рендерер; коли рендерить одна"), r_parallel},
        {"parallel.resources", N_("Пам'яті й відеопам'яті на всі копії гри"), r_parallel_resources},
        {"video.size", N_("Розмір кадру і округлення до парних"), r_video_size},
        {"video.fps", N_("Частота кадрів"), r_video_fps},
        {"video.motion", N_("Розмиття руху, швидкість і навантаження на гру"), r_video_motion},
        {"video.container", N_("Формат файлу відомий"), r_video_container},
        {"video.codec", N_("Відеокодек є, працює на цій відеокарті і підходить формату"), r_video_codec},
        {"video.pixel_format", N_("Бітність і субдискретизація, які вміє кодек"), r_video_pixel_format},
        {"video.quality", N_("Якість, бітрейт, розмір файлу і пресет кодека"), r_video_quality},
        {"video.advanced", N_("Тонкі параметри кодування"), r_video_advanced},
        {"audio.enabled", N_("Що від чого залежить, коли звук вимкнено"), r_audio_enabled},
        {"audio.codec", N_("Аудіокодек, бітрейт і частота під формат файлу"), r_audio_codec},
        {"audio.sources", N_("Джерела звуку, голоси і обробка"), r_audio_sources},
        {"audio.levels", N_("Гучність і зсуви в межах"), r_audio_levels},
        {"audio.mic", N_("Файл мікрофона"), r_audio_mic},
        {"audio.editing", N_("Окремі доріжки і пакет для монтажу"), r_audio_editing},
        {"speech.recognition", N_("Розпізнавання мовлення для тексту, перекладу й озвучення"), r_speech},
        {"subtitles.voices", N_("Субтитри «хто говорить» без голосів"), r_subtitles_voices},
        {"ai.languages", N_("Мови перекладу"), r_ai_languages},
        {"ai.translator", N_("Сервіс перекладу налаштовано"), r_ai_translator},
        {"ai.dub", N_("Озвучення: звук і куди класти"), r_ai_dub},
        {"ai.tts", N_("Рушій озвучення, мови і згода на клонування"), r_ai_tts},
        {"output.file", N_("Файл результату, перезапис, місце і розділи"), r_output},
        {"outputs.versions", N_("Додаткові версії можливі з цією збіркою FFmpeg"), r_outputs},
        {"app.graphics_api", N_("Графічний API вікна доступний"), r_app_graphics},
    };
    return list;
}

} // namespace gmdr::config::detail
