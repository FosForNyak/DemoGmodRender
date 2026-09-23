#include "parallel.hpp"

#include <algorithm>
#include <cmath>

namespace gmdr::render {

namespace {
// Найменша кількість кадрів m, за яку набігає ціла кількість тіків (30 кадрів/с і 66 тіків/с —
// кожні 5 кадрів = 11 тіків). Межа частини на такому кадрі припадає рівно на тік, тож копія гри
// починає запис саме з нього — кадри частини збігаються з кадрами звичайного рендеру точно.
// 0 — такого m немає в розумних межах (59.94 кадр/с тощо).
int64_t tick_aligned_period(double ti, double frame_dt, int64_t max_m) {
    for (int64_t m = 1; m <= max_m; ++m) {
        const double ticks = static_cast<double>(m) * frame_dt / ti;
        if (std::abs(ticks - std::round(ticks)) < 1e-4) return m;
    }
    return 0;
}
} // namespace

std::vector<PartPlan> plan_parts(int32_t start_tick, int32_t end_tick, double ti, double frame_dt, int parts,
                                 int64_t min_frames) {
    std::vector<PartPlan> out;
    if (end_tick <= start_tick || ti <= 0 || frame_dt <= 0) return out;
    const int64_t total = std::llround((end_tick - start_tick) * ti / frame_dt);
    int n = std::clamp(parts, 1, 16);
    while (n > 1 && total / n < std::max<int64_t>(1, min_frames)) --n;
    // Межі — кратні періоду, якщо він дрібний порівняно з частиною (інакше — будь-які кадри,
    // а гра почне на тіку трохи раніше, і зайві під-кадри відкинуться)
    const int64_t m = tick_aligned_period(ti, frame_dt, std::max<int64_t>(1, total / (4 * n)));
    std::vector<int64_t> bounds(static_cast<size_t>(n) + 1);
    bounds[0] = 0;
    bounds[static_cast<size_t>(n)] = total;
    for (int k = 1; k < n; ++k) {
        int64_t b = total * k / n;
        if (m > 0) b = std::llround(static_cast<double>(b) / static_cast<double>(m)) * m;
        bounds[static_cast<size_t>(k)] = std::clamp(b, bounds[static_cast<size_t>(k) - 1] + 1, total - (n - k));
    }
    // Запас на початку: драйвер вмикає запис на кілька тіків пізніше, ніж просили, тож гра
    // починає раніше, а зайві кадри відкидаються (пізній початок довелося б латати повтором
    // кадру). Запас кратний періоду — тоді кадри гри лягають рівно на сітку кадрів відео.
    constexpr int32_t kLead = 8;
    int32_t lead = kLead;
    if (m > 0) {
        const auto period = static_cast<int32_t>(std::llround(static_cast<double>(m) * frame_dt / ti));
        if (period > 0 && period <= 60) lead = (kLead + period - 1) / period * period;
    }
    const double t0 = start_tick * ti;
    for (int k = 0; k < n; ++k) {
        PartPlan p;
        p.index = k;
        p.first_frame = bounds[static_cast<size_t>(k)];
        const int64_t next = bounds[static_cast<size_t>(k) + 1];
        p.frames = next - p.first_frame;
        p.first_time = t0 + static_cast<double>(p.first_frame) * frame_dt;
        // Гра починає запис на цілому тіку — раніше першого кадру; зайві під-кадри на початку
        // відкидаються. Кінець — на пару тіків далі останнього кадру частини.
        // (Запас 1e-4 тіку — на похибку float у тривалості тіку з демо.)
        p.start_tick = std::max(0, static_cast<int32_t>(std::floor(p.first_time / ti + 1e-4)) - lead);
        const double end_time = t0 + static_cast<double>(next) * frame_dt;
        p.end_tick = k + 1 == n ? end_tick : std::min(end_tick, static_cast<int32_t>(std::ceil(end_time / ti - 1e-4)) + 2);
        out.push_back(p);
    }
    return out;
}

} // namespace gmdr::render
