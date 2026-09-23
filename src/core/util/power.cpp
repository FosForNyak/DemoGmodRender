#include "power.hpp"

#include <algorithm>
#include <cstdlib>

#include "log.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <powrprof.h>
#endif

namespace gmdr {

#ifdef _WIN32
KeepAwake::KeepAwake() { SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED); }
KeepAwake::~KeepAwake() { SetThreadExecutionState(ES_CONTINUOUS); }
#else
KeepAwake::KeepAwake() = default;
KeepAwake::~KeepAwake() = default;
#endif

const char* power_action_name(PowerAction a) {
    switch (a) {
    case PowerAction::Shutdown: return "вимкнути ПК";
    case PowerAction::Sleep: return "сон";
    default: return "нічого";
    }
}

std::optional<PowerAction> parse_power_action(const std::string& s) {
    if (s.empty() || s == "none" || s == "nothing") return PowerAction::None;
    if (s == "shutdown" || s == "poweroff") return PowerAction::Shutdown;
    if (s == "sleep" || s == "suspend") return PowerAction::Sleep;
    return std::nullopt;
}

#ifdef _WIN32
namespace {
// Вимкнення і сон потребують права SE_SHUTDOWN_NAME у токені процесу (воно є, але вимкнене)
bool enable_shutdown_privilege() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) return false;
    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    const bool ok = LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &tp.Privileges[0].Luid) &&
                    AdjustTokenPrivileges(token, FALSE, &tp, 0, nullptr, nullptr) && GetLastError() == ERROR_SUCCESS;
    CloseHandle(token);
    return ok;
}
} // namespace
#endif

int power_countdown_seconds() {
    if (const char* e = std::getenv("GMDR_TEST_POWER_COUNTDOWN")) return std::max(1, std::atoi(e));
    return 60;
}

bool do_power_action(PowerAction a, std::string* error) {
    if (a == PowerAction::None) return true;
    if (std::getenv("GMDR_TEST_POWER_DRYRUN")) {   // для перевірки: нічого не вимикаємо
        log_info("(перевірка) зараз було б: {}", power_action_name(a));
        return true;
    }
#ifdef _WIN32
    if (!enable_shutdown_privilege()) {
        if (error) *error = "Windows не дає права вимкнути ПК";
        return false;
    }
    BOOL ok = FALSE;
    if (a == PowerAction::Shutdown)
        ok = ExitWindowsEx(EWX_POWEROFF | EWX_FORCEIFHUNG,
                           SHTDN_REASON_MAJOR_APPLICATION | SHTDN_REASON_MINOR_OTHER | SHTDN_REASON_FLAG_PLANNED);
    else
        ok = SetSuspendState(FALSE, FALSE, FALSE);
    if (!ok && error) *error = "код помилки Windows " + std::to_string(GetLastError());
    return ok != FALSE;
#else
    if (error) *error = "лише у Windows";
    return false;
#endif
}

} // namespace gmdr
