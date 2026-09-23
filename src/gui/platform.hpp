// =============================================================================
//  platform.hpp — все, що залежить від операційної системи у GUI:
//  створення вікна, відеокарта для малювання інтерфейсу, діалоги файлів,
//  перетягування файлів у вікно.
//
//  Windows: Win32 + Direct3D 11 (platform_win32.cpp)
//  Linux:   GLFW + OpenGL 3   (platform_glfw.cpp) — для розробки/тестів
// =============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace gmdr::gui {

struct PlatformCallbacks {
    std::function<void(const std::vector<std::string>&)> on_files_dropped;   // шляхи в UTF-8
    std::function<bool()>                                on_close_request;   // true — дозволити закриття
};

bool  platform_init(const std::string& title, int width, int height, const PlatformCallbacks& cb);
// Обробити події вікна; false — програму закривають.
bool  platform_begin_frame();
void  platform_end_frame(const float clear_rgba[4]);
void  platform_shutdown();
float platform_dpi_scale();
void  platform_set_title(const std::string& title);

using FileFilter = std::pair<std::string, std::string>;   // {"Демо GMod", "*.dem"}
std::string open_file_dialog(const std::string& title, const std::vector<FileFilter>& filters,
                             const std::string& initial_path = {});
std::string save_file_dialog(const std::string& title, const std::vector<FileFilter>& filters,
                             const std::string& initial_path = {}, const std::string& default_ext = {});
std::string pick_folder_dialog(const std::string& title, const std::string& initial_path = {});

void open_path(const std::string& path);              // відкрити файл програмою за замовчуванням
void show_in_folder(const std::string& path);         // показати файл у провіднику
std::vector<std::string> ui_font_candidates();        // шрифти з кирилицею
std::vector<std::string> ui_symbol_font_candidates(); // резервні шрифти із символами (✓ ✗ ▶)
std::string clipboard_text_set(const std::string& text);   // скопіювати в буфер обміну

// Для автоматичних тестів: зберегти знімок вікна у PNG (лише Linux-версія).
bool platform_screenshot(const std::string& path);

// Програма вже відкрита: передати їй файл з args (подвійний клік на .dem), показати її вікно
// і повернути true — тоді цей запуск просто завершується. (Лише Windows.)
bool platform_forward_to_running_instance(const std::vector<std::string>& args);

// Текстура з пікселів RGBA (живе прев'ю кадрів). tex — попередня текстура (0 — нова);
// повертає ідентифікатор для ImGui::Image (0 — не вдалося).
uint64_t platform_update_texture(uint64_t tex, int width, int height, const uint8_t* rgba);
void     platform_destroy_texture(uint64_t tex);

// Прогрес на кнопці програми в панелі задач Windows.
enum class TaskbarState { None, Normal, Paused, Error, Indeterminate };
void platform_set_taskbar_progress(TaskbarState state, double fraction);
// Привернути увагу (блимнути кнопкою в панелі задач), якщо вікно не активне.
void platform_flash_window();

} // namespace gmdr::gui
