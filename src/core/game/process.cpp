#include "process.hpp"

#include "../util/log.hpp"
#include "../util/strings.hpp"
#include "../util/i18n.hpp"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <format>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#else
#include <csignal>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace gmdr::game {

WindowMode window_mode_from_string(const std::string& s) {
    if (s == "behind") return WindowMode::Behind;
    if (s == "offscreen" || s == "hidden") return WindowMode::Offscreen;
    return WindowMode::Normal;
}

std::string format_command_line(const std::filesystem::path& exe, const std::vector<std::string>& args) {
    std::string s = "\"" + path_to_utf8(exe) + "\"";
    for (const auto& a : args) {
        if (a.find_first_of(" \t\"") != std::string::npos) s += " \"" + replace_all(a, "\"", "\\\"") + "\"";
        else s += " " + a;
    }
    return s;
}

#ifdef _WIN32
// ============================== Windows =========================================
namespace {
using NtProcFn = LONG(NTAPI*)(HANDLE);
NtProcFn nt_proc(const char* name) {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    return ntdll ? reinterpret_cast<NtProcFn>(reinterpret_cast<void*>(GetProcAddress(ntdll, name))) : nullptr;
}

std::wstring quote_arg(const std::wstring& a) {
    if (!a.empty() && a.find_first_of(L" \t\"") == std::wstring::npos) return a;
    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : a) {
        if (c == L'\\') { ++backslashes; continue; }
        if (c == L'"') { out.append(backslashes * 2 + 1, L'\\'); out += L'"'; backslashes = 0; continue; }
        out.append(backslashes, L'\\');
        backslashes = 0;
        out += c;
    }
    out.append(backslashes * 2, L'\\');
    out += L'"';
    return out;
}

// Призупинити/відновити всі потоки процесу (запасний спосіб)
bool for_each_thread(DWORD pid, bool suspend) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    bool any = false;
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid) continue;
            HANDLE th = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
            if (!th) continue;
            if (suspend) SuspendThread(th);
            else ResumeThread(th);
            CloseHandle(th);
            any = true;
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    return any;
}
} // namespace

GameProcess::~GameProcess() {
    if (suspended_) resume();
    if (handle_) CloseHandle(static_cast<HANDLE>(handle_));
}

std::unique_ptr<GameProcess> GameProcess::launch(const std::filesystem::path& exe, const std::vector<std::string>& args,
                                                 const std::filesystem::path& working_dir,
                                                 const std::vector<std::pair<std::string, std::string>>& env,
                                                 std::string* error, bool no_activate) {
    std::wstring cmd = quote_arg(exe.native());
    for (const auto& a : args) cmd += L" " + quote_arg(utf8_to_wide(a));

    // Середовище: поточне + наші змінні (SteamAppId тощо)
    std::wstring env_block;
    {
        wchar_t* cur = GetEnvironmentStringsW();
        for (wchar_t* p = cur; p && *p; p += wcslen(p) + 1) {
            std::wstring kv = p;
            bool overridden = false;
            for (const auto& [k, v] : env) {
                const std::wstring wk = utf8_to_wide(k) + L"=";
                if (_wcsnicmp(kv.c_str(), wk.c_str(), wk.size()) == 0) overridden = true;
            }
            if (!overridden) { env_block += kv; env_block.push_back(L'\0'); }
        }
        if (cur) FreeEnvironmentStringsW(cur);
        for (const auto& [k, v] : env) {
            env_block += utf8_to_wide(k) + L"=" + utf8_to_wide(v);
            env_block.push_back(L'\0');
        }
        env_block.push_back(L'\0');
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    if (no_activate) {
        // Перший ShowWindow гри використає це значення: вікно з'явиться без фокуса
        si.dwFlags |= STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_SHOWNOACTIVATE;
    }
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
    cmd_buf.push_back(L'\0');
    const BOOL ok = CreateProcessW(exe.c_str(), cmd_buf.data(), nullptr, nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT,
                                   env_block.data(), working_dir.empty() ? nullptr : working_dir.c_str(), &si, &pi);
    if (!ok) {
        if (error) *error = trf("не вдалося запустити гру (код помилки Windows {})", GetLastError());
        return nullptr;
    }
    CloseHandle(pi.hThread);
    auto p = std::unique_ptr<GameProcess>(new GameProcess());
    p->pid_ = pi.dwProcessId;
    p->handle_ = pi.hProcess;
    return p;
}

std::unique_ptr<GameProcess> GameProcess::attach(uint32_t pid, std::string* error) {
    HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SUSPEND_RESUME |
                               PROCESS_TERMINATE | PROCESS_SET_INFORMATION,
                           FALSE, pid);
    if (!h) {
        if (error) *error = trf("не вдалося підключитися до процесу {} (код {})", pid, GetLastError());
        return nullptr;
    }
    auto p = std::unique_ptr<GameProcess>(new GameProcess());
    p->pid_ = pid;
    p->handle_ = h;
    return p;
}

