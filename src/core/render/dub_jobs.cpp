#include "dub_jobs.hpp"

#include "../dub/tts.hpp"
#include "../translate/translate.hpp"
#include "../util/log.hpp"
#include "../util/secret.hpp"
#include "../util/strings.hpp"
#include "dubbing.hpp"

#include <format>

namespace gmdr::render {

void VoiceEngineJob::run() {
    std::string err;
    if (action_ == Action::Install) {
        set_stage(tr("Встановлення рушія озвучення"), 0);
        const bool ok = dub::install_engine(cuda_, [&](double f, const std::string& what) { report_progress(what, f); }, &cancel_, &err);
        if (cancel_) {
            update([](Progress& p) { p.stage = tr("Скасовано"); });
            return;
        }
        if (!ok) {
            fail(tr("Рушій озвучення не встановлено: ") + err);
            return;
        }
        succeed(path_to_utf8(dub::engine_dir()));
        return;
    }
    set_stage(tr("Перевірка рушія озвучення (завантаження моделі)"), -1);
    const auto dev = dub::check_engine(tts_config(s_), &cancel_, &err);
    if (!dev) {
        fail(tr("Рушій озвучення не працює: ") + err);
        return;
    }
    log_info("{}", trf("Рушій озвучення працює ({})", *dev));
    succeed(*dev);
}

void ServiceCheckJob::run() {
    std::string err;
    if (what_ == What::ElevenLabs) {
        set_stage("ElevenLabs", -1);
        const auto r = dub::elevenlabs_check(unprotect_secret(s_.elevenlabs_key), &err);
        if (!r) {
            fail("ElevenLabs: " + err);
            return;
        }
        succeed(*r);
        return;
    }
    const translate::Config c = translator_config(s_);
    const auto langs = dub_language_list(s_);
    const std::string to = langs.empty() ? "en" : langs.front();
    set_stage(tr("Переклад пробної фрази"), -1);
    const std::string sample = tr("Привіт! Перевірка перекладу.");
    const auto r = translate::translate(c, {sample}, "uk", to, {}, &cancel_, &err);
    if (!r || r->empty()) {
        fail(err.empty() ? tr("сервіс нічого не повернув") : err);
        return;
    }
    succeed(std::format("{} → {}", sample, r->front()));
}

void TranslateJob::run() {
    if (!analysis_ || !voices_) {
        fail(tr("Спершу відкрийте демо"));
        return;
    }
    if (dub_language_list(s_).empty()) {
        fail(tr("Виберіть мови перекладу"));
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
    const double to = range_only_ && s_.end_tick > 0 ? s_.end_tick * ti : analysis_->last_tick * ti;
    set_stage(tr("Розпізнавання мовлення"), 0);
    std::string err;
    auto ts = ensure_transcript(sel, speakers, from, to, [&](double f, const std::string& what) { report_progress(what, f); }, &cancel_, &err);
    if (cancel_) {
        update([](Progress& p) { p.stage = tr("Скасовано"); });
        return;
    }
    if (!ts) {
        fail(tr("Не вдалося розпізнати мовлення: ") + err);
        return;
    }
    std::vector<std::string> keys;
    for (const auto* t : speakers) keys.push_back(t->key);
    // Субтитри лягають поруч із відео (якщо вибрано файл), інакше — поруч із демо
    const std::string base = !s_.output_path.empty() && s_.output_path.find('%') == std::string::npos ? s_.output_path : s_.demo_path;
    const auto made = translate_transcript_files(sel, ts->lines, keys, from, to - from, base,
                                                 [&](const std::string& what, double f) { report_progress(what, f); }, &cancel_, &err);
    if (cancel_) {
        update([](Progress& p) { p.stage = tr("Скасовано"); });
        return;
    }
    if (made.empty()) {
        fail(tr("Переклад не вдався: ") + err);
        return;
    }
    for (const auto& m : made) log_info("{}", trf("Перекладені субтитри: {}", m));
    succeed(made.front());
}

} // namespace gmdr::render
