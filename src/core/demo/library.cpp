#include "library.hpp"

#include <algorithm>
#include <chrono>
#include <set>

#include "../util/strings.hpp"
#include "demo_file.hpp"

namespace gmdr::demo {

namespace fs = std::filesystem;

namespace {
int64_t to_unix(fs::file_time_type t) {
    // file_clock -> system_clock без clock_cast (його ще немає в усіх стандартних бібліотеках)
    const auto sys = std::chrono::system_clock::now() + (t - fs::file_time_type::clock::now());
    return std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch()).count();
}

void add_file(const fs::path& p, std::vector<LibraryEntry>& out) {
    LibraryEntry e;
    e.path = path_to_utf8(p);
    e.name = path_to_utf8(p.filename());
    e.folder = path_to_utf8(p.parent_path());
    std::error_code ec;
    e.size = fs::file_size(p, ec);
    e.modified = to_unix(fs::last_write_time(p, ec));
    try {
        DemoFile d(p);
        const auto& h = d.header();
        e.map = h.map_name;
        e.server = h.server_name;
        e.recorded_by = h.client_name;
        e.seconds = h.playback_time > 0 ? h.playback_time : 0;
    } catch (const std::exception& ex) {
        e.error = ex.what();
    }
    out.push_back(std::move(e));
}
} // namespace

std::vector<LibraryEntry> scan_demo_library(const std::vector<fs::path>& dirs) {
    std::vector<LibraryEntry> out;
    std::set<std::string> seen;   // ті самі файли через різні теки — один раз
    auto consider = [&](const fs::path& p) {
        if (!ends_with_i(path_to_utf8(p.filename()), ".dem")) return;
        std::error_code ec;
        const std::string key = to_lower(path_to_utf8(fs::weakly_canonical(p, ec)));
        if (!seen.insert(key).second) return;
        add_file(p, out);
    };
    for (const auto& dir : dirs) {
        std::error_code ec;
        if (dir.empty() || !fs::is_directory(dir, ec)) continue;
        const bool game_root = path_to_utf8(dir.filename()) == "garrysmod";
        if (game_root) {
            // Сама тека гри — лише верхній рівень (там тисячі файлів аддонів), плюс demos/
            for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
                if (it->is_regular_file(ec)) consider(it->path());
            const fs::path demos = dir / "demos";
            for (fs::recursive_directory_iterator it(demos, fs::directory_options::skip_permission_denied, ec), end;
                 !ec && it != end; it.increment(ec))
                if (it->is_regular_file(ec)) consider(it->path());
        } else {
            for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
                 !ec && it != end; it.increment(ec)) {
                if (it.depth() > 4) it.disable_recursion_pending();   // не блукаємо надто глибоко
                if (it->is_regular_file(ec)) consider(it->path());
            }
        }
    }
    // Найновіші — першими
    std::sort(out.begin(), out.end(), [](const LibraryEntry& a, const LibraryEntry& b) { return a.modified > b.modified; });
    return out;
}

bool library_match(const LibraryEntry& e, const std::string& query) {
    const std::string hay = to_lower(e.name + " " + e.map + " " + e.server + " " + e.recorded_by);
    for (const auto& word : split(query, ' ')) {
        const std::string w = to_lower(trim(word));
        if (!w.empty() && hay.find(w) == std::string::npos) return false;
    }
    return true;
}

} // namespace gmdr::demo