std::vector<uint32_t> GameProcess::find_by_name(const std::vector<std::string>& names, bool main_only) {
    std::vector<std::pair<uint32_t, uint32_t>> found;   // PID, PID батьківського процесу
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return {};
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            const std::string exe = wide_to_utf8(pe.szExeFile);
            for (const auto& n : names)
                if (iequals(exe, n)) found.push_back({pe.th32ProcessID, pe.th32ParentProcessID});
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    std::vector<uint32_t> out;
    for (const auto& [pid, parent] : found) {
        // Дочірній процес того самого exe (CEF) — не гра
        const bool child = std::any_of(found.begin(), found.end(), [&](const auto& f) { return f.first == parent; });
        if (!main_only || !child) out.push_back(pid);
    }
    return out;
}

bool GameProcess::wait_all_exited(const std::vector<std::string>& names, int timeout_ms) {
    const auto start = std::chrono::steady_clock::now();
    while (!find_by_name(names).empty()) {
        if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(timeout_ms)) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    return true;
}

bool GameProcess::running() {
    if (exited_ || !handle_) return false;
    DWORD code = 0;
    if (GetExitCodeProcess(static_cast<HANDLE>(handle_), &code) && code == STILL_ACTIVE) {
        // STILL_ACTIVE може бути і справжнім кодом — перевіряємо очікуванням
        if (WaitForSingleObject(static_cast<HANDLE>(handle_), 0) == WAIT_TIMEOUT) return true;
    }
    exited_ = true;
    exit_code_ = static_cast<int>(code);
    return false;
}

std::optional<int> GameProcess::exit_code() {
    if (running()) return std::nullopt;
    return exit_code_;
}

bool GameProcess::suspend() {
    if (suspended_ || !running()) return false;
    if (auto fn = nt_proc("NtSuspendProcess"); fn && fn(static_cast<HANDLE>(handle_)) >= 0) suspended_ = true;
    else suspended_ = for_each_thread(pid_, true);
    return suspended_;
}

bool GameProcess::resume() {
    if (!suspended_) return true;
    bool ok = false;
    if (auto fn = nt_proc("NtResumeProcess"); fn && fn(static_cast<HANDLE>(handle_)) >= 0) ok = true;
    else ok = for_each_thread(pid_, false);
    if (ok) suspended_ = false;
    return ok;
}

void GameProcess::terminate() {
    if (!handle_) return;
    if (suspended_) resume();
    TerminateProcess(static_cast<HANDLE>(handle_), 1);
    WaitForSingleObject(static_cast<HANDLE>(handle_), 5000);
}

bool GameProcess::wait(int timeout_ms) {
    if (!handle_) return true;
    const DWORD r = WaitForSingleObject(static_cast<HANDLE>(handle_), timeout_ms < 0 ? INFINITE : static_cast<DWORD>(timeout_ms));
    if (r == WAIT_OBJECT_0) {
        running();
        return true;
    }
    return false;
}

void GameProcess::set_high_priority(bool high) {
    if (handle_) SetPriorityClass(static_cast<HANDLE>(handle_), high ? ABOVE_NORMAL_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS);
}


bool GameProcess::disable_background_throttling() {
    if (!handle_) return false;
    // SetProcessInformation(ProcessPowerThrottling) — Windows 10 1709+; прапорець таймера — Windows 11.
    // Структуру і константи оголошуємо самі: старі заголовки (MinGW) їх можуть не мати.
    struct PowerThrottlingState {
        ULONG Version;
        ULONG ControlMask;
        ULONG StateMask;
    };
    constexpr ULONG kVersion = 1;
    constexpr ULONG kExecutionSpeed = 0x1;         // PROCESS_POWER_THROTTLING_EXECUTION_SPEED (EcoQoS)
    constexpr ULONG kIgnoreTimerResolution = 0x4;  // PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION
    constexpr int kProcessPowerThrottling = 4;     // PROCESS_INFORMATION_CLASS::ProcessPowerThrottling
    using SetInfoFn = BOOL(WINAPI*)(HANDLE, int, LPVOID, DWORD);
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    auto fn = k32 ? reinterpret_cast<SetInfoFn>(reinterpret_cast<void*>(GetProcAddress(k32, "SetProcessInformation")))
                  : nullptr;
    if (!fn) return false;
    // ControlMask — чим керуємо, StateMask = 0 — "вимкнено"
    PowerThrottlingState st{kVersion, kExecutionSpeed | kIgnoreTimerResolution, 0};
    if (fn(static_cast<HANDLE>(handle_), kProcessPowerThrottling, &st, sizeof(st))) return true;
    // Старіші Windows 10 не знають прапорця таймера — пробуємо лише EcoQoS
    st.ControlMask = kExecutionSpeed;
    return fn(static_cast<HANDLE>(handle_), kProcessPowerThrottling, &st, sizeof(st)) != 0;
}

