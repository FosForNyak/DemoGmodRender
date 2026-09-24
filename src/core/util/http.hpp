// =============================================================================
//  http.hpp — HTTP(S)-запити до сервісів перекладу й озвучення (DeepL, Google,
//  LibreTranslate, OpenAI-сумісні API — зокрема локальні Ollama і LM Studio —
//  та ElevenLabs).
//
//  Лише коли користувач сам увімкнув переклад чи озвучення. Windows — WinHTTP
//  (http:// для локальних серверів і https://); інші ОС — помилка.
// =============================================================================
#pragma once

#include <atomic>
#include <string>
#include <utility>
#include <vector>

namespace gmdr {

struct HttpRequest {
    std::string method = "GET";
    std::string url;                                            // http://… або https://…
    std::vector<std::pair<std::string, std::string>> headers;   // {"Content-Type", "application/json"}
    std::string body;                                           // тіло запиту (байти)
    int         timeout_ms = 60000;                             // чекати відповідь не довше
};

struct HttpResponse {
    int         status = 0;   // 0 — не вдалося з'єднатися (текст — у error)
    std::string body;
    std::string content_type;
    std::string error;
    bool ok() const { return status >= 200 && status < 300; }
};

HttpResponse http_request(const HttpRequest& r, const std::atomic<bool>* cancel = nullptr);

// multipart/form-data (завантаження зразків голосу): поле зі значенням або файл
struct FormPart {
    std::string name;
    std::string filename;       // порожньо — звичайне поле
    std::string content_type;   // для файлу, напр. "audio/wav"
    std::string data;
};
// Тіло запиту; Content-Type — "multipart/form-data; boundary=" + boundary.
std::string multipart_body(const std::vector<FormPart>& parts, const std::string& boundary);

// Коротко про помилку відповіді: код і повідомлення сервісу (поле error/message/detail JSON).
std::string describe_http_error(const HttpResponse& r);

} // namespace gmdr
