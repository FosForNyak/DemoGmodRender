#include "transcribe.hpp"

#include "../audio/wav.hpp"
#include "../media/ffmpeg_util.hpp"
#include "../util/file_util.hpp"
#include "../util/json.hpp"
#include "../util/log.hpp"
#include "../util/strings.hpp"
#include "../util/subprocess.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <format>
#include <map>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

namespace gmdr::speech {

namespace fs = std::filesystem;

const std::vector<ModelInfo>& known_models() {
    // Від найточнішої до найшвидшої — у такому порядку й вибирається наявна
    static const std::vector<ModelInfo> m = {
        {"ggml-large-v3-turbo.bin", "Large v3 Turbo — найточніша", 1550},
        {"ggml-large-v3-turbo-q5_0.bin", "Large v3 Turbo, стиснута — майже так само точна", 547},
        {"ggml-small.bin", "Small — швидша, помиляється частіше", 466},
        {"ggml-base.bin", "Base — найшвидша, для проби", 142},
    };
    return m;
}

std::string model_url(const std::string& file) {
    if (const char* e = std::getenv("GMDR_TEST_MODEL_URL")) return e;   // для перевірки завантаження
    return "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/" + file;
}

std::vector<fs::path> whisper_dirs() {
    std::vector<fs::path> dirs = {executable_dir() / "whisper", app_data_dir() / "whisper"};
    // Розробка: third_party/whisper угорі від build/...
    fs::path p = executable_dir();
    for (int i = 0; i < 4 && p.has_parent_path() && p.parent_path() != p; ++i) {
        p = p.parent_path();
        std::error_code ec;
        if (fs::is_directory(p / "third_party" / "whisper", ec)) {
            dirs.push_back(p / "third_party" / "whisper");
            break;
        }
    }
    return dirs;
}

fs::path models_download_dir() { return app_data_dir() / "whisper" / "models"; }

std::optional<WhisperTools> find_whisper(const std::string& cli_override, const std::string& model_override,
                                         std::string* why) {
    std::error_code ec;
    auto env = [](const char* n) {
        const char* e = std::getenv(n);
        return e ? std::string(e) : std::string();
    };
    WhisperTools t;
#ifdef _WIN32
    const char* exe_name = "whisper-cli.exe";
#else
    const char* exe_name = "whisper-cli";
#endif
    for (const std::string& o : {cli_override, env("GMDR_WHISPER_CLI")})
        if (t.cli.empty() && !o.empty() && fs::is_regular_file(path_from_utf8(o), ec)) t.cli = path_from_utf8(o);
    for (const auto& d : whisper_dirs()) {
        if (!t.cli.empty()) break;
        for (const fs::path& c : {d / exe_name, d / "bin" / exe_name, d / "bin" / "Release" / exe_name})
            if (fs::is_regular_file(c, ec)) {
                t.cli = c;
                break;
            }
    }
    if (t.cli.empty()) {
        if (why)
            *why = std::format("не знайдено whisper-cli: покладіть його (з DLL) у теку «whisper» поруч із програмою ({})",
                               path_to_utf8(executable_dir() / "whisper"));
        return std::nullopt;
    }
    for (const std::string& o : {model_override, env("GMDR_WHISPER_MODEL")})
        if (t.model.empty() && !o.empty() && fs::is_regular_file(path_from_utf8(o), ec)) t.model = path_from_utf8(o);
    if (t.model.empty()) {
        // Усі моделі з відомих тек; найкраща — за порядком known_models, далі будь-яка ggml-*.bin
        std::vector<fs::path> found;
        std::vector<fs::path> dirs;
        for (const auto& d : whisper_dirs()) {
            dirs.push_back(d / "models");
            dirs.push_back(d);
        }
        for (const auto& d : dirs)
            for (fs::directory_iterator it(d, ec), end; !ec && it != end; it.increment(ec)) {
                const std::string n = path_to_utf8(it->path().filename());
                if (it->is_regular_file(ec) && starts_with_i(n, "ggml-") && ends_with_i(n, ".bin") &&
                    file_size_or_zero(it->path()) > (10u << 20))
                    found.push_back(it->path());
            }
        for (const auto& m : known_models())
            for (const auto& f : found)
                if (t.model.empty() && iequals(path_to_utf8(f.filename()), m.file)) t.model = f;
        if (t.model.empty() && !found.empty()) t.model = found.front();
    }
    if (t.model.empty()) {
        if (why) *why = "немає моделі розпізнавання — завантажте її («Розпізнати мовлення» → «Завантажити модель»)";
        return std::nullopt;
    }
    return t;
}

// ---- Збереження ---------------------------------------------------------------------
std::string transcript_to_json(const Transcript& t) {
    json::Value root = json::Value::object();
    root.set("model", json::Value::string(t.model));
    root.set("language", json::Value::string(t.language));
    json::Value cov = json::Value::array();
    for (const auto& c : t.covered) {
        json::Value o = json::Value::object();
        o.set("key", json::Value::string(c.key));
        o.set("from", json::Value::number(std::round(c.from * 1000) / 1000));
        o.set("to", json::Value::number(std::round(c.to * 1000) / 1000));
        cov.push(o);
    }
    root.set("covered", cov);
    json::Value lines = json::Value::array();
    for (const auto& l : t.lines) {
        json::Value o = json::Value::object();
        o.set("start", json::Value::number(std::round(l.start * 1000) / 1000));
        o.set("end", json::Value::number(std::round(l.end * 1000) / 1000));
        o.set("key", json::Value::string(l.speaker_key));
        o.set("name", json::Value::string(l.speaker));
        o.set("text", json::Value::string(l.text));
        lines.push(o);
    }
    root.set("lines", lines);
    return root.dump();
}

std::optional<Transcript> transcript_from_json(const std::string& text) {
    auto j = json::parse(text);
    if (!j || !j->is_object()) return std::nullopt;
    Transcript t;
    t.model = (*j)["model"].as_string();
    t.language = (*j)["language"].as_string();
    for (const auto& c : (*j)["covered"].items())
        t.covered.push_back({c["key"].as_string(), c["from"].as_number(), c["to"].as_number()});
    for (const auto& o : (*j)["lines"].items())
        t.lines.push_back({o["start"].as_number(), o["end"].as_number(), o["key"].as_string(), o["name"].as_string(),
                           o["text"].as_string()});
    return t;
}

fs::path transcript_path(const std::string& demo_path) {
    const fs::path p = path_from_utf8(demo_path);
    return app_data_dir() / "transcripts" /
           path_from_utf8(std::format("{}_{}.json", sanitize_filename(path_to_utf8(p.stem())), file_size_or_zero(p)));
}

std::optional<Transcript> load_transcript(const std::string& demo_path) {
    if (demo_path.empty()) return std::nullopt;
    auto text = read_file_text(transcript_path(demo_path));
    return text ? transcript_from_json(*text) : std::nullopt;
}

bool save_transcript(const std::string& demo_path, const Transcript& t, std::string* error) {
    const fs::path p = transcript_path(demo_path);
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    return write_file_atomic(p, transcript_to_json(t), error);
}

bool covers(const Transcript& t, const std::string& key, double from, double to) {
    // Відрізки гравця за порядком; [from, to] має лежати в їх об'єднанні
    std::vector<std::pair<double, double>> r;
    for (const auto& c : t.covered)
        if (c.key == key) r.push_back({c.from, c.to});
    std::sort(r.begin(), r.end());
    double reach = from;
    for (const auto& [a, b] : r) {
        if (a > reach + 1e-3) break;
        reach = std::max(reach, b);
    }
    return reach >= to - 1e-3;
}

void merge_transcript(Transcript& dst, const Transcript& fresh) {
    for (const auto& c : fresh.covered)
        std::erase_if(dst.lines, [&](const Line& l) { return l.speaker_key == c.key && l.start >= c.from && l.start < c.to; });
    for (const auto& l : fresh.lines) dst.lines.push_back(l);
    std::stable_sort(dst.lines.begin(), dst.lines.end(), [](const Line& a, const Line& b) { return a.start < b.start; });
    // Покриття: об'єднати відрізки кожного гравця
    std::map<std::string, std::vector<std::pair<double, double>>> by_key;
    for (const auto& c : dst.covered) by_key[c.key].push_back({c.from, c.to});
    for (const auto& c : fresh.covered) by_key[c.key].push_back({c.from, c.to});
    dst.covered.clear();
    for (auto& [key, r] : by_key) {
        std::sort(r.begin(), r.end());
        for (const auto& [a, b] : r) {
            if (!dst.covered.empty() && dst.covered.back().key == key && a <= dst.covered.back().to + 1e-3)
                dst.covered.back().to = std::max(dst.covered.back().to, b);
            else
                dst.covered.push_back({key, a, b});
        }
    }
    if (!fresh.model.empty()) dst.model = fresh.model;
    if (!fresh.language.empty()) dst.language = fresh.language;
}

// ---- Складники --------------------------------------------------------------------
std::vector<Piece> plan_pieces(const voice::SpeakerTrack& t, double merge_gap, double min_len, double gap,
                               double from, double to) {
    const double rate = voice::kVoiceRate;
    std::vector<Piece> out;
    double a = -1, b = -1, compact = 0;
    auto push = [&] {
        if (a < 0 || b - a < min_len) return;
        out.push_back({compact, a, b - a});
        compact += (b - a) + gap;
    };
    for (const auto& s : t.segments) {
        double sa = s.start / rate, sb = s.end() / rate;
        if (sb <= from || (to >= 0 && sa >= to)) continue;
        sa = std::max(sa, from);
        if (to >= 0) sb = std::min(sb, to);
        if (a >= 0 && sa - b < merge_gap) {
            b = std::max(b, sb);
            continue;
        }
        push();
        a = sa;
        b = sb;
    }
    push();
    return out;
}

int piece_at(const std::vector<Piece>& pieces, double t) {
    // Фраза разом із половинами пауз обабіч: whisper ставить межі з точністю до кількох десятків мс
    for (size_t i = 0; i < pieces.size(); ++i) {
        const double lo = i == 0 ? -1e9 : (pieces[i - 1].compact + pieces[i - 1].length + pieces[i].compact) / 2;
        const double hi = i + 1 == pieces.size() ? 1e18
                                                 : (pieces[i].compact + pieces[i].length + pieces[i + 1].compact) / 2;
        if (t >= lo && t < hi) return static_cast<int>(i);
    }
    return -1;
}

double compact_to_demo(const std::vector<Piece>& pieces, double t) {
    const int i = piece_at(pieces, t);
    if (i < 0) return 0;
    const Piece& p = pieces[static_cast<size_t>(i)];
    return p.demo + std::clamp(t - p.compact, 0.0, p.length);
}

std::vector<RawSegment> parse_whisper_json(const std::string& text, std::string* error) {
    std::vector<RawSegment> out;
    auto j = json::parse(text);
    if (!j || !j->is_object() || !(*j)["transcription"].is_array()) {
        if (error) *error = "незрозумілий вивід whisper-cli (немає transcription)";
        return out;
    }
    for (const auto& s : (*j)["transcription"].items())
        out.push_back({s["offsets"]["from"].as_number() / 1000.0, s["offsets"]["to"].as_number() / 1000.0,
                       trim(s["text"].as_string())});
    return out;
}

namespace {
// Нижній регістр для латиниці й кирилиці (to_lower знає лише ASCII), просто в UTF-8
std::string lower_utf8(const std::string& s) {
    std::string r = s;
    for (size_t i = 0; i < r.size();) {
        const auto c = static_cast<unsigned char>(r[i]);
        const size_t len = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        if (len == 1) {
            r[i] = static_cast<char>(std::tolower(c));
        } else if (len == 2 && i + 1 < r.size()) {
            const auto d = static_cast<unsigned char>(r[i + 1]);
            if (c == 0xD0 && d >= 0x90 && d <= 0x9F) {          // А-П -> а-п
                r[i + 1] = static_cast<char>(d + 0x20);
            } else if (c == 0xD0 && d >= 0xA0 && d <= 0xAF) {   // Р-Я -> р-я
                r[i] = static_cast<char>(0xD1);
                r[i + 1] = static_cast<char>(d - 0x20);
            } else if (c == 0xD0 && d >= 0x80 && d <= 0x8F) {   // Ё, Є, І, Ї... -> ё, є, і, ї
                r[i] = static_cast<char>(0xD1);
                r[i + 1] = static_cast<char>(d + 0x10);
            } else if (c == 0xD2 && d == 0x90) {                // Ґ -> ґ
                r[i + 1] = static_cast<char>(0x91);
            }
        }
        i += len;
    }
    return r;
}
} // namespace

bool is_noise_text(const std::string& text) {
    const std::string t = lower_utf8(trim(text));
    bool letters = false;
    for (unsigned char c : t)
        if (std::isalnum(c) || c >= 0x80) letters = true;
    if (!letters) return true;   // порожнє, "...", "!"
    if ((t.front() == '[' && t.back() == ']') || (t.front() == '(' && t.back() == ')') || t.find("♪") != std::string::npos)
        return true;             // [музыка], (смех), ♪
    // Типові "галюцинації" Whisper на тиші й шумі — титри з відео, на яких його вчили
    static const char* const kPhrases[] = {
        "субтитры сделал", "субтитры создавал", "субтитры подготовил", "редактор субтитров", "корректор",
        "продолжение следует", "спасибо за просмотр", "подписывайтесь на канал", "dimatorzok", "субтитрування",
        "дякую за перегляд", "підписуйтесь на канал", "thank you for watching", "thanks for watching",
        "please subscribe", "subtitles by", "amara.org", "субтитры by",
    };
    for (const char* p : kPhrases)
        if (t.find(p) != std::string::npos) return true;
    return false;
}

std::vector<Line> segments_to_lines(const std::vector<RawSegment>& segs, const std::vector<Piece>& pieces,
                                    const std::string& key, const std::string& name) {
    std::vector<Line> out;
    for (const auto& s : segs) {
        if (is_noise_text(s.text)) continue;
        const int i = piece_at(pieces, s.from);
        if (i < 0) continue;
        const Piece& p = pieces[static_cast<size_t>(i)];
        Line l;
        l.start = p.demo + std::clamp(s.from - p.compact, 0.0, p.length);
        // Кінець — у межах тієї самої фрази: наступна може бути через хвилину
        l.end = p.demo + std::clamp(s.to - p.compact, 0.0, p.length);
        if (l.end < l.start + 0.3) l.end = std::min(p.demo + p.length, l.start + 0.3);
        l.speaker_key = key;
        l.speaker = name;
        // Пробіли: whisper починає з пробілу, а між сегментами бувають подвійні
        std::string text;
        for (char c : trim(s.text))
            if (!(c == ' ' && !text.empty() && text.back() == ' ')) text += c;
        l.text = text;
        out.push_back(std::move(l));
    }
    return out;
}

namespace {
// Шлях для whisper-cli: на Windows він читає аргументи в кодовій сторінці ANSI, тож
// кирилиця в шляху (ім'я користувача) ламається — віддаємо коротке ім'я 8.3.
std::string arg_path(const fs::path& p) {
#ifdef _WIN32
    const fs::path dir = p.parent_path();
    std::wstring buf(1024, L'\0');
    const DWORD n = GetShortPathNameW(dir.c_str(), buf.data(), static_cast<DWORD>(buf.size()));
    if (n > 0 && n < buf.size()) {
        buf.resize(n);
        return path_to_utf8(fs::path(buf) / p.filename());
    }
#endif
    return path_to_utf8(p);
}

// Стиснуте аудіо мовця: лише його фрази, між ними тиша; 16 кГц моно 16 біт — як хоче whisper
bool write_compact_wav(const voice::SpeakerTrack& track, const std::vector<Piece>& pieces, const fs::path& path,
                       double gap, const std::atomic<bool>* cancel, std::string* error) {
    audio::WavWriter w;
    if (!w.open(path, 16000, 1, audio::WavWriter::Format::Int16, error)) return false;
    SwrContext* raw = nullptr;
    AVChannelLayout mono;
    av_channel_layout_default(&mono, 1);
    if (swr_alloc_set_opts2(&raw, &mono, AV_SAMPLE_FMT_FLT, 16000, &mono, AV_SAMPLE_FMT_FLT, voice::kVoiceRate, 0,
                            nullptr) < 0 ||
        swr_init(raw) < 0) {
        swr_free(&raw);
        if (error) *error = "не вдалося налаштувати ресемплер";
        return false;
    }
    media::SwrPtr swr(raw);
    std::vector<float> out;
    auto push = [&](const float* in, int n) {
        const int out_max = swr_get_out_samples(swr.get(), n) + 16;
        out.resize(static_cast<size_t>(out_max));
        uint8_t* o[1] = {reinterpret_cast<uint8_t*>(out.data())};
        const uint8_t* i[1] = {reinterpret_cast<const uint8_t*>(in)};
        const int got = swr_convert(swr.get(), o, out_max, in ? i : nullptr, in ? n : 0);
        if (got > 0) w.write(out.data(), static_cast<size_t>(got));
    };
    voice::VoiceStream vs(track);
    std::vector<float> buf;
    const std::vector<float> silence(static_cast<size_t>(gap * voice::kVoiceRate), 0.0f);
    for (const auto& p : pieces) {
        if (cancel && cancel->load()) return false;
        const int64_t a = std::llround(p.demo * voice::kVoiceRate);
        const int64_t len = std::llround(p.length * voice::kVoiceRate);
        for (int64_t at = 0; at < len; at += voice::kVoiceRate) {
            const size_t n = static_cast<size_t>(std::min<int64_t>(voice::kVoiceRate, len - at));
            buf.assign(n, 0.0f);
            vs.mix_into(a + at, n, buf.data());
            push(buf.data(), static_cast<int>(n));
        }
        push(silence.data(), static_cast<int>(silence.size()));
    }
    push(nullptr, 0);
    return w.close(error);
}
} // namespace

std::optional<Transcript> transcribe(const std::vector<std::pair<const voice::SpeakerTrack*, std::string>>& speakers,
                                     const WhisperTools& tools, const Options& opt, const fs::path& work_dir,
                                     const Progress& progress, const std::atomic<bool>* cancel, std::string* error) {
    constexpr double kGap = 1.0;
    Transcript tr;
    tr.model = path_to_utf8(tools.model.filename());
    tr.language = opt.language.empty() ? "auto" : opt.language;
    std::error_code ec;
    fs::create_directories(work_dir, ec);
    // Скільки всього розпізнавати — для прогресу
    std::vector<std::vector<Piece>> plans;
    double total = 0;
    for (const auto& [t, name] : speakers) {
        plans.push_back(t ? plan_pieces(*t, 0.8, 0.3, kGap, opt.from, opt.to) : std::vector<Piece>{});
        if (!plans.back().empty()) total += plans.back().back().compact + plans.back().back().length;
    }
    const int threads =
        opt.threads > 0 ? opt.threads : std::clamp(static_cast<int>(std::thread::hardware_concurrency()), 1, 16);
    std::map<std::string, int> langs;
    double done = 0;
    for (size_t k = 0; k < speakers.size(); ++k) {
        const auto& [track, name] = speakers[k];
        const auto& pieces = plans[k];
        if (!track) continue;
        const double cover_to = opt.to >= 0 ? opt.to : 1e9;   // усе демо — до кінця, хоч би що було далі
        if (pieces.empty()) {   // на цьому відрізку гравець мовчить — теж результат
            tr.covered.push_back({track->key, opt.from, cover_to});
            continue;
        }
        const double seconds = pieces.back().compact + pieces.back().length;
        if (progress) progress(total > 0 ? done / total : 0, std::format("{}: підготовка звуку", name));
        const fs::path wav = work_dir / std::format("speaker_{}.wav", k);
        const fs::path out_base = work_dir / std::format("speaker_{}", k);
        if (!write_compact_wav(*track, pieces, wav, kGap, cancel, error)) {
            if (cancel && cancel->load() && error) *error = "скасовано";
            return std::nullopt;
        }
        std::vector<std::string> args = {"-m", arg_path(tools.model), "-f", arg_path(wav), "-l", tr.language,
                                         "-oj", "-of", arg_path(out_base), "-t", std::to_string(threads), "-pp", "-sns"};
        std::vector<std::string> tail;
        const auto res = run_process(
            tools.cli, args,
            [&](const std::string& line) {
                // "whisper_print_progress_callback: progress =  45%"
                const size_t at = line.find("progress =");
                if (at != std::string::npos) {
                    const double pct = std::atof(line.c_str() + at + 10);
                    if (progress)
                        progress(total > 0 ? (done + seconds * std::clamp(pct / 100.0, 0.0, 1.0)) / total : 0,
                                 std::format("{}: розпізнавання {:.0f}%", name, pct));
                    return;
                }
                if (const size_t l = line.find("auto-detected language:"); l != std::string::npos)
                    log_debug("whisper: {} — {}", name, trim(line.substr(l)));
                tail.push_back(line);
                if (tail.size() > 8) tail.erase(tail.begin());
            },
            cancel, error);
        fs::remove(wav, ec);
        if (res.cancelled) {
            if (error) *error = "скасовано";
            return std::nullopt;
        }
        if (!res.started) return std::nullopt;
        const fs::path json_path = fs::path(out_base).concat(".json");
        auto text = read_file_text(json_path);
        fs::remove(json_path, ec);
        if (res.exit_code != 0 || !text) {
            if (error) {
                *error = std::format("whisper-cli завершився з кодом {}", res.exit_code);
                for (const auto& l : tail) *error += "\n  " + l;
            }
            return std::nullopt;
        }
        std::string perr;
        const auto segs = parse_whisper_json(*text, &perr);
        if (!perr.empty()) {
            if (error) *error = perr;
            return std::nullopt;
        }
        if (auto j = json::parse(*text)) {
            const std::string lang = (*j)["result"]["language"].as_string();
            if (!lang.empty()) ++langs[lang];
        }
        auto lines = segments_to_lines(segs, pieces, track->key, name);
        log_info("Розпізнано {}: {} реплік ({} мовлення)", name, lines.size(), format_duration(seconds));
        for (auto& l : lines) tr.lines.push_back(std::move(l));
        tr.covered.push_back({track->key, opt.from, cover_to});
        done += seconds;
    }
    std::stable_sort(tr.lines.begin(), tr.lines.end(), [](const Line& a, const Line& b) { return a.start < b.start; });
    if (tr.language == "auto" && !langs.empty()) {
        std::string l;
        for (const auto& [k, v] : langs) l += (l.empty() ? "" : ",") + k;
        tr.language = "auto:" + l;
    }
    if (progress) progress(1.0, "готово");
    return tr;
}

} // namespace gmdr::speech
