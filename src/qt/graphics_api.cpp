#include "graphics_api.hpp"

#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QtGlobal>
#include <QtGui/qtgui-config.h>

#if QT_CONFIG(opengl)
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#endif
#if QT_CONFIG(vulkan)
#include <QVulkanFunctions>
#include <QVulkanInstance>
#endif

#include "core/util/file_util.hpp"
#include "core/util/i18n.hpp"
#include "core/util/log.hpp"
#include "core/util/strings.hpp"

#ifdef _WIN32
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi.h>
#endif

#include <filesystem>
#include <format>
#include <fstream>

namespace gmdr::qt {

namespace fs = std::filesystem;
using config::Availability;
using config::GraphicsApiInfo;

namespace {

// «Автоматично» і програмне малювання є завжди; решта — «невідомо», поки не перевірено
GraphicsApiInfo make(const char* id) {
    const std::string s = id;
    return {s, graphics_api_label(s), s == "auto" || s == "software" ? Availability::Available : Availability::Unknown, {}};
}

#ifdef _WIN32
// Direct3D 11: створити пристрій на відеокарті (як це зробить Qt), одразу відпустити
bool probe_d3d11(std::string* detail) {
    HMODULE dll = LoadLibraryW(L"d3d11.dll");
    if (!dll) {
        *detail = gmdr::tr("немає d3d11.dll");
        return false;
    }
    using Fn = HRESULT(WINAPI*)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT, UINT,
                                ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
    auto create = reinterpret_cast<Fn>(GetProcAddress(dll, "D3D11CreateDevice"));
    bool ok = false;
    if (create) {
        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                            D3D_FEATURE_LEVEL_10_0};
        ID3D11Device* dev = nullptr;
        D3D_FEATURE_LEVEL got{};
        const HRESULT hr = create(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 4, D3D11_SDK_VERSION, &dev, &got,
                                  nullptr);
        ok = SUCCEEDED(hr) && dev;
        if (dev) dev->Release();
        if (!ok) *detail = std::format("D3D11CreateDevice: 0x{:08X}", static_cast<unsigned>(hr));
        else *detail = std::format("feature level {}.{}", (got >> 12) & 0xF, (got >> 8) & 0xF);
    }
    FreeLibrary(dll);
    return ok;
}

// Direct3D 12: перевірка без створення пристрою (ppDevice = nullptr повертає S_FALSE, якщо можна)
bool probe_d3d12(std::string* detail) {
    HMODULE dll = LoadLibraryW(L"d3d12.dll");
    if (!dll) {
        *detail = gmdr::tr("немає d3d12.dll");
        return false;
    }
    using Fn = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
    auto create = reinterpret_cast<Fn>(GetProcAddress(dll, "D3D12CreateDevice"));
    bool ok = false;
    if (create) {
        const HRESULT hr = create(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr);
        ok = SUCCEEDED(hr);
        if (!ok) *detail = std::format("D3D12CreateDevice: 0x{:08X}", static_cast<unsigned>(hr));
    }
    FreeLibrary(dll);
    return ok;
}
#endif

#if QT_CONFIG(vulkan)
bool probe_vulkan(std::string* detail) {
    QVulkanInstance inst;
    if (!inst.create()) {
        *detail = gmdr::tr("не вдалося створити екземпляр Vulkan (немає драйвера чи vulkan-1)");
        return false;
    }
    uint32_t count = 0;
    inst.functions()->vkEnumeratePhysicalDevices(inst.vkInstance(), &count, nullptr);
    if (count == 0) {
        *detail = gmdr::tr("Vulkan є, але відеокарт з ним немає");
        return false;
    }
    *detail = trf("відеокарт: {}", count);
    return true;
}
#endif

#if QT_CONFIG(opengl)
bool probe_opengl(std::string* detail) {
    QOpenGLContext ctx;
    if (!ctx.create()) {
        *detail = gmdr::tr("не вдалося створити контекст OpenGL");
        return false;
    }
    QOffscreenSurface surface;
    surface.setFormat(ctx.format());
    surface.create();
    if (!ctx.makeCurrent(&surface)) {
        *detail = gmdr::tr("контекст OpenGL не активується");
        return false;
    }
    const auto* ver = reinterpret_cast<const char*>(ctx.functions()->glGetString(GL_VERSION));
    const auto* renderer = reinterpret_cast<const char*>(ctx.functions()->glGetString(GL_RENDERER));
    *detail = std::string(ver ? ver : "") + (renderer ? std::string(" · ") + renderer : std::string());
    ctx.doneCurrent();
    return true;
}
#endif

fs::path pending_path() { return app_data_dir() / "gmdr_graphics_pending.txt"; }

} // namespace

