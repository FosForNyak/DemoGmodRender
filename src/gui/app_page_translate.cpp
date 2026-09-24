// =============================================================================
//  app_page_translate.cpp — сторінка «Переклад і озвучення»: мови перекладу, що
//  зробити (субтитри, озвучення, куди його), шаблони публікації, сервіс перекладу,
//  рушій озвучення (локальний OmniVoice чи ElevenLabs), клонування голосів гравців
//  (лише після підтвердження згоди) і бібліотека зразків голосів.
// =============================================================================
#include "app.hpp"

#include "app_ui.hpp"
#include "platform.hpp"

#include "core/dub/dub_mix.hpp"
#include "core/dub/tts.hpp"
#include "core/render/dub_jobs.hpp"
#include "core/render/dubbing.hpp"
#include "core/translate/translate.hpp"
#include "core/util/file_util.hpp"
#include "core/util/secret.hpp"
#include "core/util/strings.hpp"

#include "imgui.h"
#include "imgui_stdlib.h"
#include "core/util/i18n.hpp"

#include <algorithm>
#include <cstdlib>
#include <format>

namespace gmdr::gui {

using namespace ui;

namespace {
bool has_code(const std::vector<std::string>& v, const std::string& c) { return std::find(v.begin(), v.end(), c) != v.end(); }

std::string join_codes(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& c : v) s += (s.empty() ? "" : ",") + c;
    return s;
}

// Тестові сервіси (імітатори) — лише для автотестів вікна
bool show_test_services() { return std::getenv("GMDR_TEST_FAKE_SERVICES") != nullptr; }

// «Фішка»: пункт, що вмикається клацанням (мови перекладу)
bool chip(const char* text, const char* id, bool on, bool dim, float* x, float max_x) {
    const ImGuiStyle& st = ImGui::GetStyle();
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const float w = ts.x + st.FramePadding.x * 2.4f, h = ImGui::GetFrameHeight();
    if (*x > 0 && *x + w > max_x) *x = 0;
    else if (*x > 0) ImGui::SameLine(0, st.ItemSpacing.x * 0.6f);
    ImGui::PushID(id);
    const bool pressed = ImGui::InvisibleButton("##chip", ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = h * 0.5f;
    if (on) dl->AddRectFilled(a, b, kAccentSoft, r);
    else if (hovered) dl->AddRectFilled(a, b, kRaised, r);
    dl->AddRect(a, b, on ? kAccent : kPanelLine, r, 0, 1.0f);
    const ImU32 col = on ? kText : dim ? kTextFaint : kTextDim;
    dl->AddText(ImVec2(a.x + (w - ts.x) * 0.5f, a.y + (h - ts.y) * 0.5f), col, text);
    ImGui::PopID();
    *x += w + st.ItemSpacing.x * 0.6f;
    return pressed;
}
} // namespace

void App::refresh_voice_library() {
    voice_profiles_ = dub::list_profiles();
    voice_profiles_loaded_ = true;
}

// API-ключ: у налаштуваннях — лише зашифрований (DPAPI); на екрані — крапки
bool App::draw_key_field(const char* id, std::string* stored) {
    const float fs_ = ImGui::GetFontSize();
    bool changed = false;
    ImGui::PushID(id);
    bool& editing = key_editing_[id];
    if (!editing && !stored->empty()) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kColOk, "%s", tr("••••••••  збережено (зашифровано)"));
        ImGui::SameLine();
        if (action_button(tr("Змінити"), Kind::Ghost)) {
            editing = true;
            key_edit_[id].clear();
        }
        ImGui::SameLine();
        if (action_button(tr("Прибрати"), Kind::Ghost)) {
            stored->clear();
            changed = true;
        }
    } else {
        std::string& buf = key_edit_[id];
        ImGui::SetNextItemWidth(std::min(field_width(26), fs_ * 22));
        const bool enter = ImGui::InputTextWithHint("##key", tr("вставте ключ"), &buf,
                                                    ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        ImGui::BeginDisabled(trim(buf).empty());
        if (action_button(tr("Зберегти"), Kind::Secondary) || (enter && !trim(buf).empty())) {
            *stored = protect_secret(trim(buf));
            buf.assign(buf.size(), '\0');   // не тримати відкритий ключ у пам'яті довше, ніж треба
            buf.clear();
            editing = false;
            changed = true;
        }
        ImGui::EndDisabled();
        if (!stored->empty()) {
            ImGui::SameLine();
            if (action_button(tr("Скасувати"), Kind::Ghost)) {
                buf.clear();
                editing = false;
            }
        }
    }
    ImGui::PopID();
    return changed;
}

