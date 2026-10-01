// Test helper for the importer sandbox (api_engine_test / process tests): tries to open a file by path and
// writes the outcome to an inherited handle and stdout. It never reads file contents.
// Usage: sandbox_probe <path-to-try> <inherited handle>
#include <cstdio>
#include <cstdlib>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

int main(int argc, char** argv) {
    if (argc < 3)
        return 64;
#ifdef _WIN32
    const int n = MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, nullptr, 0);
    std::wstring path(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, path.data(), n);
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    const bool opened = f != INVALID_HANDLE_VALUE;
    const DWORD err = opened ? 0 : GetLastError();
    if (opened)
        CloseHandle(f);
    HANDLE out = reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(std::strtoll(argv[2], nullptr, 10)));
    const char msg[] = "inherited-ok";
    DWORD written = 0;
    const bool wrote = WriteFile(out, msg, sizeof msg - 1, &written, nullptr) != 0;
    std::printf("open=%s error=%lu inherited=%s\n", opened ? "allowed" : "denied",
                static_cast<unsigned long>(err), wrote ? "ok" : "failed");
    std::fflush(stdout);
#endif
    return 0;
}
