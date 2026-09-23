// =============================================================================
//  update_check.hpp — чи вийшла нова версія (GitHub Releases).
//
//  Лише коли користувач сам натисне «Довідка → Перевірити оновлення»: один запит
//  https://api.github.com/repos/<repo>/releases/latest, без жодних даних про ПК.
//  Приватний репозиторій або без релізів — GitHub відповідає 404, про це й кажемо.
// =============================================================================
#pragma once

#include <optional>
#include <string>

namespace gmdr {

constexpr const char* kUpdateRepo = "FosForNyak/DemoGmodRender";

struct ReleaseInfo {
    std::string version;     // "1.3.0" (без "v")
    std::string url;         // сторінка релізу
    std::string published;   // "2026-10-01"
    std::string notes;       // перші рядки опису
};

// Порівняти версії "1.2.0" / "v1.10": <0 — a старша, 0 — однакові, >0 — a новіша.
int compare_versions(const std::string& a, const std::string& b);

std::optional<ReleaseInfo> parse_latest_release(const std::string& json, std::string* error);

// Запит до GitHub (Windows — WinHTTP; на інших ОС — помилка з поясненням).
std::optional<ReleaseInfo> fetch_latest_release(const std::string& repo, std::string* error);

} // namespace gmdr
