// =============================================================================
//  test_check.hpp — перевірки для модульних тестів (спільні для всіх файлів тестів).
// =============================================================================
#pragma once

#include <cmath>
#include <cstdio>

inline int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                                                  \
    do {                                                                                             \
        if (cond) { ++g_pass; }                                                                      \
        else { ++g_fail; std::printf("  ПРОВАЛ %s:%d: %s\n", __FILE__, __LINE__, #cond); }            \
    } while (0)
#define CHECK_NEAR(a, b, eps)                                                                        \
    do {                                                                                             \
        const double _a = (a), _b = (b);                                                             \
        if (std::abs(_a - _b) <= (eps)) { ++g_pass; }                                                \
        else { ++g_fail; std::printf("  ПРОВАЛ %s:%d: %s = %g, очікувалось %g ± %g\n", __FILE__, __LINE__, #a, _a, _b, (double)(eps)); } \
    } while (0)
