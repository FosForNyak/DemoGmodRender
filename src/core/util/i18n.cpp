#include "i18n.hpp"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace gmdr {

namespace {

struct Pair {
    const char* uk;
    const char* en;
};

const Pair kEnglish[] = {
#include "i18n_en.inc"
};

std::atomic<int> g_lang{static_cast<int>(UiLang::Uk)};

const std::unordered_map<std::string_view, const char*>& table() {
    static const auto t = [] {
        std::unordered_map<std::string_view, const char*> m;
        m.reserve(std::size(kEnglish));
        for (const auto& p : kEnglish) m.emplace(p.uk, p.en);
        return m;
    }();
    return t;
}

} // namespace

std::string system_ui_language() {
#ifdef _WIN32
    // Українська — якщо такою є мова Windows або регіональний формат: у багатьох англійська Windows,
    // але формат дати/чисел український.
    auto ours = [](LANGID id) {
        const WORD p = PRIMARYLANGID(id);
        return p == LANG_UKRAINIAN || p == LANG_RUSSIAN || p == LANG_BELARUSIAN;
    };
    return ours(GetUserDefaultUILanguage()) || ours(GetUserDefaultLangID()) ? "uk" : "en";
#else
    const char* l = std::getenv("LANG");
    return l && (std::strncmp(l, "uk", 2) == 0 || std::strncmp(l, "ru", 2) == 0 || std::strncmp(l, "be", 2) == 0)
               ? "uk"
               : "en";
#endif
}

void set_ui_language(const std::string& code) {
    const std::string c = code.empty() ? system_ui_language() : code;
    g_lang = static_cast<int>(c == "en" ? UiLang::En : UiLang::Uk);
}

UiLang ui_language() { return static_cast<UiLang>(g_lang.load()); }

size_t translation_count() { return table().size(); }

const char* tr(const char* uk) {
    if (!uk || g_lang.load() != static_cast<int>(UiLang::En)) return uk;
    const auto& t = table();
    if (auto it = t.find(uk); it != t.end()) return it->second;
    // ImGui: "Текст##id" — перекладаємо текст, ідентифікатор лишаємо
    const char* hash = std::strstr(uk, "##");
    if (!hash || hash == uk) return uk;
    auto it = t.find(std::string_view(uk, static_cast<size_t>(hash - uk)));
    if (it == t.end()) return uk;
    static std::mutex m;
    static std::unordered_map<std::string, std::unique_ptr<std::string>> cache;   // рядки живуть до кінця програми
    std::lock_guard lock(m);
    auto& slot = cache[uk];
    if (!slot) slot = std::make_unique<std::string>(std::string(it->second) + hash);
    return slot->c_str();
}

} // namespace gmdr
