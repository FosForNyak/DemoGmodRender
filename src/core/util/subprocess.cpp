#include "subprocess.hpp"

#include "strings.hpp"
#include "i18n.hpp"

#include <chrono>
#include <format>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace gmdr {

namespace {
// Розрізати накопичений вивід на рядки (\n, \r\n або \r — рядки прогресу)
void emit_lines(std::string& buf, const std::function<void(const std::string&)>& on_line, bool flush) {
    size_t start = 0;
    for (size_t i = 0; i < buf.size(); ++i) {
        if (buf[i] != '\n' && buf[i] != '\r') continue;
        if (i > start && on_line) on_line(buf.substr(start, i - start));
        start = i + 1;
    }
    buf.erase(0, start);
    if (flush && !buf.empty()) {
        if (on_line) on_line(buf);
        buf.clear();
    }
}

#ifdef _WIN32
std::wstring quote_arg(const std::wstring& a) {
    if (!a.empty() && a.find_first_of(L" \t\"") == std::wstring::npos) return a;
    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : a) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        if (c == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out += L'"';
            backslashes = 0;
            continue;
        }
        out.append(backslashes, L'\\');
        backslashes = 0;
        out += c;
    }
    out.append(backslashes * 2, L'\\');
    out += L'"';
    return out;
}
#endif
} // namespace

#ifdef _WIN32
ProcessResult run_process(const std::filesystem::path& exe, const std::vector<std::string>& args,
                          const std::function<void(const std::string&)>& on_line, const std::atomic<bool>* cancel,
                          std::string* error) {
    ProcessResult r;
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) {
        if (error) *error = trf("не вдалося створити канал (код {})", GetLastError());
        return r;
    }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);   // читаємо лише ми
    std::wstring cmd = quote_arg(exe.native());
    for (const auto& a : args) cmd += L" " + quote_arg(utf8_to_wide(a));
    std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
    cmd_buf.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    const std::wstring dir = exe.parent_path().native();   // поруч лежать його DLL
    const BOOL ok = CreateProcessW(exe.c_str(), cmd_buf.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr,
                                   dir.empty() ? nullptr : dir.c_str(), &si, &pi);
    CloseHandle(wr);
    if (!ok) {
        if (error) *error = trf("не вдалося запустити {} (код {})", path_to_utf8(exe.filename()), GetLastError());
        CloseHandle(rd);
        return r;
    }
    r.started = true;
    CloseHandle(pi.hThread);
    std::string buf;
    char chunk[4096];
    for (;;) {
        if (cancel && cancel->load()) {
            TerminateProcess(pi.hProcess, 1);
            r.cancelled = true;
        }
        DWORD avail = 0;
        if (PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            DWORD got = 0;
            if (ReadFile(rd, chunk, std::min<DWORD>(avail, sizeof(chunk)), &got, nullptr) && got > 0) {
                buf.append(chunk, got);
                emit_lines(buf, on_line, false);
                continue;
            }
        }
        if (WaitForSingleObject(pi.hProcess, 30) == WAIT_OBJECT_0) {
            // Дочитати залишок
            while (PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
                DWORD got = 0;
                if (!ReadFile(rd, chunk, std::min<DWORD>(avail, sizeof(chunk)), &got, nullptr) || got == 0) break;
                buf.append(chunk, got);
            }
            break;
        }
    }
    emit_lines(buf, on_line, true);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    r.exit_code = static_cast<int>(code);
    CloseHandle(pi.hProcess);
    CloseHandle(rd);
    return r;
}
#else
ProcessResult run_process(const std::filesystem::path& exe, const std::vector<std::string>& args,
                          const std::function<void(const std::string&)>& on_line, const std::atomic<bool>* cancel,
                          std::string* error) {
    ProcessResult r;
    int fds[2];
    if (pipe(fds) != 0) {
        if (error) *error = std::string("pipe: ") + std::strerror(errno);
        return r;
    }
    std::vector<std::string> argv_s = {exe.string()};
    for (const auto& a : args) argv_s.push_back(a);
    std::vector<char*> argv;
    for (auto& s : argv_s) argv.push_back(s.data());
    argv.push_back(nullptr);
    const pid_t pid = fork();
    if (pid < 0) {
        if (error) *error = std::string("fork: ") + std::strerror(errno);
        close(fds[0]);
        close(fds[1]);
        return r;
    }
    if (pid == 0) {
        dup2(fds[1], 1);
        dup2(fds[1], 2);
        close(fds[0]);
        close(fds[1]);
        execv(argv[0], argv.data());
        _exit(127);
    }
    close(fds[1]);
    r.started = true;
    std::string buf;
    char chunk[4096];
    for (;;) {
        if (cancel && cancel->load() && !r.cancelled) {
            kill(pid, SIGTERM);
            r.cancelled = true;
        }
        pollfd p{fds[0], POLLIN, 0};
        const int n = poll(&p, 1, 50);
        if (n < 0 && errno != EINTR) break;
        if (n > 0) {
            const ssize_t got = read(fds[0], chunk, sizeof(chunk));
            if (got <= 0) break;   // кінець: процес закрив вивід
            buf.append(chunk, static_cast<size_t>(got));
            emit_lines(buf, on_line, false);
        }
    }
    emit_lines(buf, on_line, true);
    close(fds[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    r.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    if (r.exit_code == 127 && error) *error = tr("не вдалося запустити ") + exe.filename().string();
    return r;
}
#endif

} // namespace gmdr
