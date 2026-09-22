#include "crash_dump.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>

#include <cwchar>
#include <string>

namespace gmdr {

namespace {
wchar_t g_dir[MAX_PATH] = {};

using MiniDumpWriteDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                          PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);

LONG WINAPI on_crash(EXCEPTION_POINTERS* info) {
    // Тут пам'ять програми може бути пошкоджена — лише найпростіші виклики WinAPI
    static volatile LONG entered = 0;
    if (InterlockedExchange(&entered, 1) != 0) return EXCEPTION_CONTINUE_SEARCH;
    HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll");
    auto write = dbghelp ? reinterpret_cast<MiniDumpWriteDumpFn>(reinterpret_cast<void*>(GetProcAddress(dbghelp, "MiniDumpWriteDump")))
                         : nullptr;
    if (!write) return EXCEPTION_CONTINUE_SEARCH;
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t path[MAX_PATH + 64];
    swprintf(path, sizeof(path) / sizeof(path[0]), L"%s\\gmdr_crash_%04u%02u%02u_%02u%02u%02u.dmp", g_dir, t.wYear,
             t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return EXCEPTION_CONTINUE_SEARCH;
    MINIDUMP_EXCEPTION_INFORMATION ei{};
    ei.ThreadId = GetCurrentThreadId();
    ei.ExceptionPointers = info;
    ei.ClientPointers = FALSE;
    write(GetCurrentProcess(), GetCurrentProcessId(), f,
          static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory | MiniDumpWithThreadInfo),
          &ei, nullptr, nullptr);
    CloseHandle(f);
    return EXCEPTION_CONTINUE_SEARCH;   // далі — стандартна поведінка Windows (вікно про збій)
}
} // namespace

void install_crash_handler(const std::filesystem::path& dir) {
    const std::wstring d = dir.native();
    wcsncpy(g_dir, d.c_str(), MAX_PATH - 1);
    g_dir[MAX_PATH - 1] = 0;
    SetUnhandledExceptionFilter(on_crash);
}

} // namespace gmdr

#else
namespace gmdr {
void install_crash_handler(const std::filesystem::path&) {}
} // namespace gmdr
#endif
