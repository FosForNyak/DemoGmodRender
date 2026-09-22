// =============================================================================
//  file_util.hpp — читання/запис файлів цілком, безпечні файлові операції.
// =============================================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace gmdr {

namespace fs = std::filesystem;

std::optional<std::vector<uint8_t>> read_file_bytes(const fs::path& p, std::string* error = nullptr);
std::optional<std::string>          read_file_text(const fs::path& p, std::string* error = nullptr);
bool write_file_text(const fs::path& p, const std::string& text, std::string* error = nullptr);
// Атомарний запис: пише у тимчасовий файл поруч і перейменовує.
bool write_file_atomic(const fs::path& p, const std::string& text, std::string* error = nullptr);

// Копіювання з перезаписом (надійніше за fs::copy_file на різних компіляторах).
bool copy_file_overwrite(const fs::path& from, const fs::path& to, std::string* error = nullptr);

uint64_t file_size_or_zero(const fs::path& p);
bool     remove_file_quiet(const fs::path& p);
uint64_t free_disk_space(const fs::path& p);

// Каталог, де лежить наш .exe.
fs::path executable_dir();

// Каталог для налаштувань і журналу: поруч з .exe ("портативно"), а якщо туди
// не можна писати (наприклад, Program Files) — %LOCALAPPDATA%\GModDemoRender
// (на Linux — ~/.config/gmod-demo-render).
fs::path app_data_dir();

// Унікальний ідентифікатор (для завдань рендеру).
std::string make_unique_id();

} // namespace gmdr
