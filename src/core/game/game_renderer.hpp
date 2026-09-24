// =============================================================================
//  game_renderer.hpp — чим рендериться демо: звичайний Garry's Mod зі Steam чи
//  копія GMod RTX від RTXLauncher (а згодом — інші).
//
//  Рендерер описує сам себе: межі (скільки копій гри одночасно, чи малює у
//  вікні за межами екрана, чи перевірене дописування після збою), вимоги до
//  копії гри, параметри запуску і таймаути, а також що зробити з грою до і
//  після рендеру (RTX: налаштування Remix у rtx.conf і повернення оригіналу).
//  Рендер (render/jobs.cpp) і перевірка налаштувань (config/constraints.hpp)
//  питають рендерер, а не перевіряють "чи це RTX".
//
//  Новий рендерер: клас-нащадок GameRenderer і рядок у game_renderers()
//  (game_renderer.cpp); поле з папкою його копії гри — dir_setting.
// =============================================================================
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "gmod_install.hpp"

namespace gmdr::game {

// Як рендерер поводиться під час рендеру
struct RendererTraits {
    int         max_parallel = 4;            // скільки копій гри можуть рендерити одночасно
    bool        offscreen_window = true;     // малює у вікні за межами екрана
    bool        needs_64bit = false;         // потрібна 64-бітна гра (гілка x86-64)
    bool        resume = true;               // дописування урваного рендеру перевірене
    double      warmup_seconds = 5.0;        // скільки секунд демо програти перед фрагментом
    int         load_timeout_seconds = 0;    // скільки драйвер чекає на демо (0 — типово)
    int         hang_seconds = 30;           // без кадрів довше — гра зависла (якщо не зайнята)
    int         busy_wait_minutes = 5;       // скільки чекати на зайняту гру (процесор працює)
    bool        black_frame_check = false;   // перевіряти, що в кадрі не чорнота (крок «RTX у кадрі»)
    std::string busy_hint;                   // чим, імовірно, зайнята гра (для журналу)
    std::vector<std::string> launch_args;    // додаткові параметри запуску гри
    std::string dir_setting = "game_dir";    // поле налаштувань з папкою копії гри
};

class GameRenderer {
public:
    virtual ~GameRenderer() = default;
    virtual std::string id() const = 0;            // для налаштувань і CLI: standard, rtx
    virtual std::string label() const = 0;         // назва в інтерфейсі
    virtual std::string description() const = 0;   // що це і коли брати
    virtual const RendererTraits& traits() const = 0;
    // Копію гри не знайдено: що зробити
    virtual std::string not_found_message() const = 0;
    virtual std::string install_url() const { return {}; }
    // Автопошук копії гри цього рендерера
    virtual std::optional<GModInstall> detect(std::vector<std::string>* log) const = 0;
    // Чи це копія саме для цього рендерера (RTX: є rtx.conf або Remix)
    virtual bool accepts(const GModInstall& /*g*/) const { return true; }
    // Перед рендером: підготувати гру; оригінали файлів — у backup_dir (одна тека на чергу)
    virtual void prepare(const GModInstall& /*g*/, const std::filesystem::path& /*backup_dir*/) const {}
    // Після рендеру (і після збою): повернути файли гри з backup_dir; remove — прибрати копію.
    // true — було що повертати
    virtual bool restore(const GModInstall& /*g*/, const std::filesystem::path& /*backup_dir*/, bool /*remove*/) const {
        return false;
    }
    // Після рендеру: звірити, що гра прийняла налаштування (лише в журнал)
    virtual void after_render(const GModInstall& /*g*/) const {}
};

// Усі рендерери в порядку показу
const std::vector<const GameRenderer*>& game_renderers();
const GameRenderer* find_game_renderer(const std::string& id);   // nullptr — невідомий
const GameRenderer& standard_renderer();

} // namespace gmdr::game
