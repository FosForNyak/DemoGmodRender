// =============================================================================
//  audio_inputs.hpp — джерела звуку для змішування.
//
//  Усі джерела віддають стерео float 48 кГц, вирівняне відносно ПОЧАТКУ
//  ВІДЕО (позиція 0 = перший записаний кадр).
//
//   GameAudioInput  — WAV від startmovie (росте під час рендеру)
//   VoiceInput      — декодований голос гравця з демо
//   FileAudioInput  — довільний аудіофайл (напр. запис мікрофона), будь-який
//                     формат, який читає FFmpeg (wav, mp3, ogg, flac, m4a ...)
//   FilteredInput   — інші джерела, пропущені через обробку: шумодав і гейт
//                     для голосу, приглушення гри голосами
// =============================================================================
#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "../media/ffmpeg_util.hpp"
#include "../voice/voice_decoder.hpp"
#include "audio_filter.hpp"
#include "voice_clean.hpp"
#include "wav.hpp"

namespace gmdr::audio {

class AudioInput {
public:
    virtual ~AudioInput() = default;
    virtual std::string name() const = 0;
    // До якої позиції (кадрів 48 кГц) дані вже доступні. Для статичних — INT64_MAX.
    virtual int64_t available() = 0;
    // Змішати (додати) frames стерео-кадрів з позиції pos у out з гучністю gain.
    // Викликається лише для діапазонів < available().
    virtual void mix(int64_t pos, float* out, size_t frames, float gain) = 0;
    // Дані до pos більше не знадобляться.
    virtual void discard_before(int64_t /*pos*/) {}
    // Для "живих" джерел: запис завершено, після кінця — тиша.
    virtual void set_finished() {}
};

// Звук гри з WAV, який startmovie записує паралельно з кадрами.
class GameAudioInput final : public AudioInput {
public:
    // offset_seconds > 0 — затримати звук гри, < 0 — зсунути раніше.
    GameAudioInput(std::filesystem::path wav_path, bool live, double offset_seconds);
    std::string name() const override { return "Звук гри"; }
    int64_t available() override;
    void mix(int64_t pos, float* out, size_t frames, float gain) override;
    void discard_before(int64_t pos) override;
    void set_finished() override;
    bool has_file() const { return opened_; }
    int  source_rate() const { return reader_.sample_rate(); }

private:
    void pull();

    std::filesystem::path path_;
    bool                  live_;
    bool                  finished_ = false;
    bool                  opened_ = false;
    bool                  drained_ = false;
    int64_t               offset_ = 0;          // у кадрах 48 кГц
    WavReader             reader_;
    media::SwrPtr         swr_;
    std::vector<float>    buf_;                 // стерео, починається з buf_start_
    int64_t               buf_start_ = 0;       // позиція (у "часі джерела") першого кадру буфера
    int64_t               produced_ = 0;        // скільки кадрів 48 кГц вироблено всього
    std::vector<float>    raw_;
};

// Голос одного гравця (декодується потоково, лише потрібний відрізок часу).
class VoiceInput final : public AudioInput {
public:
    // origin_sample — позиція (у семплах 48 кГц від тіку 0), що відповідає початку відео.
    VoiceInput(const voice::SpeakerTrack* track, int64_t origin_sample, double extra_delay_seconds);
    std::string name() const override { return "Голос: " + track_->display_name(); }
    int64_t available() override { return INT64_MAX; }
    void mix(int64_t pos, float* out, size_t frames, float gain) override;

private:
    const voice::SpeakerTrack*          track_;
    int64_t                             origin_;
    std::unique_ptr<voice::VoiceStream> stream_;
    // Той самий шматок часу змішується в кілька доріжок (Мікс + окремі) —
    // декодуємо його один раз.
    std::vector<float>                  cache_;
    int64_t                             cache_pos_ = INT64_MIN;
};

// Довільний аудіофайл (мікрофон тощо).
class FileAudioInput final : public AudioInput {
public:
    FileAudioInput(const std::filesystem::path& path, double offset_seconds);
    ~FileAudioInput() override;
    bool ok() const { return ok_; }
    const std::string& error() const { return error_; }
    std::string name() const override { return "Файл: " + name_; }
    int64_t available() override { return INT64_MAX; }
    void mix(int64_t pos, float* out, size_t frames, float gain) override;
    void discard_before(int64_t pos) override;
    double duration_seconds() const { return duration_; }

private:
    bool decode_more();   // декодувати наступну порцію; false — кінець файлу

