// =============================================================================
//  process.hpp — запуск гри та керування процесом (пауза/продовження/завершення).
//
//  Пауза процесу використовується як "зворотний тиск": якщо кодування не
//  встигає за грою і на диску накопичилось забагато кадрів, ми тимчасово
//  "заморожуємо" гру, поки кодер не наздожене. Гра цього не помічає, бо
//  час у ній під час рендеру фіксований (host_framerate).
// =============================================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace gmdr::game {

// Де тримати вікно гри під час рендеру.
enum class WindowMode {
    Normal,      // як є (на екрані)
    Behind,      // на екрані, але позаду інших вікон, без фокуса
    Offscreen,   // за межами екрана (гра не видна); згортати не можна — згорнута гра не малює
};
WindowMode window_mode_from_string(const std::string& s);
const char* window_mode_name(WindowMode m);

class GameProcess {
public:
    ~GameProcess();
    GameProcess(const GameProcess&) = delete;
    GameProcess& operator=(const GameProcess&) = delete;

    // no_activate: показати вікно гри без фокуса (не перехоплювати клавіатуру й мишу).
    static std::unique_ptr<GameProcess> launch(const std::filesystem::path& exe, const std::vector<std::string>& args,
                                               const std::filesystem::path& working_dir,
                                               const std::vector<std::pair<std::string, std::string>>& env,
                                               std::string* error, bool no_activate = false);
    // Підключитися до вже запущеного процесу (якщо гра перезапустилась через Steam).
    static std::unique_ptr<GameProcess> attach(uint32_t pid, std::string* error);

    // Знайти процеси з такими іменами файлів (напр. "gmod.exe", "hl2.exe").
    // main_only: лише "головні" — без дочірніх процесів того самого exe (вбудований браузер
    // CEF у GMod запускає копії gmod.exe, і вони живуть ще кілька секунд після виходу гри).
    static std::vector<uint32_t> find_by_name(const std::vector<std::string>& names, bool main_only = false);
    // Дочекатися, поки зникнуть усі процеси з такими іменами. true — зникли.
    static bool wait_all_exited(const std::vector<std::string>& names, int timeout_ms);

    uint32_t pid() const { return pid_; }
    bool     running();
    std::optional<int> exit_code();
    bool     suspend();
    bool     resume();
    bool     suspended() const { return suspended_; }
    void     terminate();
    // Чекати завершення до timeout_ms. true — процес завершився.
    bool     wait(int timeout_ms);
    // Підвищити/знизити пріоритет процесу гри.
    void     set_high_priority(bool high);
    // Windows 11 вмикає для процесу з прихованим вікном енергозбереження (EcoQoS) і
    // перестає виконувати його запити на точний таймер — а на ньому Source тримає темп
    // кадрів. Вимикаємо обидва для гри. true — вдалося.
    bool     disable_background_throttling();
    // Головне вікно гри (0, якщо ще не створене).
    uintptr_t find_main_window() const;
    // Розмістити вікно гри. false — вікна ще немає.
    bool     place_window(WindowMode mode);
    // Показати вікно на екрані поверх інших (кнопка "Показати гру").
    bool     show_window_front();
    // Координати "за межами екрана" (праворуч від усіх моніторів).
    static std::pair<int, int> offscreen_position();

private:
    GameProcess() = default;
    uint32_t pid_ = 0;
    bool     suspended_ = false;
    bool     exited_ = false;
    int      exit_code_ = 0;
#ifdef _WIN32
    void*    handle_ = nullptr;
#endif
};

// Командний рядок для відображення в журналі.
std::string format_command_line(const std::filesystem::path& exe, const std::vector<std::string>& args);

} // namespace gmdr::game
