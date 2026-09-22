#include "power.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace gmdr {

#ifdef _WIN32
KeepAwake::KeepAwake() { SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED); }
KeepAwake::~KeepAwake() { SetThreadExecutionState(ES_CONTINUOUS); }
#else
KeepAwake::KeepAwake() = default;
KeepAwake::~KeepAwake() = default;
#endif

} // namespace gmdr
