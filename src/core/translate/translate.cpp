#include "translate.hpp"

#include "../util/file_util.hpp"
#include "../util/i18n.hpp"
#include "../util/json.hpp"
#include "../util/log.hpp"
#include "../util/strings.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <map>
#include <mutex>

namespace gmdr::translate {

namespace fs = std::filesystem;

const std::vector<Language>& languages() {
    static const std::vector<Language> k = {
        {"uk", "Українська", "Ukrainian", "UK", "uk", true, "ukr"},
        {"en", "English", "English", "EN-US", "en", true, "eng"},
        {"ru", "Русский", "Russian", "RU", "ru", true, "rus"},
        {"de", "Deutsch", "German", "DE", "de", true, "deu"},
        {"fr", "Français", "French", "FR", "fr", true, "fra"},
        {"es", "Español", "Spanish", "ES", "es", true, "spa"},
        {"it", "Italiano", "Italian", "IT", "it", true, "ita"},
        {"pl", "Polski", "Polish", "PL", "pl", true, "pol"},
        {"pt-BR", "Português (Brasil)", "Brazilian Portuguese", "PT-BR", "pt", true, "por"},
        {"pt-PT", "Português (Portugal)", "European Portuguese", "PT-PT", "pt-PT", true, "por"},
        {"cs", "Čeština", "Czech", "CS", "cs", true, "ces"},
        {"be", "Беларуская", "Belarusian", "", "be", false, "bel"},
        {"lt", "Lietuvių", "Lithuanian", "LT", "lt", false, "lit"},
        {"lv", "Latviešu", "Latvian", "LV", "lv", false, "lav"},
        {"et", "Eesti", "Estonian", "ET", "et", false, "est"},
        {"fi", "Suomi", "Finnish", "FI", "fi", true, "fin"},
        {"tr", "Türkçe", "Turkish", "TR", "tr", true, "tur"},
        {"zh", "简体中文", "Simplified Chinese", "ZH-HANS", "zh-CN", true, "zho"},
        {"hi", "हिन्दी", "Hindi", "", "hi", true, "hin"},
        {"gl", "Galego", "Galician", "", "gl", false, "glg"},
        {"eo", "Esperanto", "Esperanto", "", "eo", false, "epo"},
        {"ja", "日本語", "Japanese", "JA", "ja", true, "jpn"},
        {"ko", "한국어", "Korean", "KO", "ko", true, "kor"},
        {"nl", "Nederlands", "Dutch", "NL", "nl", true, "nld"},
        {"sv", "Svenska", "Swedish", "SV", "sv", true, "swe"},
    };
    return k;
}

const Language* find_language(const std::string& code) {
    for (const auto& l : languages())
        if (iequals(code, l.code)) return &l;
    // "pt" без регіону — бразильська (як у Google і OmniVoice)
    if (iequals(code, "pt")) return find_language("pt-BR");
    return nullptr;
}

std::string base_code(const std::string& code) {
    const size_t dash = code.find('-');
    return to_lower(dash == std::string::npos ? code : code.substr(0, dash));
}

const std::vector<ProviderInfo>& providers() {
    static const std::vector<ProviderInfo> k = {
        {"deepl", "DeepL", true, false, false, "https://api-free.deepl.com"},
        {"google", "Google Cloud Translation", true, false, false, "https://translation.googleapis.com"},
        {"libre", "LibreTranslate", false, true, false, "http://localhost:5000"},
        {"openai", N_("Мовна модель (Ollama, LM Studio, OpenAI…)"), false, true, true, "http://localhost:11434/v1"},
        {"fake", "Test", false, false, false, ""},
    };
    return k;
}

const ProviderInfo* find_provider(const std::string& id) {
    for (const auto& p : providers())
        if (id == p.id) return &p;
    return nullptr;
}

bool supports(const std::string& provider, const std::string& lang) {
    const Language* l = find_language(lang);
    if (!l) return false;
    if (provider == "deepl") return *l->deepl != 0;
    return true;
}

size_t batch_size(const Config& c) {
    if (c.provider == "deepl") return 50;
    if (c.provider == "google") return 100;
    if (c.provider == "openai") return 30;
    return 40;
}

namespace {
std::string url_or_default(const Config& c) {
    std::string u = trim(c.url);
    if (u.empty()) {
        if (const auto* p = find_provider(c.provider)) u = p->default_url;
        // Ключ безкоштовного тарифу DeepL закінчується на ":fx"; платного — ні
        if (c.provider == "deepl" && !c.key.ends_with(":fx")) u = "https://api.deepl.com";
    }
    while (!u.empty() && u.back() == '/') u.pop_back();
    return u;
}

// Код мови джерела для DeepL: без регіону, великими літерами ("uk" → "UK")
std::string deepl_source(const std::string& from) {
    std::string b = base_code(from);
    for (auto& ch : b) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return b;
}

json::Value string_array(const std::vector<std::string>& v) {
    json::Value a = json::Value::array();
    for (const auto& s : v) a.push(json::Value::string(s));
    return a;
}
} // namespace

HttpRequest build_request(const Config& c, const std::vector<std::string>& texts, const std::string& from, const std::string& to) {
    HttpRequest r;
    r.method = "POST";
    r.headers.push_back({"Content-Type", "application/json"});
    const Language* tl = find_language(to);
    const bool auto_src = from.empty() || from == "auto";
    if (c.provider == "deepl") {
        r.url = url_or_default(c) + "/v2/translate";
        r.headers.push_back({"Authorization", "DeepL-Auth-Key " + c.key});
        json::Value j = json::Value::object();
        j.set("text", string_array(texts));
        j.set("target_lang", json::Value::string(tl && *tl->deepl ? tl->deepl : to));
        if (!auto_src) j.set("source_lang", json::Value::string(deepl_source(from)));
        r.body = j.dump();
    } else if (c.provider == "google") {
        r.url = url_or_default(c) + "/language/translate/v2?key=" + c.key;
        json::Value j = json::Value::object();
        j.set("q", string_array(texts));
        j.set("target", json::Value::string(tl ? tl->google : to));
        if (!auto_src) j.set("source", json::Value::string(base_code(from)));
        j.set("format", json::Value::string("text"));
        r.body = j.dump();
    } else if (c.provider == "libre") {
        r.url = url_or_default(c) + "/translate";
        json::Value j = json::Value::object();
        j.set("q", string_array(texts));
        j.set("source", json::Value::string(auto_src ? "auto" : base_code(from)));
        j.set("target", json::Value::string(base_code(to)));
        j.set("format", json::Value::string("text"));
        if (!c.key.empty()) j.set("api_key", json::Value::string(c.key));
        r.body = j.dump();
    } else if (c.provider == "openai") {
        r.url = url_or_default(c) + "/chat/completions";
        r.timeout_ms = 300000;   // локальна модель на процесорі може думати довго
        if (!c.key.empty()) r.headers.push_back({"Authorization", "Bearer " + c.key});
        const Language* fl = auto_src ? nullptr : find_language(from);
        const std::string src = fl ? fl->english : "the original language";
        const std::string dst = tl ? tl->english : to;
        const std::string system =
            std::format("You translate voice chat and text chat from Garry's Mod gameplay videos. Translate every item from {} "
                        "into {}. Keep the meaning, tone, slang and profanity level; keep player names, game terms and numbers "
                        "as they are. Reply with a JSON array of exactly {} strings in the same order and nothing else.",
                        src, dst, texts.size());
        json::Value msgs = json::Value::array();
        json::Value m1 = json::Value::object();
        m1.set("role", json::Value::string("system"));
        m1.set("content", json::Value::string(system));
        json::Value m2 = json::Value::object();
        m2.set("role", json::Value::string("user"));
        m2.set("content", json::Value::string(string_array(texts).dump()));
        msgs.push(std::move(m1));
        msgs.push(std::move(m2));
        json::Value j = json::Value::object();
        j.set("model", json::Value::string(c.model));
        j.set("temperature", json::Value::number(0.2));
        j.set("stream", json::Value::boolean(false));
        j.set("messages", std::move(msgs));
        r.body = j.dump();
    }
    return r;
}

std::optional<std::vector<std::string>> parse_response(const Config& c, const std::string& body, size_t expected, std::string* error) {
    auto fail = [&](const std::string& msg) -> std::optional<std::vector<std::string>> {
        if (error) *error = msg;
        return std::nullopt;
    };
    std::string perr;
    auto j = json::parse(body, &perr);
    if (!j) return fail(tr("незрозуміла відповідь сервісу: ") + perr);
    std::vector<std::string> out;
    if (c.provider == "deepl") {
        for (const auto& t : (*j)["translations"].items()) out.push_back(t["text"].as_string());
    } else if (c.provider == "google") {
        for (const auto& t : (*j)["data"]["translations"].items()) out.push_back(t["translatedText"].as_string());
    } else if (c.provider == "libre") {
        const auto& t = (*j)["translatedText"];
        if (t.is_array())
            for (const auto& x : t.items()) out.push_back(x.as_string());
        else if (t.type() == json::Value::Type::String)
            out.push_back(t.as_string());
    } else if (c.provider == "openai") {
        std::string content = (*j)["choices"][0]["message"]["content"].as_string();
        // Модель може загорнути відповідь у ```json … ``` чи додати слова — беремо сам масив
        const size_t a = content.find('['), b = content.rfind(']');
        if (a == std::string::npos || b == std::string::npos || b < a) return fail(tr("мовна модель не повернула список перекладів"));
        auto arr = json::parse(std::string_view(content).substr(a, b - a + 1), &perr);
        if (!arr || !arr->is_array()) return fail(tr("мовна модель повернула неправильний список: ") + perr);
        for (const auto& x : arr->items()) out.push_back(x.as_string());
    }
    if (out.size() != expected) return fail(trf("сервіс повернув {} перекладів замість {}", out.size(), expected));
    return out;
}

// ---- Кеш перекладів -------------------------------------------------------------------
namespace {
std::mutex g_cache_mutex;

fs::path cache_path(const std::string& lang) { return app_data_dir() / "translations" / path_from_utf8(sanitize_filename(lang) + ".json"); }

std::map<std::string, std::string> load_cache(const std::string& lang) {
    std::map<std::string, std::string> m;
    if (auto text = read_file_text(cache_path(lang)))
        if (auto j = json::parse(*text))
            for (const auto& [k, v] : j->members()) m[k] = v.as_string();
    return m;
}

void save_cache(const std::string& lang, const std::map<std::string, std::string>& m) {
    json::Value j = json::Value::object();
    for (const auto& [k, v] : m) j.set(k, json::Value::string(v));
    std::error_code ec;
    fs::create_directories(cache_path(lang).parent_path(), ec);
    std::string err;
    if (!write_file_atomic(cache_path(lang), j.dump(), &err)) log_warn("{}", trf("Не вдалося зберегти кеш перекладів: {}", err));
}

std::string cache_key(const Config& c, const std::string& from, const std::string& text) {
    return c.provider + (c.provider == "openai" ? ":" + c.model : "") + "|" + (from.empty() ? "auto" : from) + "|" + text;
}

// Перекласти одну пачку; модель, що повернула не стільки рядків, — ще раз половинками
std::optional<std::vector<std::string>> translate_batch(const Config& c, const std::vector<std::string>& texts, const std::string& from,
                                                        const std::string& to, const std::atomic<bool>* cancel, std::string* error) {
    if (c.provider == "fake") {
        std::vector<std::string> out;
        for (const auto& t : texts) out.push_back("[" + to + "] " + t);
        return out;
    }
    const HttpResponse res = http_request(build_request(c, texts, from, to), cancel);
    if (!res.ok()) {
        if (error) *error = describe_http_error(res);
        return std::nullopt;
    }
    std::string perr;
    if (auto out = parse_response(c, res.body, texts.size(), &perr)) return out;
    if (c.provider == "openai" && texts.size() > 1) {
        const std::vector<std::string> a(texts.begin(), texts.begin() + static_cast<std::ptrdiff_t>(texts.size() / 2));
        const std::vector<std::string> b(texts.begin() + static_cast<std::ptrdiff_t>(texts.size() / 2), texts.end());
        auto ra = translate_batch(c, a, from, to, cancel, error);
        if (!ra) return std::nullopt;
        auto rb = translate_batch(c, b, from, to, cancel, error);
        if (!rb) return std::nullopt;
        ra->insert(ra->end(), rb->begin(), rb->end());
        return ra;
    }
    if (error) *error = perr;
    return std::nullopt;
}
} // namespace

std::optional<std::vector<std::string>> translate(const Config& c, const std::vector<std::string>& texts, const std::string& from,
                                                  const std::string& to, const std::function<void(double)>& progress,
                                                  const std::atomic<bool>* cancel, std::string* error) {
    const ProviderInfo* p = find_provider(c.provider);
    if (!p) {
        if (error) *error = trf("невідомий сервіс перекладу: {}", c.provider);
        return std::nullopt;
    }
    if (p->needs_key && trim(c.key).empty()) {
        if (error) *error = trf("для {} потрібен API-ключ (сторінка «Переклад і озвучення»)", tr(p->label));
        return std::nullopt;
    }
    if (p->uses_model && trim(c.model).empty()) {
        if (error) *error = tr("вкажіть модель для перекладу (напр. qwen2.5:7b в Ollama)");
        return std::nullopt;
    }
    if (!supports(c.provider, to)) {
        if (error) *error = trf("{} не перекладає на цю мову ({}) — виберіть інший сервіс", tr(p->label), to);
        return std::nullopt;
    }
    std::vector<std::string> out(texts.size());
    std::map<std::string, std::string> cache;
    {
        std::lock_guard lock(g_cache_mutex);
        cache = load_cache(to);
    }
    // Що ще не перекладено (однакові рядки — один раз)
    std::vector<std::string> todo;
    std::map<std::string, size_t> todo_index;
    for (size_t i = 0; i < texts.size(); ++i) {
        const std::string t = trim(texts[i]);
        if (t.empty()) continue;
        if (auto it = cache.find(cache_key(c, from, t)); it != cache.end() && c.provider != "fake") {
            out[i] = it->second;
            continue;
        }
        if (!todo_index.count(t)) {
            todo_index[t] = todo.size();
            todo.push_back(t);
        }
    }
    std::vector<std::string> done(todo.size());
    const size_t bs = batch_size(c);
    for (size_t at = 0; at < todo.size(); at += bs) {
        if (cancel && cancel->load()) {
            if (error) *error = tr("скасовано");
            return std::nullopt;
        }
        const std::vector<std::string> batch(todo.begin() + static_cast<std::ptrdiff_t>(at),
                                             todo.begin() + static_cast<std::ptrdiff_t>(std::min(todo.size(), at + bs)));
        auto r = translate_batch(c, batch, from, to, cancel, error);
        if (!r) return std::nullopt;
        for (size_t i = 0; i < r->size(); ++i) done[at + i] = trim((*r)[i]);
        if (progress) progress(static_cast<double>(std::min(todo.size(), at + bs)) / static_cast<double>(todo.size()));
    }
    for (size_t i = 0; i < texts.size(); ++i) {
        const std::string t = trim(texts[i]);
        if (auto it = todo_index.find(t); it != todo_index.end()) out[i] = done[it->second];
    }
    if (!todo.empty() && c.provider != "fake") {
        std::lock_guard lock(g_cache_mutex);
        auto fresh = load_cache(to);   // інший потік міг дописати свої
        for (const auto& [t, i] : todo_index) fresh[cache_key(c, from, t)] = done[i];
        save_cache(to, fresh);
    }
    return out;
}

} // namespace gmdr::translate
