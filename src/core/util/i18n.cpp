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
    const char* tr;
};

// Таблиці перекладів (генерує scripts/i18n.py); у кінці кожної — {nullptr, nullptr}
const Pair kEn[] = {
#include "i18n/en.inc"
    {nullptr, nullptr}};
const Pair kRu[] = {
#include "i18n/ru.inc"
    {nullptr, nullptr}};
const Pair kBe[] = {
#include "i18n/be.inc"
    {nullptr, nullptr}};
const Pair kPl[] = {
#include "i18n/pl.inc"
    {nullptr, nullptr}};
const Pair kCs[] = {
#include "i18n/cs.inc"
    {nullptr, nullptr}};
const Pair kDe[] = {
#include "i18n/de.inc"
    {nullptr, nullptr}};
const Pair kFr[] = {
#include "i18n/fr.inc"
    {nullptr, nullptr}};
const Pair kEs[] = {
#include "i18n/es.inc"
    {nullptr, nullptr}};
const Pair kIt[] = {
#include "i18n/it.inc"
    {nullptr, nullptr}};
const Pair kPtBr[] = {
#include "i18n/pt-BR.inc"
    {nullptr, nullptr}};
const Pair kPtPt[] = {
#include "i18n/pt-PT.inc"
    {nullptr, nullptr}};
const Pair kGl[] = {
#include "i18n/gl.inc"
    {nullptr, nullptr}};
const Pair kLt[] = {
#include "i18n/lt.inc"
    {nullptr, nullptr}};
const Pair kLv[] = {
#include "i18n/lv.inc"
    {nullptr, nullptr}};
const Pair kEt[] = {
#include "i18n/et.inc"
    {nullptr, nullptr}};
const Pair kFi[] = {
#include "i18n/fi.inc"
    {nullptr, nullptr}};
const Pair kTr[] = {
#include "i18n/tr.inc"
    {nullptr, nullptr}};
const Pair kEo[] = {
#include "i18n/eo.inc"
    {nullptr, nullptr}};
const Pair kHi[] = {
#include "i18n/hi.inc"
    {nullptr, nullptr}};
const Pair kZh[] = {
#include "i18n/zh.inc"
    {nullptr, nullptr}};

struct LangEntry {
    UiLanguage  info;
    const Pair* table;   // nullptr — українська (ключі)
};

const std::vector<LangEntry>& entries() {
    static const std::vector<LangEntry> k = {
        {{"uk", "Українська", "Ukrainian"}, nullptr},
        {{"en", "English", "English"}, kEn},
        {{"ru", "Русский", "Russian"}, kRu},
        {{"be", "Беларуская", "Belarusian"}, kBe},
        {{"pl", "Polski", "Polish"}, kPl},
        {{"cs", "Čeština", "Czech"}, kCs},
        {{"de", "Deutsch", "German"}, kDe},
        {{"fr", "Français", "French"}, kFr},
        {{"es", "Español", "Spanish"}, kEs},
        {{"it", "Italiano", "Italian"}, kIt},
        {{"pt-BR", "Português (Brasil)", "Portuguese (Brazil)"}, kPtBr},
        {{"pt-PT", "Português (Portugal)", "Portuguese (Portugal)"}, kPtPt},
        {{"gl", "Galego", "Galician"}, kGl},
        {{"lt", "Lietuvių", "Lithuanian"}, kLt},
        {{"lv", "Latviešu", "Latvian"}, kLv},
        {{"et", "Eesti", "Estonian"}, kEt},
        {{"fi", "Suomi", "Finnish"}, kFi},
        {{"tr", "Türkçe", "Turkish"}, kTr},
        {{"eo", "Esperanto", "Esperanto"}, kEo},
        {{"hi", "हिन्दी", "Hindi"}, kHi},
        {{"zh", "简体中文", "Chinese (Simplified)"}, kZh},
    };
    return k;
}

using Table = std::unordered_map<std::string_view, const char*>;

// Словник мови (будується один раз, при першому зверненні; далі — без блокувань)
const Table& table_of(size_t lang) {
    constexpr size_t kMax = 32;
    static std::once_flag flags[kMax];
    static Table tables[kMax];
    std::call_once(flags[lang], [lang] {
        if (const Pair* p = entries()[lang].table)
            for (; p->uk; ++p) tables[lang].emplace(p->uk, p->tr);
    });
    return tables[lang];
}

std::atomic<size_t> g_lang{0};   // номер у entries(); 0 — українська

size_t index_of(const std::string& code) {
    const auto& e = entries();
    for (size_t i = 0; i < e.size(); ++i)
        if (code == e[i].info.code) return i;
    // "pt" без регіону — бразильська; "zh-CN" — спрощена китайська
    if (code == "pt") return index_of("pt-BR");
    if (code.rfind("zh", 0) == 0) return index_of("zh");
    return SIZE_MAX;
}

