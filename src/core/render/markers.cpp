#include "markers.hpp"

#include "../util/file_util.hpp"
#include "../util/json.hpp"
#include "../util/strings.hpp"
#include "../util/i18n.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace gmdr::render {

namespace {
std::string one_line(std::string_view s) {
    std::string out;
    for (const char c : s) out.push_back(c == '\t' || c == '\n' || c == '\r' ? ' ' : c);
    return trim(out);
}

// Демо впізнається за іменем файлу і розміром — так позначки не губляться при переміщенні
std::string demo_key(const std::string& demo_path) {
    const fs::path p = path_from_utf8(demo_path);
    return std::format("{}|{}", path_to_utf8(p.filename()), file_size_or_zero(p));
}
} // namespace

std::vector<Marker> parse_markers(std::string_view text) {
    std::vector<Marker> out;
    for (const auto& line : split(replace_all(std::string(text), "\r", ""), '\n')) {
        const size_t tab = line.find('\t');
        const auto tick = parse_int(trim(line.substr(0, tab)));
        if (!tick || *tick < 0) continue;
        add_marker(out, {static_cast<int32_t>(*tick), tab == std::string::npos ? std::string() : one_line(line.substr(tab + 1))});
    }
    return out;
}

std::string format_markers(const std::vector<Marker>& markers) {
    std::string out;
    for (const auto& m : markers) out += std::format("{}\t{}\n", m.tick, one_line(m.title));
    return out;
}

void add_marker(std::vector<Marker>& markers, Marker m) {
    m.title = one_line(m.title);
    auto it = std::find_if(markers.begin(), markers.end(), [&](const Marker& x) { return x.tick == m.tick; });
    if (it != markers.end()) {
        if (!m.title.empty()) it->title = m.title;
        return;
    }
    markers.insert(std::upper_bound(markers.begin(), markers.end(), m, [](const Marker& a, const Marker& b) { return a.tick < b.tick; }),
                   std::move(m));
}

std::vector<Chapter> chapters_for_range(const std::vector<Marker>& markers, int32_t start_tick, int32_t end_tick,
                                        double ti) {
    std::vector<Chapter> out;
    if (end_tick <= start_tick || ti <= 0) return out;
    const double total = (end_tick - start_tick) * ti;
    for (const auto& m : markers) {
        if (m.tick < start_tick || m.tick >= end_tick) continue;
        Chapter c;
        c.start = (m.tick - start_tick) * ti;
        c.title = m.title.empty() ? trf("Позначка {}", out.size() + 1) : m.title;
        if (!out.empty() && c.start - out.back().start < 0.5) continue;   // надто близько — зайвий розділ
        out.push_back(std::move(c));
    }
    if (out.empty()) return out;
    if (out.front().start >= 1.0) out.insert(out.begin(), Chapter{0, 0, tr("Початок")});
    else out.front().start = 0;
    for (size_t i = 0; i < out.size(); ++i) out[i].end = i + 1 < out.size() ? out[i + 1].start : total;
    return out;
}

std::string chapters_as_text(const std::vector<Chapter>& chapters) {
    std::string out;
    for (const auto& c : chapters) {
        const int t = static_cast<int>(std::floor(c.start));
        out += t >= 3600 ? std::format("{}:{:02}:{:02} {}\n", t / 3600, t / 60 % 60, t % 60, c.title)
                         : std::format("{}:{:02} {}\n", t / 60, t % 60, c.title);
    }
    return out;
}

std::vector<Marker> load_demo_markers(const fs::path& store, const std::string& demo_path) {
    std::vector<Marker> out;
    auto text = read_file_text(store);
    if (!text) return out;
    auto j = json::parse(*text);
    if (!j || !j->is_object()) return out;
    for (const auto& m : (*j)[demo_key(demo_path)].items())
        add_marker(out, {static_cast<int32_t>(m["tick"].as_int(0)), m["title"].as_string()});
    return out;
}

bool save_demo_markers(const fs::path& store, const std::string& demo_path, const std::vector<Marker>& markers,
                       std::string* error) {
    json::Value root = json::Value::object();
    if (auto text = read_file_text(store))
        if (auto j = json::parse(*text); j && j->is_object()) root = *j;
    json::Value list = json::Value::array();
    for (const auto& m : markers) {
        json::Value o = json::Value::object();
        o.set("tick", json::Value::number(m.tick));
        o.set("title", json::Value::string(m.title));
        list.push(o);
    }
    root.set(demo_key(demo_path), markers.empty() ? json::Value() : list);
    // Порожні записи не зберігаємо
    json::Value clean = json::Value::object();
    for (const auto& [k, v] : root.members())
        if (v.is_array() && !v.items().empty()) clean.set(k, v);
    return write_file_atomic(store, clean.dump(), error);
}

} // namespace gmdr::render
