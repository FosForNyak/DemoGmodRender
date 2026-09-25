// =============================================================================
//  preflight.hpp — перевірка налаштувань перед виконанням: рендер, тестовий
//  прогін, пункт черги, консольне `gmdr-cli check`.
//
//  Швидко оглядає середовище (scan_environment), пробує відкрити вибраний
//  GPU-кодек (інакше він відмовив би лише на першому кадрі, після завантаження
//  гри) і проганяє ті самі правила (evaluate), що й вікно. Помилки — рендер
//  не починається, гра не запускається.
// =============================================================================
#pragma once

#include <string>

#include "constraints.hpp"

namespace gmdr::demo {
struct DemoAnalysis;
}

namespace gmdr::config {

struct PreflightOptions {
    ValidationContext::Purpose purpose = ValidationContext::Purpose::Render;
    const demo::DemoAnalysis*  analysis = nullptr;   // тривалість фрагмента (без неї ці правила мовчать)
    int                        speakers = -1;        // гравців із голосом (-1 — невідомо)
    bool                       probe_gpu = true;     // пробувати відкрити вибраний GPU-кодек
    bool                       resume = false;       // дописування: вихідний файл уже є за задумом
};

struct Preflight {
    EnvironmentCapabilities env;
    ValidationResult        result;
};

Preflight preflight(const render::RenderSettings& s, const PreflightOptions& opt = {});

// Кілька наборів налаштувань підряд (черга): середовище оглядається один раз, кожен GPU-кодек
// пробується один раз; шляхи (демо, мікрофон, вихід) — для кожного набору свої
class PreflightCache {
public:
    ValidationResult check(const render::RenderSettings& s, const PreflightOptions& opt);
    const EnvironmentCapabilities& environment() const { return env_; }

private:
    EnvironmentCapabilities env_;
    bool                    scanned_ = false;
};

// Контекст перевірки з аналізу демо
ValidationContext context_for(ValidationContext::Purpose purpose, const demo::DemoAnalysis* analysis, int speakers = -1);

// Текст для помилки завдання: «Налаштування не дозволяють почати рендер:» і помилки з виправленнями
std::string preflight_error(const ValidationResult& r);

} // namespace gmdr::config
