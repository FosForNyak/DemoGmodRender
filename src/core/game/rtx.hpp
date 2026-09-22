// =============================================================================
//  rtx.hpp — рендер з трасуванням променів через копію гри від RTXLauncher
//  (RTX Remix). Свій RTX програма не малює: вона лише запускає RTX-копію гри
//  і на час рендеру підставляє в rtx.conf налаштування для офлайн-рендеру.
//
//  Що з'ясовано на практиці (GMod x86-64, Remix 12759b3):
//   * startmovie записує кадр з RTX і з HUD, і без нього — але лише коли вікно
//     гри на моніторі. За межами екрана Remix віддає чорні кадри, тож для RTX
//     вікно тримається "позаду інших вікон";
//   * шар налаштувань користувача Remix (user_settings) і пресет якості мають
//     пріоритет над rtx.conf, тому пресет ставиться у Custom.
// =============================================================================
#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "gmod_install.hpp"

namespace gmdr::game {

// Копія гри, яку поставив RTXLauncher: шлях береться з його settings.xml
// (%LOCALAPPDATA%\RTXLauncher\settings.xml → ManuallySpecifiedInstallPath),
// інакше — стандартна папка %LOCALAPPDATA%\RTXLauncher\Game.
std::optional<GModInstall> detect_rtx_install(std::vector<std::string>* log = nullptr);

// Чи це RTX-копія (є rtx.conf або Remix-овий d3d9.dll поруч із грою).
bool is_rtx_install(const GModInstall& g);

// Налаштування Remix для рендеру: повна роздільна здатність (DLAA), без
// генерації кадрів, без заставки. Оригінальний rtx.conf зберігається в backup.
bool apply_rtx_render_profile(const GModInstall& g, const std::filesystem::path& backup, std::string* error);
bool restore_rtx_profile(const GModInstall& g, const std::filesystem::path& backup);

// Ключі, які підставляє профіль (для перевірки за журналом Remix).
const std::map<std::string, std::string>& rtx_render_profile_values();

// Підсумкові значення параметрів Remix з його журналу (rtx-remix/logs/remix-dxvk.log).
std::map<std::string, std::string> read_remix_effective_options(const GModInstall& g);

} // namespace gmdr::game
