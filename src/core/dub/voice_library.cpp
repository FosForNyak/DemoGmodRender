#include "voice_library.hpp"

#include "../audio/wav.hpp"
#include "../util/file_util.hpp"
#include "../util/i18n.hpp"
#include "../util/json.hpp"
#include "../util/log.hpp"
#include "../util/strings.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <format>

namespace gmdr::dub {

namespace fs = std::filesystem;

double VoiceProfile::total_seconds() const {
    double s = 0;
    for (const auto& x : samples) s += x.seconds;
    return s;
}

const std::string& VoiceProfile::name() const { return names.empty() ? key : names.front(); }

fs::path voices_dir() { return app_data_dir() / "voices"; }

bool persistent_key(const std::string& key) { return key.rfind("steam:", 0) == 0 && key.size() > 6; }

fs::path profile_dir(const std::string& key) { return voices_dir() / path_from_utf8(sanitize_filename(replace_all(key, ":", "_"))); }

std::string profile_to_json(const VoiceProfile& p) {
    json::Value j = json::Value::object();
    j.set("key", json::Value::string(p.key));
    json::Value names = json::Value::array();
    for (const auto& n : p.names) names.push(json::Value::string(n));
    j.set("names", std::move(names));
    json::Value arr = json::Value::array();
    for (const auto& s : p.samples) {
        json::Value o = json::Value::object();
        o.set("file", json::Value::string(s.file));
        o.set("text", json::Value::string(s.text));
        o.set("language", json::Value::string(s.language));
        o.set("seconds", json::Value::number(s.seconds));
        o.set("demo", json::Value::string(s.demo));
        o.set("demo_time", json::Value::number(s.demo_time));
        o.set("added", json::Value::number(static_cast<double>(s.added)));
        arr.push(std::move(o));
    }
    j.set("samples", std::move(arr));
    if (!p.elevenlabs_voice_id.empty()) j.set("elevenlabs_voice_id", json::Value::string(p.elevenlabs_voice_id));
    return j.dump();
}

std::optional<VoiceProfile> profile_from_json(const std::string& text) {
    auto j = json::parse(text);
    if (!j || !j->is_object()) return std::nullopt;
    VoiceProfile p;
    p.key = (*j)["key"].as_string();
    for (const auto& n : (*j)["names"].items()) p.names.push_back(n.as_string());
    for (const auto& o : (*j)["samples"].items()) {
        VoiceSample s;
        s.file = o["file"].as_string();
        s.text = o["text"].as_string();
        s.language = o["language"].as_string();
        s.seconds = o["seconds"].as_number();
        s.demo = o["demo"].as_string();
        s.demo_time = o["demo_time"].as_number();
        s.added = o["added"].as_int();
        if (!s.file.empty()) p.samples.push_back(std::move(s));
    }
    p.elevenlabs_voice_id = (*j)["elevenlabs_voice_id"].as_string();
    if (p.key.empty()) return std::nullopt;
    return p;
}

std::optional<VoiceProfile> load_profile(const std::string& key) {
    auto text = read_file_text(profile_dir(key) / "profile.json");
    if (!text) return std::nullopt;
    return profile_from_json(*text);
}

bool save_profile(const VoiceProfile& p, std::string* error) {
    if (!persistent_key(p.key)) return true;   // голоси без SteamID не накопичуються
    std::error_code ec;
    fs::create_directories(profile_dir(p.key), ec);
    return write_file_atomic(profile_dir(p.key) / "profile.json", profile_to_json(p), error);
}

std::vector<VoiceProfile> list_profiles() {
    std::vector<VoiceProfile> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(voices_dir(), ec)) {
        if (!e.is_directory(ec)) continue;
        if (auto text = read_file_text(e.path() / "profile.json"))
            if (auto p = profile_from_json(*text)) out.push_back(std::move(*p));
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.total_seconds() > b.total_seconds(); });
    return out;
}

bool delete_profile(const std::string& key) {
    std::error_code ec;
    fs::remove_all(profile_dir(key), ec);
    return !ec;
}

std::vector<Candidate> sample_candidates(const std::string& key, const std::vector<speech::Line>& lines) {
    std::vector<Candidate> out;
    for (const auto& l : lines) {
        if (l.speaker_key != key) continue;
        const double len = l.end - l.start;
        const std::string t = trim(l.text);
        if (len < 1.5 || len > 12.0 || t.size() < 8 || speech::is_noise_text(t)) continue;
        out.push_back({l.start, l.end, t});
    }
    std::stable_sort(out.begin(), out.end(), [](const Candidate& a, const Candidate& b) { return a.end - a.start > b.end - b.start; });
    return out;
}

