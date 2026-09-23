// =============================================================================
//  main_gui.cpp — точка входу програми з вікном.
// =============================================================================
#include "app.hpp"
#include "platform.hpp"

#include "core/util/crash_dump.hpp"
#include "core/util/file_util.hpp"
#include "core/util/strings.hpp"

#include "imgui.h"

#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

static int run(const std::vector<std::string>& args) {
    using namespace gmdr::gui;
    if (platform_forward_to_running_instance(args)) return 0;   // .dem відкриється у вже відкритій програмі
    gmdr::install_crash_handler(gmdr::app_data_dir());   // gmdr_crash_*.dmp, якщо програма впаде
    App app;
    PlatformCallbacks cb;
    cb.on_files_dropped = [&](const std::vector<std::string>& files) { app.on_files_dropped(files); };
    cb.on_close_request = [&]() { return app.on_close_request(); };
    if (!platform_init("GMod Demo Render", 1280, 820, cb)) return 1;
    app.init(args);

    // Для автотестів (Linux): зробити знімок вікна і вийти
    const char* shot = std::getenv("GMDR_SCREENSHOT");
    int frames = 0;
    const int shot_after = std::getenv("GMDR_SCREENSHOT_FRAMES") ? std::atoi(std::getenv("GMDR_SCREENSHOT_FRAMES")) : 60;

    const float clear[4] = {0.08f, 0.09f, 0.11f, 1.0f};
    while (!app.wants_quit()) {
        if (!platform_begin_frame()) break;
        ImGui::NewFrame();
        app.frame();
        ImGui::Render();
        platform_end_frame(clear);
        if (shot && ++frames == shot_after) {
            platform_screenshot(shot);
            break;
        }
    }
    platform_shutdown();
    return 0;
}

#ifdef _WIN32
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int n = 0;
    LPWSTR* w = CommandLineToArgvW(GetCommandLineW(), &n);
    std::vector<std::string> args;
    for (int i = 0; i < n; ++i) args.push_back(gmdr::wide_to_utf8(w[i]));
    LocalFree(w);
    return run(args);
}
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
    (void)argc;
    (void)argv;
    return wWinMain(GetModuleHandleW(nullptr), nullptr, GetCommandLineW(), SW_SHOWDEFAULT);
#else
    std::vector<std::string> args(argv, argv + argc);
    return run(args);
#endif
}
