#include "derived.hpp"

#include "../media/ffmpeg_util.hpp"
#include "../media/muxer.hpp"
#include "../util/file_util.hpp"
#include "../util/strings.hpp"
#include "../util/i18n.hpp"

extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
}

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <functional>

namespace gmdr::render {

using namespace media;

namespace {

struct GraphDeleter { void operator()(AVFilterGraph* g) const { avfilter_graph_free(&g); } };
using GraphPtr = std::unique_ptr<AVFilterGraph, GraphDeleter>;

// Декодер відеодоріжки файлу + ланцюжок фільтрів над кадрами.
class VideoPipe {
public:
    bool open(const std::string& path, std::string* error) {
        AVFormatContext* ic = nullptr;
        int r = avformat_open_input(&ic, path.c_str(), nullptr, nullptr);
        if (r < 0) return fail(error, tr("не вдалося відкрити відео: ") + av_error_string(r));
        in_.reset(ic);
        if (avformat_find_stream_info(ic, nullptr) < 0) return fail(error, tr("не вдалося прочитати відео"));
        stream_ = av_find_best_stream(ic, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (stream_ < 0) return fail(error, tr("у файлі немає відео"));
        const AVStream* st = ic->streams[stream_];
        const AVCodec* dec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!dec) return fail(error, tr("немає декодера для цього відео"));
        dec_.reset(avcodec_alloc_context3(dec));
        avcodec_parameters_to_context(dec_.get(), st->codecpar);
        dec_->thread_count = 0;
        if ((r = avcodec_open2(dec_.get(), dec, nullptr)) < 0) return fail(error, tr("декодер: ") + av_error_string(r));
        return true;
    }

    double duration() const {
        const AVStream* st = in_->streams[stream_];
        if (st->duration > 0) return st->duration * av_q2d(st->time_base);
        return in_->duration > 0 ? in_->duration / static_cast<double>(AV_TIME_BASE) : 0;
    }

    // Ланцюжок фільтрів: вхід — кадри декодера, вихід — out_fmt (вхід і вихід без міток).
    bool set_filters(const std::string& chain, AVPixelFormat out_fmt, std::string* error) {
        graph_.reset(avfilter_graph_alloc());
        const AVStream* st = in_->streams[stream_];
        const std::string args =
            std::format("video_size={}x{}:pix_fmt={}:time_base={}/{}:pixel_aspect=1/1", dec_->width, dec_->height,
                        static_cast<int>(dec_->pix_fmt), st->time_base.num, st->time_base.den);
        int r = avfilter_graph_create_filter(&src_, avfilter_get_by_name("buffer"), "in", args.c_str(), nullptr,
                                             graph_.get());
        if (r >= 0)
            r = avfilter_graph_create_filter(&sink_, avfilter_get_by_name("buffersink"), "out", nullptr, nullptr,
                                             graph_.get());
        if (r < 0) return fail(error, tr("фільтри: ") + av_error_string(r));
        const std::string full = chain + std::format(",format={}", av_get_pix_fmt_name(out_fmt));
        AVFilterInOut* outputs = avfilter_inout_alloc();
        AVFilterInOut* inputs = avfilter_inout_alloc();
        outputs->name = av_strdup("in");
        outputs->filter_ctx = src_;
        inputs->name = av_strdup("out");
        inputs->filter_ctx = sink_;
        r = avfilter_graph_parse_ptr(graph_.get(), full.c_str(), &inputs, &outputs, nullptr);
        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);
        if (r >= 0) r = avfilter_graph_config(graph_.get(), nullptr);
        if (r < 0) return fail(error, tr("фільтри (") + full + "): " + av_error_string(r));
        return true;
    }

    int in_w() const { return dec_->width; }
    int out_w() const { return av_buffersink_get_w(sink_); }
    int out_h() const { return av_buffersink_get_h(sink_); }
    AVRational out_time_base() const { return av_buffersink_get_time_base(sink_); }

