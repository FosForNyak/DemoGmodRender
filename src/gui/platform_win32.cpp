// =============================================================================
//  platform_win32.cpp — вікно Win32 + Direct3D 11 для Dear ImGui.
// =============================================================================
#ifdef _WIN32

#include "platform.hpp"

#include "core/util/strings.hpp"

#include <algorithm>
#include <filesystem>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace gmdr::gui {

namespace {
HWND                    g_hwnd = nullptr;
WNDCLASSEXW             g_wc{};
ID3D11Device*           g_device = nullptr;
ID3D11DeviceContext*    g_context = nullptr;
IDXGISwapChain*         g_swapchain = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
bool                    g_occluded = false;
UINT                    g_resize_w = 0, g_resize_h = 0;
PlatformCallbacks       g_cb;
float                   g_dpi = 1.0f;
bool                    g_quit = false;

// Текстури прев'ю і прогрес у панелі задач
struct UserTexture {
    ID3D11Texture2D*          tex = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    int                       w = 0, h = 0;
};
std::vector<UserTexture> g_textures;
ITaskbarList3*           g_taskbar = nullptr;
bool                     g_taskbar_tried = false;

// Трей і "холості" кадри (вікно згорнуте або в треї: програма стежить за рендером, але не малює)
constexpr UINT           kTrayMessage = WM_APP + 1;
NOTIFYICONDATAW          g_tray{};
bool                     g_tray_shown = false;
bool                     g_minimize_to_tray = false;
bool                     g_idle_frame = false;

void restore_from_tray() {
    ShowWindow(g_hwnd, SW_SHOW);
    if (IsIconic(g_hwnd)) ShowWindow(g_hwnd, SW_RESTORE);
    SetForegroundWindow(g_hwnd);
}

bool tray_add() {
    if (g_tray_shown) return true;
    g_tray = {};
    g_tray.cbSize = sizeof(g_tray);
    g_tray.hWnd = g_hwnd;
    g_tray.uID = 1;
    g_tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_tray.uCallbackMessage = kTrayMessage;
    g_tray.hIcon = g_wc.hIcon;
    wcsncpy(g_tray.szTip, L"GMod Demo Render", ARRAYSIZE(g_tray.szTip) - 1);
    g_tray_shown = Shell_NotifyIconW(NIM_ADD, &g_tray) != FALSE;
    return g_tray_shown;
}

void tray_remove() {
    if (!g_tray_shown) return;
    Shell_NotifyIconW(NIM_DELETE, &g_tray);
    g_tray_shown = false;
}

void create_rtv() {
    ID3D11Texture2D* back = nullptr;
    g_swapchain->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) {
        g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
        back->Release();
    }
}
void release_rtv() {
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
}

bool create_device(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                               D3D11_SDK_VERSION, &sd, &g_swapchain, &g_device, &got, &g_context);
    if (hr == DXGI_ERROR_UNSUPPORTED)   // немає відеокарти — програмний рендер WARP
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                           &sd, &g_swapchain, &g_device, &got, &g_context);
    if (FAILED(hr)) return false;
    create_rtv();
    return true;
}

void cleanup_device() {
    release_rtv();
    if (g_swapchain) { g_swapchain->Release(); g_swapchain = nullptr; }
    if (g_context) { g_context->Release(); g_context = nullptr; }
    if (g_device) { g_device->Release(); g_device = nullptr; }
}

constexpr ULONG_PTR kCopyDataOpen = 0x52444D47;   // "GMDR": шлях до файлу від другого запуску програми