void App::start_translate_only() {
    if (!analysis_ || !voices_ || job_running()) return;
    job_ = std::make_unique<render::TranslateJob>(s_, analysis_, voices_, !whole_demo_);
    job_reported_ = false;
    job_->start();
}

void App::start_voice_engine(bool install, bool cuda) {
    if (job_running()) {
        log_warn("{}", trf("Зачекайте завершення поточного завдання"));
        return;
    }
    service_status_.clear();
    job_ = std::make_unique<render::VoiceEngineJob>(install ? render::VoiceEngineJob::Action::Install : render::VoiceEngineJob::Action::Check,
                                                    cuda, s_);
    job_reported_ = false;
    job_->start();
}

void App::start_service_check(bool elevenlabs) {
    if (job_running()) {
        log_warn("{}", trf("Зачекайте завершення поточного завдання"));
        return;
    }
    service_status_.clear();
    job_ = std::make_unique<render::ServiceCheckJob>(elevenlabs ? render::ServiceCheckJob::What::ElevenLabs
                                                                : render::ServiceCheckJob::What::Translator,
                                                     s_);
    job_reported_ = false;
    job_->start();
}

// ============================== Сторінка =============================================
void App::draw_page_translate() {
    page_header(tr("Переклад і озвучення"),
                tr("Розмови гравців — іншими мовами: перекладені субтитри й озвучення голосом самого гравця чи "
                   "готовими голосами. Робиться після рендеру, з розпізнаного мовлення."));
    refresh_whisper_status();
    if (!whisper_ok_) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(kColWarn, "%s",
                           tr("Спершу потрібне розпізнавання мовлення: на сторінці «Чат і мовлення» завантажте модель whisper."));
        ImGui::PopTextWrapPos();
        if (page_visible(Page::Chat)) {
            if (action_button(tr("До сторінки «Чат і мовлення»"), Kind::Secondary, 0, Icon::Chat)) go_to(Page::Chat);
        } else if (action_button(tr("Завантажити модель..."), Kind::Secondary, 0, Icon::Download)) {
            open_model_popup_ = true;
        }
        ImGui::Spacing();
    }
    draw_translate_languages();
    draw_translate_outputs();
    draw_translator_card();
    draw_voice_engine_card();
    draw_voice_library_card();
    draw_model_popup();
}

