#include "frame_transport.hpp"

#include "../frames/frame_pipe.hpp"
#include "../util/file_util.hpp"
#include "../util/i18n.hpp"
#include "../util/json.hpp"
#include "../util/strings.hpp"

#include <chrono>
#include <ctime>
#include <format>

namespace gmdr::render {

namespace fs = std::filesystem;

namespace {
constexpr double kForgetAfterSeconds = 30.0 * 24 * 3600;

fs::path store_path() { return app_data_dir() / "frame_transport.json"; }

// Ключ гри: шлях exe, його розмір і час зміни — після оновлення гри канал пробується знову
std::string game_key(const fs::path& exe) {
    std::error_code ec;
    const uint64_t size = fs::file_size(exe, ec);
    const auto mtime = fs::last_write_time(exe, ec);
    const auto ticks = ec ? 0 : static_cast<long long>(mtime.time_since_epoch().count());
    return std::format("{}|{}|{}", to_lower(path_to_utf8(exe.lexically_normal())), size, ticks);
}

// Якою назвою програма пропонує гри канал. Невдачу з іншою назвою (з попередньої версії програми)
// не зважаємо: канал пробується знову
std::string pipe_form() { return frames::pipe_movie_name("", ""); }

json::Value load_store() {
    if (auto text = read_file_text(store_path()))
        if (auto j = json::parse(*text); j && j->is_object()) return *j;
    return json::Value::object();
}
} // namespace

std::string normalize_frame_transport(const std::string& value) {
    const std::string v = to_lower(trim(value));
    return v == "pipe" || v == "files" ? v : "auto";
}

TransportChoice choose_frame_transport(const RenderSettings& s, const fs::path& game_exe) {
    TransportChoice c;
    const std::string mode = normalize_frame_transport(s.frame_transport);
    if (mode == "files") return c;
    if (s.manual_mode) {
        // startmovie вводить сам користувач, і програма не знає, коли саме, — тож і перевірити,
        // чи гра пише в канал, нема коли
        c.note = tr("ручний режим");
        return c;
    }
    if (!frames::frame_pipes_supported()) {
        c.note = tr("канали не підтримуються в цій системі");
        return c;
    }
    if (mode == "pipe") {
        c.pipe = c.strict = true;
        return c;
    }
    const json::Value store = load_store();
    const json::Value& e = store[game_key(game_exe)];
    const double when = e["time"].as_number(0);
    if (when > 0 && static_cast<double>(std::time(nullptr)) - when < kForgetAfterSeconds && e["name"].as_string() == pipe_form()) {
        c.note = trf("з цією грою канал уже не спрацював ({})", e["why"].as_string());
        return c;
    }
    c.pipe = true;
    return c;
}

void remember_pipe_failure(const fs::path& game_exe, const std::string& why) {
    json::Value store = load_store();
    json::Value e = json::Value::object();
    e.set("time", json::Value::number(static_cast<double>(std::time(nullptr))));
    std::string w = trim(why);
    while (!w.empty() && w.back() == '.') w.pop_back();   // пояснення йде в дужки в журналі
    e.set("why", json::Value::string(w));
    e.set("name", json::Value::string(pipe_form()));
    store.set(game_key(game_exe), std::move(e));
    write_file_atomic(store_path(), store.dump(), nullptr);
}

void forget_pipe_failure(const fs::path& game_exe) {
    const json::Value store = load_store();
    const std::string key = game_key(game_exe);
    if (!store.has(key)) return;
    json::Value out = json::Value::object();
    for (const auto& [k, v] : store.members())
        if (k != key) out.set(k, v);
    write_file_atomic(store_path(), out.dump(), nullptr);
}

} // namespace gmdr::render
