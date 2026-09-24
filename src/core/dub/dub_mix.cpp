#include "dub_mix.hpp"

#include "../audio/audio_filter.hpp"
#include "../media/muxer.hpp"
#include "../util/i18n.hpp"
#include "../util/log.hpp"
#include "../util/strings.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <numeric>

namespace gmdr::dub {

namespace fs = std::filesystem;
using media::kMixRate;

// ============================ Розстановка ===============================================
std::vector<Placement> place_clips(const std::vector<Slot>& slots, double max_tempo, double gap) {
    std::vector<Placement> out(slots.size());
    std::vector<size_t> order(slots.size());
    std::iota(order.begin(), order.end(), size_t{0});
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return slots[a].start < slots[b].start; });
    std::map<std::string, double> free_at;   // гравець → коли звільняється
    for (size_t oi = 0; oi < order.size(); ++oi) {
        const size_t i = order[oi];
        const Slot& s = slots[i];
        Placement& p = out[i];
        p.at = s.start;
        if (s.length <= 0) continue;
        if (auto it = free_at.find(s.key); it != free_at.end()) p.at = std::max(p.at, it->second);
        // Скільки місця до наступної фрази цього гравця (після останньої — до кінця оригіналу + трохи)
        double limit = s.end + 0.6;
        for (size_t oj = oi + 1; oj < order.size(); ++oj)
            if (slots[order[oj]].key == s.key && slots[order[oj]].length > 0) {
                limit = std::max(s.end, slots[order[oj]].start - gap);
                break;
            }
        const double room = std::max(0.05, limit - p.at);
        if (s.length > room) p.tempo = std::min(max_tempo, s.length / room);
        free_at[s.key] = p.at + s.length / p.tempo + gap;
    }
    return out;
}

// ============================ Фрази =====================================================
std::optional<std::vector<float>> load_clip(const fs::path& wav, std::string* error) {
    audio::FileAudioInput in(wav, 0.0);
    if (!in.ok()) {
        if (error) *error = in.error();
        return std::nullopt;
    }
    const int64_t total = static_cast<int64_t>(std::ceil(in.duration_seconds() * kMixRate)) + kMixRate / 10;
    std::vector<float> pcm(static_cast<size_t>(total) * 2, 0.0f);
    for (int64_t pos = 0; pos < total; pos += 8192) {
        const size_t n = static_cast<size_t>(std::min<int64_t>(8192, total - pos));
        in.mix(pos, pcm.data() + pos * 2, n, 1.0f);
    }
    // Тишу на краях прибираємо (лишаємо 40 мс): інакше фраза звучить із запізненням
    const float thr = 0.003f;   // ≈ -50 дБ
    size_t first = 0, last = pcm.size() / 2;
    while (first < last && std::max(std::fabs(pcm[first * 2]), std::fabs(pcm[first * 2 + 1])) < thr) ++first;
    while (last > first && std::max(std::fabs(pcm[(last - 1) * 2]), std::fabs(pcm[(last - 1) * 2 + 1])) < thr) --last;
    if (last <= first) {
        if (error) *error = tr("тиша замість мовлення");
        return std::nullopt;
    }
    const size_t margin = kMixRate / 25;
    first = first > margin ? first - margin : 0;
    last = std::min(pcm.size() / 2, last + margin);
    std::vector<float> clip(pcm.begin() + static_cast<ptrdiff_t>(first * 2), pcm.begin() + static_cast<ptrdiff_t>(last * 2));
    // Рівень: ~-20 дБ RMS, як у звичайного голосу в грі
    double sum = 0;
    for (float v : clip) sum += static_cast<double>(v) * v;
    const double rms = std::sqrt(sum / std::max<size_t>(1, clip.size()));
    float gain = rms > 1e-6 ? static_cast<float>(std::clamp(0.1 / rms, 0.3, 4.0)) : 1.0f;
    float peak = 0;
    for (float v : clip) peak = std::max(peak, std::fabs(v));
    if (peak * gain > 0.95f) gain = 0.95f / peak;
    const size_t frames = clip.size() / 2, fade = std::min<size_t>(frames / 2, kMixRate / 100);
    for (size_t k = 0; k < frames; ++k) {
        float g = gain;
        if (k < fade) g *= static_cast<float>(k) / fade;
        if (frames - k <= fade) g *= static_cast<float>(frames - k) / fade;
        clip[k * 2] *= g;
        clip[k * 2 + 1] *= g;
    }
    return clip;
}