namespace {
struct FindWindowCtx {
    DWORD pid;
    HWND  best;
    LONG  best_area;
};
BOOL CALLBACK find_window_cb(HWND hwnd, LPARAM lp) {
    auto* ctx = reinterpret_cast<FindWindowCtx*>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != ctx->pid || GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;
    wchar_t cls[64] = {};
    GetClassNameW(hwnd, cls, 64);
    RECT r{};
    GetWindowRect(hwnd, &r);
    const LONG area = (r.right - r.left) * (r.bottom - r.top);
    // Вікно рушія Source має клас "Valve001"; інакше беремо найбільше видиме
    if (wcscmp(cls, L"Valve001") == 0) {
        ctx->best = hwnd;
        ctx->best_area = LONG_MAX;
        return FALSE;
    }
    if (IsWindowVisible(hwnd) && area > ctx->best_area) {
        ctx->best = hwnd;
        ctx->best_area = area;
    }
    return TRUE;
}
} // namespace

uintptr_t GameProcess::find_main_window() const {
    FindWindowCtx ctx{pid_, nullptr, 0};
    EnumWindows(find_window_cb, reinterpret_cast<LPARAM>(&ctx));
    return reinterpret_cast<uintptr_t>(ctx.best);
}

std::pair<int, int> GameProcess::offscreen_position() {
    // Праворуч від правого краю всіх моніторів, з запасом
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    return {vx + vw + 200, vy};
}

