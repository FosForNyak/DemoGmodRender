#include "core/process.h"

#include "core/text.h"

#include <string>
#include <utility>

#ifdef _WIN32
#include <userenv.h>
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

// Built with AddressSanitizer (GCC defines __SANITIZE_ADDRESS__, Clang reports it through __has_feature).
#if defined(__SANITIZE_ADDRESS__)
#define GMDR_UNDER_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define GMDR_UNDER_ASAN 1
#endif
#endif
#ifndef GMDR_UNDER_ASAN
#define GMDR_UNDER_ASAN 0
#endif

namespace gmdr {

#ifdef _WIN32

namespace {

std::wstring widen(const std::string& utf8) {
    if (utf8.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), w.data(), n);
    return w;
}

// Quotes one argument following the CommandLineToArgvW rules.
void appendQuoted(std::wstring& cmd, const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        cmd += arg;
        return;
    }
    cmd.push_back(L'"');
    for (auto it = arg.begin();; ++it) {
        std::size_t backslashes = 0;
        while (it != arg.end() && *it == L'\\') {
            ++it;
            ++backslashes;
        }
        if (it == arg.end()) {
            cmd.append(backslashes * 2, L'\\');
            break;
        }
        if (*it == L'"') {
            cmd.append(backslashes * 2 + 1, L'\\');
            cmd.push_back(*it);
        } else {
            cmd.append(backslashes, L'\\');
            cmd.push_back(*it);
        }
    }
    cmd.push_back(L'"');
}

Error winError(const char* code, const char* what) {
    return makeError(code, what, "win32 error " + std::to_string(GetLastError()));
}

} // namespace

struct ChildProcess::Impl {
    HANDLE process = nullptr;
    HANDLE job = nullptr;
    HANDLE stdoutRead = nullptr;
    std::string buffer;
    bool eof = false;
    bool appContainer = false;
    std::string sandboxNote;

    ~Impl() {
        if (stdoutRead)
            CloseHandle(stdoutRead);
        if (process)
            CloseHandle(process);
        if (job)
            CloseHandle(job); // KILL_ON_JOB_CLOSE terminates the child if it is still running
    }
};