int collect_samples(VoiceProfile& p, const fs::path& dir, const voice::SpeakerTrack& track, const std::vector<speech::Line>& lines,
                    const std::string& demo_name, const std::string& language, int max_new, double max_total) {
    if (!track.name.empty() && (p.names.empty() || p.names.front() != track.name)) {
        std::erase(p.names, track.name);
        p.names.insert(p.names.begin(), track.name);
    }
    std::error_code ec;
    fs::create_directories(dir / "clips", ec);
    int added = 0;
    for (const auto& c : sample_candidates(track.key, lines)) {
        if (added >= max_new) break;
        // Цю фразу з цього демо вже взято
        const bool dup = std::any_of(p.samples.begin(), p.samples.end(),
                                     [&](const VoiceSample& s) { return s.demo == demo_name && std::abs(s.demo_time - c.start) < 0.5; });
        if (dup) continue;
        // Трохи запасу по краях: розпізнавання ставить межі фрази щільно
        const int64_t from = std::max<int64_t>(0, static_cast<int64_t>((c.start - 0.15) * voice::kVoiceRate));
        const int64_t to = static_cast<int64_t>((c.end + 0.2) * voice::kVoiceRate);
        std::vector<float> pcm = voice::decode_range(track, from, to);
        if (pcm.size() < static_cast<size_t>(voice::kVoiceRate)) continue;
        double sum = 0;
        size_t clipped = 0;
        for (float v : pcm) {
            sum += static_cast<double>(v) * v;
            if (std::abs(v) > 0.99f) ++clipped;
        }
        const double rms_db = 10.0 * std::log10(std::max(1e-12, sum / static_cast<double>(pcm.size())));
        if (rms_db < -42.0 || clipped > pcm.size() / 200) continue;   // тихо або перевантажено
        const std::string file = sanitize_filename(std::format("{}_{:.1f}", demo_name, c.start)) + ".wav";
        audio::WavWriter w;
        std::string err;
        if (!w.open(dir / "clips" / path_from_utf8(file), voice::kVoiceRate, 1, audio::WavWriter::Format::Int16, &err) ||
            !w.write(pcm.data(), pcm.size()) || !w.close(&err)) {
            log_warn("{}", trf("Зразок голосу не записано: {}", err));
            continue;
        }
        VoiceSample s;
        s.file = file;
        s.text = c.text;
        s.language = language;
        s.seconds = static_cast<double>(pcm.size()) / voice::kVoiceRate;
        s.demo = demo_name;
        s.demo_time = c.start;
        s.added = static_cast<int64_t>(std::time(nullptr));
        p.samples.push_back(std::move(s));
        ++added;
    }
    // Не більше max_total секунд: лишаються найдовші (у них найбільше голосу)
    std::stable_sort(p.samples.begin(), p.samples.end(), [](const auto& a, const auto& b) { return a.seconds > b.seconds; });
    double total = 0;
    std::vector<VoiceSample> keep;
    for (auto& s : p.samples) {
        if (total + s.seconds > max_total && !keep.empty()) {
            fs::remove(dir / "clips" / path_from_utf8(s.file), ec);
            continue;
        }
        total += s.seconds;
        keep.push_back(std::move(s));
    }
    p.samples = std::move(keep);
    if (added > 0) p.elevenlabs_voice_id.clear();   // зразки змінились — хмарний клон створиться заново
    return added;
}

std::optional<Reference> make_reference(const VoiceProfile& p, const fs::path& samples_dir, const fs::path& work_dir,
                                        double max_seconds) {
    if (p.samples.empty()) return std::nullopt;
    std::vector<const VoiceSample*> order;
    for (const auto& s : p.samples) order.push_back(&s);
    std::stable_sort(order.begin(), order.end(), [](auto* a, auto* b) { return a->seconds > b->seconds; });
    Reference ref;
    std::error_code ec;
    fs::create_directories(work_dir, ec);
    ref.wav = work_dir / path_from_utf8(sanitize_filename(replace_all(p.key, ":", "_")) + "_ref.wav");
    audio::WavWriter w;
    std::string err;
    if (!w.open(ref.wav, voice::kVoiceRate, 1, audio::WavWriter::Format::Int16, &err)) {
        log_warn("{}", trf("Еталон голосу не записано: {}", err));
        return std::nullopt;
    }
    const std::vector<float> pause(static_cast<size_t>(voice::kVoiceRate * 0.25), 0.0f);
    std::vector<float> buf(4096);
    for (const VoiceSample* s : order) {
        const fs::path f = samples_dir / "clips" / path_from_utf8(s->file);
        ref.files.push_back(f);
        if (ref.seconds > 0 && ref.seconds + s->seconds > max_seconds) continue;   // еталон — до max_seconds
        audio::WavReader r;
        if (!r.open(f, false, &err) || r.channels() != 1) continue;
        if (ref.seconds > 0) {
            w.write(pause.data(), pause.size());
            ref.seconds += 0.25;
        }
        for (size_t got; (got = r.read(buf.data(), buf.size())) > 0;) w.write(buf.data(), got);
        ref.seconds += s->seconds;
        ref.text += (ref.text.empty() ? "" : " ") + s->text;
    }
    if (!w.close(&err) || ref.seconds <= 0) return std::nullopt;
    return ref;
}

} // namespace gmdr::dub