std::vector<float> change_tempo(std::vector<float> clip, double tempo) {
    if (std::abs(tempo - 1.0) <= 0.01 || clip.size() < 4) return clip;
    audio::AudioFilterChain chain;
    std::string ferr;
    if (chain.open(audio::tempo_filter(tempo), 1, 2, &ferr) && chain.push(0, clip.data(), clip.size() / 2, &ferr) &&
        chain.finish(&ferr) && chain.out_end() > chain.out_start())
        return std::vector<float>(chain.out_at(chain.out_start()), chain.out_at(chain.out_end()));
    log_warn("{}", trf("Фразу не пришвидшено: {}", ferr));
    return clip;
}

ClipsInput::ClipsInput(std::vector<Clip> clips) : clips_(std::move(clips)) {
    std::stable_sort(clips_.begin(), clips_.end(), [](const Clip& a, const Clip& b) { return a.at < b.at; });
}

std::string ClipsInput::name() const { return tr("Озвучення"); }

void ClipsInput::mix(int64_t pos, float* out, size_t frames, float gain) {
    const int64_t end = pos + static_cast<int64_t>(frames);
    for (size_t i = first_; i < clips_.size() && clips_[i].at < end; ++i) {
        const Clip& c = clips_[i];
        const int64_t c_end = c.at + static_cast<int64_t>(c.stereo.size() / 2);
        const int64_t a = std::max(pos, c.at), b = std::min(end, c_end);
        for (int64_t p = a; p < b; ++p) {
            const size_t src = static_cast<size_t>(p - c.at) * 2, dst = static_cast<size_t>(p - pos) * 2;
            out[dst] += c.stereo[src] * gain;
            out[dst + 1] += c.stereo[src + 1] * gain;
        }
    }
}

void ClipsInput::discard_before(int64_t pos) {
    // Фрази можуть перекриватися: зсуваємось лише за ті, що скінчились, поки не трапиться незавершена
    while (first_ < clips_.size() && clips_[first_].at + static_cast<int64_t>(clips_[first_].stereo.size() / 2) <= pos) {
        clips_[first_].stereo = {};
        ++first_;
    }
}

// ============================ Мікс ======================================================
const std::vector<AudioFormat>& audio_formats() {
    static const std::vector<AudioFormat> k = {
        {"mp3", ".mp3", "libmp3lame", 256000},
        {"flac", ".flac", "flac", 0},
        {"wav", ".wav", "pcm_s16le", 0},
        {"m4a", ".m4a", "aac", 256000},
    };
    return k;
}

const AudioFormat& find_audio_format(const std::string& id) {
    for (const auto& f : audio_formats())
        if (id == f.id) return f;
    return audio_formats().front();
}

