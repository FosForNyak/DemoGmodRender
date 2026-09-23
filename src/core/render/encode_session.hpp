// =============================================================================
//  encode_session.hpp — "конвеєр" кодування: під-кадри -> motion blur ->
//  відеокодек -> контейнер, плюс змішування і кодування звуку.
//
//  Етапи розведені по потоках: змішування під-кадрів іде в потоці, що подає
//  кадри (разом із читанням наступних), а перетворення кольору, кодування і
//  звук — в окремому потоці кодера. Між ними — коротка черга, тож кожен етап
//  працює на своєму ядрі, а не по черзі.
// =============================================================================
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../audio/audio_inputs.hpp"
#include "../frames/blender.hpp"
#include "../media/audio_encoder.hpp"
#include "../media/muxer.hpp"
#include "../media/video_encoder.hpp"
#include "../util/thread_pool.hpp"
#include "overlay.hpp"

namespace gmdr::render {

// Додаткова версія того самого відео (інший розмір, кодек, кадрування) — кодується з тих
// самих кадрів одночасно з основною. Звук — загальний мікс.
struct ExtraOutput {
    std::string                 label;         // для журналу: "Discord (до 10 МБ)"
    std::string                 output_path;   // UTF-8
    std::string                 container;     // порожньо — за розширенням
    media::VideoEncoderSettings video;
    media::AudioEncoderSettings audio;
};

struct EncodeSettings {
    media::VideoEncoderSettings video;
    int                         motion_blur_samples = 1;   // під-кадрів на кадр
    double                      shutter_degrees = 180.0;
    bool                        audio_enabled = true;
    media::AudioEncoderSettings audio;
    bool                        separate_tracks = false;   // окремі доріжки для монтажу
    std::string                 output_path;               // UTF-8
    std::string                 container;                 // порожньо — за розширенням
    bool                        faststart = true;
    bool                        crash_safe = false;        // MP4/MOV фрагментами (відкривається навіть після збою)
    std::vector<ExtraOutput>    extras;                    // додаткові версії
    std::string                 stems_dir;                 // пакет для монтажу: окремі WAV кожного джерела (UTF-8)
};

// Які джерела звуку змішувати
struct AudioSourcesSpec {
    bool                                    game_audio = true;
    std::filesystem::path                   game_wav;
    bool                                    game_wav_live = false;
    double                                  game_offset = 0.0;
    float                                   game_gain = 1.0f;
    std::vector<const voice::SpeakerTrack*> voices;
    std::vector<float>                      voice_gains;   // гучність кожного голосу (порожньо — усі 1.0)
    int64_t                                 voice_origin_sample = 0;   // семпл (48 кГц) демо, що = початок відео
    double                                  voice_delay = 0.0;
    float                                   voice_gain = 1.0f;
    std::filesystem::path                   mic_file;
    double                                  mic_offset = 0.0;
    float                                   mic_gain = 1.0f;
    std::vector<audio::VoiceCleanup>        voice_cleanup;         // обробка кожного голосу (порожньо — без)
    bool                                    duck_game = false;     // приглушувати гру, коли говорять
    double                                  loudness_target = 0;   // LUFS загального міксу; 0 — не змінювати
    double                                  speed = 1.0;           // швидкість відео (0.5 — уповільнення); звук — atempo
    bool                                    speed_mute = false;    // при зміні швидкості — без звуку
};

// Маленька копія поточного кадру для живого прев'ю у вікні програми.
struct PreviewFrame {
    int                  width = 0, height = 0;
    std::vector<uint8_t> rgba;       // RGBA, рядки зверху вниз
    uint64_t             serial = 0; // зростає з кожним новим кадром
};

class PreviewSink {
public:
    void set(int w, int h, std::vector<uint8_t>&& rgba);
    // Скопіювати кадр, якщо він новіший за have_serial.
    bool get_if_newer(uint64_t have_serial, PreviewFrame& out) const;

private:
    mutable std::mutex mutex_;
    PreviewFrame       frame_;
};

// Середній час етапів на кадр (мс) — щоб було видно, що гальмує.
struct PipelineStats {
    int64_t subframes = 0, frames = 0;
    double  blend_ms = 0;     // на під-кадр
    double  convert_ms = 0;   // на кадр відео
    double  encode_ms = 0;    // на кадр відео (кодек + запис у файл)
    double  audio_ms = 0;     // на кадр відео
    int     queue = 0;        // кадрів у черзі до кодера
};

class EncodeSession {
public:
    EncodeSession(EncodeSettings s, ThreadPool* pool, PreviewSink* preview = nullptr);
    ~EncodeSession();

    // Створити файл і кодеки, коли відомий розмір кадрів гри.
    bool begin(int frame_w, int frame_h, const AudioSourcesSpec& audio, std::string* error);
    bool started() const { return started_; }

