// =============================================================================
//  dub_jobs.hpp — фонові завдання сторінки «Переклад і озвучення»: встановити чи
//  перевірити локальний рушій озвучення, перевірити сервіс перекладу чи ключ
//  ElevenLabs, перекласти розшифровку демо в субтитри без рендеру.
// =============================================================================
#pragma once

#include <memory>
#include <string>

#include "../demo/analysis.hpp"
#include "../voice/voice_decoder.hpp"
#include "jobs.hpp"
#include "settings.hpp"

namespace gmdr::render {

// Локальний рушій озвучення (OmniVoice): встановити (~5 ГБ) або перевірити
class VoiceEngineJob final : public Job {
public:
    enum class Action { Install, Check };
    VoiceEngineJob(Action a, bool cuda, RenderSettings s) : action_(a), cuda_(cuda), s_(std::move(s)) {}
    std::string name() const override { return tr("Рушій озвучення"); }

protected:
    void run() override;

private:
    Action         action_;
    bool           cuda_;
    RenderSettings s_;
};

// Перевірити сервіс: перекласти пробну фразу чи дізнатися залишок символів ElevenLabs
class ServiceCheckJob final : public Job {
public:
    enum class What { Translator, ElevenLabs };
    ServiceCheckJob(What w, RenderSettings s) : what_(w), s_(std::move(s)) {}
    std::string name() const override { return tr("Перевірка сервісу"); }

protected:
    void run() override;

private:
    What           what_;
    RenderSettings s_;
};

// Перекласти розшифровку демо (розпізнати мовлення, якщо ще ні) у субтитри
// <відео чи демо>.<мова>.srt — без рендеру. range_only — лише вибраний фрагмент.
class TranslateJob final : public Job {
public:
    TranslateJob(RenderSettings s, std::shared_ptr<const demo::DemoAnalysis> analysis,
                 std::shared_ptr<const voice::VoiceDecodeResult> voices, bool range_only)
        : s_(std::move(s)), analysis_(std::move(analysis)), voices_(std::move(voices)), range_only_(range_only) {}
    std::string name() const override { return tr("Переклад субтитрів"); }

protected:
    void run() override;

private:
    RenderSettings                                  s_;
    std::shared_ptr<const demo::DemoAnalysis>       analysis_;
    std::shared_ptr<const voice::VoiceDecodeResult> voices_;
    bool                                            range_only_;
};

} // namespace gmdr::render
