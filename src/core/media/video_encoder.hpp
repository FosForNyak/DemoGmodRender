// =============================================================================
//  video_encoder.hpp — кодування кадрів у відео будь-яким кодеком FFmpeg.
//
//  CPU: libx264, libx265, libsvtav1, libaom-av1, libvpx-vp9, prores_ks, ffv1,
//       png, utvideo ... (усі багатопотокові)
//  GPU: h264/hevc/av1_nvenc (NVIDIA), *_amf (AMD), *_qsv (Intel),
//       *_vaapi/*_vulkan (Linux) — кодування виконує відеокарта.
//
//  Перетворення кольору (RGB або YUV кадрів JPEG -> YUV BT.709) і масштабування
//  виконує swscale через sws_scale_frame() — у кілька потоків. Старий
//  sws_scale() ігнорує параметр threads і працює в одному потоці.
// =============================================================================
#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "../frames/image.hpp"
#include "ffmpeg_util.hpp"

namespace gmdr::media {

struct VideoEncoderSettings {
    std::string codec = "libx264";
    int         width = 0;              // вихідний розмір (0 — як у кадрів гри)
    int         height = 0;
    AVRational  fps{60, 1};
    std::string pix_fmt;                // порожньо — автоматичний вибір
    int         bit_depth = 8;          // 8 / 10 / 12 (для автоматичного вибору)
    int         chroma = 420;           // 420 / 422 / 444 (для автоматичного вибору)
    int         quality = -1;           // CRF / CQ / QP (-1 — типове для кодека)
    int64_t     bitrate = 0;            // біт/с (0 — режим постійної якості)
    std::string preset;                 // швидкість (medium, slow, p7 ...)
    std::vector<std::pair<std::string, std::string>> options;   // додаткові параметри кодека
    std::string scaler = "lanczos";     // фільтр масштабування (коли розмір кадру змінюється)
    bool        accurate_color = false; // максимальна точність кольору (повільніше; різниця на око не видна)
    int         threads = 0;            // 0 — усі ядра
    int         gop_seconds = 2;        // ключовий кадр кожні N секунд (0 — типово)
    bool        full_range = false;     // повний діапазон 0..255 замість 16..235
    std::string hw_device;              // для VAAPI/Vulkan: пристрій (напр. /dev/dri/renderD128)
    double      crop_aspect = 0;        // > 0: лише центр кадру з таким співвідношенням сторін (9/16 — вертикальне)
};

// Центральна частина кадру w×h зі співвідношенням сторін aspect (парні координати — для YUV 4:2:0).
struct CropRect {
    int x = 0, y = 0, w = 0, h = 0;
};
CropRect center_crop(int w, int h, double aspect);

// Типова якість і назва параметра для кодека (для GUI).
struct QualityInfo {
    std::string param;       // "crf", "cq", "qp", "global_quality", "q:v" ...
    int         min = 0, max = 51, def = 18;
    bool        lower_is_better = true;
    std::vector<std::string> presets;   // допустимі пресети швидкості
    std::string default_preset;
};
QualityInfo quality_info_for(const std::string& codec);

class VideoEncoder {
public:
    using PacketSink = std::function<bool(AVPacket*)>;

    VideoEncoder() = default;
    ~VideoEncoder();
    VideoEncoder(const VideoEncoder&) = delete;
    VideoEncoder& operator=(const VideoEncoder&) = delete;

    // in_w/in_h — розмір кадрів, що надходитимуть; global_header — вимога контейнера.
    bool open(const VideoEncoderSettings& s, int in_w, int in_h, bool global_header, std::string* error);
    bool encode(const frames::Image& img, int64_t pts, const PacketSink& sink, std::string* error);
    bool flush(const PacketSink& sink, std::string* error);

    AVCodecContext* context() const { return ctx_.get(); }
    AVPixelFormat   output_pix_fmt() const { return sw_fmt_; }
    std::string     describe() const;

    // Перетворити кадр у формат кодера (dst — кадр із розміром і форматом кодера).
    // Публічне для тестів точності кольору.
    bool convert(const frames::Image& img, AVFrame* dst, std::string* error);
    // Сумарний час етапів (мс): перетворення кольору і власне кодування.
    double convert_ms() const { return convert_ms_; }
    double encode_ms() const { return encode_ms_; }

private:
    bool setup_hw_frames(const AVCodec* codec, std::string* error);
    bool send(AVFrame* frame, const PacketSink& sink, std::string* error);
    bool init_legacy_sws(const frames::Image& img, int src_w, int src_h, AVPixelFormat in_fmt, const AVFrame* dst,
                         std::string* error);

    VideoEncoderSettings s_;
    CodecCtxPtr          ctx_;
    SwsContext*          sws_ = nullptr;
    int                  sws_in_w_ = 0, sws_in_h_ = 0;
    int                  sws_in_fmt_ = -1;
    int                  sws_in_range_ = -1, sws_in_matrix_ = -1;
    FramePtr             src_frame_;                  // обгортка над пікселями Image (без копії)
    double               convert_ms_ = 0, encode_ms_ = 0;
    AVPixelFormat        sw_fmt_ = AV_PIX_FMT_NONE;   // програмний формат, у який конвертуємо
    BufferRefPtr         hw_device_;
    BufferRefPtr         hw_frames_;
    FramePtr             frame_;
    FramePtr             hw_frame_;
    PacketPtr            pkt_;
    bool                 hardware_ = false;
    std::string          chosen_desc_;
};

// Формат FFmpeg, що відповідає розміщенню пікселів кадру.
AVPixelFormat image_pix_fmt(frames::PixelLayout l);

// Вибрати формат пікселів для кодека (з урахуванням бітності/субдискретизації).
AVPixelFormat choose_pix_fmt(const AVCodec* codec, int bit_depth, int chroma, const std::string& forced);

} // namespace gmdr::media
