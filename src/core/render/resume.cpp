#include "resume.hpp"

#include "../util/file_util.hpp"
#include "../util/json.hpp"
#include "../util/strings.hpp"

#include <algorithm>
#include <ctime>

namespace gmdr::render {

namespace fs = std::filesystem;

fs::path resume_dir() { return app_data_dir() / "resume"; }

fs::path parts_dir_for(const std::string& output_path) {
    const fs::path out = path_from_utf8(output_path);
    return out.parent_path() / path_from_utf8(path_to_utf8(out.stem()) + ".gmdr_parts");
}

fs::path resume_head_path(const std::string& output_path) {
    return parts_dir_for(output_path) / path_from_utf8("part0" + path_to_utf8(path_from_utf8(output_path).extension()));
}

bool save_resume(const ResumeRecord& r, std::string* error) {
    json::Value j = json::Value::object();
    j.set("version", json::Value::number(1));
    j.set("id", json::Value::string(r.id));
    j.set("settings", r.settings.to_json());
    j.set("video_t0", json::Value::number(r.video_t0));
    j.set("frames", json::Value::number(static_cast<double>(r.frames)));
    j.set("seconds", json::Value::number(r.seconds));
    json::Value w = json::Value::array();
    for (const auto& [path, t] : r.wavs) {
        json::Value e = json::Value::array();
        e.push(json::Value::string(path));
        e.push(json::Value::number(t));
        w.push(std::move(e));
    }
    j.set("wavs", std::move(w));
    j.set("updated", json::Value::number(static_cast<double>(std::time(nullptr))));
    std::error_code ec;
    fs::create_directories(resume_dir(), ec);
    return write_file_atomic(resume_dir() / (r.id + ".json"), j.dump(), error);
}

std::optional<ResumeRecord> load_resume(const fs::path& file) {
    auto text = read_file_text(file);
    if (!text) return std::nullopt;
    auto j = json::parse(*text);
    if (!j || !j->is_object() || (*j)["version"].as_int() != 1) return std::nullopt;
    ResumeRecord r;
    r.id = (*j)["id"].as_string();
    r.settings = RenderSettings::from_json((*j)["settings"]);
    r.video_t0 = (*j)["video_t0"].as_number();
    r.frames = (*j)["frames"].as_int();
    r.seconds = (*j)["seconds"].as_number();
    for (const auto& e : (*j)["wavs"].items())
        if (e.is_array() && e.items().size() == 2) r.wavs.push_back({e[size_t{0}].as_string(), e[size_t{1}].as_number()});
    r.updated = (*j)["updated"].as_int();
    if (r.id.empty() || r.settings.output_path.empty()) return std::nullopt;
    return r;
}

void forget_resume(const std::string& id) {
    if (id.empty()) return;
    std::error_code ec;
    const fs::path file = resume_dir() / (id + ".json");
    if (const auto r = load_resume(file)) {
        // Звук гри, перенесений до теки частин, більше не потрібен, а частковий відеофайл, якщо
        // дописування встигло забрати його туди, повертається на своє місце
        const fs::path dir = parts_dir_for(r->settings.output_path);
        for (const auto& [wav, t] : r->wavs)
            if (path_is_inside(path_from_utf8(wav), dir)) fs::remove(path_from_utf8(wav), ec);
        const fs::path out = path_from_utf8(r->settings.output_path), head = resume_head_path(r->settings.output_path);
        if (fs::exists(head, ec) && !fs::exists(out, ec)) fs::rename(head, out, ec);
        fs::remove(dir, ec);   // лише якщо порожня
    }
    fs::remove(file, ec);
}

std::vector<ResumeRecord> pending_resumes() {
    std::vector<ResumeRecord> out;
    std::error_code ec;
    for (fs::directory_iterator it(resume_dir(), ec), end; !ec && it != end; it.increment(ec)) {
        if (!ends_with_i(path_to_utf8(it->path().filename()), ".json")) continue;
        auto r = load_resume(it->path());
        if (!r) continue;
        std::error_code fec;
        const bool have = fs::exists(path_from_utf8(r->settings.output_path), fec) ||
                          fs::exists(resume_head_path(r->settings.output_path), fec);
        if (have) out.push_back(std::move(*r));
        else fs::remove(it->path(), fec);   // файлу вже немає (видалили чи нічого не встигло записатись)
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.updated > b.updated; });
    return out;
}

} // namespace gmdr::render
