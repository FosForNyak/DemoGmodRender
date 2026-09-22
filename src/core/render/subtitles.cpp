#include "subtitles.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>

namespace gmdr::render {

std::string srt_timestamp(double seconds) {
    const int64_t ms = std::max<int64_t>(0, static_cast<int64_t>(std::llround(seconds * 1000.0)));
    return std::format("{:02}:{:02}:{:02},{:03}", ms / 3600000, (ms / 60000) % 60, (ms / 1000) % 60, ms % 1000);
}

std::string make_speaker_srt(const std::vector<SpeakerSubtitleSource>& speakers, int64_t origin_sample,
                             double duration, double delay) {
    constexpr double kMergeGap = 0.6;     // паузи коротші — та сама репліка
    constexpr double kMinLength = 0.25;   // коротші уривки не показуємо
    const double rate = voice::kVoiceRate;
    // Події "почав/закінчив говорити" для кожного гравця
    std::map<double, std::vector<std::pair<size_t, int>>> events;   // час -> (гравець, +1/-1)
    for (size_t i = 0; i < speakers.size(); ++i) {
        const auto* t = speakers[i].track;
        if (!t) continue;
        double cur_a = -1, cur_b = -1;
        auto flush = [&] {
            if (cur_a < 0) return;
            const double a = std::max(0.0, cur_a), b = std::min(duration, cur_b);
            if (b - a >= kMinLength) {
                events[a].push_back({i, +1});
                events[b].push_back({i, -1});
            }
            cur_a = cur_b = -1;
        };
        for (const auto& seg : t->segments) {
            const double a = (seg.start - origin_sample) / rate + delay;
            const double b = (seg.end() - origin_sample) / rate + delay;
            if (b <= 0 || a >= duration) continue;
            if (cur_a >= 0 && a - cur_b < kMergeGap) {
                cur_b = std::max(cur_b, b);
            } else {
                flush();
                cur_a = a;
                cur_b = b;
            }
        }
        flush();
    }
    std::string out;
    std::vector<int> active(speakers.size(), 0);
    double span_start = 0;
    std::string current;
    int index = 0;
    auto names = [&] {
        std::string s;
        for (size_t i = 0; i < speakers.size(); ++i)
            if (active[i] > 0) s += (s.empty() ? "" : ", ") + speakers[i].name;
        return s;
    };
    for (const auto& [t, list] : events) {
        const std::string before = current;
        for (const auto& [who, d] : list) active[who] += d;
        const std::string after = names();
        if (after == before) continue;
        if (!before.empty() && t - span_start >= 0.05)
            out += std::format("{}\n{} --> {}\n{}\n\n", ++index, srt_timestamp(span_start), srt_timestamp(t), before);
        current = after;
        span_start = t;
    }
    return out;
}

} // namespace gmdr::render
