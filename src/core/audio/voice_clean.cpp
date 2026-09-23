#include "voice_clean.hpp"

#include "../voice/voice_decoder.hpp"

#include <algorithm>
#include <cmath>

namespace gmdr::audio {

namespace {

constexpr int    kRate = 48000;
constexpr size_t kSubBlock = kRate / 10;   // 100 мс: блоки BS.1770 (400 мс) ідуть з кроком 100 мс
constexpr size_t kWindow = kRate / 50;     // 20 мс: вікно детектора фону і мови

// Двоступеневий K-фільтр BS.1770 (коефіцієнти для 48 кГц).
struct Biquad {
    double b0, b1, b2, a1, a2;
    double z1 = 0, z2 = 0;
    double run(double x) {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

class LoudnessMeter {
public:
    void add(const float* x, size_t n) {
        for (size_t i = 0; i < n; ++i) {
            const double y = hp_.run(shelf_.run(x[i]));
            acc_ += y * y;
            if (++acc_n_ == kSubBlock) {
                sub_.push_back(acc_ / static_cast<double>(kSubBlock));
                acc_ = 0;
                acc_n_ = 0;
            }
        }
    }
    double integrated() const {
        // Блоки по 400 мс з перекриттям 75% = середнє чотирьох підблоків по 100 мс
        std::vector<double> z;
        for (size_t j = 0; j + 4 <= sub_.size(); ++j) z.push_back((sub_[j] + sub_[j + 1] + sub_[j + 2] + sub_[j + 3]) / 4.0);
        auto lufs = [](double ms) { return ms > 0 ? -0.691 + 10.0 * std::log10(ms) : -999.0; };
        double sum = 0;
        size_t cnt = 0;
        for (double v : z)
            if (lufs(v) > -70.0) {
                sum += v;
                ++cnt;
            }
        if (cnt == 0) return -99.0;
        const double rel = lufs(sum / static_cast<double>(cnt)) - 10.0;
        sum = 0;
        cnt = 0;
        for (double v : z)
            if (lufs(v) > -70.0 && lufs(v) > rel) {
                sum += v;
                ++cnt;
            }
        return cnt ? lufs(sum / static_cast<double>(cnt)) : -99.0;
    }

private:
    Biquad shelf_{1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585};
    Biquad hp_{1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621};
    double acc_ = 0;
    size_t acc_n_ = 0;
    std::vector<double> sub_;
};

// Збирає гучність і рівні вікон детектора з кількох шматків мовлення.
class ProfileBuilder {
public:
    void add(const float* x, size_t n) {
        meter_.add(x, n);
        for (size_t i = 0; i + kWindow <= n; i += kWindow) {
            double s = 0;
            for (size_t k = 0; k < kWindow; ++k) s += static_cast<double>(x[i + k]) * x[i + k];
            const double db = 10.0 * std::log10(s / kWindow + 1e-20);
            if (db > -90.0) db_.push_back(db);   // цифрова тиша (пропуски пакетів) — не фон
        }
        samples_ += n;
    }
    VoiceProfile result() {
        VoiceProfile p;
        p.seconds = static_cast<double>(db_.size() * kWindow) / kRate;
        if (db_.empty()) return p;
        p.loudness = meter_.integrated();
        auto pct = [&](double q) {
            const size_t k = std::min(db_.size() - 1, static_cast<size_t>(q * static_cast<double>(db_.size())));
            std::nth_element(db_.begin(), db_.begin() + static_cast<ptrdiff_t>(k), db_.end());
            return db_[k];
        };
        p.noise_db = pct(0.15);
        p.speech_db = pct(0.90);
        return p;
    }

private:
    LoudnessMeter       meter_;
    std::vector<double> db_;
    size_t              samples_ = 0;
};

} // namespace

double integrated_loudness(const float* mono, size_t n) {
    LoudnessMeter m;
    m.add(mono, n);
    return m.integrated();
}

VoiceProfile profile_samples(const float* mono, size_t n) {
    ProfileBuilder b;
    b.add(mono, n);
    return b.result();
}

VoiceProfile profile_voice(const voice::SpeakerTrack& track, double max_seconds) {
    int64_t total = 0;
    for (const auto& s : track.segments) total += s.length;
    if (total <= 0) return {};
    // Довге мовлення аналізуємо вибірково: шматки по 10 с рівномірно по всьому демо
    const double ratio = std::min(1.0, max_seconds * kRate / static_cast<double>(total));
    const int64_t chunk = 10LL * kRate;
    ProfileBuilder b;
    voice::VoiceStream stream(track);
    std::vector<float> buf;
    double credit = 0;
    for (const auto& s : track.segments) {
        for (int64_t at = s.start; at < s.end(); at += chunk) {
            const int64_t len = std::min(chunk, s.end() - at);
            credit += static_cast<double>(len) * ratio;
            if (credit + 0.5 < static_cast<double>(len)) continue;
            credit -= static_cast<double>(len);
            buf.assign(static_cast<size_t>(len), 0.0f);
            stream.mix_into(at, buf.size(), buf.data());
            b.add(buf.data(), buf.size());
        }
    }
    return b.result();
}

float level_gain(const VoiceProfile& p, double target_lufs) {
    if (!p.valid()) return 1.0f;
    const double db = std::clamp(target_lufs - p.loudness, -12.0, 15.0);
    return static_cast<float>(std::pow(10.0, db / 20.0));
}

GateParams gate_for(const VoiceProfile& p, float gain) {
    GateParams g;
    if (!p.valid()) {
        g.floor_db = 0;   // нічого не знаємо — гейт нічого не робить
        return g;
    }
    const double shift = 20.0 * std::log10(std::max(1e-6f, gain));
    const double noise = p.noise_db + shift;
    const double speech = p.speech_db + shift;
    const double gap = speech - noise;
    g.open_db = noise + std::max(6.0, 0.4 * gap);
    g.close_db = std::max(noise + 3.0, g.open_db - 4.0);
    // Фон майже такий самий гучний, як мова (музика в мікрофон, крик): лише злегка приглушуємо
    g.floor_db = gap < 12.0 ? -10.0 : -25.0;
    return g;
}

NoiseGate::NoiseGate(const GateParams& p)
    : window_(kWindow),
      open_pow_(std::pow(10.0, p.open_db / 10.0)),
      close_pow_(std::pow(10.0, p.close_db / 10.0)),
      floor_(static_cast<float>(std::pow(10.0, p.floor_db / 20.0))),
      attack_step_((1.0f - floor_) / (0.005f * kRate)),
      release_step_((1.0f - floor_) / (0.12f * kRate)),
      hold_(static_cast<size_t>(0.15 * kRate)),
      gain_(floor_) {}

void NoiseGate::process(float* x, size_t n) {
    const double inv = 1.0 / static_cast<double>(window_);
    if (!primed_) {
        sum_ = 0;
        for (size_t k = 0; k < window_; ++k) sum_ += static_cast<double>(x[k]) * x[k];
        primed_ = true;
    }
    for (size_t i = 0; i < n; ++i) {
        // Рівень вікна [i, i + 20 мс): гейт відкривається трохи раніше за початок слова
        const double pw = std::max(0.0, sum_) * inv;
        if (pw > open_pow_) {
            open_ = true;
            hold_left_ = hold_;
        } else if (open_) {
            if (pw > close_pow_) hold_left_ = hold_;
            else if (hold_left_ > 0) --hold_left_;
            else open_ = false;
        }
        gain_ = open_ ? std::min(1.0f, gain_ + attack_step_) : std::max(floor_, gain_ - release_step_);
        const float raw = x[i];
        const float next = x[i + window_];
        sum_ += static_cast<double>(next) * next - static_cast<double>(raw) * raw;
        x[i] = raw * gain_;
        if (++since_recalc_ >= static_cast<size_t>(kRate)) {
            // Похибка ковзної суми: раз на секунду рахуємо вікно заново
            since_recalc_ = 0;
            double s = 0;
            for (size_t k = i + 1; k <= i + window_; ++k) s += static_cast<double>(x[k]) * x[k];
            sum_ = s;
        }
    }
}

} // namespace gmdr::audio