LRESULT WINAPI wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) return true;
    switch (msg) {
    case WM_SIZE:
        if (wparam == SIZE_MINIMIZED) {
            if (g_minimize_to_tray && tray_add()) ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        g_resize_w = LOWORD(lparam);
        g_resize_h = HIWORD(lparam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wparam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DROPFILES: {
        HDROP drop = reinterpret_cast<HDROP>(wparam);
        const UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        std::vector<std::string> files;
        for (UINT i = 0; i < n; ++i) {
            const UINT len = DragQueryFileW(drop, i, nullptr, 0);
            std::wstring w(len + 1, L'\0');
            DragQueryFileW(drop, i, w.data(), len + 1);
            w.resize(len);
            files.push_back(wide_to_utf8(w));
        }
        DragFinish(drop);
        if (g_cb.on_files_dropped) g_cb.on_files_dropped(files);
        return 0;
    }
    case kTrayMessage:
        if (lparam == WM_LBUTTONUP || lparam == WM_LBUTTONDBLCLK || lparam == WM_RBUTTONUP ||
            lparam == NIN_BALLOONUSERCLICK)
            restore_from_tray();
        return 0;
    case WM_COPYDATA: {
        const auto* cds = reinterpret_cast<const COPYDATASTRUCT*>(lparam);
        if (!cds || cds->dwData != kCopyDataOpen) break;
        if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
        SetForegroundWindow(hwnd);
        if (cds->lpData && cds->cbData > 0 && g_cb.on_files_dropped)
            g_cb.on_files_dropped({std::string(static_cast<const char*>(cds->lpData), cds->cbData)});
        return TRUE;
    }
    case WM_CLOSE:
        if (g_cb.on_close_request && !g_cb.on_close_request()) return 0;
        g_quit = true;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lparam);
        mmi->ptMinTrackSize.x = static_cast<LONG>(900 * g_dpi);
        mmi->ptMinTrackSize.y = static_cast<LONG>(600 * g_dpi);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

std::wstring filters_to_spec_storage(const std::vector<FileFilter>& filters, std::vector<COMDLG_FILTERSPEC>& specs,
                                     std::vector<std::wstring>& storage) {
    storage.clear();
    specs.clear();
    storage.reserve(filters.size() * 2);
    for (const auto& [name, pattern] : filters) {
        storage.push_back(utf8_to_wide(name));
        storage.push_back(utf8_to_wide(pattern));
    }
    for (size_t i = 0; i < filters.size(); ++i) specs.push_back({storage[i * 2].c_str(), storage[i * 2 + 1].c_str()});
    return {};
}

std::string file_dialog(int kind /*0 open, 1 save, 2 folder*/, const std::string& title,
                        const std::vector<FileFilter>& filters, const std::string& initial, const std::string& ext) {
    IFileDialog* dlg = nullptr;
    const CLSID clsid = kind == 1 ? CLSID_FileSaveDialog : CLSID_FileOpenDialog;
    if (FAILED(CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return {};
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    opts |= FOS_FORCEFILESYSTEM;
    if (kind == 2) opts |= FOS_PICKFOLDERS;
    if (kind == 1) opts |= FOS_OVERWRITEPROMPT;
    dlg->SetOptions(opts);
    dlg->SetTitle(utf8_to_wide(title).c_str());
    std::vector<COMDLG_FILTERSPEC> specs;
    std::vector<std::wstring> storage;
    if (kind != 2 && !filters.empty()) {
        filters_to_spec_storage(filters, specs, storage);
        dlg->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
    }
    if (!ext.empty()) dlg->SetDefaultExtension(utf8_to_wide(ext).c_str());
    if (!initial.empty()) {
        std::filesystem::path p = path_from_utf8(initial);
        std::error_code ec;
        std::filesystem::path folder = std::filesystem::is_directory(p, ec) ? p : p.parent_path();
        IShellItem* item = nullptr;
        if (!folder.empty() &&
            SUCCEEDED(SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_PPV_ARGS(&item)))) {
            dlg->SetFolder(item);
            item->Release();
        }
        if (kind == 1 && !std::filesystem::is_directory(p, ec)) dlg->SetFileName(p.filename().c_str());
    }
    std::string result;
    if (SUCCEEDED(dlg->Show(g_hwnd))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                result = wide_to_utf8(path);
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dlg->Release();
    return result;
}
} // namespace

bool platform_forward_to_running_instance(const std::vector<std::string>& args) {
    if (std::getenv("GMDR_MULTI_INSTANCE")) return false;   // для розробки й автотестів
    // М'ютекс живе, доки працює перша копія програми
    static const HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\GModDemoRender.instance");
    static const bool another = mutex && GetLastError() == ERROR_ALREADY_EXISTS;
    if (!another) return false;
    HWND other = FindWindowW(L"GModDemoRenderWnd", nullptr);
    if (!other) return false;   // перша копія ще не створила вікно — працюємо як звичайно
    DWORD pid = 0;
    GetWindowThreadProcessId(other, &pid);
    AllowSetForegroundWindow(pid);   // інакше Windows не дасть їй вийти на передній план
    std::string file;
    for (size_t i = 1; i < args.size(); ++i)
        if (!args[i].empty() && args[i][0] != '-') {
            file = args[i];
            break;
        }
    COPYDATASTRUCT cds{kCopyDataOpen, static_cast<DWORD>(file.size()), file.empty() ? nullptr : file.data()};
    DWORD_PTR result = 0;
    SendMessageTimeoutW(other, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds), SMTO_ABORTIFHUNG, 5000, &result);
    return true;
}

bool platform_init(const std::string& title, int width, int height, const PlatformCallbacks& cb) {
    g_cb = cb;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    ImGui_ImplWin32_EnableDpiAwareness();
    g_dpi = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));
    if (const char* s = std::getenv("GMDR_UI_SCALE"); s && std::atof(s) > 0.1) g_dpi = static_cast<float>(std::atof(s));   // для перевірки інтерфейсу
    g_wc = {sizeof(g_wc), CS_CLASSDC, wnd_proc, 0, 0, GetModuleHandleW(nullptr), nullptr, nullptr, nullptr, nullptr,
            L"GModDemoRenderWnd", nullptr};
    g_wc.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101));
    g_wc.hIconSm = g_wc.hIcon;
    g_wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&g_wc);
    const int w = static_cast<int>(width * g_dpi), h = static_cast<int>(height * g_dpi);
    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    const int x = std::max<int>(wa.left, wa.left + ((wa.right - wa.left) - w) / 2);
    const int y = std::max<int>(wa.top, wa.top + ((wa.bottom - wa.top) - h) / 2);
    g_hwnd = CreateWindowW(g_wc.lpszClassName, utf8_to_wide(title).c_str(), WS_OVERLAPPEDWINDOW, x, y,
                           std::min<int>(w, wa.right - wa.left), std::min<int>(h, wa.bottom - wa.top), nullptr, nullptr,
                           g_wc.hInstance, nullptr);
    if (!g_hwnd) return false;
    // Темна рамка вікна (Windows 10 20H1+ / 11)
    BOOL dark = TRUE;
    DwmSetWindowAttribute(g_hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
    if (!create_device(g_hwnd)) {
        cleanup_device();
        MessageBoxW(nullptr, L"Не вдалося ініціалізувати Direct3D 11", L"GMod Demo Render", MB_ICONERROR);
        return false;
    }
    DragAcceptFiles(g_hwnd, TRUE);
    ShowWindow(g_hwnd, SW_SHOWDEFAULT);
    UpdateWindow(g_hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);
    return true;
}

