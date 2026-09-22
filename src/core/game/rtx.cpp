#include "rtx.hpp"

#include "../util/file_util.hpp"
#include "../util/log.hpp"
#include "../util/strings.hpp"

#include <cstdlib>
#include <regex>
#include <system_error>

namespace gmdr::game {

namespace {
fs::path local_app_data() {
#ifdef _WIN32
    if (const wchar_t* la = _wgetenv(L"LOCALAPPDATA"); la && *la) return fs::path(la);
#endif
    return {};
}

// Значення атрибута XML (RTXLauncher пише налаштування одним тегом з атрибутами)
std::string xml_attr(const std::string& xml, const std::string& name) {
    const std::regex re("\\b" + name + "=\"([^\"]*)\"");
    std::smatch m;
    if (std::regex_search(xml, m, re)) {
        std::string v = m[1].str();
        v = replace_all(v, "&amp;", "&");
        v = replace_all(v, "&quot;", "\"");
        return v;
    }
    return {};
}

// Кожен параметр профілю — з поясненням, чому саме таке значення.
const std::map<std::string, std::string> kProfile = {
    // Пресет якості має найвищий пріоритет у Remix; Custom — щоб діяли параметри нижче
    {"rtx.graphicsPreset", "4"},
    // Режим DLSS "Full Resolution" (DLAA): трасування у повній роздільній здатності
    {"rtx.qualityDLSS", "5"},
    // Генерація кадрів не потрібна: кожен кадр рендериться окремо, з фіксованим кроком
    {"rtx.frameGenerationType", "0"},
    // Заставка "Alt+X — меню Remix" не повинна потрапити у відео
    {"rtx.hideSplashMessage", "True"},
};
constexpr const char* kProfileMarker = "# GMod Demo Render";
} // namespace

const std::map<std::string, std::string>& rtx_render_profile_values() { return kProfile; }

bool is_rtx_install(const GModInstall& g) {
    std::error_code ec;
    return fs::exists(g.root / "rtx.conf", ec) || fs::exists(g.root / "rtx-remix", ec);
}

std::optional<GModInstall> detect_rtx_install(std::vector<std::string>* log) {
    const fs::path base = local_app_data() / "RTXLauncher";
    std::vector<fs::path> candidates;
    if (auto xml = read_file_text(base / "settings.xml")) {
        const std::string manual = xml_attr(*xml, "ManuallySpecifiedInstallPath");
        if (!manual.empty()) candidates.push_back(path_from_utf8(manual));
    }
    candidates.push_back(base / "Game");
    for (const auto& c : candidates) {
        auto g = gmod_from_dir(c);
        if (g && g->valid() && is_rtx_install(*g)) {
            if (log) log->push_back("Знайдено GMod RTX (RTXLauncher): " + path_to_utf8(g->root));
            return g;
        }
    }
    return std::nullopt;
}

bool apply_rtx_render_profile(const GModInstall& g, const fs::path& backup, std::string* error) {
    const fs::path conf = g.root / "rtx.conf";
    std::error_code ec;
    std::string text;
    if (fs::exists(conf, ec)) {
        if (!copy_file_overwrite(conf, backup, error)) return false;
        text = read_file_text(conf).value_or("");
    } else {
        write_file_text(backup, "", nullptr);   // порожня копія = файлу не було
    }
    // Прибираємо з файлу рядки з тими самими ключами, щоб не було двозначності
    std::string out;
    for (const auto& line : split(replace_all(text, "\r", ""), '\n', false)) {
        const std::string t = trim(line);
        const size_t eq = t.find('=');
        const std::string key = eq == std::string::npos ? "" : trim(t.substr(0, eq));
        if (kProfile.count(key) || t.rfind(kProfileMarker, 0) == 0) continue;
        out += line + "\n";
    }
    while (out.size() >= 2 && out[out.size() - 1] == '\n' && out[out.size() - 2] == '\n') out.pop_back();
    out += std::string(kProfileMarker) + ": налаштування на час рендеру (оригінал буде повернуто)\n";
    for (const auto& [k, v] : kProfile) out += k + " = " + v + "\n";
    return write_file_text(conf, out, error);
}

bool restore_rtx_profile(const GModInstall& g, const fs::path& backup) {
    std::error_code ec;
    if (!fs::exists(backup, ec)) return false;
    const fs::path conf = g.root / "rtx.conf";
    std::string err;
    bool ok;
    if (file_size_or_zero(backup) == 0) {
        ok = !fs::exists(conf, ec) || fs::remove(conf, ec);   // до рендеру файлу не було
    } else {
        ok = copy_file_overwrite(backup, conf, &err);
    }
    if (!ok) {
        log_warn("Не вдалося повернути rtx.conf: {}", err.empty() ? ec.message() : err);
        return false;
    }
    fs::remove(backup, ec);
    return true;
}

std::map<std::string, std::string> read_remix_effective_options(const GModInstall& g) {
    std::map<std::string, std::string> out;
    auto text = read_file_text(g.root / "rtx-remix" / "logs" / "remix-dxvk.log");
    if (!text) return out;
    // Беремо останній блок "Effective RtxOption values"
    const size_t at = text->rfind("Effective RtxOption values");
    if (at == std::string::npos) return out;
    const auto lines = split(text->substr(at), '\n');
    for (size_t i = 1; i < lines.size(); ++i) {
        const std::string& l = lines[i];
        const size_t info = l.find("info:    rtx.");
        if (info == std::string::npos) {
            if (i > 1) break;   // кінець блоку
            continue;
        }
        const std::string kv = l.substr(info + 9);
        const size_t eq = kv.find(" = ");
        if (eq == std::string::npos) continue;
        out[trim(kv.substr(0, eq))] = trim(kv.substr(eq + 3));
    }
    return out;
}

} // namespace gmdr::game
