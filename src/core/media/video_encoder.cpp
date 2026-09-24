#include "video_encoder.hpp"

#include "../util/log.hpp"
#include "../util/strings.hpp"
#include "../util/i18n.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <format>
#include <thread>

namespace gmdr::media {

namespace {
bool has(const std::string& s, const char* sub) { return s.find(sub) != std::string::npos; }

} // namespace

AVPixelFormat image_pix_fmt(frames::PixelLayout l) {
    switch (l) {
    case frames::PixelLayout::BGR24: return AV_PIX_FMT_BGR24;
    case frames::PixelLayout::BGRA32: return AV_PIX_FMT_BGR0;   // альфа-канал у відео не потрібен
    case frames::PixelLayout::BGR48: return AV_PIX_FMT_BGR48LE;
    case frames::PixelLayout::YUV420P: return AV_PIX_FMT_YUV420P;
    case frames::PixelLayout::YUV422P: return AV_PIX_FMT_YUV422P;
    case frames::PixelLayout::YUV444P: return AV_PIX_FMT_YUV444P;
    case frames::PixelLayout::YUV420P16: return AV_PIX_FMT_YUV420P16LE;
    case frames::PixelLayout::YUV422P16: return AV_PIX_FMT_YUV422P16LE;
    case frames::PixelLayout::YUV444P16: return AV_PIX_FMT_YUV444P16LE;
    }
    return AV_PIX_FMT_BGR24;
}

namespace {
int scaler_flag(const std::string& scaler) {
    const std::string s = to_lower(scaler);
    if (s == "bicubic") return SWS_BICUBIC;
    if (s == "bilinear") return SWS_BILINEAR;
    if (s == "spline") return SWS_SPLINE;
    if (s == "area") return SWS_AREA;
    if (s == "point" || s == "neighbor") return SWS_POINT;
    return SWS_LANCZOS;
}

// Прапорці swscale. Без зміни розміру фільтр масштабування впливає лише на
// субдискретизацію кольору, а lanczos/accurate_rnd/full_chr лише сповільнюють:
// різниця з простим bicubic — ~48 дБ PSNR, на око не видно. Тому точний режим —
// окрема галочка.
int sws_flags_for(const std::string& scaler, bool scaling, bool accurate) {
    int f = (scaling || accurate) ? scaler_flag(scaler) : SWS_BICUBIC;
    if (accurate) f |= SWS_ACCURATE_RND | SWS_FULL_CHR_H_INP | SWS_FULL_CHR_H_INT;
    return f;
}

// FFmpeg 8+ (libswscale 9): sws_scale_frame() працює "динамічно" — усе (формат,
// розмір, матриця кольору, діапазон) береться з самих кадрів. У старіших
// версіях контекст ініціалізується явно (GMDR_SWS_LEGACY=1 — примусово, для тестів).
#if LIBSWSCALE_VERSION_MAJOR >= 9
constexpr bool kSwsDynamicAvailable = true;
#else
constexpr bool kSwsDynamicAvailable = false;
#endif
bool sws_use_legacy() {
    static const bool legacy = !kSwsDynamicAvailable || std::getenv("GMDR_SWS_LEGACY") != nullptr;
    return legacy;
}

int sws_threads(int configured) {
    const int hw = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    return std::clamp(configured > 0 ? configured : hw, 1, 32);
}
} // namespace

QualityInfo quality_info_for(const std::string& c) {
    QualityInfo q;
    const std::vector<std::string> x26x = {"ultrafast", "superfast", "veryfast", "faster", "fast",
                                           "medium",    "slow",      "slower",   "veryslow", "placebo"};
    if (c == "libx264" || c == "libx264rgb") { q = {"crf", 0, 51, 18, true, x26x, "slow"}; }
    else if (c == "libx265") { q = {"crf", 0, 51, 20, true, x26x, "medium"}; }
    else if (c == "libsvtav1") { q = {"crf", 1, 63, 28, true, {"0","1","2","3","4","5","6","7","8","9","10","11","12","13"}, "6"}; }
    else if (c == "libaom-av1") { q = {"crf", 0, 63, 28, true, {"0","1","2","3","4","5","6","7","8"}, "6"}; }
    else if (c == "librav1e") { q = {"qp", 0, 255, 80, true, {"0","1","2","3","4","5","6","7","8","9","10"}, "6"}; }
    else if (c == "libvpx-vp9") { q = {"crf", 0, 63, 24, true, {"good", "best", "realtime"}, "good"}; }
    else if (c == "libvpx") { q = {"crf", 4, 63, 10, true, {"good", "best", "realtime"}, "good"}; }
    else if (has(c, "nvenc")) {
        q = {"cq", 0, 51, 19, true, {"p1","p2","p3","p4","p5","p6","p7"}, "p5"};
        if (has(c, "av1")) { q.max = 63; q.def = 30; }
    }
    else if (has(c, "_amf")) { q = {"qp", 0, 51, 20, true, {"speed", "balanced", "quality"}, "quality"}; if (has(c, "av1")) { q.max = 255; q.def = 90; } }
    else if (has(c, "_qsv")) { q = {"global_quality", 1, 51, 21, true, {"veryfast","faster","fast","medium","slow","slower","veryslow"}, "slow"}; }
    else if (has(c, "_vaapi")) { q = {"qp", 0, 51, 20, true, {}, ""}; }
    else if (has(c, "_vulkan")) { q = {"qp", 0, 51, 20, true, {}, ""}; }
    else if (has(c, "_mf")) { q = {"quality", 0, 100, 80, false, {}, ""}; }
    else if (c == "prores_ks" || c == "prores" || c == "prores_aw") { q = {"profile", 0, 5, 3, false, {"0 proxy","1 lt","2 standard","3 hq","4 4444","5 4444xq"}, "3 hq"}; }
    else if (c == "mjpeg" || c == "mpeg4" || c == "mpeg2video" || c == "mpeg1video") { q = {"q:v", 1, 31, 2, true, {}, ""}; }
    else if (c == "libvvenc") { q = {"qp", 0, 63, 28, true, {"faster","fast","medium","slow","slower"}, "medium"}; }
    else { q = {"", 0, 0, 0, true, {}, ""}; }   // безвтратні або без керування якістю
    return q;
}

AVPixelFormat choose_pix_fmt(const AVCodec* codec, int depth, int chroma, const std::string& forced) {
    if (!forced.empty() && forced != "auto") {
        const AVPixelFormat f = av_get_pix_fmt(forced.c_str());
        if (f != AV_PIX_FMT_NONE) return f;
        log_warn("{}", trf("Невідомий формат пікселів '{}' — обираю автоматично", forced));
    }
    auto supported = codec_pix_fmts(codec);
    std::vector<AVPixelFormat> sw;
    for (auto f : supported)
        if (!pix_fmt_is_hw(f)) sw.push_back(f);
    const std::string name = codec->name;

    // Бажані формати в порядку пріоритету
    std::vector<AVPixelFormat> wanted;
    auto add = [&](std::initializer_list<AVPixelFormat> l) { wanted.insert(wanted.end(), l); };
    const bool rgb_codec = name == "png" || name == "libx264rgb" || name == "qtrle" || name == "bmp" ||
                           name == "tiff" || name == "exr" || name == "targa" || name == "gif" || name == "apng";
    if (rgb_codec) {
        if (depth > 8) add({AV_PIX_FMT_RGB48BE, AV_PIX_FMT_RGB48LE, AV_PIX_FMT_GBRPF32LE, AV_PIX_FMT_GBRP16LE});
        add({AV_PIX_FMT_RGB24, AV_PIX_FMT_BGR24, AV_PIX_FMT_BGR0, AV_PIX_FMT_RGBA, AV_PIX_FMT_GBRP});
    }
    if (chroma == 444) {
        if (depth >= 12) add({AV_PIX_FMT_YUV444P12LE, AV_PIX_FMT_GBRP12LE, AV_PIX_FMT_YUV444P16LE});
        if (depth >= 10) add({AV_PIX_FMT_YUV444P10LE, AV_PIX_FMT_GBRP10LE, AV_PIX_FMT_YUV444P16LE, AV_PIX_FMT_X2RGB10LE});
        add({AV_PIX_FMT_YUV444P, AV_PIX_FMT_GBRP, AV_PIX_FMT_BGR0, AV_PIX_FMT_RGB0});
    }
    if (chroma == 422 || chroma == 444) {
        if (depth >= 12) add({AV_PIX_FMT_YUV422P12LE});
        if (depth >= 10) add({AV_PIX_FMT_YUV422P10LE, AV_PIX_FMT_P210LE});
        add({AV_PIX_FMT_YUV422P});
    }
    if (depth >= 12) add({AV_PIX_FMT_YUV420P12LE});
    if (depth >= 10) add({AV_PIX_FMT_YUV420P10LE, AV_PIX_FMT_P010LE});
    add({AV_PIX_FMT_YUV420P, AV_PIX_FMT_NV12, AV_PIX_FMT_YUVJ420P});

    if (sw.empty() && supported.empty()) {
        // Кодек не повідомляє список — беремо перший бажаний
        return wanted.front();
    }
    for (auto w : wanted)
        if (std::find(sw.begin(), sw.end(), w) != sw.end()) return w;
    // Нічого з бажаного — найближчий за бітністю і субдискретизацією
    AVPixelFormat best = AV_PIX_FMT_NONE;
    int best_score = 1 << 30;
    for (auto f : sw) {
        const int score = std::abs(pix_fmt_bit_depth(f) - depth) * 10 + (pix_fmt_chroma(f) == chroma ? 0 : 5);
        if (score < best_score) {
            best_score = score;
            best = f;
        }
    }
    return best;
}

VideoEncoder::~VideoEncoder() { sws_freeContext(sws_); }

bool VideoEncoder::setup_hw_frames(const AVCodec* codec, std::string* error) {
    // Шукаємо конфігурацію "потрібні апаратні кадри"
    for (int i = 0;; ++i) {
        const AVCodecHWConfig* cfg = avcodec_get_hw_config(codec, i);
        if (!cfg) break;
        if (!(cfg->methods & AV_CODEC_HW_CONFIG_METHOD_HW_FRAMES_CTX)) continue;
        AVBufferRef* dev = nullptr;
        const char* dev_name = s_.hw_device.empty() ? nullptr : s_.hw_device.c_str();
        int r = av_hwdevice_ctx_create(&dev, cfg->device_type, dev_name, nullptr, 0);
        if (r < 0) {
            if (error) *error = trf("не вдалося відкрити GPU-пристрій {}: {}",
                                            av_hwdevice_get_type_name(cfg->device_type), av_error_string(r));
            continue;
        }
        hw_device_.reset(dev);
        AVBufferRef* frames_ref = av_hwframe_ctx_alloc(dev);
        auto* fc = reinterpret_cast<AVHWFramesContext*>(frames_ref->data);
        fc->format = cfg->pix_fmt;
        fc->sw_format = s_.bit_depth > 8 ? AV_PIX_FMT_P010LE : AV_PIX_FMT_NV12;
        fc->width = ctx_->width;
        fc->height = ctx_->height;
        fc->initial_pool_size = 16;
        r = av_hwframe_ctx_init(frames_ref);
        if (r < 0) {
            av_buffer_unref(&frames_ref);
            if (error) *error = tr("не вдалося створити пул GPU-кадрів: ") + av_error_string(r);
            continue;
        }
        hw_frames_.reset(frames_ref);
        ctx_->pix_fmt = cfg->pix_fmt;
        ctx_->hw_frames_ctx = av_buffer_ref(frames_ref);
        sw_fmt_ = fc->sw_format;
        return true;
    }
    if (error && error->empty()) *error = tr("кодек вимагає апаратних кадрів, але жоден GPU-пристрій не підійшов");
    return false;
}

CropRect center_crop(int w, int h, double aspect) {
    CropRect r{0, 0, w, h};
    if (aspect <= 0 || w <= 0 || h <= 0) return r;
    if (static_cast<double>(w) / h > aspect) {
        r.w = std::max(2, static_cast<int>(std::lround(h * aspect)) & ~1);
        r.x = ((w - r.w) / 2) & ~1;
    } else {
        r.h = std::max(2, static_cast<int>(std::lround(w / aspect)) & ~1);
        r.y = ((h - r.h) / 2) & ~1;
    }
    return r;
}

bool VideoEncoder::open(const VideoEncoderSettings& s, int in_w, int in_h, bool global_header, std::string* error) {
    s_ = s;
    if (s.crop_aspect > 0) {
        const CropRect c = center_crop(in_w, in_h, s.crop_aspect);
        in_w = c.w;
        in_h = c.h;
    }
    const AVCodec* codec = avcodec_find_encoder_by_name(s.codec.c_str());
    if (!codec) {
        if (error) *error = trf("кодек '{}' відсутній у цій збірці FFmpeg", s.codec);
        return false;
    }
    if (codec->type != AVMEDIA_TYPE_VIDEO) {
        if (error) *error = trf("'{}' — не відеокодек", s.codec);
        return false;
    }
    ctx_.reset(avcodec_alloc_context3(codec));
    AVCodecContext* c = ctx_.get();
    int w = s.width > 0 ? s.width : in_w;
    int h = s.height > 0 ? s.height : in_h;
    c->width = w;
    c->height = h;
    c->time_base = AVRational{s.fps.den, s.fps.num};
    c->framerate = s.fps;
    c->sample_aspect_ratio = AVRational{1, 1};
    c->thread_count = s.threads > 0 ? s.threads : 0;   // 0 — кодек сам обирає (усі ядра)
    if (s.gop_seconds > 0) c->gop_size = std::max(1, static_cast<int>(std::lround(s.fps.num / static_cast<double>(s.fps.den) * s.gop_seconds)));
    if (global_header) c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    const std::string name = codec->name;
    hardware_ = has(name, "nvenc") || has(name, "_amf") || has(name, "_qsv") || has(name, "_vaapi") ||
                has(name, "_vulkan") || has(name, "_d3d12va") || has(name, "_mf") || has(name, "_videotoolbox");

    // Формат пікселів
    auto supported = codec_pix_fmts(codec);
    const bool all_hw = !supported.empty() &&
                        std::all_of(supported.begin(), supported.end(), [](AVPixelFormat f) { return pix_fmt_is_hw(f); });
    int chroma = s.chroma;
    if ((name == "prores_ks" || name == "prores" || name == "prores_aw") && chroma == 420) chroma = 422;
    if (all_hw) {
        std::string herr;
        if (!setup_hw_frames(codec, &herr)) {
            if (error) *error = herr;
            return false;
        }
    } else {
        sw_fmt_ = choose_pix_fmt(codec, s.bit_depth, chroma, s.pix_fmt);
        if (sw_fmt_ == AV_PIX_FMT_NONE) {
            if (error) *error = tr("не вдалося підібрати формат пікселів для кодека");
            return false;
        }
        c->pix_fmt = sw_fmt_;
        // YUV 4:2:0 / 4:2:2 вимагає парних розмірів
        const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(sw_fmt_);
        if (d && (d->log2_chroma_w || d->log2_chroma_h)) {
            if (c->width & 1) c->width -= 1;
            if (d->log2_chroma_h && (c->height & 1)) c->height -= 1;
        }
    }
    const bool rgb_out = pix_fmt_is_rgb(sw_fmt_);
    c->color_range = (s.full_range || rgb_out) ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
    c->colorspace = rgb_out ? AVCOL_SPC_RGB : AVCOL_SPC_BT709;
    c->color_primaries = AVCOL_PRI_BT709;
    c->color_trc = AVCOL_TRC_BT709;

    // Параметри кодека: спершу типові, потім користувацькі (вони мають пріоритет)
    AVDictionary* opts = nullptr;
    const QualityInfo qi = quality_info_for(name);
    const int q = s.quality >= 0 ? s.quality : qi.def;
    auto set = [&](const char* k, const std::string& v) { av_dict_set(&opts, k, v.c_str(), 0); };
    std::string preset = s.preset.empty() ? qi.default_preset : s.preset;

    if (name == "libx264" || name == "libx264rgb" || name == "libx265") {
        if (!preset.empty()) set("preset", preset);
        if (s.bitrate > 0) c->bit_rate = s.bitrate;
        else set("crf", std::to_string(q));
        if (name == "libx265") set("x265-params", "log-level=error");
    } else if (name == "libsvtav1") {
        if (!preset.empty()) set("preset", preset);
        if (s.bitrate > 0) c->bit_rate = s.bitrate;
        else set("crf", std::to_string(q));
    } else if (name == "libaom-av1") {
        set("cpu-used", preset.empty() ? "6" : preset);
        set("row-mt", "1");
        if (s.bitrate > 0) c->bit_rate = s.bitrate;
        else { set("crf", std::to_string(q)); c->bit_rate = 0; }
    } else if (name == "librav1e") {
        if (!preset.empty()) set("speed", preset);
        if (s.bitrate > 0) c->bit_rate = s.bitrate;
        else set("qp", std::to_string(q));
    } else if (name == "libvpx-vp9" || name == "libvpx") {
        set("deadline", preset.empty() ? "good" : preset);
        set("cpu-used", "2");
        set("row-mt", "1");
        if (s.bitrate > 0) c->bit_rate = s.bitrate;
        else { set("crf", std::to_string(q)); c->bit_rate = name == "libvpx" ? 20000000 : 0; }
    } else if (has(name, "nvenc")) {
        if (!preset.empty()) set("preset", preset);
        set("tune", "hq");
        if (s.bitrate > 0) {
            c->bit_rate = s.bitrate;
            set("rc", "vbr");
        } else {
            set("rc", "vbr");
            set("cq", std::to_string(q));
            c->bit_rate = 0;
        }
        set("spatial-aq", "1");
    } else if (has(name, "_amf")) {
        if (!preset.empty()) set("quality", preset);
        if (s.bitrate > 0) {
            c->bit_rate = s.bitrate;
            set("rc", "vbr_peak");
        } else {
            set("rc", "cqp");
            set("qp_i", std::to_string(q));
            set("qp_p", std::to_string(q));
            if (name == "h264_amf") set("qp_b", std::to_string(q));
        }
    } else if (has(name, "_qsv")) {
        if (!preset.empty()) set("preset", preset);
        if (s.bitrate > 0) c->bit_rate = s.bitrate;
        else c->global_quality = q;
    } else if (has(name, "_vaapi") || has(name, "_vulkan")) {
        if (s.bitrate > 0) c->bit_rate = s.bitrate;
        else set("qp", std::to_string(q));
    } else if (name == "prores_ks" || name == "prores" || name == "prores_aw") {
        int profile = s.quality >= 0 ? s.quality : (pix_fmt_chroma(sw_fmt_) == 444 ? 4 : 3);
        if (pix_fmt_chroma(sw_fmt_) == 444 && profile < 4) profile = 4;
        set("profile", std::to_string(profile));
        if (name == "prores_ks") set("vendor", "apl0");
    } else if (name == "ffv1") {
        set("level", "3");
        set("slicecrc", "1");
        set("slices", "24");
        c->gop_size = 1;
    } else if (name == "mjpeg" || name == "mpeg4" || name == "mpeg2video" || name == "mpeg1video") {
        if (s.bitrate > 0) c->bit_rate = s.bitrate;
        else {
            c->flags |= AV_CODEC_FLAG_QSCALE;
            c->global_quality = FF_QP2LAMBDA * q;
        }
    } else if (name == "dnxhd") {
        set("profile", "dnxhr_hq");
    } else if (name == "libvvenc") {
        if (!preset.empty()) set("preset", preset);
        if (s.bitrate > 0) c->bit_rate = s.bitrate;
        else set("qp", std::to_string(q));
    } else {
        if (s.bitrate > 0) c->bit_rate = s.bitrate;
    }
    for (const auto& [k, v] : s.options) {
        if (k == "g") { c->gop_size = static_cast<int>(parse_int(v).value_or(c->gop_size)); continue; }
        if (k == "bf") { c->max_b_frames = static_cast<int>(parse_int(v).value_or(c->max_b_frames)); continue; }
        set(k.c_str(), v);
    }

    int r = avcodec_open2(c, codec, &opts);
    // Невідомі параметри залишаються у словнику — попереджаємо
    AVDictionaryEntry* e = nullptr;
    while ((e = av_dict_get(opts, "", e, AV_DICT_IGNORE_SUFFIX)))
        log_warn("{}", trf("Кодек {} не знає параметра '{}={}'", name, e->key, e->value));
    av_dict_free(&opts);
    if (r < 0) {
        if (error) {
            *error = trf("не вдалося відкрити кодек {}: {}", name, av_error_string(r));
            if (hardware_) *error += tr(" (перевірте, що відеокарта підтримує цей кодек і драйвер оновлено)");
        }
        return false;
    }
    frame_ = make_frame();
    frame_->format = sw_fmt_;
    frame_->width = c->width;
    frame_->height = c->height;
    frame_->color_range = c->color_range;
    frame_->colorspace = c->colorspace;
    frame_->color_primaries = c->color_primaries;
    frame_->color_trc = c->color_trc;
    if (av_frame_get_buffer(frame_.get(), 64) < 0) {
        if (error) *error = tr("не вистачає пам'яті для кадру");
        return false;
    }
    if (hw_frames_) hw_frame_ = make_frame();
    pkt_ = make_packet();
    chosen_desc_ = std::format("{} {}x{} {} {} fps{}", name, c->width, c->height,
                               av_get_pix_fmt_name(sw_fmt_) ? av_get_pix_fmt_name(sw_fmt_) : "?",
                               rational_to_string(Rational{s.fps.num, s.fps.den}), hardware_ ? " [GPU]" : "");
    return true;
}

std::string VideoEncoder::describe() const { return chosen_desc_; }

bool VideoEncoder::init_legacy_sws(const frames::Image& img, int src_w, int src_h, AVPixelFormat in_fmt,
                                   const AVFrame* dst, std::string* error) {
    sws_freeContext(sws_);
    sws_ = sws_alloc_context();
    const bool scaling = src_w != dst->width || src_h != dst->height;
    av_opt_set_int(sws_, "srcw", src_w, 0);
    av_opt_set_int(sws_, "srch", src_h, 0);
    av_opt_set_int(sws_, "src_format", in_fmt, 0);
    av_opt_set_int(sws_, "dstw", dst->width, 0);
    av_opt_set_int(sws_, "dsth", dst->height, 0);
    av_opt_set_int(sws_, "dst_format", dst->format, 0);
    av_opt_set_int(sws_, "sws_flags", sws_flags_for(s_.scaler, scaling, s_.accurate_color), 0);
    av_opt_set_int(sws_, "threads", sws_threads(s_.threads), 0);
    if (sws_init_context(sws_, nullptr, nullptr) < 0) {
        sws_freeContext(sws_);
        sws_ = nullptr;
        if (error) *error = tr("swscale: неможливе перетворення формату");
        return false;
    }
    const bool dst_rgb = pix_fmt_is_rgb(static_cast<AVPixelFormat>(dst->format));
    if (!dst_rgb) {
        // Вхід: RGB (повний діапазон) або YUV кадру JPEG (BT.601, повний діапазон);
        // вихід: YUV BT.709 (обмежений 16-235 або повний)
        const bool src_yuv = frames::is_yuv(img.layout);
        const int* in_coefs = sws_getCoefficients(src_yuv && !img.bt709 ? SWS_CS_ITU601 : SWS_CS_ITU709);
        const int* out_coefs = sws_getCoefficients(SWS_CS_ITU709);
        const int src_range = src_yuv ? (img.full_range ? 1 : 0) : 1;
        sws_setColorspaceDetails(sws_, in_coefs, src_range, out_coefs, s_.full_range ? 1 : 0, 0, 1 << 16, 1 << 16);
    }
    return true;
}

bool VideoEncoder::convert(const frames::Image& img, AVFrame* dst, std::string* error) {
    const auto t0 = std::chrono::steady_clock::now();
    const AVPixelFormat in_fmt = image_pix_fmt(img.layout);
    const bool src_yuv = frames::is_yuv(img.layout);
    if (!src_frame_) src_frame_ = make_frame();
    // Обгортка над пікселями кадру — без копіювання. Від'ємний крок рядка (TGA знизу
    // вгору) swscale розуміє сам.
    AVFrame* src = src_frame_.get();
    // Вертикальне відео тощо: лише центр кадру — зсув початку рядків, без копіювання
    const CropRect crop = center_crop(img.width, img.height, s_.crop_aspect);
    const frames::LayoutInfo li = frames::layout_info(img.layout);
    src->format = in_fmt;
    src->width = crop.w;
    src->height = crop.h;
    for (int p = 0; p < 4; ++p) {
        src->data[p] = nullptr;
        src->linesize[p] = 0;
    }
    for (int p = 0; p < img.planes(); ++p) {
        const int sx = p > 0 && li.yuv ? li.log2_chroma_w : 0, sy = p > 0 && li.yuv ? li.log2_chroma_h : 0;
        src->data[p] = const_cast<uint8_t*>(img.row(p, crop.y >> sy)) + static_cast<ptrdiff_t>(crop.x >> sx) * li.bytes_per_pixel;
        src->linesize[p] = img.stride[p];
    }
    if (src_yuv) {
        src->colorspace = img.bt709 ? AVCOL_SPC_BT709 : AVCOL_SPC_SMPTE170M;
        src->color_range = img.full_range ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
    } else {
        src->colorspace = AVCOL_SPC_RGB;
        src->color_range = AVCOL_RANGE_JPEG;
    }
    // Первинні кольори і гамма ті самі, що й у виходу: гра малює у звичайному sRGB/BT.709,
    // тож потрібна лише зміна матриці і діапазону, без перерахунку гамуту.
    src->color_primaries = dst->color_primaries;
    src->color_trc = dst->color_trc;

    if (av_frame_make_writable(dst) < 0) {
        if (error) *error = tr("кадр енкодера зайнятий");
        return false;
    }
    int r = 0;
    if (!sws_use_legacy()) {
#if LIBSWSCALE_VERSION_MAJOR >= 9
        if (!sws_) sws_ = sws_alloc_context();
        const bool scaling = crop.w != dst->width || crop.h != dst->height;
        sws_->flags = static_cast<unsigned>(sws_flags_for(s_.scaler, scaling, s_.accurate_color));
        sws_->threads = sws_threads(s_.threads);
        r = sws_scale_frame(sws_, dst, src);
#endif
    } else {
        const int matrix = src_yuv && !img.bt709 ? 601 : 709;
        const int range = src_yuv && !img.full_range ? 0 : 1;
        if (!sws_ || sws_in_w_ != crop.w || sws_in_h_ != crop.h || sws_in_fmt_ != in_fmt ||
            sws_in_range_ != range || sws_in_matrix_ != matrix) {
            if (!init_legacy_sws(img, crop.w, crop.h, in_fmt, dst, error)) return false;
            sws_in_w_ = crop.w;
            sws_in_h_ = crop.h;
            sws_in_fmt_ = in_fmt;
            sws_in_range_ = range;
            sws_in_matrix_ = matrix;
        }
        // У "класичному" режимі sws_scale_frame() робить av_frame_ref() вхідного кадру,
        // а для кадру без буфера це означало б повну копію. Тому загортаємо пікселі в
        // буфер, що нічого не звільняє.
        src->buf[0] = av_buffer_create(img.data.empty() ? nullptr : const_cast<uint8_t*>(img.data.data()),
                                       img.data.size(), [](void*, uint8_t*) {}, nullptr, AV_BUFFER_FLAG_READONLY);
        r = sws_scale_frame(sws_, dst, src);
        av_buffer_unref(&src->buf[0]);
    }
    convert_ms_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (r < 0) {
        if (error) *error = tr("помилка перетворення кольору: ") + av_error_string(r);
        return false;
    }
    return true;
}

bool VideoEncoder::send(AVFrame* frame, const PacketSink& sink, std::string* error) {
    int r = avcodec_send_frame(ctx_.get(), frame);
    if (r < 0 && r != AVERROR_EOF) {
        if (error) *error = tr("помилка кодування кадру: ") + av_error_string(r);
        return false;
    }
    for (;;) {
        r = avcodec_receive_packet(ctx_.get(), pkt_.get());
        if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) return true;
        if (r < 0) {
            if (error) *error = tr("помилка отримання пакета: ") + av_error_string(r);
            return false;
        }
        const bool ok = sink(pkt_.get());
        av_packet_unref(pkt_.get());
        if (!ok) {
            if (error && error->empty()) *error = tr("помилка запису відео у файл");
            return false;
        }
    }
}

bool VideoEncoder::encode(const frames::Image& img, int64_t pts, const PacketSink& sink, std::string* error) {
    if (!convert(img, frame_.get(), error)) return false;
    struct Timer {
        double& acc;
        std::chrono::steady_clock::time_point t;
        ~Timer() { acc += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count(); }
    } timer{encode_ms_, std::chrono::steady_clock::now()};
    frame_->pts = pts;
    if (hw_frames_) {
        av_frame_unref(hw_frame_.get());
        int r = av_hwframe_get_buffer(hw_frames_.get(), hw_frame_.get(), 0);
        if (r >= 0) r = av_hwframe_transfer_data(hw_frame_.get(), frame_.get(), 0);
        if (r < 0) {
            if (error) *error = tr("не вдалося передати кадр у відеокарту: ") + av_error_string(r);
            return false;
        }
        hw_frame_->pts = pts;
        return send(hw_frame_.get(), sink, error);
    }
    return send(frame_.get(), sink, error);
}

bool VideoEncoder::flush(const PacketSink& sink, std::string* error) {
    if (!ctx_) return true;
    return send(nullptr, sink, error);
}

} // namespace gmdr::media