Result<ChildProcess> ChildProcess::spawn(const ProcessOptions& options) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE readEnd = nullptr, writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &sa, 0))
        return winError("core.process_pipe", "CreatePipe failed");
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

    std::vector<HANDLE> inherit{writeEnd};
    for (auto h : options.inheritHandles) {
        HANDLE hh = reinterpret_cast<HANDLE>(h);
        SetHandleInformation(hh, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
        inherit.push_back(hh);
    }

    const std::wstring exe = options.executable.wstring();
    std::wstring cmd;
    appendQuoted(cmd, exe);
    for (const auto& a : options.args) {
        cmd.push_back(L' ');
        appendQuoted(cmd, widen(a));
    }
    // The child starts in its own folder: an AppContainer cannot use the parent's working directory.
    const std::wstring cwd = options.executable.parent_path().wstring();

    // One CreateProcess attempt, with or without the AppContainer. Returns the Win32 error (0 = started).
    auto create = [&](PSID appContainerSid, PROCESS_INFORMATION& pi) -> DWORD {
        const DWORD attrCount = appContainerSid ? 2 : 1;
        SIZE_T attrSize = 0;
        InitializeProcThreadAttributeList(nullptr, attrCount, 0, &attrSize);
        std::vector<unsigned char> attrBuf(attrSize);
        auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
        if (!InitializeProcThreadAttributeList(attrs, attrCount, 0, &attrSize))
            return GetLastError();
        SECURITY_CAPABILITIES caps{};
        caps.AppContainerSid = appContainerSid;
        bool ok = UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit.data(),
                                            inherit.size() * sizeof(HANDLE), nullptr, nullptr) != 0;
        if (ok && appContainerSid)
            ok = UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES, &caps,
                                           sizeof(caps), nullptr, nullptr) != 0;
        DWORD err = ok ? 0 : GetLastError();
        if (ok) {
            STARTUPINFOEXW si{};
            si.StartupInfo.cb = sizeof(si);
            si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
            si.StartupInfo.hStdInput = nullptr;
            si.StartupInfo.hStdOutput = writeEnd;
            si.StartupInfo.hStdError = nullptr;
            si.lpAttributeList = attrs;
            std::wstring cmdCopy = cmd;
            if (!CreateProcessW(exe.c_str(), cmdCopy.data(), nullptr, nullptr, TRUE,
                                CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, nullptr,
                                cwd.empty() ? nullptr : cwd.c_str(), &si.StartupInfo, &pi))
                err = GetLastError();
        }
        DeleteProcThreadAttributeList(attrs);
        return err;
    };

    PROCESS_INFORMATION pi{};
    bool inAppContainer = false;
    std::string sandboxNote;
    DWORD err = 0;
    bool started = false;
    if (!options.appContainer.empty()) {
        const std::wstring name = widen(options.appContainer);
        PSID sid = nullptr;
        HRESULT hr = CreateAppContainerProfile(name.c_str(), name.c_str(), L"DemoGmodRender demo importer",
                                               nullptr, 0, &sid);
        if (hr == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS))
            hr = DeriveAppContainerSidFromAppContainerName(name.c_str(), &sid);
        if (FAILED(hr) || !sid) {
            sandboxNote =
                "AppContainer profile unavailable (HRESULT " + std::to_string(static_cast<long>(hr)) + ")";
        } else {
            err = create(sid, pi);
            FreeSid(sid);
            if (err == 0) {
                started = inAppContainer = true;
            } else {
                sandboxNote = "AppContainer start failed (win32 error " + std::to_string(err) + ")";
                if (err == ERROR_ACCESS_DENIED)
                    sandboxNote += ": the program folder is not readable by AppContainer apps "
                                   "(scripts/allow-appcontainer.cmd)";
            }
        }
        if (!started && !options.appContainerFallback) {
            CloseHandle(writeEnd);
            CloseHandle(readEnd);
            for (auto h : options.inheritHandles)
                SetHandleInformation(reinterpret_cast<HANDLE>(h), HANDLE_FLAG_INHERIT, 0);
            return makeError("core.process_appcontainer", "cannot start the process in its AppContainer",
                             sandboxNote);
        }
    }
    if (!started) {
        err = create(nullptr, pi);
        started = err == 0;
    }
    CloseHandle(writeEnd);
    for (auto h : options.inheritHandles)
        SetHandleInformation(reinterpret_cast<HANDLE>(h), HANDLE_FLAG_INHERIT, 0);
    if (!started) {
        CloseHandle(readEnd);
        return makeError("core.process_create", "CreateProcess failed", "win32 error " + std::to_string(err));
    }

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
                                              JOB_OBJECT_LIMIT_ACTIVE_PROCESS |
                                              JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
    limits.BasicLimitInformation.ActiveProcessLimit = 1;
    if (options.memoryLimitBytes) {
        limits.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_MEMORY;
        limits.ProcessMemoryLimit = static_cast<SIZE_T>(options.memoryLimitBytes);
    }
    JOBOBJECT_BASIC_UI_RESTRICTIONS ui{};
    ui.UIRestrictionsClass = JOB_OBJECT_UILIMIT_DESKTOP | JOB_OBJECT_UILIMIT_DISPLAYSETTINGS |
                             JOB_OBJECT_UILIMIT_EXITWINDOWS | JOB_OBJECT_UILIMIT_GLOBALATOMS |
                             JOB_OBJECT_UILIMIT_HANDLES | JOB_OBJECT_UILIMIT_READCLIPBOARD |
                             JOB_OBJECT_UILIMIT_SYSTEMPARAMETERS | JOB_OBJECT_UILIMIT_WRITECLIPBOARD;
    if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) ||
        !SetInformationJobObject(job, JobObjectBasicUIRestrictions, &ui, sizeof(ui)) ||
        !AssignProcessToJobObject(job, pi.hProcess)) {
        Error e = winError("core.process_job", "cannot place the child process in a job object");
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        CloseHandle(readEnd);
        if (job)
            CloseHandle(job);
        return e;
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    ChildProcess child;
    child.impl_->process = pi.hProcess;
    child.impl_->job = job;
    child.impl_->stdoutRead = readEnd;
    child.impl_->appContainer = inAppContainer;
    child.impl_->sandboxNote = std::move(sandboxNote);
    return child;
}

bool ChildProcess::readLine(std::string& line) {
    auto& im = *impl_;
    while (true) {
        const auto nl = im.buffer.find('\n');
        if (nl != std::string::npos) {
            line.assign(im.buffer, 0, nl);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            im.buffer.erase(0, nl + 1);
            return true;
        }
        if (im.eof) {
            if (im.buffer.empty())
                return false;
            line = std::move(im.buffer);
            im.buffer.clear();
            return true;
        }
        char chunk[4096];
        DWORD got = 0;
        if (!ReadFile(im.stdoutRead, chunk, sizeof(chunk), &got, nullptr) || got == 0) {
            im.eof = true;
            continue;
        }
        im.buffer.append(chunk, got);
        if (im.buffer.size() > (8u << 20)) { // a runaway child cannot exhaust our memory
            im.eof = true;
            im.buffer.clear();
        }
    }
}

void ChildProcess::kill() {
    if (impl_ && impl_->job)
        TerminateJobObject(impl_->job, 1);
}

