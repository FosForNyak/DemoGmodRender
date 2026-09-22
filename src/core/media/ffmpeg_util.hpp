// =============================================================================
//  ffmpeg_util.hpp — зручні обгортки над C-API бібліотек FFmpeg.
//
//  FFmpeg — це C-бібліотека, де ресурси треба звільняти вручну
//  (av_frame_free, avcodec_free_context ...). Тут ми загортаємо їх у
//  std::unique_ptr з власними "видалювачами" (deleter), щоб C++ сам
//  звільняв пам'ять (ідіома RAII).
//
//  Підтримуються FFmpeg 6.x, 7.x, 8.x та свіжий master.
// =============================================================================
#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <memory>
#include <string>
#include <vector>

namespace gmdr::media {

constexpr int kMixRate = 48000;   // внутрішня частота змішування звуку (Гц)

struct FrameDeleter   { void operator()(AVFrame* f) const { av_frame_free(&f); } };
struct PacketDeleter  { void operator()(AVPacket* p) const { av_packet_free(&p); } };
struct CodecCtxDeleter{ void operator()(AVCodecContext* c) const { avcodec_free_context(&c); } };
struct SwsDeleter     { void operator()(SwsContext* s) const { sws_freeContext(s); } };
struct SwrDeleter     { void operator()(SwrContext* s) const { swr_free(&s); } };
struct FifoDeleter    { void operator()(AVAudioFifo* f) const { av_audio_fifo_free(f); } };
struct BufferRefDeleter { void operator()(AVBufferRef* b) const { av_buffer_unref(&b); } };
struct InputCtxDeleter{ void operator()(AVFormatContext* c) const { avformat_close_input(&c); } };

using FramePtr    = std::unique_ptr<AVFrame, FrameDeleter>;
using PacketPtr   = std::unique_ptr<AVPacket, PacketDeleter>;
using CodecCtxPtr = std::unique_ptr<AVCodecContext, CodecCtxDeleter>;
using SwsPtr      = std::unique_ptr<SwsContext, SwsDeleter>;
using SwrPtr      = std::unique_ptr<SwrContext, SwrDeleter>;
using FifoPtr     = std::unique_ptr<AVAudioFifo, FifoDeleter>;
using BufferRefPtr= std::unique_ptr<AVBufferRef, BufferRefDeleter>;
using InputCtxPtr = std::unique_ptr<AVFormatContext, InputCtxDeleter>;

inline FramePtr  make_frame()  { return FramePtr(av_frame_alloc()); }
inline PacketPtr make_packet() { return PacketPtr(av_packet_alloc()); }

// Текст помилки FFmpeg за її кодом.
std::string av_error_string(int err);

// Списки підтримуваних кодеком параметрів (працює і в старих, і в нових FFmpeg).
std::vector<AVPixelFormat>  codec_pix_fmts(const AVCodec* codec);
std::vector<AVSampleFormat> codec_sample_fmts(const AVCodec* codec);
std::vector<int>            codec_sample_rates(const AVCodec* codec);

bool pix_fmt_is_hw(AVPixelFormat f);
int  pix_fmt_bit_depth(AVPixelFormat f);
// 420 / 422 / 444 (для RGB повертає 444)
int  pix_fmt_chroma(AVPixelFormat f);
bool pix_fmt_is_rgb(AVPixelFormat f);

// Приглушити/ввімкнути журнал FFmpeg і перенаправити його в наш журнал.
void install_ffmpeg_log_bridge(int ffmpeg_level = AV_LOG_WARNING);

struct EncoderInfo {
    std::string name;        // libx264, h264_nvenc ...
    std::string long_name;
    std::string codec_name;  // h264, hevc, av1 ...
    bool        is_video = true;
    bool        hardware = false;   // GPU (NVENC/AMF/QSV/VAAPI/Vulkan/...)
    std::string vendor;             // NVIDIA / AMD / Intel / ...
};
// Усі енкодери у цій збірці FFmpeg.
std::vector<EncoderInfo> list_encoders(bool video);

} // namespace gmdr::media