    bool                   ok_ = false;
    bool                   eof_ = false;
    std::string            error_;
    std::string            name_;
    int64_t                offset_ = 0;
    double                 duration_ = 0;
    AVFormatContext*       fmt_ = nullptr;
    media::CodecCtxPtr     dec_;
    media::SwrPtr          swr_;
    media::PacketPtr       pkt_;
    media::FramePtr        frame_;
    int                    stream_ = -1;
    std::vector<float>     buf_;
    int64_t                buf_start_ = 0;   // у "часі файлу" (кадри 48 кГц)
};

// ---- Змішувач --------------------------------------------------------------------
struct TrackSource {
    AudioInput* input = nullptr;
    float       gain = 1.0f;
};

// Джерело, пропущене через обробку: фільтри FFmpeg (шумодав; приглушення гри голосами —
// тоді другий вхід фільтра — сума голосів) і/або гейт тиші між фразами. Входи — суми
// джерел, як доріжки змішувача. Обробка йде послідовно вперед, а результат читається за
// позицією, як з будь-якого джерела; затримку фільтрів враховано (available() менше).
class FilteredInput final : public AudioInput {
public:
    FilteredInput(std::string name, std::vector<std::vector<TrackSource>> inputs, bool mono);
    // chain — фільтри FFmpeg (може бути порожнім), gate — гейт (лише для моно).
    bool open(const std::string& chain, const GateParams* gate, std::string* error);
    // Віддати джерело у володіння (його більше ніхто не читає — напр. голос за шумодавом).
    void own(std::unique_ptr<AudioInput> in) { owned_.push_back(std::move(in)); }
    // Почати обробку з позиції pos, а не з 0 (до першого читання). Раніше — тиша.
    void start_at(int64_t pos) { fed_ = buf_start_ = gated_end_ = origin_ = pos; }
    std::string name() const override { return name_; }
    int64_t available() override;
    void mix(int64_t pos, float* out, size_t frames, float gain) override;
    void discard_before(int64_t pos) override;
    void set_finished() override;

private:
    int64_t source_available();
    int64_t ready_end() const;
    void    produce_until(int64_t end, int64_t avail);
    void    feed_chunk(size_t n);
    void    fail(const std::string& why);

    std::string                              name_;
    std::vector<std::vector<TrackSource>>    inputs_;
    std::vector<std::unique_ptr<AudioInput>> owned_;
    size_t                                   ch_;
    std::unique_ptr<AudioFilterChain>        chain_;
    std::unique_ptr<NoiseGate>               gate_;
    bool                                     failed_ = false;
    int64_t                                  origin_ = 0;      // позиція, з якої почали (= 0 у фільтрі)
    int64_t                                  fed_ = 0;         // до якої позиції подано на входи
    std::vector<float>                       buf_;             // результат (ch_ каналів) від buf_start_
    int64_t                                  buf_start_ = 0;
    int64_t                                  gated_end_ = 0;   // до цієї позиції гейт уже відпрацював
    std::vector<float>                       tmp_, mono_;
};

struct AudioTrackPlan {
    std::string              title;
    std::vector<TrackSource> sources;
    std::string              post_filter;   // фільтри FFmpeg для готової доріжки (loudnorm); порожньо — без
};

// Змішує кілька доріжок з набору джерел і віддає результат шматками.
class AudioMixer {
public:
    AudioMixer(std::vector<std::unique_ptr<AudioInput>> inputs, std::vector<AudioTrackPlan> tracks);