std::string graphics_api_label(const std::string& id) {
    if (id == "auto") return gmdr::tr("Автоматично");
    if (id == "d3d11") return "Direct3D 11";
    if (id == "d3d12") return "Direct3D 12";
    if (id == "vulkan") return "Vulkan";
    if (id == "opengl") return "OpenGL";
    if (id == "metal") return "Metal";
    if (id == "software") return gmdr::tr("Програмне малювання (без відеокарти)");
    return id;
}

std::vector<GraphicsApiInfo> platform_graphics_apis() {
    std::vector<GraphicsApiInfo> out;
    out.push_back(make("auto"));
#ifdef _WIN32
    out.push_back(make("d3d11"));
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    out.push_back(make("d3d12"));
#endif
#endif
#ifdef __APPLE__
    out.push_back(make("metal"));
#endif
#if QT_CONFIG(vulkan)
    out.push_back(make("vulkan"));
#endif
#if QT_CONFIG(opengl)
    out.push_back(make("opengl"));
#endif
    out.push_back(make("software"));
    return out;
}

std::vector<GraphicsApiInfo> probe_graphics_apis() {
    auto list = platform_graphics_apis();
    for (auto& a : list) {
        std::string detail;
        bool ok = false;
        if (a.id == "auto" || a.id == "software") ok = true;
#ifdef _WIN32
        else if (a.id == "d3d11") ok = probe_d3d11(&detail);
        else if (a.id == "d3d12") ok = probe_d3d12(&detail);
#endif
#if QT_CONFIG(vulkan)
        else if (a.id == "vulkan") ok = probe_vulkan(&detail);
#endif
#if QT_CONFIG(opengl)
        else if (a.id == "opengl") ok = probe_opengl(&detail);
#endif
        else if (a.id == "metal") ok = true;   // macOS: Metal є завжди
        a.state = ok ? Availability::Available : Availability::Unavailable;
        a.detail = detail;
    }
    return list;
}

std::string apply_graphics_api(const std::string& id) {
    using Api = QSGRendererInterface::GraphicsApi;
    Api api = Api::Unknown;
#ifdef _WIN32
    if (id == "d3d11") api = Api::Direct3D11;
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    if (id == "d3d12") api = Api::Direct3D12;
#endif
#endif
#ifdef __APPLE__
    if (id == "metal") api = Api::Metal;
#endif
#if QT_CONFIG(vulkan)
    if (id == "vulkan") api = Api::Vulkan;
#endif
#if QT_CONFIG(opengl)
    if (id == "opengl") api = Api::OpenGL;
#endif
    if (id == "software") api = Api::Software;
    if (api == Api::Unknown) return "auto";   // Qt вибере сам (Windows — Direct3D 11)
    QQuickWindow::setGraphicsApi(api);
    return id;
}

std::string active_graphics_api(QQuickWindow* window) {
    if (!window || !window->rendererInterface()) return {};
    using Api = QSGRendererInterface::GraphicsApi;
    switch (window->rendererInterface()->graphicsApi()) {
    case Api::Direct3D11: return "d3d11";
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    case Api::Direct3D12: return "d3d12";
#endif
    case Api::Vulkan: return "vulkan";
    case Api::OpenGL: return "opengl";
    case Api::Metal: return "metal";
    case Api::Software: return "software";
    default: return {};
    }
}

std::string take_failed_graphics_api() {
    std::ifstream f(pending_path());
    std::string id;
    if (f) std::getline(f, id);
    f.close();
    std::error_code ec;
    fs::remove(pending_path(), ec);
    return trim(id);
}

void mark_graphics_pending(const std::string& id) {
    if (id == "auto") return;   // «Автоматично» Qt вибирає сам — повертатися нікуди
    std::ofstream(pending_path()) << id << "\n";
}

void mark_graphics_ok() {
    std::error_code ec;
    fs::remove(pending_path(), ec);
}

} // namespace gmdr::qt