    // Декодувати відрізок [from, to) секунд, пропустити через фільтри; on_frame — кожен
    // кадр на виході (false — досить). Наприкінці фільтри отримують EOF (palettegen, thumbnail).
    bool run(double from, double to, const std::function<bool(AVFrame*)>& on_frame, std::string* error) {
        const AVStream* st = in_->streams[stream_];
        if (from > 0) {
            const int64_t ts = static_cast<int64_t>(from / av_q2d(st->time_base));
            av_seek_frame(in_.get(), stream_, ts, AVSEEK_FLAG_BACKWARD);
            avcodec_flush_buffers(dec_.get());
        }
        PacketPtr pkt = make_packet();
        FramePtr frame = make_frame(), out = make_frame();
        bool more = true, stop = false;
        auto drain = [&]() -> bool {
            for (;;) {
                const int r = av_buffersink_get_frame(sink_, out.get());
                if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) return true;
                if (r < 0) return fail(error, tr("фільтри: ") + av_error_string(r));
                if (!stop && !on_frame(out.get())) stop = true;
                av_frame_unref(out.get());
            }
        };
        auto feed = [&](AVFrame* f) -> bool {
            const double t = f->best_effort_timestamp * av_q2d(st->time_base);
            if (t < from - 1e-6) return true;
            if (t >= to) {
                more = false;
                return true;
            }
            f->pts = f->best_effort_timestamp;
            if (av_buffersrc_add_frame_flags(src_, f, AV_BUFFERSRC_FLAG_KEEP_REF) < 0)
                return fail(error, tr("фільтри не прийняли кадр"));
            return drain();
        };
        while (more && !stop) {
            const int r = av_read_frame(in_.get(), pkt.get());
            if (r < 0) break;
            if (pkt->stream_index == stream_) {
                if (avcodec_send_packet(dec_.get(), pkt.get()) < 0) {
                    av_packet_unref(pkt.get());
                    continue;
                }
                while (more && avcodec_receive_frame(dec_.get(), frame.get()) == 0) {
                    const bool ok = feed(frame.get());
                    av_frame_unref(frame.get());
                    if (!ok) return false;
                }
            }
            av_packet_unref(pkt.get());
        }
        if (more && !stop) {   // кінець файлу: кадри, що лишились у декодері
            avcodec_send_packet(dec_.get(), nullptr);
            while (more && avcodec_receive_frame(dec_.get(), frame.get()) == 0) {
                const bool ok = feed(frame.get());
                av_frame_unref(frame.get());
                if (!ok) return false;
            }
        }
        if (av_buffersrc_add_frame_flags(src_, nullptr, 0) < 0) return fail(error, tr("фільтри не завершились"));
        return drain();
    }

private:
    static bool fail(std::string* error, const std::string& msg) {
        if (error) *error = msg;
        return false;
    }

    InputCtxPtr      in_;
    CodecCtxPtr      dec_;
    int              stream_ = -1;
    GraphPtr         graph_;
    AVFilterContext* src_ = nullptr;
    AVFilterContext* sink_ = nullptr;
};

bool fail(std::string* error, const std::string& msg) {
    if (error) *error = msg;
    return false;
}

} // namespace

bool make_thumbnail(const std::string& video, const std::string& out_jpg, double at_seconds, int max_w, int max_h,
                    std::string* error) {
    VideoPipe pipe;
    if (!pipe.open(video, error)) return false;
    const double dur = pipe.duration();
    double at = at_seconds >= 0 ? at_seconds : dur / 2;
    if (dur > 0) at = std::clamp(at, 0.0, std::max(0.0, dur - 0.1));
    // thumbnail вибирає найтиповіший кадр із ~3 с (без розмитих і перехідних кадрів)
    const double from = std::max(0.0, at - 1.5), to = dur > 0 ? std::min(dur, at + 1.5) : at + 1.5;
    const std::string chain = std::format(
        "thumbnail=n=100,scale=w={}:h={}:force_original_aspect_ratio=decrease:force_divisible_by=2:flags=lanczos",
        max_w, max_h);
    if (!pipe.set_filters(chain, AV_PIX_FMT_YUVJ420P, error)) return false;

    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    if (!codec) return fail(error, tr("немає кодека JPEG"));
    CodecCtxPtr enc;
    std::vector<uint8_t> jpeg;
    std::string enc_err;
    const bool ok = pipe.run(from, to, [&](AVFrame* f) {
        enc.reset(avcodec_alloc_context3(codec));
        enc->width = f->width;
        enc->height = f->height;
        enc->pix_fmt = AV_PIX_FMT_YUVJ420P;
        enc->time_base = {1, 25};
        enc->color_range = AVCOL_RANGE_JPEG;
        enc->flags |= AV_CODEC_FLAG_QSCALE;
        enc->global_quality = FF_QP2LAMBDA * 2;   // q=2: майже без втрат
        enc->strict_std_compliance = FF_COMPLIANCE_UNOFFICIAL;
        int r = avcodec_open2(enc.get(), codec, nullptr);
        if (r < 0) {
            enc_err = "JPEG: " + av_error_string(r);
            return false;
        }
        f->pts = 0;
        f->quality = enc->global_quality;
        PacketPtr pkt = make_packet();
        if (avcodec_send_frame(enc.get(), f) >= 0 && avcodec_send_frame(enc.get(), nullptr) >= 0 &&
            avcodec_receive_packet(enc.get(), pkt.get()) == 0)
            jpeg.assign(pkt->data, pkt->data + pkt->size);
        return false;   // потрібен лише один кадр
    }, error);
    if (!ok) return false;
    if (jpeg.empty()) return fail(error, enc_err.empty() ? tr("не вдалося взяти кадр для обкладинки") : enc_err);
    std::ofstream f(path_from_utf8(out_jpg), std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(jpeg.data()), static_cast<std::streamsize>(jpeg.size()));
    if (!f) return fail(error, tr("не вдалося записати ") + out_jpg);
    return true;
}

