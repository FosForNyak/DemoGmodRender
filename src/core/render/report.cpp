#include "report.hpp"

#include "../game/gmod_install.hpp"
#include "../game/lua_driver.hpp"
#include "../game/rtx.hpp"
#include "../media/ffmpeg_util.hpp"
#include "../util/file_util.hpp"
#include "../util/strings.hpp"
#include "../util/system_info.hpp"
#include "../util/zip_writer.hpp"
#include "../util/i18n.hpp"
#include "settings.hpp"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <format>
#include <set>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/utsname.h>
#include <unistd.h>
#endif

#ifndef GMDR_VERSION
#define GMDR_VERSION "?"
#endif

namespace gmdr::render {

namespace fs = std::filesystem;

namespace {

std::string memory_description() {
    const uint64_t total = total_memory(), avail = available_memory();
    if (total == 0) return "?";
    return avail ? trf("{} (вільно {})", format_bytes(total), format_bytes(avail)) : format_bytes(total);
}

std::string profile_dir() {
#ifdef _WIN32
    if (const wchar_t* p = _wgetenv(L"USERPROFILE"); p && *p) return path_to_utf8(fs::path(p));
#else
    if (const char* h = std::getenv("HOME"); h && *h) return h;
#endif
    return {};
}

// Найсвіжіший дамп збою програми
fs::path latest_crash_dump() {
    fs::path best;
    fs::file_time_type best_t{};
    std::error_code ec;
    for (fs::directory_iterator it(app_data_dir(), ec), end; !ec && it != end; it.increment(ec)) {
        const std::string n = path_to_utf8(it->path().filename());
        if (!starts_with_i(n, "gmdr_crash_") || !ends_with_i(n, ".dmp")) continue;
        const auto t = it->last_write_time(ec);
        if (best.empty() || t > best_t) {
            best = it->path();
            best_t = t;
        }
    }
    return best;
}

// Замінити всі входження needle без урахування регістру (шляхи у Windows)
void replace_all_ci(std::string& s, const std::string& needle, const std::string& with) {
    if (needle.empty()) return;
    const std::string low_needle = to_lower(needle);
    std::string low = to_lower(s);
    size_t pos = 0;
    while ((pos = low.find(low_needle, pos)) != std::string::npos) {
        s.replace(pos, needle.size(), with);
        low.replace(pos, needle.size(), with);
        pos += with.size();
    }
}

} // namespace

std::string anonymize_paths(std::string text, const std::string& profile) {
    if (profile.size() < 4) return text;
    std::string back = profile, fwd = profile, json = profile;
    std::replace(back.begin(), back.end(), '/', '\\');
    std::replace(fwd.begin(), fwd.end(), '\\', '/');
    json = replace_all(back, "\\", "\\\\");   // у JSON зворотні скісні подвоєні
    replace_all_ci(text, json, "%USERPROFILE%");
    replace_all_ci(text, back, "%USERPROFILE%");
    replace_all_ci(text, fwd, "%USERPROFILE%");
    return text;
}

std::string system_summary() {
    std::string s;
    const std::time_t now = std::time(nullptr);
    char when[32] = {};
    std::strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    s += trf("GMod Demo Render {}\nЗвіт створено: {}\n\n", GMDR_VERSION, when);
    s += tr("ОС: ") + os_description() + "\n";
    s += trf("Процесор: {} ({} потоків)\n", cpu_name(), std::thread::hardware_concurrency());
    s += tr("Пам'ять: ") + memory_description() + "\n";
    for (const auto& g : system_gpus()) {
        s += tr("Відеоадаптер: ") + g.name;
        if (g.vram_bytes) s += trf(", пам'ять {}", format_bytes(g.vram_bytes));
        if (!g.driver.empty()) s += trf(", драйвер {}", g.driver);
        s += "\n";
    }
    s += std::format("FFmpeg: {} (avcodec {}.{}, avformat {}.{})\n", av_version_info(), LIBAVCODEC_VERSION_MAJOR,
                     LIBAVCODEC_VERSION_MINOR, LIBAVFORMAT_VERSION_MAJOR, LIBAVFORMAT_VERSION_MINOR);
    std::string gpu_enc;
    for (const auto& e : media::list_encoders(true))
        if (e.hardware) gpu_enc += (gpu_enc.empty() ? "" : ", ") + e.name;
    s += tr("GPU-кодеки у збірці FFmpeg: ") + (gpu_enc.empty() ? std::string(tr("немає")) : gpu_enc) + "\n";
    s += trf("Папка програми: {} (вільно {})\n", path_to_utf8(app_data_dir()), format_bytes(free_disk_space(app_data_dir())));

    s += "\nGarry's Mod:\n";
    if (auto g = game::detect_gmod()) {
        s += tr("  папка: ") + path_to_utf8(g->root) + trf(" (вільно {})\n", format_bytes(free_disk_space(g->root)));
        for (const auto& e : g->executables) s += "  exe: " + game::GModInstall::exe_label(e) + " — " + path_to_utf8(e) + "\n";
        const auto st = game::driver_state(*g);
        s += std::string(tr("  драйвер: ")) +
             (st == game::DriverState::Installed ? tr("встановлено") : st == game::DriverState::Outdated ? tr("застарілий") : tr("не встановлено")) +
             trf(" (версія програми {})\n", game::kDriverVersion);
        if (game::is_rtx_install(*g)) s += tr("  це копія GMod RTX\n");
    } else {
        s += tr("  не знайдено автоматично\n");
    }
    if (auto rtx = game::detect_rtx_install()) s += "GMod RTX: " + path_to_utf8(rtx->root) + "\n";
    return s;
}

std::string default_report_name() {
    const std::time_t now = std::time(nullptr);
    char buf[64] = {};
    std::strftime(buf, sizeof buf, "%Y-%m-%d_%H-%M", std::localtime(&now));
    return std::string(tr("gmdr_звіт_")) + buf + ".zip";
}

bool make_problem_report(const fs::path& out_zip, const ReportInput& in, std::vector<std::string>* contents,
                         std::string* error) {
    ZipWriter zip;
    if (!zip.open(out_zip, error)) return false;
    const std::string profile = profile_dir();
    std::vector<std::string> added;
    auto add_text = [&](const std::string& name, const std::string& text) {
        if (zip.add(name, anonymize_paths(text, profile))) added.push_back(name);
    };
    auto add_text_file = [&](const std::string& name, const fs::path& src) {
        if (auto t = read_file_text(src)) add_text(name, *t);
    };
    add_text(tr("ПРОЧИТАЙ.txt"),
             tr("Звіт GMod Demo Render для діагностики проблеми.\n\n"
             "журнал.txt — журнал програми (і попередній запуск);\n"
             "налаштування.json, черга.json — налаштування рендеру і черга;\n"
             "система.txt — ОС, процесор, пам'ять, відеокарти, FFmpeg, Garry's Mod і драйвер;\n"
             "консоль_gmod.txt — останні рядки консолі гри (garrysmod/console.log);\n"
             "*.dmp — останній дамп збою програми, якщо вона падала (у ньому пам'ять програми).\n\n"
             "Шлях до вашого профілю Windows у текстових файлах замінено на %USERPROFILE%.\n"
             "Архів нікуди не надсилається автоматично — перегляньте його і передайте сам.\n"));
    std::string sys = system_summary();
    if (!in.extra_text.empty()) sys += "\n" + in.extra_text + "\n";
    add_text(tr("система.txt"), sys);
    add_text_file(tr("журнал.txt"), app_data_dir() / "gmdr_log.txt");
    add_text_file(tr("журнал_попередній.txt"), app_data_dir() / "gmdr_log.old.txt");
    // Налаштування й черга — без API-ключів сервісів перекладу й озвучення
    auto add_json_file = [&](const std::string& name, const fs::path& src) {
        if (auto t = read_file_text(src)) add_text(name, redact_secrets_json(*t));
    };
    if (!in.settings_path.empty()) add_json_file(tr("налаштування.json"), path_from_utf8(in.settings_path));
    add_json_file(tr("черга.json"), app_data_dir() / "gmdr_queue.json");
    if (auto g = game::detect_gmod()) {
        const auto tail = game::console_log_tail(*g, 2000);
        if (!tail.empty()) {
            std::string t;
            for (const auto& l : tail) t += l + "\n";
            add_text(tr("консоль_gmod.txt"), t);
        }
    }
    const fs::path dump = latest_crash_dump();
    std::error_code ec;
    if (!dump.empty() && fs::file_size(dump, ec) < (200ull << 20)) {
        const std::string name = path_to_utf8(dump.filename());
        if (zip.add_file(name, dump)) added.push_back(name);
    }
    if (!zip.close(error)) return false;
    if (contents) *contents = std::move(added);
    return true;
}

} // namespace gmdr::render