    bool push_subframe(frames::Image&& img, std::string* error);
    // Прокачати звук до поточної тривалості відео (скільки дозволяють джерела).
    bool pump_audio(std::string* error);
    // Позначити, що гра більше не пише звук (WAV завершено).
    void game_audio_finished();
    // Гру перезапущено після збою: звук гри далі — з wav, що починається на demo_seconds
    // (секунди часу демо від першого кадру відео).
    void game_audio_new_segment(const std::filesystem::path& wav, double demo_seconds);

    bool finish(std::string* error);
    void abort();

    int64_t     subframes_in() const { return subframes_in_; }
    int64_t     frames_encoded() const { return frames_out_.load(); }
    double      video_seconds() const;
    int64_t     audio_position() const;
    int64_t     bytes_written() const { return muxer_.bytes_written(); }
    std::string video_description() const { return video_desc_; }
    std::string audio_description() const;
    bool        game_audio_opened() const;
    const EncodeSettings& settings() const { return s_; }
    PipelineStats stats() const;
    // Додаткові версії, що записалися без помилок (після finish)
    std::vector<std::string> finished_extras() const;
    // Окремі WAV пакета для монтажу: {назва доріжки, шлях}
    struct StemFile {
        std::string title, path;
    };
    const std::vector<StemFile>& stem_files() const { return stem_files_; }
    // Підписи «хто говорить» на кадрах (після motion blur, до кодування)
    void set_overlay(std::unique_ptr<SpeakerOverlay> o) { overlay_ = std::move(o); }

private:
    struct Extra {
        ExtraOutput                          cfg;
        media::Muxer                         muxer;
        media::VideoEncoder                  video;
        int                                  video_stream = -1;
        std::unique_ptr<media::AudioEncoder> audio;
        int                                  audio_stream = -1;
        std::atomic<bool>                    ok{true};
        bool                                 finished = false;
    };
    bool open_extra(Extra& e, bool with_audio, std::string* error);
    void fail_extra(Extra& e, const std::string& error);

    bool encode_video_frame(const frames::Image& img, std::string* error);
    bool produce_audio(int64_t until, std::string* error, bool final = false);   // під audio_mutex_
    bool enqueue(frames::Image&& img, std::string* error);
    void draw_overlay(frames::Image& img);
    bool stop_worker(std::string* error);
    void worker_loop();
    void capture_preview(const frames::Image& img);

    EncodeSettings                            s_;
    ThreadPool*                               pool_;
    PreviewSink*                              preview_;
    bool                                      started_ = false;
    bool                                      finished_ = false;
    media::Muxer                              muxer_;
    media::VideoEncoder                       video_;
    std::string                               video_desc_;
    int                                       video_stream_ = -1;
    std::unique_ptr<frames::MotionBlender>    blender_;
    std::unique_ptr<audio::AudioMixer>        mixer_;
    audio::GameAudioInput*                    game_input_ = nullptr;
    std::vector<std::unique_ptr<media::AudioEncoder>> audio_encoders_;
    std::vector<int>                          audio_streams_;
    std::unique_ptr<audio::WavWriter>         side_wav_;   // для послідовності зображень
    std::vector<std::unique_ptr<Extra>>       extras_;
    std::unique_ptr<SpeakerOverlay>           overlay_;
    std::vector<std::unique_ptr<audio::WavWriter>> stem_wavs_;   // за номером доріжки мікшера (0 — мікс, без файлу)
    std::vector<StemFile>                     stem_files_;
    int64_t                                   blended_frames_ = 0;   // кадрів після motion blur (час підписів)
    int64_t                                   subframes_in_ = 0;
    std::atomic<int64_t>                      frames_out_{0};
    int                                       frame_w_ = 0, frame_h_ = 0;

    // ---- Потік кодера ----
    mutable std::mutex                        q_mutex_;
    std::condition_variable                   q_cv_;        // з'явився кадр або зупинка
    std::condition_variable                   q_space_cv_;  // звільнилось місце в черзі
    std::deque<frames::Image>                 queue_;
    size_t                                    queue_max_ = 3;
    bool                                      worker_stop_ = false;
    bool                                      worker_failed_ = false;
    std::string                               worker_error_;
    std::thread                               worker_;
    mutable std::mutex                        audio_mutex_;  // мікшер і аудіокодери

    // ---- Статистика ----
    double                                    blend_ms_ = 0;          // потік подачі кадрів
    std::atomic<double>                       convert_ms_{0}, encode_ms_{0}, audio_ms_{0};
    // ---- Прев'ю ----
    SwsContext*                               preview_sws_ = nullptr;
    int64_t                                   preview_last_ms_ = 0;
};

} // namespace gmdr::render
