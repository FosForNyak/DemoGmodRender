#include "system_info.hpp"

#include "file_util.hpp"
#include "i18n.hpp"
#include "strings.hpp"

#include <cstdlib>
#include <filesystem>
#include <format>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dxgi.h>
#else
#include <sys/utsname.h>
#include <unistd.h>
#endif

namespace gmdr {

namespace fs = std::filesystem;

std::string gpu_vendor_name(uint32_t id) {
    switch (id) {
    case 0x10DE: return "nvidia";
    case 0x1002: case 0x1022: return "amd";
    case 0x8086: return "intel";
    case 0x1414: return "microsoft";
    default: return {};
    }
}

#ifdef _WIN32
std::string os_id() { return "windows"; }

std::string os_description() {
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    OSVERSIONINFOW v{sizeof(v)};
    if (HMODULE nt = GetModuleHandleW(L"ntdll.dll"))
        if (auto fn = reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(nt, "RtlGetVersion"))))
            fn(&v);
    // Windows 11 теж повідомляє 10.0 — відрізняється номером збірки (22000+)
    const char* name = v.dwMajorVersion == 10 && v.dwBuildNumber >= 22000 ? "Windows 11" : "Windows";
    return trf("{} {}.{} (збірка {})", name, v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber);
}

std::string cpu_name() {
    wchar_t buf[256] = {};
    DWORD size = sizeof(buf);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString",
                     RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS)
        return "?";
    return trim(path_to_utf8(fs::path(buf)));
}

uint64_t total_memory() {
    MEMORYSTATUSEX m{sizeof(m)};
    return GlobalMemoryStatusEx(&m) ? m.ullTotalPhys : 0;
}

uint64_t available_memory() {
    MEMORYSTATUSEX m{sizeof(m)};
    return GlobalMemoryStatusEx(&m) ? m.ullAvailPhys : 0;
}

std::vector<SystemGpu> system_gpus(bool* ok) {
    std::vector<SystemGpu> out;
    // DXGI вантажиться лише тут: програма без відеокарти (сервер) теж запуститься
    HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
    using CreateFn = HRESULT(WINAPI*)(REFIID, void**);
    auto create = dxgi ? reinterpret_cast<CreateFn>(reinterpret_cast<void*>(GetProcAddress(dxgi, "CreateDXGIFactory1"))) : nullptr;
    IDXGIFactory1* factory = nullptr;
    if (!create || FAILED(create(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))) || !factory) {
        if (ok) *ok = false;
        return out;
    }
    IDXGIAdapter1* a = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 d{};
        if (SUCCEEDED(a->GetDesc1(&d))) {
            SystemGpu g;
            g.name = trim(path_to_utf8(fs::path(d.Description)));
            g.vendor_id = d.VendorId;
            g.vram_bytes = d.DedicatedVideoMemory;
            g.software = (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
            LARGE_INTEGER ver{};
            if (SUCCEEDED(a->CheckInterfaceSupport(__uuidof(IDXGIDevice), &ver)))
                g.driver = std::format("{}.{}.{}.{}", HIWORD(ver.HighPart), LOWORD(ver.HighPart), HIWORD(ver.LowPart),
                                       LOWORD(ver.LowPart));
            out.push_back(std::move(g));
        }
        a->Release();
    }
    factory->Release();
    if (ok) *ok = true;
    return out;
}
#else
std::string os_id() {
#ifdef __APPLE__
    return "macos";
#else
    return "linux";
#endif
}

std::string os_description() {
    utsname u{};
    if (uname(&u) != 0) return "?";
    return std::format("{} {} ({})", u.sysname, u.release, u.machine);
}

std::string cpu_name() {
    auto text = read_file_text("/proc/cpuinfo");
    if (!text) return "?";
    for (const auto& line : split(*text, '\n'))
        if (starts_with_i(line, "model name")) return trim(line.substr(line.find(':') + 1));
    return "?";
}

uint64_t total_memory() {
    const long pages = sysconf(_SC_PHYS_PAGES), page = sysconf(_SC_PAGE_SIZE);
    return pages > 0 && page > 0 ? static_cast<uint64_t>(pages) * static_cast<uint64_t>(page) : 0;
}

uint64_t available_memory() {
    if (auto text = read_file_text("/proc/meminfo"))
        for (const auto& line : split(*text, '\n'))
            if (starts_with_i(line, "MemAvailable:"))
                if (auto kb = parse_int(trim(line.substr(13, line.find("kB") - 13)))) return static_cast<uint64_t>(*kb) * 1024;
    return 0;
}

// /sys/class/drm/cardN/device: vendor (0x10de), модуль ядра (driver → nvidia/amdgpu/i915),
// пам'ять — лише в amdgpu (mem_info_vram_total). Назви моделі ядро не дає — лише ідентифікатори.
std::vector<SystemGpu> system_gpus(bool* ok) {
    std::vector<SystemGpu> out;
    std::error_code ec;
    const fs::path drm = "/sys/class/drm";
    if (!fs::exists(drm, ec)) {
        if (ok) *ok = false;
        return out;
    }
    for (fs::directory_iterator it(drm, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string n = path_to_utf8(it->path().filename());
        if (n.rfind("card", 0) != 0 || n.find('-') != std::string::npos) continue;
        const fs::path dev = it->path() / "device";
        auto vendor = read_file_text(dev / "vendor");
        auto device = read_file_text(dev / "device");
        if (!vendor) continue;
        SystemGpu g;
        g.vendor_id = static_cast<uint32_t>(std::strtoul(trim(*vendor).c_str(), nullptr, 16));
        std::error_code lec;
        const fs::path drv = fs::read_symlink(dev / "driver", lec);
        if (!lec) g.driver = path_to_utf8(drv.filename());
        if (auto v = read_file_text(dev / "mem_info_vram_total")) g.vram_bytes = std::strtoull(trim(*v).c_str(), nullptr, 10);
        // Назва — ідентифікатори PCI (виробник:модель), як їх дає ядро
        const std::string vn = gpu_vendor_name(g.vendor_id);
        const std::string vendor_label = vn == "nvidia" ? "NVIDIA" : vn == "amd" ? "AMD" : vn == "intel" ? "Intel" : "GPU";
        std::string dev_id = device ? trim(*device) : std::string();
        if (dev_id.rfind("0x", 0) == 0) dev_id = dev_id.substr(2);
        g.name = std::format("{} [{:04x}:{}]", vendor_label, g.vendor_id, dev_id);
        out.push_back(std::move(g));
    }
    if (ok) *ok = true;
    return out;
}
#endif

} // namespace gmdr