// Вікно чужого процесу — лише асинхронно (SWP_ASYNCWINDOWPOS): інакше SetWindowPos чекає, поки
// потік гри обробить повідомлення, а призупинена (suspend) чи зависла гра не відповість ніколи.
bool GameProcess::place_window(WindowMode mode) {
    HWND hwnd = reinterpret_cast<HWND>(find_main_window());
    if (!hwnd) return false;
    switch (mode) {
    case WindowMode::Normal:
        return true;
    case WindowMode::Behind:
        SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
        return true;
    case WindowMode::Offscreen: {
        const auto [x, y] = offscreen_position();
        SetWindowPos(hwnd, HWND_BOTTOM, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
        return true;
    }
    }
    return false;
}

bool GameProcess::show_window_front() {
    HWND hwnd = reinterpret_cast<HWND>(find_main_window());
    if (!hwnd) return false;
    RECT r{};
    GetWindowRect(hwnd, &r);
    // Повертаємо на основний монітор (у лівий верхній кут робочої області)
    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    const bool visible = r.right > GetSystemMetrics(SM_XVIRTUALSCREEN) &&
                         r.left < GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    SetWindowPos(hwnd, HWND_TOP, visible ? r.left : wa.left, visible ? r.top : wa.top, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_ASYNCWINDOWPOS);
    return true;
}

#else
// =============================== POSIX ==========================================
GameProcess::~GameProcess() {
    if (suspended_) resume();
}

std::unique_ptr<GameProcess> GameProcess::launch(const std::filesystem::path& exe, const std::vector<std::string>& args,
                                                 const std::filesystem::path& working_dir,
                                                 const std::vector<std::pair<std::string, std::string>>& env,
                                                 std::string* error, bool /*no_activate*/) {
    std::vector<std::string> argv_s;
    argv_s.push_back(exe.string());
    for (const auto& a : args) argv_s.push_back(a);
    std::vector<char*> argv;
    for (auto& s : argv_s) argv.push_back(s.data());
    argv.push_back(nullptr);

    std::vector<std::string> env_s;
    for (char** e = environ; e && *e; ++e) {
        std::string kv = *e;
        bool overridden = false;
        for (const auto& [k, v] : env)
            if (kv.rfind(k + "=", 0) == 0) overridden = true;
        if (!overridden) env_s.push_back(kv);
    }
    for (const auto& [k, v] : env) env_s.push_back(k + "=" + v);
    std::vector<char*> envp;
    for (auto& s : env_s) envp.push_back(s.data());
    envp.push_back(nullptr);

    const pid_t pid = fork();
    if (pid < 0) {
        if (error) *error = std::string("fork: ") + std::strerror(errno);
        return nullptr;
    }
    if (pid == 0) {
        if (!working_dir.empty() && chdir(working_dir.c_str()) != 0) _exit(127);
        setpgid(0, 0);
        execve(argv[0], argv.data(), envp.data());
        _exit(127);
    }
    auto p = std::unique_ptr<GameProcess>(new GameProcess());
    p->pid_ = static_cast<uint32_t>(pid);
    return p;
}

std::unique_ptr<GameProcess> GameProcess::attach(uint32_t pid, std::string* error) {
    if (kill(static_cast<pid_t>(pid), 0) != 0) {
        if (error) *error = tr("процес не знайдено");
        return nullptr;
    }
    auto p = std::unique_ptr<GameProcess>(new GameProcess());
    p->pid_ = pid;
    return p;
}

std::vector<uint32_t> GameProcess::find_by_name(const std::vector<std::string>& names, bool /*main_only*/) {
    std::vector<uint32_t> out;
    DIR* d = opendir("/proc");
    if (!d) return out;
    while (dirent* e = readdir(d)) {
        char* end = nullptr;
        const long pid = std::strtol(e->d_name, &end, 10);
        if (!end || *end || pid <= 0) continue;
        char buf[512] = {};
        const std::string link = std::format("/proc/{}/exe", pid);
        const ssize_t n = readlink(link.c_str(), buf, sizeof(buf) - 1);
        if (n <= 0) continue;
        const std::string exe = std::filesystem::path(std::string(buf, static_cast<size_t>(n))).filename().string();
        for (const auto& nm : names)
            if (exe == nm) out.push_back(static_cast<uint32_t>(pid));
    }
    closedir(d);
    return out;
}

bool GameProcess::running() {
    if (exited_) return false;
    int status = 0;
    const pid_t r = waitpid(static_cast<pid_t>(pid_), &status, WNOHANG);
    if (r == 0) return true;
    if (r < 0) {
        // не наш дочірній процес (attach) — перевіряємо існування
        if (errno == ECHILD) {
            if (kill(static_cast<pid_t>(pid_), 0) == 0) return true;
        }
        exited_ = true;
        return false;
    }
    exited_ = true;
    exit_code_ = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return false;
}

std::optional<int> GameProcess::exit_code() {
    if (running()) return std::nullopt;
    return exit_code_;
}

bool GameProcess::suspend() {
    if (suspended_ || !running()) return false;
    if (kill(-static_cast<pid_t>(pid_), SIGSTOP) == 0 || kill(static_cast<pid_t>(pid_), SIGSTOP) == 0) suspended_ = true;
    return suspended_;
}

bool GameProcess::resume() {
    if (!suspended_) return true;
    kill(-static_cast<pid_t>(pid_), SIGCONT);
    kill(static_cast<pid_t>(pid_), SIGCONT);
    suspended_ = false;
    return true;
}

void GameProcess::terminate() {
    if (suspended_) resume();
    if (!running()) return;
    kill(static_cast<pid_t>(pid_), SIGTERM);
    for (int i = 0; i < 50 && running(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (running()) kill(static_cast<pid_t>(pid_), SIGKILL);
    running();
}

bool GameProcess::wait(int timeout_ms) {
    const auto start = std::chrono::steady_clock::now();
    while (running()) {
        if (timeout_ms >= 0 && std::chrono::steady_clock::now() - start > std::chrono::milliseconds(timeout_ms)) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return true;
}

void GameProcess::set_high_priority(bool) {}
bool GameProcess::wait_all_exited(const std::vector<std::string>& names, int timeout_ms) {
    const auto start = std::chrono::steady_clock::now();
    while (!find_by_name(names).empty()) {
        if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(timeout_ms)) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    return true;
}
bool GameProcess::disable_background_throttling() { return false; }
uintptr_t GameProcess::find_main_window() const { return 0; }
bool GameProcess::place_window(WindowMode) { return false; }
bool GameProcess::show_window_front() { return false; }
std::pair<int, int> GameProcess::offscreen_position() { return {0, 0}; }
#endif

} // namespace gmdr::game