void App::draw_translate_languages() {
    if (!card_begin(tr("Мови"), tr("На які мови перекладати. Мову оригіналу розпізнавання визначає саме або її вказано на сторінці «Чат і мовлення»."),
                    Icon::Globe)) {
        card_end();
        return;
    }
    auto langs = render::dub_language_list(s_);
    float x = 0;
    const float max_x = ImGui::GetContentRegionAvail().x;
    bool changed = false;
    for (const auto& l : translate::languages()) {
        const bool on = has_code(langs, l.code);
        const bool tr_ok = translate::supports(s_.translator, l.code);
        const bool tts_ok = !s_.dub || dub::engine_supports(s_.tts_engine, s_.elevenlabs_model, l.code);
        if (chip(l.native, l.code, on, !tr_ok || !tts_ok, &x, max_x)) {
            if (on) std::erase(langs, std::string(l.code));
            else langs.push_back(l.code);
            changed = true;
        }
        if (ImGui::IsItemHovered()) {
            std::string tip = std::format("{} ({})", l.english, l.code);
            if (!tr_ok) tip += "\n" + trf("{} не перекладає на цю мову — виберіть інший сервіс",
                                          tr(translate::find_provider(s_.translator) ? translate::find_provider(s_.translator)->label : "?"));
            if (!tts_ok) tip += "\n" + std::string(tr("Вибраний рушій не озвучує цю мову (лише субтитри)"));
            ImGui::SetTooltip("%s", tip.c_str());
        }
    }
    if (changed) {
        s_.dub_languages = join_codes(langs);
        mark_dirty();
    }
    ImGui::Spacing();
    if (langs.empty()) ImGui::TextColored(kColDim, "%s", tr("Виберіть одну чи кілька мов."));
    else {
        std::string names;
        for (const auto& c : langs)
            if (const auto* l = translate::find_language(c)) names += (names.empty() ? "" : ", ") + std::string(l->native);
        ImGui::TextColored(kColDim, "%s", trf("Вибрано: {}", names).c_str());
        ImGui::SameLine();
        if (action_button(tr("Очистити"), Kind::Ghost)) {
            s_.dub_languages.clear();
            mark_dirty();
        }
    }
    card_end();
}

void App::draw_translate_outputs() {
    const float fs_ = ImGui::GetFontSize();
    if (!card_begin(tr("Що зробити"), tr("Шаблон вмикає все потрібне для сервісу; далі можна змінити вручну."), Icon::Sparkle)) {
        card_end();
        return;
    }
    bool changed = false;
    // Шаблони публікації
    subheading(tr("Шаблон публікації"));
    for (const auto& t : render::publish_templates()) {
        const bool cur = s_.dub_template == t.id;
        const char* lbl = tr(t.label);
        if (ImGui::GetCursorPosX() > ImGui::GetStyle().WindowPadding.x + 1 &&
            ImGui::GetContentRegionAvail().x < action_width(lbl, Kind::Secondary) + ImGui::GetStyle().ItemSpacing.x)
            ImGui::NewLine();
        if (action_button(std::format("{}##tpl{}", lbl, t.id).c_str(), cur ? Kind::Cta : Kind::Secondary)) {
            render::apply_template(s_, t);
            s_.dub = true;   // шаблони — про те, куди озвучення
            changed = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(fs_ * 26);
            ImGui::TextUnformatted(tr(t.hint));
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();
    if (const auto* t = render::find_template(s_.dub_template)) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(kColDim, "%s", tr(t->hint));
        ImGui::PopTextWrapPos();
    }
    ImGui::Spacing();
    subheading(tr("Результат"));
    changed |= toggle(tr("Перекладені субтитри (.srt для кожної мови)"), &s_.translate_subtitles);
    if (toggle(tr("Озвучити переклад"), &s_.dub)) changed = true;
    help_marker(tr("Синтезований голос читає переклад на місці кожної репліки; звук гри лишається. Оригінальні\n"
                   "голоси можна залишити тихо під озвученням. Довша фраза трохи пришвидшується."));
    ImGui::BeginDisabled(!s_.dub);
    ImGui::Indent(fs_ * 1.2f);
    auto out_toggle = [&](const char* text, const char* id) {
        bool on = render::dub_output(s_, id);
        if (checkbox(text, &on)) {
            std::vector<std::string> v;
            for (const char* k : {"tracks", "videos", "audio"})
                if (k == std::string(id) ? on : render::dub_output(s_, k)) v.push_back(k);
            s_.dub_outputs = join_codes(v);
            changed = true;
        }
    };
    out_toggle(tr("Доріжки в цьому відео (з мітками мови)"), "tracks");
    out_toggle(tr("Окреме відео для кожної мови"), "videos");
    out_toggle(tr("Окремі аудіофайли"), "audio");
    if (render::dub_output(s_, "audio")) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fs_ * 6);
        if (begin_combo("##dubfmt", s_.dub_audio_format.c_str())) {
            for (const auto& f : dub::audio_formats())
                if (ImGui::Selectable(f.id, s_.dub_audio_format == f.id)) {
                    s_.dub_audio_format = f.id;
                    changed = true;
                }
            ImGui::EndCombo();
        }
    }
    ImGui::Unindent(fs_ * 1.2f);
    label(tr("Оригінальні голоси"));
    float ov = static_cast<float>(s_.dub_original_volume * 100);
    if (slider_float("##dubov", &ov, 0, 50, "%.0f%%", field_width(18))) {
        s_.dub_original_volume = ov / 100.0;
        changed = true;
    }
    help_marker(tr("Гучність оригінальних голосів під озвученням: 0 — лише переклад, 10–15% — «закадровий» переклад."));
    if (s_.dub && !s_.audio) ImGui::TextColored(kColWarn, "%s", tr("Звук відео вимкнено (сторінка «Звук і голоси») — озвучувати нема на що."));
    ImGui::EndDisabled();
    if (changed) mark_dirty();

    ImGui::Spacing();
    const bool can = analysis_ && voices_ && !voices_->speakers.empty() && whisper_ok_ && !render::dub_language_list(s_).empty();
    ImGui::BeginDisabled(!can || job_running());
    if (action_button(tr("Перекласти субтитри зараз"), Kind::Secondary, 0, Icon::Chat)) start_translate_only();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", tr("Без рендеру: розпізнати мовлення (якщо ще ні) і записати перекладені субтитри\n"
                                  "поруч із вихідним файлом. Час — від початку фрагмента (або демо, якщо вибрано все)."));
    card_end();
}