bool platform_begin_frame() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (msg.message == WM_QUIT) g_quit = true;
    }
    if (g_quit) return false;
    // Вікно згорнуте, у треї або повністю закрите іншими: кадр раз на 100 мс і без малювання —
    // програма далі стежить за рендером (сповіщення, дія після завершення), а процесор вільний.
    // (Раніше тут був рекурсивний виклик кожні 20 мс — за довгий рендер у згорнутому вікні стек ріс.)
    g_idle_frame = !IsWindowVisible(g_hwnd) || IsIconic(g_hwnd) ||
                   (g_occluded && g_swapchain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED);
    if (g_idle_frame) Sleep(100);
    else g_occluded = false;
    if (g_resize_w && g_resize_h) {
        release_rtv();
        g_swapchain->ResizeBuffers(0, g_resize_w, g_resize_h, DXGI_FORMAT_UNKNOWN, 0);
        g_resize_w = g_resize_h = 0;
        create_rtv();
    }
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    return true;
}

void platform_end_frame(const float clear[4]) {
    if (g_idle_frame) return;   // нічого не видно — не малюємо
    g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
    g_context->ClearRenderTargetView(g_rtv, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    const HRESULT hr = g_swapchain->Present(1, 0);
    g_occluded = hr == DXGI_STATUS_OCCLUDED;
}

void platform_shutdown() {
    tray_remove();
    while (!g_textures.empty()) platform_destroy_texture(reinterpret_cast<uint64_t>(g_textures.back().srv));
    if (g_taskbar) {
        g_taskbar->Release();
        g_taskbar = nullptr;
    }
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    cleanup_device();
    if (g_hwnd && IsWindow(g_hwnd)) DestroyWindow(g_hwnd);
    UnregisterClassW(g_wc.lpszClassName, g_wc.hInstance);
    CoUninitialize();
}

float platform_dpi_scale() { return g_dpi; }
void platform_set_title(const std::string& title) { SetWindowTextW(g_hwnd, utf8_to_wide(title).c_str()); }

std::string open_file_dialog(const std::string& title, const std::vector<FileFilter>& filters, const std::string& initial) {
    return file_dialog(0, title, filters, initial, {});
}
std::string save_file_dialog(const std::string& title, const std::vector<FileFilter>& filters, const std::string& initial,
                             const std::string& ext) {
    return file_dialog(1, title, filters, initial, ext);
}
std::string pick_folder_dialog(const std::string& title, const std::string& initial) {
    return file_dialog(2, title, {}, initial, {});
}

void open_path(const std::string& path) {
    ShellExecuteW(nullptr, L"open", utf8_to_wide(path).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void show_in_folder(const std::string& path) {
    const std::wstring args = L"/select,\"" + utf8_to_wide(path) + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

std::vector<std::string> ui_font_candidates() {
    wchar_t windir[MAX_PATH] = L"C:\\Windows";
    GetWindowsDirectoryW(windir, MAX_PATH);
    const std::string w = wide_to_utf8(windir);
    return {w + "\\Fonts\\segoeui.ttf", w + "\\Fonts\\tahoma.ttf", w + "\\Fonts\\arial.ttf"};
}

std::vector<std::string> ui_bold_font_candidates() {
    wchar_t windir[MAX_PATH] = L"C:\\Windows";
    GetWindowsDirectoryW(windir, MAX_PATH);
    const std::string w = wide_to_utf8(windir);
    return {w + "\\Fonts\\seguisb.ttf", w + "\\Fonts\\segoeuib.ttf", w + "\\Fonts\\tahomabd.ttf", w + "\\Fonts\\arialbd.ttf"};
}

std::vector<std::string> ui_symbol_font_candidates() {
    wchar_t windir[MAX_PATH] = L"C:\\Windows";
    GetWindowsDirectoryW(windir, MAX_PATH);
    const std::string w = wide_to_utf8(windir);
    return {w + "\\Fonts\\seguisym.ttf", w + "\\Fonts\\segoeuisymbol.ttf"};
}

std::string clipboard_text_set(const std::string& text) {
    ImGui::SetClipboardText(text.c_str());
    return text;
}

bool platform_screenshot(const std::string& path) {
    if (!g_swapchain || !g_rtv || !ImGui::GetDrawData()) return false;
    // Кадр уже показано (Present, задній буфер після нього невизначений) — малюємо його ще раз і копіюємо
    const float clear[4] = {0, 0, 0, 1};
    g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
    g_context->ClearRenderTargetView(g_rtv, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    ID3D11Texture2D* back = nullptr;
    if (FAILED(g_swapchain->GetBuffer(0, IID_PPV_ARGS(&back)))) return false;
    D3D11_TEXTURE2D_DESC d{};
    back->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    d.MiscFlags = 0;
    ID3D11Texture2D* staging = nullptr;
    const bool ok = SUCCEEDED(g_device->CreateTexture2D(&d, nullptr, &staging));
    if (ok) g_context->CopyResource(staging, back);
    back->Release();
    if (!ok) return false;
    D3D11_MAPPED_SUBRESOURCE m{};
    bool written = false;
    if (SUCCEEDED(g_context->Map(staging, 0, D3D11_MAP_READ, 0, &m))) {
        // PPM (P6), як і в Linux-версії
        std::FILE* f = _wfopen(utf8_to_wide(path).c_str(), L"wb");
        if (f) {
            std::fprintf(f, "P6\n%u %u\n255\n", d.Width, d.Height);
            std::vector<uint8_t> row(static_cast<size_t>(d.Width) * 3);
            for (UINT y = 0; y < d.Height; ++y) {
                const uint8_t* src = static_cast<const uint8_t*>(m.pData) + static_cast<size_t>(y) * m.RowPitch;
                for (UINT x = 0; x < d.Width; ++x) std::memcpy(&row[x * 3], src + x * 4, 3);
                std::fwrite(row.data(), 1, row.size(), f);
            }
            written = std::fclose(f) == 0;
        }
        g_context->Unmap(staging, 0);
    }
    staging->Release();
    return written;
}

uint64_t platform_update_texture(uint64_t id, int w, int h, const uint8_t* rgba) {
    if (!g_device || w <= 0 || h <= 0) return 0;
    UserTexture* t = nullptr;
    for (auto& x : g_textures)
        if (reinterpret_cast<uint64_t>(x.srv) == id && id != 0) t = &x;
    if (t && (t->w != w || t->h != h)) {
        platform_destroy_texture(id);
        t = nullptr;
    }
    if (!t) {
        D3D11_TEXTURE2D_DESC d{};
        d.Width = static_cast<UINT>(w);
        d.Height = static_cast<UINT>(h);
        d.MipLevels = 1;
        d.ArraySize = 1;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DYNAMIC;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        UserTexture n;
        n.w = w;
        n.h = h;
        if (FAILED(g_device->CreateTexture2D(&d, nullptr, &n.tex))) return 0;
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = d.Format;
        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels = 1;
        if (FAILED(g_device->CreateShaderResourceView(n.tex, &sd, &n.srv))) {
            n.tex->Release();
            return 0;
        }
        g_textures.push_back(n);
        t = &g_textures.back();
    }
    D3D11_MAPPED_SUBRESOURCE m{};
    if (SUCCEEDED(g_context->Map(t->tex, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        for (int y = 0; y < h; ++y)
            memcpy(static_cast<uint8_t*>(m.pData) + static_cast<size_t>(y) * m.RowPitch, rgba + static_cast<size_t>(y) * w * 4,
                   static_cast<size_t>(w) * 4);
        g_context->Unmap(t->tex, 0);
    }
    return reinterpret_cast<uint64_t>(t->srv);
}

void platform_destroy_texture(uint64_t id) {
    for (auto it = g_textures.begin(); it != g_textures.end(); ++it) {
        if (reinterpret_cast<uint64_t>(it->srv) != id) continue;
        it->srv->Release();
        it->tex->Release();
        g_textures.erase(it);
        return;
    }
}

void platform_set_taskbar_progress(TaskbarState state, double fraction) {
    if (!g_taskbar_tried) {
        g_taskbar_tried = true;
        if (FAILED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_taskbar)))) g_taskbar = nullptr;
        else if (FAILED(g_taskbar->HrInit())) {
            g_taskbar->Release();
            g_taskbar = nullptr;
        }
    }
    if (!g_taskbar || !g_hwnd) return;
    TBPFLAG f = TBPF_NOPROGRESS;
    switch (state) {
    case TaskbarState::Normal: f = TBPF_NORMAL; break;
    case TaskbarState::Paused: f = TBPF_PAUSED; break;
    case TaskbarState::Error: f = TBPF_ERROR; break;
    case TaskbarState::Indeterminate: f = TBPF_INDETERMINATE; break;
    default: break;
    }
    g_taskbar->SetProgressState(g_hwnd, f);
    if (state == TaskbarState::Normal || state == TaskbarState::Paused || state == TaskbarState::Error) {
        const double fr = fraction < 0 ? 0 : fraction > 1 ? 1 : fraction;
        g_taskbar->SetProgressValue(g_hwnd, static_cast<ULONGLONG>(fr * 1000), 1000);
    }
}

void platform_tray(bool show, const std::string& tooltip) {
    if (!g_hwnd) return;
    if (!show) {
        if (!IsWindowVisible(g_hwnd)) return;   // вікно в треї — значок потрібен, щоб його повернути
        tray_remove();
        return;
    }
    if (!tray_add()) return;
    const std::wstring tip = utf8_to_wide(tooltip.empty() ? "GMod Demo Render" : tooltip);
    if (tip == g_tray.szTip) return;
    wcsncpy(g_tray.szTip, tip.c_str(), ARRAYSIZE(g_tray.szTip) - 1);
    g_tray.szTip[ARRAYSIZE(g_tray.szTip) - 1] = 0;
    g_tray.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_tray);
}

void platform_notify(const std::string& title, const std::string& text) {
    // Сповіщення показується від імені значка в треї; App прибере значок, коли він стане непотрібним
    if (!g_hwnd || !tray_add()) return;
    NOTIFYICONDATAW n = g_tray;
    n.uFlags = NIF_INFO;
    wcsncpy(n.szInfoTitle, utf8_to_wide(title).c_str(), ARRAYSIZE(n.szInfoTitle) - 1);
    wcsncpy(n.szInfo, utf8_to_wide(text).c_str(), ARRAYSIZE(n.szInfo) - 1);
    n.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON;
    n.hBalloonIcon = g_wc.hIcon;
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

void platform_set_minimize_to_tray(bool on) { g_minimize_to_tray = on; }

bool platform_window_hidden() { return g_hwnd && (!IsWindowVisible(g_hwnd) || IsIconic(g_hwnd)); }

void platform_restore_window() {
    if (g_hwnd) restore_from_tray();
}

void platform_flash_window() {
    if (!g_hwnd || GetForegroundWindow() == g_hwnd) return;
    FLASHWINFO fi{};
    fi.cbSize = sizeof(fi);
    fi.hwnd = g_hwnd;
    fi.dwFlags = FLASHW_TRAY | FLASHW_TIMERNOFG;
    fi.uCount = 3;
    FlashWindowEx(&fi);
}

} // namespace gmdr::gui

#endif // _WIN32
