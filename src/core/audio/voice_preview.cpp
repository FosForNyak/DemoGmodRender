#include "voice_preview.hpp"

#include "../voice/voice_decoder.hpp"
#include "audio_inputs.hpp"

#include <algorithm>

namespace gmdr::audio {

VoiceClip make_voice_clip(const voice::SpeakerTrack& track, int64_t from, const VoiceCleanup* fx, double max_speech) {
    constexpr int64_t R = media::kMixRate;
    constexpr int64_t kPad = R * 15 / 100;   // 0.15 с до і після фрази
    VoiceClip clip;
    const auto& segs = track.segments;
    if (segs.empty()) return clip;
    size_t k0 = static_cast<size_t>(std::upper_bound(segs.begin(), segs.end(), from,
                                                     [](int64_t v, const voice::VoiceSegment& s) { return v < s.end(); }) -
                                    segs.begin());
    if (k0 >= segs.size()) {
        k0 = 0;
        from = 0;
    }
    // Відрізки, які буде чути: фрази (з невеликим запасом) до max_speech секунд мовлення
    const int64_t a = std::max<int64_t>(0, std::max(from, segs[k0].start) - kPad);
    std::vector<std::pair<int64_t, int64_t>> keep;
    int64_t need = static_cast<int64_t>(max_speech * R);
    for (size_t k = k0; k < segs.size() && need > 0; ++k) {
        const int64_t lo = std::max(from, segs[k].start);
        const int64_t hi = std::min(segs[k].end(), lo + need);
        if (hi <= lo) continue;
        if (lo - a > 180 * R) break;   // далі — надто довго декодувати заради уривка
        need -= hi - lo;
        const int64_t s = std::max(a, lo - kPad), e = hi + kPad;
        if (!keep.empty() && s <= keep.back().second) keep.back().second = std::max(keep.back().second, e);
        else keep.push_back({s, e});
    }
    if (keep.empty()) return clip;
    const int64_t b = keep.back().second;

    // Той самий шлях, що й під час рендеру: VoiceInput -> [шумодав і гейт] -> підсилення
    std::unique_ptr<AudioInput> src = std::make_unique<VoiceInput>(&track, 0, 0.0);
    if (fx && fx->denoise && fx->profile.valid()) {
        auto f = std::make_unique<FilteredInput>(src->name(), std::vector<std::vector<TrackSource>>{{{src.get(), 1.0f}}},
                                                 true);
        const GateParams gp = gate_for(fx->profile);
        std::string err;
        if (f->open(denoise_filter(fx->profile.noise_db), &gp, &err) || f->open({}, &gp, &err)) {
            f->start_at(a);
            f->own(std::move(src));
            src = std::move(f);
        }
    }
    const float gain = fx ? fx->level : 1.0f;
    std::vector<float> stereo;
    for (const auto& [s, e] : keep) {
        // Проміжки між відрізками теж проходять через обробку (гейт має бачити тишу), але не чутні
        for (int64_t p = s; p < e; p += R) {
            const size_t n = static_cast<size_t>(std::min(R, e - p));
            stereo.assign(n * 2, 0.0f);
            src->mix(p, stereo.data(), n, gain);
            for (size_t i = 0; i < n; ++i) clip.mono.push_back(std::clamp(stereo[i * 2], -1.0f, 1.0f));
            src->discard_before(p + static_cast<int64_t>(n));
        }
        clip.speech_seconds += static_cast<double>(e - s) / R;
    }
    clip.start = a;
    return clip;
}

} // namespace gmdr::audio
