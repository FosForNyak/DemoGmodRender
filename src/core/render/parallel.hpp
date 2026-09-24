// =============================================================================
//  parallel.hpp — паралельний рендер кількома копіями гри (-multirun).
//
//  Фрагмент ділиться на частини за кадрами відео; кожну частину рендерить своя копія
//  гри у свій файл (той самий кодек і контейнер), а потім частини склеюються пакетами
//  без перекодування, звук же міксується заново на всю довжину (EncodeSettings::video_parts).
// =============================================================================
#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

namespace gmdr::render {

struct PartPlan {
    int     index = 0;
    int64_t first_frame = 0;   // перший кадр частини у відео всього фрагмента
    int64_t frames = 0;        // скільки кадрів у частині
    double  first_time = 0;    // час демо (с) першого кадру частини
    int32_t start_tick = 0;    // з якого тіку гра починає запис (з запасом раніше first_time)
    int32_t end_tick = 0;      // до якого тіку гра програє демо (трохи далі кінця частини)
};

// Поділ відрізка тіків [start_tick, end_tick) на parts частин. frame_dt — секунд демо на кадр
// відео (швидкість / FPS). Частин менше, якщо котрась вийшла б коротшою за min_frames кадрів.
// Кадри частин ідуть підряд без пропусків: кадр n усього відео — це час демо start + n·frame_dt,
// хоч би в якій частині він був.
std::vector<PartPlan> plan_parts(int32_t start_tick, int32_t end_tick, double tick_interval, double frame_dt,
                                 int parts, int64_t min_frames);
// Те саме для кадрів [first_frame, total_frames) відео, чий кадр 0 — час демо t0 (дорендерити
// решту після збою: початок уже є у файлі).
std::vector<PartPlan> plan_parts_range(double t0, int64_t first_frame, int64_t total_frames, int32_t end_tick,
                                       double tick_interval, double frame_dt, int parts, int64_t min_frames);

// Найменша кількість кадрів m (до max_m), за яку набігає ціла кількість тіків (30 кадр/с і 66 тік/с —
// 5 кадрів = 11 тіків): межа на кадрі, кратному m, припадає рівно на тік. 0 — такого m немає.
int64_t tick_aligned_period(double tick_interval, double frame_dt, int64_t max_m);

// Найкоротша частина, заради якої варто запускати ще одну копію гри, с (20; для автотестів —
// змінна GMDR_TEST_MIN_PART). Паралельний рендер — лише для фрагментів від 2 × цього.
double min_part_seconds();

// Копії гри запускаються по черзі: драйвер у меню кожної копії забирає спільний job.txt, тож
// наступне завдання можна писати, лише коли попередня копія своє вже забрала.
struct LaunchGate {
    std::mutex m;
};

} // namespace gmdr::render
