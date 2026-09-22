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

struct AudioTrackPlan {
    std::string              title;
    std::vector<TrackSource> sources;
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
    template <class Sink>
    void produce(int64_t until, Sink&& sink) {
        std::vector<float> buf;
        while (pos_ < until) {
            const size_t n = static_cast<size_t>(std::min<int64_t>(until - pos_, 4096));
            for (size_t t = 0; t < tracks_.size(); ++t) {
                buf.assign(n * 2, 0.0f);
                for (const auto& src : tracks_[t].sources) src.input->mix(pos_, buf.data(), n, src.gain);
                soft_limit(buf);
                sink(t, buf.data(), n);
            }
            pos_ += static_cast<int64_t>(n);
            for (auto& in : inputs_) in->discard_before(pos_);
        }
    }
    void set_finished();
    std::vector<std::unique_ptr<AudioInput>>& inputs() { return inputs_; }

private:
    static void soft_limit(std::vector<float>& b);

    std::vector<std::unique_ptr<AudioInput>> inputs_;
    std::vector<AudioTrackPlan>              tracks_;
    int64_t                                  pos_ = 0;
};

} // namespace gmdr::audio