int ChildProcess::wait() {
    if (!impl_ || !impl_->process)
        return -1;
    WaitForSingleObject(impl_->process, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(impl_->process, &code);
    return static_cast<int>(code);
}

std::filesystem::path currentExecutablePath() {
    std::wstring buf(1024, L'\0');
    while (true) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n < buf.size()) {
            buf.resize(n);
            return std::filesystem::path(buf);
        }
        buf.resize(buf.size() * 2);
    }
}

#else // POSIX

struct ChildProcess::Impl {
    pid_t pid = -1;
    int stdoutRead = -1;
    std::string buffer;
    bool eof = false;
    bool reaped = false;
    int exitCode = -1;
    bool appContainer = false;
    std::string sandboxNote;

    ~Impl() {
        if (stdoutRead >= 0)
            ::close(stdoutRead);
        if (pid > 0 && !reaped) {
            ::kill(pid, SIGKILL);
            ::waitpid(pid, nullptr, 0);
        }
    }
};

Result<ChildProcess> ChildProcess::spawn(const ProcessOptions& options) {
    int fds[2];
    if (::pipe2(fds, O_CLOEXEC) != 0)
        return makeError("core.process_pipe", "pipe failed", std::strerror(errno));

    const std::string exe = pathToUtf8(options.executable);
    std::vector<std::string> argStore{exe};
    for (const auto& a : options.args)
        argStore.push_back(a);
    std::vector<char*> argv;
    for (auto& a : argStore)
        argv.push_back(a.data());
    argv.push_back(nullptr);

    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(fds[0]);
        ::close(fds[1]);
        return makeError("core.process_create", "fork failed", std::strerror(errno));
    }
    if (pid == 0) {
        // Child: only async-signal-safe calls until exec.
        ::dup2(fds[1], 1);
        for (auto h : options.inheritHandles) {
            const int fd = static_cast<int>(h);
            const int flags = ::fcntl(fd, F_GETFD);
            ::fcntl(fd, F_SETFD, flags & ~FD_CLOEXEC);
        }
#if !GMDR_UNDER_ASAN
        // ASan reserves terabytes of address space for its shadow memory and cannot start under an
        // RLIMIT_AS; sanitizer builds (CI only) run the child without the cap.
        if (options.memoryLimitBytes) {
            struct rlimit rl{};
            rl.rlim_cur = rl.rlim_max = static_cast<rlim_t>(options.memoryLimitBytes);
            ::setrlimit(RLIMIT_AS, &rl);
        }
#endif
        ::execv(exe.c_str(), argv.data());
        ::_exit(127);
    }
    ::close(fds[1]);
    ChildProcess child;
    child.impl_->pid = pid;
    child.impl_->stdoutRead = fds[0];
    return child;
}

bool ChildProcess::readLine(std::string& line) {
    auto& im = *impl_;
    while (true) {
        const auto nl = im.buffer.find('\n');
        if (nl != std::string::npos) {
            line.assign(im.buffer, 0, nl);
            im.buffer.erase(0, nl + 1);
            return true;
        }
        if (im.eof) {
            if (im.buffer.empty())
                return false;
            line = std::move(im.buffer);
            im.buffer.clear();
            return true;
        }
        char chunk[4096];
        const ssize_t got = ::read(im.stdoutRead, chunk, sizeof(chunk));
        if (got < 0 && errno == EINTR)
            continue;
        if (got <= 0) {
            im.eof = true;
            continue;
        }
        im.buffer.append(chunk, static_cast<std::size_t>(got));
        if (im.buffer.size() > (8u << 20)) {
            im.eof = true;
            im.buffer.clear();
        }
    }
}

void ChildProcess::kill() {
    if (impl_ && impl_->pid > 0 && !impl_->reaped)
        ::kill(impl_->pid, SIGKILL);
}

int ChildProcess::wait() {
    if (!impl_ || impl_->pid <= 0)
        return -1;
    if (!impl_->reaped) {
        int status = 0;
        while (::waitpid(impl_->pid, &status, 0) < 0 && errno == EINTR) {
        }
        impl_->reaped = true;
        impl_->exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    }
    return impl_->exitCode;
}

std::filesystem::path currentExecutablePath() {
    std::error_code ec;
    return std::filesystem::read_symlink("/proc/self/exe", ec);
}

#endif

ChildProcess::ChildProcess() : impl_(std::make_unique<Impl>()) {}
bool ChildProcess::inAppContainer() const {
    return impl_ && impl_->appContainer;
}

const std::string& ChildProcess::sandboxNote() const {
    static const std::string kEmpty;
    return impl_ ? impl_->sandboxNote : kEmpty;
}

ChildProcess::~ChildProcess() = default;
ChildProcess::ChildProcess(ChildProcess&&) noexcept = default;
ChildProcess& ChildProcess::operator=(ChildProcess&&) noexcept = default;

} // namespace gmdr
