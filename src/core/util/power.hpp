// =============================================================================
//  power.hpp — живлення ПК:
//   * "не засинати": поки KeepAwake живий, Windows не переходить у сон (нічний
//     рендер не перерветься). Діє для потоку, що створив об'єкт;
//   * дія після рендеру чи черги — вимкнути ПК або сон (обирає користувач, з
//     відліком, який можна скасувати). GMDR_TEST_POWER_DRYRUN=1 — лише запис у журнал.
// =============================================================================
#pragma once

#include <optional>
#include <string>

namespace gmdr {

class KeepAwake {
public:
    KeepAwake();
    ~KeepAwake();
    KeepAwake(const KeepAwake&) = delete;
    KeepAwake& operator=(const KeepAwake&) = delete;
};

enum class PowerAction { None, Shutdown, Sleep };
const char*                power_action_name(PowerAction a);      // "вимкнути ПК", "сон"
std::optional<PowerAction> parse_power_action(const std::string& s);   // none / shutdown / sleep
bool                       do_power_action(PowerAction a, std::string* error);
// Скільки секунд дати на скасування (60; для тестів — GMDR_TEST_POWER_COUNTDOWN)
int                        power_countdown_seconds();

} // namespace gmdr
