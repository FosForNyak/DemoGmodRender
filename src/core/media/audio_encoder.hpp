// =============================================================================
//  audio_encoder.hpp — кодування звуку (AAC, Opus, FLAC, PCM, ALAC, MP3 ...).
//  На вхід — стерео float 48 кГц (інтерлівінг), усе інше робить swresample.
// =============================================================================
#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "ffmpeg_util.hpp"

namespace gmdr::media {



struct AudioEncoderSettings {
    std::string codec = "aac";
    int64_t     bitrate = 320000;
    int         sample_rate = 48000;
    int         channels = 2;
    std::string sample_fmt;   // напр. "s16" — примусовий формат семплів (якщо кодек його підтримує)
    std::vector<std::pair<std::string, std::string>> options;
};

class AudioEncoder {
public:
    using PacketSink = std::function<bool(AVPacket*)>;

    AudioEncoder() = default;
    bool open(const AudioEncoderSettings& s, bool global_header, std::string* error);
    // Додати семпли (стерео float 48 кГц). Кодує, коли назбирається повний кадр кодека.
    bool push(const float* interleaved, size_t frames, const PacketSink& sink, std::string* error);
    bool flush(const PacketSink& sink, std::string* error);
    // Прибрати затримку кодера (initial_padding: AAC — 1024 семпли) з самого звуку, а не з часу
    // пакетів: перші стільки семплів входу відкидаються, а час пакетів зсувається на стільки ж, тож
    // перший пакет має час 0, а декодований звук — свій час і без підказки контейнера програвачу.
    // Для контейнерів, що підказати не вміють (AVI). Викликати до першого push().
    void drop_encoder_delay();

    AVCodecContext* context() const { return ctx_.get(); }
    std::string     describe() const;

private:
    bool encode_from_fifo(bool final_flush, const PacketSink& sink, std::string* error);
    bool send(AVFrame* f, const PacketSink& sink, std::string* error);

    AudioEncoderSettings s_;
    CodecCtxPtr          ctx_;
    SwrPtr               swr_;
    FifoPtr              fifo_;
    FramePtr             frame_;
    PacketPtr            pkt_;
    int                  frame_size_ = 1024;
    int64_t              next_pts_ = 0;
    int64_t              drop_samples_ = 0;   // скільки семплів входу ще відкинути (drop_encoder_delay)
    int64_t              ts_shift_ = 0;       // зсув часу пакетів, у time_base кодека
};

} // namespace gmdr::media
