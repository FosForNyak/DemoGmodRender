// =============================================================================
//  audio_filter.hpp — фільтри звуку FFmpeg (libavfilter) для змішувача.
//
//  AudioFilterChain — граф фільтрів з одним або двома входами (float, 48 кГц)
//  і одним виходом. Результат розкладається за мітками часу (pts), тож
//  затримка фільтрів (loudnorm дивиться на 3 с уперед) не зсуває звук:
//  вихід просто з'являється пізніше, а позиції збігаються з входом.
//
//  Рядки фільтрів — нижче: шумодав afftdn, приглушення гри голосами
//  (sidechaincompress), фінальна гучність за EBU R128 (loudnorm).
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct AVFilterGraph;
struct AVFilterContext;
struct AVFrame;

namespace gmdr::audio {

class AudioFilterChain {
public:
    AudioFilterChain() = default;
    ~AudioFilterChain();
    AudioFilterChain(const AudioFilterChain&) = delete;
    AudioFilterChain& operator=(const AudioFilterChain&) = delete;

    // chain — фільтри в синтаксисі FFmpeg ("afftdn=nr=12,volume=2"). Для двох входів
    // ланцюжок сам починається з міток: "[in0][in1]sidechaincompress=...".
    bool open(const std::string& chain, int inputs, int channels, std::string* error);
    bool is_open() const { return graph_ != nullptr; }

    // Подати наступні frames кадрів (channels каналів підряд) на вхід input.
    bool push(int input, const float* data, size_t frames, std::string* error);
    // Кінець усіх входів: злити те, що фільтри ще тримають.
    bool finish(std::string* error);

    int     channels() const { return channels_; }
    int64_t pushed(int input) const { return pushed_[static_cast<size_t>(input)]; }
    // Готовий результат — позиції [out_start(), out_end()) у кадрах 48 кГц.
    int64_t out_start() const { return out_start_; }
    int64_t out_end() const { return out_start_ + static_cast<int64_t>(out_.size()) / channels_; }
    const float* out_at(int64_t pos) const { return out_.data() + (pos - out_start_) * channels_; }
    void discard_before(int64_t pos);

private:
    bool drain(std::string* error);
    void place(int64_t pos, const float* data, size_t frames);

    AVFilterGraph*                graph_ = nullptr;
    std::vector<AVFilterContext*> src_;
    AVFilterContext*              sink_ = nullptr;
    AVFrame*                      frame_ = nullptr;
    std::string                   chain_;
    int                           channels_ = 2;
    std::vector<int64_t>          pushed_;
    std::vector<float>            out_;
    int64_t                       out_start_ = 0;
    bool                          got_output_ = false;
};

// ---- Готові ланцюжки ---------------------------------------------------------------
// Шумодав для шипіння мікрофона; noise_db — рівень фону гравця (дБ).
std::string denoise_filter(double noise_db);
// Приглушення першого входу (гра) другим (голоси).
std::string duck_filter();
// Фінальна гучність за EBU R128 (I — ціль у LUFS). Порожньо, якщо target >= 0 (вимкнено).
std::string loudness_filter(double target_lufs);

} // namespace gmdr::audio