bool mix_dub(const MixSpec& spec, std::vector<Clip> clips, const std::vector<MixOutput>& outputs,
             const std::function<void(double)>& progress, const std::atomic<bool>* cancel, std::string* error) {
    const int64_t total = static_cast<int64_t>(std::llround(spec.seconds * kMixRate));
    if (total <= 0 || outputs.empty()) {
        if (error) *error = tr("порожнє відео");
        return false;
    }
    std::vector<std::unique_ptr<audio::AudioInput>> inputs;
    audio::AudioInput* game = nullptr;
    if (!spec.game.empty()) {
        auto g = std::make_unique<audio::FileAudioInput>(spec.game, 0.0);
        if (g->ok()) {
            game = g.get();
            inputs.push_back(std::move(g));
        } else {
            log_warn("{}", trf("Звук гри для озвучення не прочитано: {}", g->error()));
        }
    }
    std::vector<audio::AudioInput*> originals;
    if (spec.original_gain > 0.0f)
        for (const auto& p : spec.originals) {
            auto v = std::make_unique<audio::FileAudioInput>(p, 0.0);
            if (!v->ok()) continue;
            originals.push_back(v.get());
            inputs.push_back(std::move(v));
        }
    auto ci = std::make_unique<ClipsInput>(std::move(clips));
    audio::AudioInput* dub = ci.get();
    inputs.push_back(std::move(ci));
    audio::AudioInput* game_in_mix = game;
    if (game && spec.duck) {
        auto d = std::make_unique<audio::FilteredInput>(
            tr("Гра (стихає під голоси)"),
            std::vector<std::vector<audio::TrackSource>>{{{game, 1.0f}}, {{dub, spec.dub_gain}}}, false);
        std::string ferr;
        if (d->open(audio::duck_filter(), nullptr, &ferr)) {
            game_in_mix = d.get();
            inputs.push_back(std::move(d));
        } else {
            log_warn("{}", trf("Гра не стихатиме під голоси: {}", ferr));
        }
    }
    audio::AudioTrackPlan mix;
    mix.title = outputs.front().title;
    if (game_in_mix) mix.sources.push_back({game_in_mix, 1.0f});
    for (auto* o : originals) mix.sources.push_back({o, spec.original_gain});
    mix.sources.push_back({dub, spec.dub_gain});
    mix.post_filter = audio::loudness_filter(spec.loudness);
    audio::AudioMixer mixer(std::move(inputs), {mix});

    struct Out {
        media::Muxer       muxer;
        media::AudioEncoder enc;
        int                stream = -1;
    };
    std::vector<std::unique_ptr<Out>> outs;
    for (const auto& o : outputs) {
        auto x = std::make_unique<Out>();
        std::error_code ec;
        const fs::path parent = path_from_utf8(o.path).parent_path();
        if (!parent.empty()) fs::create_directories(parent, ec);
        if (!x->muxer.open(o.path, o.container, error)) return false;
        if (!x->muxer.supports_codec(o.audio.codec)) {
            if (error) *error = trf("контейнер '{}' не підтримує аудіокодек {}", x->muxer.format()->name, o.audio.codec);
            return false;
        }
        if (!x->enc.open(o.audio, x->muxer.needs_global_header(), error)) return false;
        x->stream = x->muxer.add_stream(x->enc.context(), o.title, o.language);
        if (!x->muxer.write_header(true, false, error)) return false;
        outs.push_back(std::move(x));
    }
    bool ok = true;
    std::string err;
    auto sink = [&](size_t, const float* data, size_t frames) {
        for (auto& x : outs) {
            if (!ok) return;
            const AVRational tb = x->enc.context()->time_base;
            if (!x->enc.push(data, frames, [&](AVPacket* p) { return x->muxer.write_packet(x->stream, p, tb); }, &err)) ok = false;
        }
    };
    for (int64_t pos = 0; pos < total && ok;) {
        if (cancel && cancel->load()) {
            for (auto& x : outs) x->muxer.abort();
            if (error) *error = tr("скасовано");
            return false;
        }
        const int64_t until = std::min(total, pos + kMixRate);
        mixer.produce(until, sink);
        pos = until;
        if (progress) progress(static_cast<double>(pos) / total);
    }
    mixer.set_finished();
    if (ok) mixer.flush(total, sink);
    for (auto& x : outs) {
        const AVRational tb = x->enc.context()->time_base;
        if (ok && !x->enc.flush([&](AVPacket* p) { return x->muxer.write_packet(x->stream, p, tb); }, &err)) ok = false;
        if (ok && !x->muxer.finish(&err)) ok = false;
    }
    if (!ok) {
        for (auto& x : outs) x->muxer.abort();
        if (error) *error = err;
        return false;
    }
    return true;
}

} // namespace gmdr::dub