bool make_animation(const std::string& video, const std::string& out_path, AnimFormat format, double max_seconds,
                    int width, int fps, std::string* error) {
    VideoPipe pipe;
    if (!pipe.open(video, error)) return false;
    const bool gif = format == AnimFormat::Gif;
    const char* codec_name = gif ? "gif" : "libwebp_anim";
    const AVCodec* codec = avcodec_find_encoder_by_name(codec_name);
    if (!codec) return fail(error, trf("кодек {} відсутній у цій збірці FFmpeg", codec_name));
    // GIF: палітра на 256 кольорів саме під це відео; "rectangle" — перераховуються лише змінені області
    width = std::max(2, std::min(width, pipe.in_w()) & ~1);   // не більше за саме відео
    const std::string scale = std::format("fps={},scale={}:-2:flags=lanczos", fps, width);
    const std::string chain =
        gif ? scale + ",split[a][b];[a]palettegen=stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=4:"
                      "diff_mode=rectangle"
            : scale;
    if (!pipe.set_filters(chain, gif ? AV_PIX_FMT_PAL8 : AV_PIX_FMT_YUV420P, error)) return false;

    Muxer mux;
    if (!mux.open(out_path, gif ? "gif" : "webp", error)) return false;
    mux.set_format_option("loop", "0");   // по колу (у webp типово — один раз)
    CodecCtxPtr enc(avcodec_alloc_context3(codec));
    enc->width = pipe.out_w();
    enc->height = pipe.out_h();
    enc->pix_fmt = gif ? AV_PIX_FMT_PAL8 : AV_PIX_FMT_YUV420P;
    enc->time_base = pipe.out_time_base();
    enc->framerate = {fps, 1};
    if (!gif) av_opt_set(enc->priv_data, "quality", "70", 0);
    if (mux.needs_global_header()) enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    int r = avcodec_open2(enc.get(), codec, nullptr);
    if (r < 0) return fail(error, std::format("{}: {}", codec_name, av_error_string(r)));
    const int stream = mux.add_stream(enc.get(), "GMod demo");
    if (!mux.write_header(false, false, error)) return false;
    PacketPtr pkt = make_packet();
    std::string werr;
    auto pull = [&]() {
        while (avcodec_receive_packet(enc.get(), pkt.get()) == 0) {
            if (!mux.write_packet(stream, pkt.get(), enc->time_base)) werr = mux.last_error();
            av_packet_unref(pkt.get());
        }
    };
    const bool ok = pipe.run(0, max_seconds, [&](AVFrame* f) {
        if (avcodec_send_frame(enc.get(), f) < 0) {
            werr = tr("кодек не прийняв кадр");
            return false;
        }
        pull();
        return werr.empty();
    }, error);
    if (!ok) {
        mux.abort();
        return false;
    }
    avcodec_send_frame(enc.get(), nullptr);
    pull();
    if (!werr.empty()) {
        mux.abort();
        return fail(error, werr);
    }
    return mux.finish(error);
}

} // namespace gmdr::render
