#include "settings.hpp"

#include "../game/game_renderer.hpp"
#include "../util/file_util.hpp"
#include "../util/strings.hpp"

namespace gmdr::render {

namespace {
json::Value to_value(const std::string& v) { return json::Value::string(v); }
json::Value to_value(bool v) { return json::Value::boolean(v); }
json::Value to_value(int v) { return json::Value::number(v); }
json::Value to_value(double v) { return json::Value::number(v); }

void from_value(const json::Value& j, std::string& v) { if (!j.is_null()) v = j.as_string(v); }
void from_value(const json::Value& j, bool& v) { if (!j.is_null()) v = j.as_bool(v); }
void from_value(const json::Value& j, int& v) { if (!j.is_null()) v = static_cast<int>(j.as_int(v)); }
void from_value(const json::Value& j, double& v) { if (!j.is_null()) v = j.as_number(v); }
} // namespace

json::Value RenderSettings::to_json() const {
    json::Value j = json::Value::object();
    j.set("configuration_version", json::Value::number(kConfigurationVersion));
#define X(name) j.set(#name, to_value(name));
    GMDR_SETTINGS_FIELDS(X)
#undef X
    return j;
}

RenderSettings RenderSettings::from_json(const json::Value& j) {
    RenderSettings s;
#define X(name) from_value(j[#name], s.name);
    GMDR_SETTINGS_FIELDS(X)
#undef X
    // Міграція: версія 0 (до номерів версій) — режим RTX був прапорцем "rtx"
    const int version = static_cast<int>(j["configuration_version"].as_int(0));
    if (version < 1 && j["game_renderer"].is_null() && j["rtx"].as_bool(false)) s.game_renderer = "rtx";
    return s;
}

const game::GameRenderer& renderer_of(const RenderSettings& s) {
    const game::GameRenderer* r = game::find_game_renderer(s.game_renderer);
    return r ? *r : game::standard_renderer();
}

std::string& renderer_game_dir(RenderSettings& s) {
    return renderer_of(s).traits().dir_setting == "rtx_game_dir" ? s.rtx_game_dir : s.game_dir;
}

const std::string& renderer_game_dir(const RenderSettings& s) {
    return renderer_of(s).traits().dir_setting == "rtx_game_dir" ? s.rtx_game_dir : s.game_dir;
}

bool is_secret_field(const std::string& name) { return name.size() > 4 && name.ends_with("_key"); }

namespace {
json::Value redact(const json::Value& v) {
    if (v.is_object()) {
        json::Value o = json::Value::object();
        for (const auto& [k, x] : v.members())
            o.set(k, is_secret_field(k) && !x.as_string().empty() ? json::Value::string("(removed)") : redact(x));
        return o;
    }
    if (v.is_array()) {
        json::Value a = json::Value::array();
        for (const auto& x : v.items()) a.push(redact(x));
        return a;
    }
    return v;
}
} // namespace

std::string redact_secrets_json(const std::string& text) {
    auto j = json::parse(text);
    return j ? redact(*j).dump() : text;
}

bool save_settings(const RenderSettings& s, const std::string& path, std::string* error) {
    // Трохи "красивіший" JSON: кожне поле з нового рядка
    std::string text = "{\n";
    const auto obj = s.to_json();
    bool first = true;
    for (const auto& [k, v] : obj.members()) {
        if (!first) text += ",\n";
        first = false;
        text += "  " + json::escape_string(k) + ": " + v.dump();
    }
    text += "\n}\n";
    return write_file_atomic(path_from_utf8(path), text, error);
}

bool load_settings(RenderSettings& s, const std::string& path, std::string* error) {
    auto text = read_file_text(path_from_utf8(path), error);
    if (!text) return false;
    auto j = json::parse(*text, error);
    if (!j) return false;
    s = RenderSettings::from_json(*j);
    return true;
}

} // namespace gmdr::render