    size_t track_count() const { return tracks_.size(); }
    const AudioTrackPlan& track(size_t i) const { return tracks_[i]; }
    int64_t position() const { return pos_; }

    // Скільки можна змішати зараз (мінімум доступності всіх джерел).
    int64_t ready_until();
    // Змішати [position, until) і для кожної доріжки викликати sink(track, дані, кадри).
    // Доріжка з post_filter віддається із затримкою фільтра — решту віддасть flush().
    template <class Sink>
    void produce(int64_t until, Sink&& sink) {
        std::vector<float> buf;
        while (pos_ < until) {
            const size_t n = static_cast<size_t>(std::min<int64_t>(until - pos_, 4096));
            for (size_t t = 0; t < tracks_.size(); ++t) {
                buf.assign(n * 2, 0.0f);
                for (const auto& src : tracks_[t].sources) src.input->mix(pos_, buf.data(), n, src.gain);
                if (post_[t]) {
                    std::string err;
                    if (post_[t]->push(0, buf.data(), n, &err)) {
                        emit_post(t, pos_ + static_cast<int64_t>(n), sink);
                        continue;
                    }
                    drop_post(t, err, sink);
                }
                soft_limit(buf.data(), buf.size());
                sink(t, buf.data(), n);
                emitted_[t] = pos_ + static_cast<int64_t>(n);
            }
            pos_ += static_cast<int64_t>(n);
            for (auto& in : inputs_) in->discard_before(pos_);
        }
    }
    // Кінець: віддати те, що ще тримають фільтри доріжок, рівно до total кадрів.
    template <class Sink>
    void flush(int64_t total, Sink&& sink) {
        for (size_t t = 0; t < tracks_.size(); ++t) {
            if (post_[t]) {
                std::string err;
                if (post_[t]->finish(&err)) emit_post(t, total, sink);
                else drop_post(t, err, sink);
            }
            pad_to(t, total, sink);
        }
    }
    void set_finished();
    std::vector<std::unique_ptr<AudioInput>>& inputs() { return inputs_; }

private:
    static void soft_limit(float* b, size_t n);

    template <class Sink>
    void emit_post(size_t t, int64_t limit, Sink& sink) {
        AudioFilterChain& c = *post_[t];
        const int64_t from = std::max(emitted_[t], c.out_start());
        const int64_t to = std::min(c.out_end(), limit);
        if (from > emitted_[t]) pad_to(t, from, sink);
        if (to <= from) return;
        out_.assign(c.out_at(from), c.out_at(to));
        soft_limit(out_.data(), out_.size());
        sink(t, out_.data(), static_cast<size_t>(to - from));
        emitted_[t] = to;
        c.discard_before(to);
    }
    // Фільтр доріжки зламався: те, що він тримав, стає тишею, далі — без фільтра.
    template <class Sink>
    void drop_post(size_t t, const std::string& err, Sink& sink) {
        log_post_failure(t, err);
        post_[t].reset();
        pad_to(t, pos_, sink);
    }
    template <class Sink>
    void pad_to(size_t t, int64_t end, Sink& sink) {
        while (emitted_[t] < end) {
            const size_t n = static_cast<size_t>(std::min<int64_t>(end - emitted_[t], 4096));
            out_.assign(n * 2, 0.0f);
            sink(t, out_.data(), n);
            emitted_[t] += static_cast<int64_t>(n);
        }
    }
    void log_post_failure(size_t t, const std::string& err) const;

    std::vector<std::unique_ptr<AudioInput>>        inputs_;
    std::vector<AudioTrackPlan>                     tracks_;
    std::vector<std::unique_ptr<AudioFilterChain>>  post_;
    std::vector<int64_t>                            emitted_;   // скільки кадрів віддано по кожній доріжці
    std::vector<float>                              out_;
    int64_t                                         pos_ = 0;
};

} // namespace gmdr::audio
