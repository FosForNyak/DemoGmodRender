#include "ffmpeg_util.hpp"

#include "../util/log.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace gmdr::media {

std::string av_error_string(int err) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buf, sizeof(buf));
    return buf;
}

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
#define GMDR_HAVE_SUPPORTED_CONFIG 1
#endif

template <class T>
static std::vector<T> collect_list(const T* list, T terminator) {
    std::vector<T> out;
    if (!list) return out;
    for (const T* p = list; *p != terminator; ++p) out.push_back(*p);
    return out;
}

std::vector<AVPixelFormat> codec_pix_fmts(const AVCodec* codec) {
    if (!codec) return {};
#ifdef GMDR_HAVE_SUPPORTED_CONFIG
    const void* cfg = nullptr;
    int n = 0;
    if (avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0, &cfg, &n) >= 0 && cfg) {
        const auto* f = static_cast<const AVPixelFormat*>(cfg);
        return std::vector<AVPixelFormat>(f, f + n);
    }
    return {};
#else
    return collect_list(codec->pix_fmts, AV_PIX_FMT_NONE);
#endif
}

std::vector<AVSampleFormat> codec_sample_fmts(const AVCodec* codec) {
    if (!codec) return {};
#ifdef GMDR_HAVE_SUPPORTED_CONFIG
    const void* cfg = nullptr;
    int n = 0;
    if (avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0, &cfg, &n) >= 0 && cfg) {
        const auto* f = static_cast<const AVSampleFormat*>(cfg);
        return std::vector<AVSampleFormat>(f, f + n);
    }
    return {};
#else
    return collect_list(codec->sample_fmts, AV_SAMPLE_FMT_NONE);
#endif
}

std::vector<int> codec_sample_rates(const AVCodec* codec) {
    if (!codec) return {};
#ifdef GMDR_HAVE_SUPPORTED_CONFIG
    const void* cfg = nullptr;
    int n = 0;
    if (avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_SAMPLE_RATE, 0, &cfg, &n) >= 0 && cfg) {
        const auto* f = static_cast<const int*>(cfg);
        return std::vector<int>(f, f + n);
    }
    return {};
#else
    return collect_list(codec->supported_samplerates, 0);
#endif
}

bool pix_fmt_is_hw(AVPixelFormat f) {
    const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(f);
    return d && (d->flags & AV_PIX_FMT_FLAG_HWACCEL);
}

int pix_fmt_bit_depth(AVPixelFormat f) {
    const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(f);
    if (!d || d->nb_components == 0) return 8;
    return d->comp[0].depth;
}

bool pix_fmt_is_rgb(AVPixelFormat f) {
    const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(f);
    return d && (d->flags & AV_PIX_FMT_FLAG_RGB);
}

int pix_fmt_chroma(AVPixelFormat f) {
    const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(f);
    if (!d) return 420;
    if (d->flags & AV_PIX_FMT_FLAG_RGB) return 444;
    if (d->nb_components < 3) return 400;
    if (d->log2_chroma_w == 1 && d->log2_chroma_h == 1) return 420;
    if (d->log2_chroma_w == 1 && d->log2_chroma_h == 0) return 422;
    if (d->log2_chroma_w == 0 && d->log2_chroma_h == 0) return 444;
    return 420;
}

namespace {
std::mutex g_ff_log_mutex;
void ffmpeg_log_callback(void* avcl, int level, const char* fmt, va_list vl) {
    if (level > av_log_get_level()) return;
    char line[1024];
    static int print_prefix = 1;
    std::lock_guard lock(g_ff_log_mutex);
    av_log_format_line2(avcl, level, fmt, vl, line, sizeof(line), &print_prefix);
    std::string s(line);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    if (s.empty()) return;
    if (level <= AV_LOG_ERROR) log_message(LogLevel::Warn, "FFmpeg: " + s);
    else if (level <= AV_LOG_WARNING) log_message(LogLevel::Debug, "FFmpeg: " + s);
    else log_message(LogLevel::Debug, "FFmpeg: " + s);
}
} // namespace

void install_ffmpeg_log_bridge(int ffmpeg_level) {
    av_log_set_level(ffmpeg_level);
    av_log_set_callback(ffmpeg_log_callback);
}

std::vector<EncoderInfo> list_encoders(bool video) {
    std::vector<EncoderInfo> out;
    void* it = nullptr;
    const AVCodec* c = nullptr;
    while ((c = av_codec_iterate(&it))) {
        if (!av_codec_is_encoder(c)) continue;
        if (video && c->type != AVMEDIA_TYPE_VIDEO) continue;
        if (!video && c->type != AVMEDIA_TYPE_AUDIO) continue;
        EncoderInfo e;
        e.name = c->name;
        e.long_name = c->long_name ? c->long_name : "";
        e.codec_name = avcodec_get_name(c->id);
        e.is_video = c->type == AVMEDIA_TYPE_VIDEO;
        const std::string n = e.name;
        auto has = [&](const char* s) { return n.find(s) != std::string::npos; };
        if (has("nvenc")) { e.hardware = true; e.vendor = "NVIDIA"; }
        else if (has("_amf")) { e.hardware = true; e.vendor = "AMD"; }
        else if (has("_qsv")) { e.hardware = true; e.vendor = "Intel"; }
        else if (has("_vaapi")) { e.hardware = true; e.vendor = "VAAPI"; }
        else if (has("_vulkan")) { e.hardware = true; e.vendor = "Vulkan"; }
        else if (has("_d3d12va")) { e.hardware = true; e.vendor = "D3D12"; }
        else if (has("_mf")) { e.hardware = true; e.vendor = "MediaFoundation"; }
        else if (has("_videotoolbox")) { e.hardware = true; e.vendor = "Apple"; }
        else if (has("_v4l2m2m")) { e.hardware = true; e.vendor = "V4L2"; }
        out.push_back(std::move(e));
    }
    std::sort(out.begin(), out.end(), [](const EncoderInfo& a, const EncoderInfo& b) { return a.name < b.name; });
    return out;
}

} // namespace gmdr::media
