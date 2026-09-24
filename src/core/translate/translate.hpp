// =============================================================================
//  translate.hpp — переклад тексту (розпізнані репліки, чат, субтитри) на інші
//  мови через онлайн- чи локальний сервіс:
//
//    deepl   — DeepL API (ключ; безкоштовний тариф — api-free.deepl.com)
//    google  — Google Cloud Translation v2 (ключ)
//    libre   — LibreTranslate (свій сервер або публічний з ключем)
//    openai  — будь-яка OpenAI-сумісна модель: локально Ollama (http://localhost:11434/v1)
//              чи LM Studio (http://localhost:1234/v1), або хмара (OpenAI, OpenRouter...)
//    fake    — «[de] текст», для тестів
//
//  Переклади кешуються (<дані програми>/translations/<мова>.json): той самий рядок
//  тим самим сервісом удруге не перекладається — і не оплачується.
// =============================================================================
#pragma once

#include <atomic>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "../util/http.hpp"

namespace gmdr::translate {

// Мова перекладу й озвучення
struct Language {
    const char* code;          // "uk", "en", "pt-BR" …
    const char* native;        // назва мовою самої мови
    const char* english;       // для підказки мовній моделі
    const char* deepl;         // цільовий код DeepL ("" — DeepL не вміє)
    const char* google;        // код Google Cloud Translation
    bool        elevenlabs;    // озвучення ElevenLabs Multilingual v2
    const char* iso3;          // ISO 639-2 для мітки мови звукової доріжки ("deu")
};
const std::vector<Language>& languages();
const Language*              find_language(const std::string& code);
// Базовий код без регіону: "pt-BR" → "pt"
std::string base_code(const std::string& code);

struct Config {
    std::string provider = "deepl";   // deepl / google / libre / openai / fake
    std::string key;                  // API-ключ (якщо сервіс його вимагає)
    std::string url;                  // libre / openai: адреса сервера (порожньо — типова)
    std::string model;                // openai: модель ("gpt-4o-mini", "qwen2.5:7b" …)
};
struct ProviderInfo {
    const char* id;
    const char* label;
    bool        needs_key;     // без ключа не працює
    bool        uses_url;      // адресу можна змінити
    bool        uses_model;    // треба вказати модель
    const char* default_url;
};
const std::vector<ProviderInfo>& providers();
const ProviderInfo*              find_provider(const std::string& id);
// Чи вміє сервіс цю мову (DeepL — лише частину; решта — усі з languages()).
bool supports(const std::string& provider, const std::string& lang);

// ---- Складники (тестуються окремо) --------------------------------------------------
// Скільки рядків в одному запиті
size_t batch_size(const Config& c);
HttpRequest build_request(const Config& c, const std::vector<std::string>& texts, const std::string& from, const std::string& to);
std::optional<std::vector<std::string>> parse_response(const Config& c, const std::string& body, size_t expected, std::string* error);

// Перекласти рядки з мови from ("auto" чи "" — визначить сервіс) на to. Порожні рядки
// лишаються порожніми. progress — частка готового.
std::optional<std::vector<std::string>> translate(const Config& c, const std::vector<std::string>& texts, const std::string& from,
                                                  const std::string& to, const std::function<void(double)>& progress,
                                                  const std::atomic<bool>* cancel, std::string* error);

} // namespace gmdr::translate
