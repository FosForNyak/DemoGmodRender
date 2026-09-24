#include "tts.hpp"

#include "../audio/wav.hpp"
#include "../translate/translate.hpp"
#include "../util/file_util.hpp"
#include "../util/http.hpp"
#include "../util/http_download.hpp"
#include "../util/i18n.hpp"
#include "../util/json.hpp"
#include "../util/log.hpp"
#include "../util/strings.hpp"
#include "../util/subprocess.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <format>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

namespace gmdr::dub {

namespace fs = std::filesystem;

namespace {
constexpr int kTtsRate = 24000;
constexpr const char* kElevenLabs = "https://api.elevenlabs.io";

void set_env(const char* name, const std::string& value) {
#ifdef _WIN32
    SetEnvironmentVariableW(utf8_to_wide(name).c_str(), utf8_to_wide(value).c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

bool write_pcm_wav(const fs::path& path, const std::vector<float>& pcm, std::string* error) {
    audio::WavWriter w;
    return w.open(path, kTtsRate, 1, audio::WavWriter::Format::Int16, error) && w.write(pcm.data(), pcm.size()) &&
           w.close(error);
}

// ElevenLabs Multilingual v2 не приймає language_code (мову визначає сам з тексту)
bool elevenlabs_takes_language(const std::string& model) { return model.find("multilingual_v2") == std::string::npos; }
} // namespace

const std::vector<EngineInfo>& engines() {
    static const std::vector<EngineInfo> k = {
        {"omnivoice", N_("Локально (OmniVoice)"), false},
        {"elevenlabs", "ElevenLabs", true},
        {"fake", "Test", false},
    };
    return k;
}

const EngineInfo* find_engine(const std::string& id) {
    for (const auto& e : engines())
        if (id == e.id) return &e;
    return nullptr;
}

bool engine_supports(const std::string& engine, const std::string& elevenlabs_model, const std::string& lang) {
    const translate::Language* l = translate::find_language(lang);
    if (!l) return false;
    if (engine == "elevenlabs") {
        // Eleven v3 — 70+ мов (без есперанто); v2 і v2.5 — 29–32 мови
        if (elevenlabs_model.find("v3") != std::string::npos) return std::string(l->code) != "eo";
        return l->elevenlabs;
    }
    return true;   // OmniVoice — 600+ мов, усі з таблиці
}

std::string engine_language(const std::string& lang) {
    const std::string b = translate::base_code(lang);
    return b.empty() ? "en" : b;
}

std::string generic_instruct(size_t index) {
    static const char* k[] = {"male, moderate pitch", "male, low pitch",       "female, moderate pitch", "male, high pitch",
                              "female, low pitch",    "male, very low pitch",  "female, high pitch",     "elderly, male"};
    return k[index % std::size(k)];
}

std::string generic_elevenlabs_voice(size_t index) {
    // Готові голоси ElevenLabs (доступні з будь-яким ключем): George, Brian, Sarah, Charlie, Daniel, Laura, Liam, Alice
    static const char* k[] = {"JBFqnCBsd6RMkjVDRZzb", "nPczCjzI2devNBz1zQrb", "EXAVITQu4vr4xnJGGxGw", "IKne3meq5aSn9XLyUdCD",
                              "onwK4e9ZLuTAKqWW03F9", "FGY2WhTYpPnrIDTdsKH5", "TX3LPaxmHKxFdv7VOQHJ", "Xb7hH8MSUJpSbSDYk0k2"};
    return k[index % std::size(k)];
}

std::string make_job_json(const TtsConfig& c, const std::vector<SpeakerVoice>& voices, const std::vector<TtsItem>& items) {
    json::Value j = json::Value::object();
    j.set("device", json::Value::string(c.device.empty() ? "auto" : c.device));
    j.set("num_step", json::Value::number(std::clamp(c.num_step, 4, 64)));
    json::Value arr = json::Value::array();
    for (const auto& it : items) {
        json::Value o = json::Value::object();
        o.set("text", json::Value::string(it.text));
        o.set("language", json::Value::string(engine_language(it.language)));
        o.set("out", json::Value::string(path_to_utf8(it.out)));
        if (it.voice < voices.size()) {
            const SpeakerVoice& v = voices[it.voice];
            if (!v.ref_wav.empty()) {
                o.set("ref_audio", json::Value::string(path_to_utf8(v.ref_wav)));
                if (!v.ref_text.empty()) o.set("ref_text", json::Value::string(v.ref_text));
            } else if (!v.instruct.empty()) {
                o.set("instruct", json::Value::string(v.instruct));
            }
        }
        arr.push(std::move(o));
    }
    j.set("items", std::move(arr));
    return j.dump();
}

HelperEvent parse_helper_line(const std::string& line) {
    HelperEvent e;
    const std::string l = trim(line);
    auto rest = [&](size_t n) { return l.size() > n ? trim(l.substr(n)) : std::string(); };
    if (l == "GMDR_LOADING") e.kind = HelperEvent::Loading;
    else if (l.rfind("GMDR_READY", 0) == 0) {
        e.kind = HelperEvent::Ready;
        e.text = rest(10);
    } else if (l.rfind("GMDR_DONE ", 0) == 0 || l.rfind("GMDR_FAIL ", 0) == 0) {
        e.kind = l[5] == 'D' ? HelperEvent::Done : HelperEvent::Fail;
        const std::string r = rest(10);
        const size_t sp = r.find(' ');
        const auto n = parse_int(r.substr(0, sp));
        if (!n) return {};
        e.index = static_cast<int>(*n);
        if (sp != std::string::npos) e.text = trim(r.substr(sp + 1));
    } else if (l.rfind("GMDR_ERROR", 0) == 0) {
        e.kind = HelperEvent::Error;
        e.text = rest(10);
    }
    return e;
}

// ============================ Локальний рушій ===========================================
fs::path engine_dir() { return app_data_dir() / "voice_engine"; }

namespace {
fs::path venv_python(const fs::path& venv) {
#ifdef _WIN32
    return venv / "Scripts" / "python.exe";
#else
    return venv / "bin" / "python";
#endif
}

fs::path system_tool(const char* name) {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    const UINT n = GetSystemDirectoryW(buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) return fs::path(std::wstring(buf, n)) / (std::string(name) + ".exe");
    return {};
#else
    for (const char* d : {"/usr/bin/", "/bin/", "/usr/local/bin/"})
        if (std::error_code ec; fs::exists(fs::path(d) / name, ec)) return fs::path(d) / name;
    return {};
#endif
}

// Змінні для Python рушія: модель — у теці рушія, вивід — UTF-8
void prepare_python_env() {
    set_env("HF_HOME", path_to_utf8(engine_dir() / "hf"));
    set_env("PYTHONUTF8", "1");
    set_env("PYTHONIOENCODING", "utf-8");
    set_env("HF_HUB_DISABLE_TELEMETRY", "1");
}

// Кілька останніх рядків виводу — для повідомлення про помилку
struct Tail {
    std::vector<std::string> lines;
    void add(const std::string& l) {
        if (trim(l).empty()) return;
        lines.push_back(l);
        if (lines.size() > 6) lines.erase(lines.begin());
    }
    std::string text() const {
        std::string s;
        for (const auto& l : lines) s += (s.empty() ? "" : " | ") + trim(l);
        return s;
    }
};
} // namespace

std::optional<fs::path> engine_python(const TtsConfig& c) {
    std::error_code ec;
    if (!trim(c.python).empty()) {
        const fs::path p = path_from_utf8(trim(c.python));
        if (fs::exists(p, ec)) return p;
        return std::nullopt;
    }
    const fs::path p = venv_python(engine_dir() / "venv");
    if (fs::exists(p, ec) && fs::exists(engine_dir() / "installed.txt", ec)) return p;
    return std::nullopt;
}

fs::path write_helper_script(std::string* error) {
    static const unsigned char bytes[] = {
#include "gmdr_voice_py.inc"
    };
    const fs::path p = engine_dir() / "gmdr_voice.py";
    const std::string text(reinterpret_cast<const char*>(bytes));
    if (read_file_text(p).value_or("") != text) {
        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);
        if (!write_file_atomic(p, text, error)) return {};
    }
    return p;
}

bool has_nvidia_gpu() {
    const fs::path smi = system_tool("nvidia-smi");
    std::error_code ec;
    if (smi.empty() || !fs::exists(smi, ec)) return false;
    bool gpu = false;
    const auto r = run_process(smi, {"-L"}, [&](const std::string& l) { gpu = gpu || l.rfind("GPU ", 0) == 0; });
    return r.started && r.exit_code == 0 && gpu;
}

std::optional<std::string> check_engine(const TtsConfig& c, const std::atomic<bool>* cancel, std::string* error) {
    const auto py = engine_python(c);
    if (!py) {
        if (error) *error = tr("локальний рушій озвучення не встановлено (сторінка «Переклад і озвучення»)");
        return std::nullopt;
    }
    const fs::path script = write_helper_script(error);
    if (script.empty()) return std::nullopt;
    prepare_python_env();
    std::string device, fatal;
    Tail tail;
    const auto r = run_process(*py, {path_to_utf8(script), "--check", "--device", c.device.empty() ? "auto" : c.device},
                               [&](const std::string& l) {
                                   const HelperEvent e = parse_helper_line(l);
                                   if (e.kind == HelperEvent::Ready) device = e.text;
                                   else if (e.kind == HelperEvent::Error) fatal = e.text;
                                   else tail.add(l);
                               },
                               cancel, error);
    if (!r.started) return std::nullopt;
    if (r.cancelled) {
        if (error) *error = tr("скасовано");
        return std::nullopt;
    }
    if (r.exit_code != 0 || device.empty()) {
        if (error) *error = !fatal.empty() ? fatal : trf("помічник завершився з кодом {}: {}", r.exit_code, tail.text());
        return std::nullopt;
    }
    return device;
}

bool install_engine(bool cuda, const TtsProgress& progress, const std::atomic<bool>* cancel, std::string* error) {
#ifndef _WIN32
    (void)cuda, (void)progress, (void)cancel;
    if (error) *error = tr("автоматичне встановлення — лише у Windows; укажіть свій Python з пакетом omnivoice");
    return false;
#else
    const fs::path dir = engine_dir();
    std::error_code ec;
    fs::create_directories(dir, ec);
    fs::remove(dir / "installed.txt", ec);
    auto stage = [&](double f, const std::string& what) {
        if (progress) progress(f, what);
        log_info("{}", trf("Рушій озвучення: {}", what));
    };
    auto cancelled = [&] {
        if (cancel && cancel->load()) {
            if (error) *error = tr("скасовано");
            return true;
        }
        return false;
    };
    // 1. uv — менеджер Python-пакетів (один .exe), з GitHub
    const fs::path uv = dir / "uv" / "uv.exe";
    if (!fs::exists(uv, ec)) {
        stage(0.0, tr("завантаження uv"));
        const fs::path zip = dir / "uv.zip";
        if (!download_file("https://github.com/astral-sh/uv/releases/latest/download/uv-x86_64-pc-windows-msvc.zip", zip,
                           [&](uint64_t got, uint64_t total) {
                               if (progress && total > 0) progress(0.03 * static_cast<double>(got) / total, tr("завантаження uv"));
                           },
                           cancel, error))
            return false;
        fs::create_directories(dir / "uv", ec);
        const fs::path tar = system_tool("tar");
        std::string terr;
        const auto r = run_process(tar, {"-xf", path_to_utf8(zip), "-C", path_to_utf8(dir / "uv")}, {}, cancel, &terr);
        fs::remove(zip, ec);
        if (!r.started || r.exit_code != 0 || !fs::exists(uv, ec)) {
            if (error) *error = trf("не вдалося розпакувати uv: {}", terr.empty() ? trf("код {}", r.exit_code) : terr);
            return false;
        }
    }
    if (cancelled()) return false;
    // Усе — всередині теки рушія: Python, кеш, модель
    set_env("UV_PYTHON_INSTALL_DIR", path_to_utf8(dir / "python"));
    set_env("UV_CACHE_DIR", path_to_utf8(dir / "cache"));
    set_env("UV_PYTHON_PREFERENCE", "only-managed");
    set_env("UV_NO_CONFIG", "1");
    set_env("UV_LINK_MODE", "copy");
    prepare_python_env();
    auto run_uv = [&](const std::vector<std::string>& args, double f0, double f1, const std::string& what) {
        stage(f0, what);
        Tail tail;
        double f = f0;
        std::string rerr;
        const auto r = run_process(uv, args,
                                   [&](const std::string& l) {
                                       tail.add(l);
                                       // uv пише по рядку на пакет — рухаємо смужку потроху
                                       f = std::min(f1 - 0.01, f + (f1 - f) * 0.02);
                                       if (progress) progress(f, what);
                                   },
                                   cancel, &rerr);
        if (r.cancelled || cancelled()) {
            if (error) *error = tr("скасовано");
            return false;
        }
        if (!r.started || r.exit_code != 0) {
            if (error) *error = what + ": " + (!rerr.empty() ? rerr : tail.text());
            return false;
        }
        return true;
    };
    const fs::path venv = dir / "venv";
    const std::string py = path_to_utf8(venv_python(venv));
    if (!fs::exists(venv_python(venv), ec) &&
        !run_uv({"venv", "--python", "3.12", path_to_utf8(venv)}, 0.04, 0.10, tr("встановлення Python 3.12")))
        return false;
    std::vector<std::string> torch = {"pip", "install", "--python", py};
    if (cuda) {
        for (const char* a : {"torch==2.8.0+cu128", "torchaudio==2.8.0+cu128", "--extra-index-url",
                              "https://download.pytorch.org/whl/cu128", "--index-strategy", "unsafe-best-match"})
            torch.push_back(a);
    } else {
        for (const char* a : {"torch==2.8.0", "torchaudio==2.8.0"}) torch.push_back(a);
    }
    if (!run_uv(torch, 0.10, 0.65, cuda ? tr("завантаження PyTorch з CUDA (~3 ГБ)") : tr("завантаження PyTorch")))
        return false;
    if (!run_uv({"pip", "install", "--python", py, "omnivoice"}, 0.65, 0.78, tr("встановлення OmniVoice"))) return false;
    // Кеш завантажень більше не потрібен (кілька ГБ)
    fs::remove_all(dir / "cache", ec);
    // Модель (з Hugging Face) завантажиться під час першої перевірки
    stage(0.8, tr("завантаження моделі й перевірка"));
    write_file_text(dir / "installed.txt", cuda ? "cuda\n" : "cpu\n");
    TtsConfig c;
    c.device = cuda ? "cuda" : "cpu";
    const auto dev = check_engine(c, cancel, error);
    if (!dev) {
        fs::remove(dir / "installed.txt", ec);
        return false;
    }
    stage(1.0, trf("готово ({})", *dev));
    return true;
#endif
}

bool remove_engine(std::string* error) {
    std::error_code ec;
    fs::remove_all(engine_dir(), ec);
    if (ec && error) *error = ec.message();
    return !ec;
}

// ============================ Синтез ====================================================
namespace {
bool synth_fake(const std::vector<TtsItem>& items, const TtsProgress& progress, const std::atomic<bool>* cancel,
                std::string* error) {
    for (size_t i = 0; i < items.size(); ++i) {
        if (cancel && cancel->load()) {
            if (error) *error = tr("скасовано");
            return false;
        }
        // Тон замість мови: довжина — як у звичайному мовленні (~14 символів за секунду)
        const double secs = std::clamp(0.3 + static_cast<double>(items[i].text.size()) / 14.0, 0.5, 15.0);
        const double hz = 160.0 + 45.0 * static_cast<double>(items[i].voice % 8);
        std::vector<float> pcm(static_cast<size_t>(secs * kTtsRate));
        for (size_t k = 0; k < pcm.size(); ++k) {
            const double t = static_cast<double>(k) / kTtsRate;
            const double env = std::min({1.0, t * 20.0, (secs - t) * 20.0});
            pcm[k] = static_cast<float>(0.25 * env * std::sin(2.0 * 3.14159265358979 * hz * t));
        }
        if (!write_pcm_wav(items[i].out, pcm, error)) return false;
        if (progress) progress(static_cast<double>(i + 1) / items.size(), {});
    }
    return true;
}

bool synth_elevenlabs(const TtsConfig& c, const std::vector<SpeakerVoice>& voices, const std::vector<TtsItem>& items,
                      const TtsProgress& progress, const std::atomic<bool>* cancel, std::vector<std::string>* warnings,
                      std::string* error) {
    if (trim(c.elevenlabs_key).empty()) {
        if (error) *error = trf("для {} потрібен API-ключ (сторінка «Переклад і озвучення»)", "ElevenLabs");
        return false;
    }
    const std::string model = c.elevenlabs_model.empty() ? "eleven_multilingual_v2" : c.elevenlabs_model;
    int failed_in_row = 0;
    for (size_t i = 0; i < items.size(); ++i) {
        if (cancel && cancel->load()) {
            if (error) *error = tr("скасовано");
            return false;
        }
        const TtsItem& it = items[i];
        std::string voice = it.voice < voices.size() ? voices[it.voice].elevenlabs_voice_id : std::string();
        if (voice.empty()) voice = generic_elevenlabs_voice(it.voice);
        json::Value body = json::Value::object();
        body.set("text", json::Value::string(it.text));
        body.set("model_id", json::Value::string(model));
        if (elevenlabs_takes_language(model)) body.set("language_code", json::Value::string(engine_language(it.language)));
        HttpRequest rq;
        rq.method = "POST";
        rq.url = std::format("{}/v1/text-to-speech/{}?output_format=pcm_24000", kElevenLabs, voice);
        rq.headers = {{"xi-api-key", trim(c.elevenlabs_key)}, {"Content-Type", "application/json"}, {"Accept", "audio/pcm"}};
        rq.body = body.dump();
        rq.timeout_ms = 120000;
        HttpResponse r;
        for (int attempt = 0; attempt < 4; ++attempt) {
            r = http_request(rq, cancel);
            if (r.status != 429 && r.status < 500) break;
            // Забагато запитів одночасно чи збій сервісу — трохи зачекати
            std::this_thread::sleep_for(std::chrono::seconds(2 << attempt));
        }
        if (!r.ok()) {
            const std::string why = describe_http_error(r);
            // Ключ, тариф чи ліміт символів — далі теж не вийде
            if (r.status == 0 || r.status == 401 || r.status == 402 || r.status == 403 || ++failed_in_row >= 3) {
                if (error) *error = "ElevenLabs: " + why;
                return false;
            }
            if (warnings) warnings->push_back(trf("фразу «{}» не озвучено: {}", it.text, why));
            continue;
        }
        failed_in_row = 0;
        std::vector<float> pcm(r.body.size() / 2);
        for (size_t k = 0; k < pcm.size(); ++k) {
            const auto lo = static_cast<uint8_t>(r.body[2 * k]);
            const auto hi = static_cast<uint8_t>(r.body[2 * k + 1]);
            pcm[k] = static_cast<float>(static_cast<int16_t>(lo | (hi << 8))) / 32768.0f;
        }
        if (!write_pcm_wav(it.out, pcm, error)) return false;
        if (progress) progress(static_cast<double>(i + 1) / items.size(), {});
    }
    return true;
}

bool synth_omnivoice(const TtsConfig& c, const std::vector<SpeakerVoice>& voices, const std::vector<TtsItem>& items,
                     const fs::path& work_dir, const TtsProgress& progress, const std::atomic<bool>* cancel,
                     std::vector<std::string>* warnings, std::string* error) {
    const auto py = engine_python(c);
    if (!py) {
        if (error) *error = tr("локальний рушій озвучення не встановлено (сторінка «Переклад і озвучення»)");
        return false;
    }
    const fs::path script = write_helper_script(error);
    if (script.empty()) return false;
    std::error_code ec;
    fs::create_directories(work_dir, ec);
    const fs::path job = work_dir / "tts_job.json";
    if (!write_file_text(job, make_job_json(c, voices, items), error)) return false;
    prepare_python_env();
    std::string fatal;
    Tail tail;
    size_t done = 0;
    const auto r = run_process(*py, {path_to_utf8(script), "--job", path_to_utf8(job)},
                               [&](const std::string& l) {
                                   const HelperEvent e = parse_helper_line(l);
                                   switch (e.kind) {
                                   case HelperEvent::Loading:
                                       if (progress) progress(0, tr("завантаження моделі"));
                                       break;
                                   case HelperEvent::Ready:
                                       log_info("{}", trf("OmniVoice: модель завантажено ({})", e.text));
                                       break;
                                   case HelperEvent::Done:
                                   case HelperEvent::Fail:
                                       ++done;
                                       if (e.kind == HelperEvent::Fail && warnings && e.index >= 0 &&
                                           static_cast<size_t>(e.index) < items.size())
                                           warnings->push_back(trf("фразу «{}» не озвучено: {}", items[static_cast<size_t>(e.index)].text, e.text));
                                       if (progress) progress(static_cast<double>(done) / items.size(), {});
                                       break;
                                   case HelperEvent::Error: fatal = e.text; break;
                                   default: tail.add(l); break;
                                   }
                               },
                               cancel, error);
    fs::remove(job, ec);
    if (!r.started) return false;
    if (r.cancelled) {
        if (error) *error = tr("скасовано");
        return false;
    }
    // 3 — частину фраз не озвучено (вони вже у warnings)
    if (r.exit_code != 0 && r.exit_code != 3) {
        if (error) *error = !fatal.empty() ? "OmniVoice: " + fatal : trf("помічник завершився з кодом {}: {}", r.exit_code, tail.text());
        return false;
    }
    return true;
}
} // namespace

bool synthesize(const TtsConfig& c, const std::vector<SpeakerVoice>& voices, const std::vector<TtsItem>& items,
                const fs::path& work_dir, const TtsProgress& progress, const std::atomic<bool>* cancel,
                std::vector<std::string>* warnings, std::string* error) {
    if (items.empty()) return true;
    if (c.engine == "fake") return synth_fake(items, progress, cancel, error);
    if (c.engine == "elevenlabs") return synth_elevenlabs(c, voices, items, progress, cancel, warnings, error);
    if (c.engine == "omnivoice") return synth_omnivoice(c, voices, items, work_dir, progress, cancel, warnings, error);
    if (error) *error = trf("невідомий рушій озвучення: {}", c.engine);
    return false;
}

// ============================ ElevenLabs: голоси ========================================
std::optional<std::string> elevenlabs_clone(const std::string& key, const std::string& name, const std::vector<fs::path>& samples,
                                            const std::atomic<bool>* cancel, std::string* error) {
    std::vector<FormPart> parts = {{"name", "", "", name},
                                   {"description", "", "", "GModDemoRender: voice of a player, for dubbing translations"}};
    for (size_t i = 0; i < samples.size() && i < 25; ++i) {
        auto data = read_file_bytes(samples[i]);
        if (!data) continue;
        parts.push_back({"files", path_to_utf8(samples[i].filename()), "audio/wav", std::string(data->begin(), data->end())});
    }
    if (parts.size() <= 2) {
        if (error) *error = tr("немає зразків голосу");
        return std::nullopt;
    }
    const std::string boundary = "gmdr" + make_unique_id();
    HttpRequest rq;
    rq.method = "POST";
    rq.url = std::string(kElevenLabs) + "/v1/voices/add";
    rq.headers = {{"xi-api-key", trim(key)}, {"Content-Type", "multipart/form-data; boundary=" + boundary}};
    rq.body = multipart_body(parts, boundary);
    rq.timeout_ms = 180000;
    const HttpResponse r = http_request(rq, cancel);
    if (!r.ok()) {
        if (error) *error = "ElevenLabs: " + describe_http_error(r);
        return std::nullopt;
    }
    const auto j = json::parse(r.body);
    const std::string id = j ? (*j)["voice_id"].as_string() : std::string();
    if (id.empty()) {
        if (error) *error = tr("незрозуміла відповідь сервісу: ") + r.body.substr(0, 200);
        return std::nullopt;
    }
    return id;
}

bool elevenlabs_delete_voice(const std::string& key, const std::string& voice_id, std::string* error) {
    HttpRequest rq;
    rq.method = "DELETE";
    rq.url = std::format("{}/v1/voices/{}", kElevenLabs, voice_id);
    rq.headers = {{"xi-api-key", trim(key)}};
    const HttpResponse r = http_request(rq);
    if (!r.ok() && r.status != 404) {
        if (error) *error = describe_http_error(r);
        return false;
    }
    return true;
}

std::optional<std::string> elevenlabs_check(const std::string& key, std::string* error) {
    HttpRequest rq;
    rq.url = std::string(kElevenLabs) + "/v1/user/subscription";
    rq.headers = {{"xi-api-key", trim(key)}};
    rq.timeout_ms = 20000;
    const HttpResponse r = http_request(rq);
    if (!r.ok()) {
        if (error) *error = describe_http_error(r);
        return std::nullopt;
    }
    const auto j = json::parse(r.body);
    if (!j) {
        if (error) *error = tr("незрозуміла відповідь сервісу: ") + r.body.substr(0, 200);
        return std::nullopt;
    }
    const int64_t used = (*j)["character_count"].as_int(), limit = (*j)["character_limit"].as_int();
    return trf("{}, залишилось {} з {} символів", (*j)["tier"].as_string("?"), std::max<int64_t>(0, limit - used), limit);
}

} // namespace gmdr::dub
