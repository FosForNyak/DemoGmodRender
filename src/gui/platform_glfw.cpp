// =============================================================================
//  platform_glfw.cpp — вікно GLFW + OpenGL 3 для Dear ImGui (Linux).
//  Основна платформа програми — Windows; ця версія для розробки й тестів.
// =============================================================================
#ifndef _WIN32

#include "platform.hpp"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <vector>

namespace gmdr::gui {

namespace {
GLFWwindow*       g_window = nullptr;
PlatformCallbacks g_cb;
float             g_dpi = 1.0f;

void drop_callback(GLFWwindow*, int count, const char** paths) {
    std::vector<std::string> files;
    for (int i = 0; i < count; ++i) files.emplace_back(paths[i]);
    if (g_cb.on_files_dropped) g_cb.on_files_dropped(files);
}

void close_callback(GLFWwindow* w) {
    if (g_cb.on_close_request && !g_cb.on_close_request()) glfwSetWindowShouldClose(w, GLFW_FALSE);
}

bool have_zenity() { return std::system("command -v zenity > /dev/null 2>&1") == 0; }

std::string run_capture(const std::string& cmd) {
    std::string out;
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return out;
    char buf[4096];
    while (fgets(buf, sizeof(buf), p)) out += buf;
    pclose(p);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}

std::string shell_quote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    return out + "'";
}
} // namespace

bool platform_init(const std::string& title, int width, int height, const PlatformCallbacks& cb) {
    g_cb = cb;
    if (!glfwInit()) return false;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    g_dpi = 1.0f;
    if (const char* s = std::getenv("GMDR_UI_SCALE")) g_dpi = static_cast<float>(std::atof(s));
    if (g_dpi <= 0.1f) g_dpi = 1.0f;
    g_window = glfwCreateWindow(static_cast<int>(width * g_dpi), static_cast<int>(height * g_dpi), title.c_str(),
                                nullptr, nullptr);
    if (!g_window) return false;
    glfwMakeContextCurrent(g_window);
    glfwSwapInterval(1);
    glfwSetDropCallback(g_window, drop_callback);
    glfwSetWindowCloseCallback(g_window, close_callback);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(g_window, true);
    ImGui_ImplOpenGL3_Init("#version 130");
    return true;
}

bool platform_begin_frame() {
    glfwPollEvents();
    if (glfwWindowShouldClose(g_window)) return false;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    return true;
}

void platform_end_frame(const float c[4]) {
    int w, h;
    glfwGetFramebufferSize(g_window, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(c[0], c[1], c[2], c[3]);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(g_window);
}

void platform_shutdown() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(g_window);
    glfwTerminate();
}

float platform_dpi_scale() { return g_dpi; }
bool platform_prefers_light_theme() { return false; }
void platform_set_frame_style(bool, unsigned) {}
void platform_set_title(const std::string& title) { glfwSetWindowTitle(g_window, title.c_str()); }

std::string open_file_dialog(const std::string& title, const std::vector<FileFilter>& filters, const std::string& initial) {
    if (!have_zenity()) return {};
    std::string cmd = "zenity --file-selection --title=" + shell_quote(title);
    for (const auto& [n, p] : filters) cmd += " --file-filter=" + shell_quote(n + " | " + p);
    if (!initial.empty()) cmd += " --filename=" + shell_quote(initial);
    return run_capture(cmd + " 2>/dev/null");
}

std::string save_file_dialog(const std::string& title, const std::vector<FileFilter>& filters, const std::string& initial,
                             const std::string&) {
    if (!have_zenity()) return {};
    std::string cmd = "zenity --file-selection --save --confirm-overwrite --title=" + shell_quote(title);
    for (const auto& [n, p] : filters) cmd += " --file-filter=" + shell_quote(n + " | " + p);
    if (!initial.empty()) cmd += " --filename=" + shell_quote(initial);
    return run_capture(cmd + " 2>/dev/null");
}

std::string pick_folder_dialog(const std::string& title, const std::string& initial) {
    if (!have_zenity()) return {};
    std::string cmd = "zenity --file-selection --directory --title=" + shell_quote(title);
    if (!initial.empty()) cmd += " --filename=" + shell_quote(initial + "/");
    return run_capture(cmd + " 2>/dev/null");
}

void open_path(const std::string& path) {
    if (std::system(("xdg-open " + shell_quote(path) + " >/dev/null 2>&1 &").c_str()) != 0) {}
}
void show_in_folder(const std::string& path) {
    const auto slash = path.find_last_of('/');
    open_path(slash == std::string::npos ? "." : path.substr(0, slash));
}

std::vector<std::vector<std::string>> ui_fallback_fonts() {
    return {{"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
             "/usr/share/fonts/dejavu/DejaVuSans.ttf"},
            {"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
             "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc", "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc"},
            {"/usr/share/fonts/truetype/noto/NotoSansDevanagari-Regular.ttf", "/usr/share/fonts/noto/NotoSansDevanagari-Regular.ttf"}};
}

std::vector<std::string> ui_bold_font_candidates() {
    return {"/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
            "/usr/share/fonts/truetype/noto/NotoSans-SemiBold.ttf", "/usr/share/fonts/noto/NotoSans-SemiBold.ttf"};
}

std::vector<std::string> ui_font_candidates() {
    return {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf", "/usr/share/fonts/noto/NotoSans-Regular.ttf"};
}

std::string clipboard_text_set(const std::string& text) {
    ImGui::SetClipboardText(text.c_str());
    return text;
}

bool platform_forward_to_running_instance(const std::vector<std::string>&) { return false; }
void platform_tray(bool, const std::string&) {}
void platform_notify(const std::string&, const std::string&) {}
void platform_set_minimize_to_tray(bool) {}
bool platform_window_hidden() { return false; }
void platform_restore_window() {}

bool platform_screenshot(const std::string& path) {
    int w, h;
    glfwGetFramebufferSize(g_window, &w, &h);
    std::vector<unsigned char> px(static_cast<size_t>(w) * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    std::ofstream f(path, std::ios::binary);
    f << "P6\n" << w << " " << h << "\n255\n";
    for (int y = h - 1; y >= 0; --y) f.write(reinterpret_cast<const char*>(px.data() + static_cast<size_t>(y) * w * 3), w * 3);
    return static_cast<bool>(f);
}

uint64_t platform_update_texture(uint64_t id, int w, int h, const uint8_t* rgba) {
    GLuint tex = static_cast<GLuint>(id);
    if (!tex) glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    return tex;
}

void platform_destroy_texture(uint64_t id) {
    GLuint tex = static_cast<GLuint>(id);
    if (tex) glDeleteTextures(1, &tex);
}

void platform_set_taskbar_progress(TaskbarState, double) {}
void platform_flash_window() {}

} // namespace gmdr::gui

#endif // !_WIN32
