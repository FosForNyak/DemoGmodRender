// =============================================================================
//  power.hpp — "не засинати": поки об'єкт живий, Windows не переходить у сон
//  (нічний рендер не перерветься). Діє для потоку, що створив об'єкт.
// =============================================================================
#pragma once

namespace gmdr {

class KeepAwake {
public:
    KeepAwake();
    ~KeepAwake();
    KeepAwake(const KeepAwake&) = delete;
    KeepAwake& operator=(const KeepAwake&) = delete;
};

} // namespace gmdr