void App::draw_translator_card() {
    if (!card_begin(tr("Сервіс перекладу"), tr("Онлайн-сервіс з ключем або локальна мовна модель на вашому ПК."), Icon::Link)) {
        card_end();
        return;
    }
    bool changed = false;
    const translate::ProviderInfo* cur = translate::find_provider(s_.translator);
    label(tr("Сервіс"));
    ImGui::SetNextItemWidth(field_width(22));
    if (begin_combo("##translator", cur ? tr(cur->label) : s_.translator.c_str())) {
        for (const auto& p : translate::providers()) {
            if (std::string(p.id) == "fake" && !show_test_services()) continue;
            if (ImGui::Selectable(tr(p.label), s_.translator == p.id)) {
                s_.translator = p.id;
                service_status_.clear();
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    cur = translate::find_provider(s_.translator);
    if (cur) {
        const std::string id = cur->id;
        const char* hint = id == "deepl"  ? tr("Найприродніший переклад. Безкоштовний ключ — 500 000 символів на місяць (deepl.com/pro-api).")
                         : id == "google" ? tr("Google Cloud Translation: ключ API з увімкненим Cloud Translation API.")
                         : id == "libre"  ? tr("Свій сервер LibreTranslate (безкоштовно, локально) або публічний з ключем.")
                         : id == "openai" ? tr("Локально й безкоштовно: Ollama (ollama pull qwen2.5:7b) чи LM Studio. Або хмара "
                                               "з OpenAI-сумісним API (OpenAI, OpenRouter…) — тоді потрібен ключ.")
                                          : "";
        if (*hint) {
            ImGui::PushTextWrapPos(0);
            ImGui::TextColored(kColDim, "%s", hint);
            ImGui::PopTextWrapPos();
        }
        if (cur->uses_url) {
            label(tr("Адреса"));
            ImGui::SetNextItemWidth(field_width(26));
            changed |= ImGui::InputTextWithHint("##trurl", cur->default_url, &s_.translator_url);
        }
        if (cur->uses_model) {
            label(tr("Модель"));
            ImGui::SetNextItemWidth(field_width(18));
            changed |= ImGui::InputTextWithHint("##trmodel", "qwen2.5:7b", &s_.translator_model);
        }
        if (std::string* key = render::translator_key_field(s_, id)) {
            label(cur->needs_key ? tr("Ключ API") : tr("Ключ API (якщо треба)"));
            changed |= draw_key_field(("tr_" + id).c_str(), key);
        }
        ImGui::Spacing();
        ImGui::BeginDisabled(job_running());
        if (action_button(tr("Перевірити"), Kind::Secondary, 0, Icon::Check)) start_service_check(false);
        ImGui::EndDisabled();
        if (!service_status_.empty() && dynamic_cast<render::ServiceCheckJob*>(job_.get())) {
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(service_status_ok_ ? kColOk : kColErr, "%s", service_status_.c_str());
        }
        if (id != "fake" && !(id == "libre" || id == "openai") ) {
            ImGui::PushTextWrapPos(0);
            ImGui::TextColored(kColDim, "%s", tr("Текст реплік надсилається цьому сервісу. Переклади кешуються — той самий рядок удруге не оплачується."));
            ImGui::PopTextWrapPos();
        }
    }
    if (changed) mark_dirty();
    card_end();
}

void App::draw_voice_engine_card() {
    const float fs_ = ImGui::GetFontSize();
    if (!card_begin(tr("Озвучення"), tr("Чим озвучувати переклад і чиїм голосом."), Icon::Mic)) {
        card_end();
        return;
    }
    bool changed = false;
    ImGui::BeginDisabled(!s_.dub);
    int eng = s_.tts_engine == "elevenlabs" ? 1 : s_.tts_engine == "fake" ? 2 : 0;
    const float seg_w = std::min(ImGui::GetContentRegionAvail().x, fs_ * 22);
    if (show_test_services() ? segmented("##tts", &eng, {tr("Локально (OmniVoice)"), "ElevenLabs", "Test"}, seg_w)
                             : segmented("##tts", &eng, {tr("Локально (OmniVoice)"), "ElevenLabs"}, seg_w)) {
        s_.tts_engine = eng == 1 ? "elevenlabs" : eng == 2 ? "fake" : "omnivoice";
        service_status_.clear();
        changed = true;
    }
    ImGui::Spacing();
    const render::RenderSettings& cs = s_;
    if (s_.tts_engine == "omnivoice") {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(kColDim, "%s", tr("OmniVoice (k2-fsa, Apache-2.0): 600+ мов, зокрема українська, англійська й російська; "
                                            "клонує голос із кількох секунд зразка. Працює на вашому ПК, нічого нікуди не надсилає."));
        ImGui::PopTextWrapPos();
        if (nvidia_gpu_ < 0) nvidia_gpu_ = dub::has_nvidia_gpu() ? 1 : 0;
        const auto py = dub::engine_python(render::tts_config(cs));
        label(tr("Стан"));
        if (py) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(kColOk, "%s", s_.tts_python.empty() ? tr("встановлено") : tr("свій Python"));
        } else {
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(kColWarn, "%s", tr("не встановлено"));
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(job_running());
        if (!py && s_.tts_python.empty()) {
            if (action_button(tr("Встановити..."), Kind::Cta, 0, Icon::Download)) {
                engine_cuda_ = nvidia_gpu_ == 1;
                open_engine_popup_ = true;
            }
        } else {
            if (action_button(tr("Перевірити"), Kind::Secondary, 0, Icon::Check)) start_voice_engine(false, false);
            if (s_.tts_python.empty()) {
                ImGui::SameLine();
                if (action_button(tr("Видалити"), Kind::Ghost)) {
                    std::string err;
                    if (!dub::remove_engine(&err)) log_warn("{}", trf("Не вдалося видалити: {}", err));
                    service_status_.clear();
                }
            }
        }
        ImGui::EndDisabled();
        if (!service_status_.empty() && dynamic_cast<render::VoiceEngineJob*>(job_.get())) {
            ImGui::TextColored(service_status_ok_ ? kColOk : kColErr, "%s", service_status_.c_str());
        }
        label(tr("Пристрій"));
        int dev = s_.tts_device == "cuda" ? 1 : s_.tts_device == "cpu" ? 2 : 0;
        if (segmented("##ttsdev", &dev, {tr("Авто"), "GPU (CUDA)", tr("Процесор")})) {
            s_.tts_device = dev == 1 ? "cuda" : dev == 2 ? "cpu" : "auto";
            changed = true;
        }
        if (nvidia_gpu_ == 0) {
            ImGui::TextColored(kColWarn, "%s", tr("GPU NVIDIA не знайдено — на процесорі озвучення в рази повільніше."));
        }
        if (s_.ui_advanced) {
            label(tr("Свій Python"));
            ImGui::SetNextItemWidth(field_width(26));
            changed |= ImGui::InputTextWithHint("##ttspy", tr("порожньо — встановлений програмою"), &s_.tts_python);
            help_marker(tr("Python, у якому вже є пакет omnivoice (pip install omnivoice) — напр. на Linux."));
        }
    } else if (s_.tts_engine == "elevenlabs") {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(kColDim, "%s", tr("ElevenLabs — хмарний сервіс із природними голосами; потрібен ключ (elevenlabs.io → API Keys). "
                                            "Текст перекладу, а з клонуванням — і зразки голосів гравців надсилаються в ElevenLabs."));
        ImGui::PopTextWrapPos();
        label(tr("Ключ API"));
        changed |= draw_key_field("el", &s_.elevenlabs_key);
        label(tr("Модель"));
        ImGui::SetNextItemWidth(field_width(18));
        static const std::pair<const char*, const char*> kModels[] = {
            {"eleven_multilingual_v2", "Multilingual v2 (29)"},
            {"eleven_v3", "Eleven v3 (70+)"},
            {"eleven_turbo_v2_5", "Turbo v2.5 (32)"},
            {"eleven_flash_v2_5", "Flash v2.5 (32)"}};
        std::string ml = s_.elevenlabs_model;
        for (const auto& [id, l] : kModels)
            if (s_.elevenlabs_model == id) ml = l;
        if (begin_combo("##elmodel", ml.c_str())) {
            for (const auto& [id, l] : kModels)
                if (ImGui::Selectable(l, s_.elevenlabs_model == id)) {
                    s_.elevenlabs_model = id;
                    changed = true;
                }
            ImGui::EndCombo();
        }
        help_marker(tr("У дужках — скільки мов. v3 — найбільше мов і найвиразніше; Flash — найшвидше й найдешевше."));
        if (s_.ui_advanced) {
            label(tr("Голос для всіх"));
            ImGui::SetNextItemWidth(field_width(18));
            changed |= ImGui::InputTextWithHint("##elvoice", tr("порожньо — різні готові голоси"), &s_.elevenlabs_voice);
            help_marker(tr("voice_id голосу з вашої бібліотеки ElevenLabs — для гравців без клону."));
        }
        ImGui::BeginDisabled(job_running() || s_.elevenlabs_key.empty());
        if (action_button(tr("Перевірити ключ"), Kind::Secondary, 0, Icon::Check)) start_service_check(true);
        ImGui::EndDisabled();
        if (!service_status_.empty() && dynamic_cast<render::ServiceCheckJob*>(job_.get())) {
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(service_status_ok_ ? kColOk : kColErr, "%s", service_status_.c_str());
        }
    }
    ImGui::Spacing();
    subheading(tr("Голоси гравців"));
    bool clone = s_.tts_clone && s_.tts_clone_ack;
    if (toggle(tr("Озвучувати голосом самого гравця (клонування)"), &clone)) {
        if (clone && !s_.tts_clone_ack) {
            consent_for_library_ = false;
            consent_checked_ = false;
            open_consent_popup_ = true;
        } else {
            s_.tts_clone = clone;
            changed = true;
        }
    }
    help_marker(tr("Голос кожного гравця клонується зі зразків його ж фраз у демо (3–10 с чистого мовлення).\n"
                   "Без клонування кожен гравець отримує свій готовий голос."));
    if (s_.tts_clone_ack) {
        ImGui::TextColored(kColDim, "%s", tr("Згоду гравців підтверджено."));
        ImGui::SameLine();
        if (ImGui::TextLink(tr("Відкликати"))) {
            s_.tts_clone_ack = false;
            s_.tts_clone = false;
            s_.voice_library_auto = false;
            changed = true;
        }
    }
    ImGui::EndDisabled();
    if (changed) mark_dirty();
    card_end();
}

void App::draw_voice_library_card() {
    const float fs_ = ImGui::GetFontSize();
    if (!card_begin(tr("Бібліотека голосів"), tr("Зразки голосів гравців (за SteamID) для клонування — з кожного нового демо додаються найчистіші фрази."),
                    Icon::Library, true, false)) {
        card_end();
        return;
    }
    if (!voice_profiles_loaded_) refresh_voice_library();
    bool lib = s_.voice_library_auto && s_.tts_clone_ack;
    if (toggle(tr("Накопичувати зразки з кожного розпізнаного демо"), &lib)) {
        if (lib && !s_.tts_clone_ack) {
            consent_for_library_ = true;
            consent_checked_ = false;
            open_consent_popup_ = true;
        } else {
            s_.voice_library_auto = lib;
            mark_dirty();
        }
    }
    help_marker(tr("Що більше зразків, то точніше клон. Зберігаються лише на цьому ПК, у теці програми;\n"
                   "гравці без SteamID (боти) не накопичуються."));
    ImGui::Spacing();
    if (voice_profiles_.empty()) {
        ImGui::TextColored(kColDim, "%s", tr("Поки порожньо."));
    } else if (ImGui::BeginTable("##voicelib", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn(tr("Гравець"), ImGuiTableColumnFlags_WidthStretch, 3);
        ImGui::TableSetupColumn(tr("Зразків"), ImGuiTableColumnFlags_WidthFixed, fs_ * 4.5f);
        ImGui::TableSetupColumn(tr("Секунд"), ImGuiTableColumnFlags_WidthFixed, fs_ * 4.5f);
        ImGui::TableSetupColumn(tr("Клон"), ImGuiTableColumnFlags_WidthFixed, fs_ * 6);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, fs_ * 2);
        ImGui::TableHeadersRow();
        std::string remove_key;
        for (const auto& p : voice_profiles_) {
            ImGui::PushID(p.key.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(p.name().c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.key.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%zu", p.samples.size());
            ImGui::TableNextColumn();
            ImGui::Text("%.0f", p.total_seconds());
            ImGui::TableNextColumn();
            if (!p.elevenlabs_voice_id.empty()) ImGui::TextColored(kColOk, "ElevenLabs");
            ImGui::TableNextColumn();
            if (icon_button("##del", Icon::Close, tr("Видалити зразки цього гравця"))) remove_key = p.key;
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (!remove_key.empty()) {
            dub::delete_profile(remove_key);
            refresh_voice_library();
        }
    }
    ImGui::Spacing();
    if (action_button(tr("Оновити"), Kind::Ghost)) refresh_voice_library();
    ImGui::SameLine();
    if (action_button(tr("Відкрити теку"), Kind::Ghost, 0, Icon::Folder)) {
        std::error_code ec;
        std::filesystem::create_directories(dub::voices_dir(), ec);
        open_path(path_to_utf8(dub::voices_dir()));
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(voice_profiles_.empty());
    if (action_button(tr("Очистити все..."), Kind::Ghost)) open_clear_voices_popup_ = true;
    ImGui::EndDisabled();
    card_end();
}

// ============================== Попапи ===============================================
void App::draw_translate_popups() {
    const float fs_ = ImGui::GetFontSize();
    auto title = [&](const char* t) {
        ImGui::PushFont(bold_font(), ImGui::GetStyle().FontSizeBase * 1.15f);
        ImGui::TextUnformatted(t);
        ImGui::PopFont();
        ImGui::Spacing();
    };
    // Згода гравців на клонування голосу
    if (open_consent_popup_) {
        ImGui::OpenPopup("##consent");
        open_consent_popup_ = false;
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(fs_ * 26, 0), ImVec2(fs_ * 36, fs_ * 40));
    if (ImGui::BeginPopupModal("##consent", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        title(tr("Клонування голосів гравців"));
        ImGui::PushTextWrapPos(fs_ * 34);
        ImGui::TextUnformatted(tr("Голос — особисті дані. Клон звучить як сама людина, тому озвучувати ним переклад можна лише "
                                  "з її дозволу, і глядач має знати, що голос синтезований (доріжки підписуються «озвучення ШІ»)."));
        ImGui::Spacing();
        ImGui::TextUnformatted(tr("Зразки голосів зберігаються лише на цьому ПК; з ElevenLabs вони надсилаються в їхній сервіс для "
                                  "створення клону. Бібліотеку можна будь-коли очистити."));
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        checkbox(tr("Гравці в моїх демо погодились на клонування своїх голосів"), &consent_checked_);
        ImGui::Spacing();
        ImGui::BeginDisabled(!consent_checked_);
        if (action_button(tr("Підтвердити"), Kind::Cta)) {
            s_.tts_clone_ack = true;
            if (consent_for_library_) s_.voice_library_auto = true;
            else s_.tts_clone = true;
            mark_dirty();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (action_button(tr("Скасувати"), Kind::Secondary)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    // Встановлення локального рушія: великий обсяг — лише з підтвердженням
    if (open_engine_popup_) {
        ImGui::OpenPopup("##engine");
        open_engine_popup_ = false;
    }
    if (ImGui::BeginPopupModal("##engine", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        title(tr("Встановити локальний рушій озвучення?"));
        ImGui::PushTextWrapPos(fs_ * 34);
        ImGui::TextUnformatted(trf("Буде завантажено близько {} з інтернету: менеджер пакетів uv, Python 3.12, PyTorch{}, OmniVoice і "
                                   "його модель. Усе — в одну теку, в системі нічого не змінюється; видалити можна тут же.",
                                   engine_cuda_ ? tr("5 ГБ") : tr("2 ГБ"), engine_cuda_ ? tr(" з CUDA") : "")
                                   .c_str());
        ImGui::Spacing();
        ImGui::TextColored(kColDim, "%s", path_to_utf8(dub::engine_dir()).c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (nvidia_gpu_ == 1) checkbox(tr("Для GPU NVIDIA (CUDA, значно швидше)"), &engine_cuda_);
        else ImGui::TextColored(kColWarn, "%s", tr("GPU NVIDIA не знайдено — версія для процесора."));
        ImGui::Spacing();
        if (action_button(tr("Встановити"), Kind::Cta, 0, Icon::Download)) {
            start_voice_engine(true, engine_cuda_ && nvidia_gpu_ == 1);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (action_button(tr("Скасувати"), Kind::Secondary)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    // Очистити бібліотеку голосів
    if (open_clear_voices_popup_) {
        ImGui::OpenPopup("##clearvoices");
        open_clear_voices_popup_ = false;
    }
    if (ImGui::BeginPopupModal("##clearvoices", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        title(tr("Очистити бібліотеку голосів?"));
        ImGui::TextUnformatted(trf("Буде видалено зразки всіх гравців ({}).", voice_profiles_.size()).c_str());
        ImGui::Spacing();
        if (action_button(tr("Видалити все"), Kind::Negative)) {
            std::error_code ec;
            std::filesystem::remove_all(dub::voices_dir(), ec);
            refresh_voice_library();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (action_button(tr("Скасувати"), Kind::Secondary)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

} // namespace gmdr::gui