// Переклад рядка мовою lang; nullptr — немає
const char* lookup(size_t lang, std::string_view uk) {
    const Table& t = table_of(lang);
    auto it = t.find(uk);
    return it == t.end() ? nullptr : it->second;
}

} // namespace

const std::vector<UiLanguage>& ui_languages() {
    static const std::vector<UiLanguage> k = [] {
        std::vector<UiLanguage> v;
        for (const auto& e : entries()) v.push_back(e.info);
        return v;
    }();
    return k;
}

std::string system_ui_language() {
#ifdef _WIN32
    // Мова Windows, а якщо її програма не знає — мова регіонального формату (у багатьох англійська
    // Windows, але формат дати/чисел свій)
    auto code_of = [](LANGID id) -> std::string {
        switch (PRIMARYLANGID(id)) {
        case LANG_UKRAINIAN: return "uk";
        case LANG_ENGLISH: return "en";
        case LANG_RUSSIAN: return "ru";
        case LANG_BELARUSIAN: return "be";
        case LANG_POLISH: return "pl";
        case LANG_CZECH: return "cs";
        case LANG_GERMAN: return "de";
        case LANG_FRENCH: return "fr";
        case LANG_SPANISH: return "es";
        case LANG_ITALIAN: return "it";
        case LANG_PORTUGUESE: return SUBLANGID(id) == SUBLANG_PORTUGUESE_BRAZILIAN ? "pt-BR" : "pt-PT";
        case LANG_GALICIAN: return "gl";
        case LANG_LITHUANIAN: return "lt";
        case LANG_LATVIAN: return "lv";
        case LANG_ESTONIAN: return "et";
        case LANG_FINNISH: return "fi";
        case LANG_TURKISH: return "tr";
        case LANG_HINDI: return "hi";
        case LANG_CHINESE: return SUBLANGID(id) == SUBLANG_CHINESE_SIMPLIFIED || SUBLANGID(id) == SUBLANG_CHINESE_SINGAPORE ? "zh" : "";
        default: return "";
        }
    };
    std::string c = code_of(GetUserDefaultUILanguage());
    if (c.empty() || c == "en") {
        const std::string region = code_of(GetUserDefaultLangID());
        if (!region.empty()) c = region;
    }
    return c.empty() ? "en" : c;
#else
    for (const char* var : {"LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG"}) {
        const char* l = std::getenv(var);
        if (!l || !*l) continue;
        std::string s(l);
        s = s.substr(0, s.find_first_of(".:@"));
        if (s.rfind("pt_BR", 0) == 0) return "pt-BR";
        if (s.rfind("pt", 0) == 0) return "pt-PT";
        const std::string two = s.substr(0, 2);
        if (index_of(two) != SIZE_MAX) return two;
    }
    return "en";
#endif
}

void set_ui_language(const std::string& code) {
    size_t i = index_of(code.empty() ? system_ui_language() : code);
    if (i == SIZE_MAX) i = index_of("en");
    g_lang = i;
}

std::string ui_language() { return entries()[g_lang.load()].info.code; }

size_t translation_count() {
    const size_t l = g_lang.load();
    return table_of(l == 0 ? 1 : l).size();
}

const char* tr_lang(const std::string& code, const char* uk) {
    const size_t lang = index_of(code);
    if (!uk || lang == 0 || lang == SIZE_MAX) return uk;
    if (const char* r = lookup(lang, uk)) return r;
    const char* r = lookup(1, uk);
    return r ? r : uk;
}

const char* tr(const char* uk) {
    const size_t lang = g_lang.load();
    if (!uk || lang == 0) return uk;
    const size_t en = 1;
    auto find = [&](std::string_view key) {
        const char* r = lookup(lang, key);
        return r || lang == en ? r : lookup(en, key);   // чого в мові немає — англійською
    };
    if (const char* r = find(uk)) return r;
    // ImGui: "Текст##id" — перекладаємо текст, ідентифікатор лишаємо
    const char* hash = std::strstr(uk, "##");
    if (!hash || hash == uk) return uk;
    const char* r = find(std::string_view(uk, static_cast<size_t>(hash - uk)));
    if (!r) return uk;
    static std::mutex m;
    static std::unordered_map<std::string, std::unique_ptr<std::string>> cache;   // рядки живуть до кінця програми
    std::lock_guard lock(m);
    auto& slot = cache[std::to_string(lang) + '\x1f' + uk];
    if (!slot) slot = std::make_unique<std::string>(std::string(r) + hash);
    return slot->c_str();
}

} // namespace gmdr
