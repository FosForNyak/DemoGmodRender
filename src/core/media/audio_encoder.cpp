#include "audio_encoder.hpp"

#include "../util/log.hpp"
#include "../util/strings.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace gmdr::media {

bool AudioEncoder::open(const AudioEncoderSettings& s, bool global_header, std::string* error) {
    s_ = s;
    const AVCodec* codec = avcodec_find_encoder_by_name(s.codec.c_str());
    if (!codec) {
        if (error) *error = std::format("аудіокодек '{}' відсутній у цій збірці FFmpeg", s.codec);
        return false;
    }
    if (codec->type != AVMEDIA_TYPE_AUDIO) {
        if (error) *error = std::format("'{}' — не аудіокодек", s.codec);
        return false;
    }
    ctx_.reset(avcodec_alloc_context3(codec));
    AVCodecContext* c = ctx_.get();

    // Частота: бажана, або найближча підтримувана
    int rate = s.sample_rate > 0 ? s.sample_rate : 48000;
    auto rates = codec_sample_rates(codec);
    if (!rates.empty() && std::find(rates.begin(), rates.end(), rate) == rates.end()) {
        int best = rates.front();
        for (int r : rates)
            if (std::abs(r - rate) < std::abs(best - rate)) best = r;
        log_warn("Кодек {} не підтримує {} Гц — використовую {} Гц", s.codec, rate, best);
        rate = best;
    }
    c->sample_rate = rate;
    c->time_base = AVRational{1, rate};
    av_channel_layout_default(&c->ch_layout, std::clamp(s.channels, 1, 8));

    // Формат семплів: float > s32 > s16
    auto fmts = codec_sample_fmts(codec);
    AVSampleFormat fmt = fmts.empty() ? AV_SAMPLE_FMT_FLTP : fmts.front();
    const AVSampleFormat pref[] = {AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_S32P, AV_SAMPLE_FMT_S32,
                                   AV_SAMPLE_FMT_S16P, AV_SAMPLE_FMT_S16};
    for (auto p : pref)
        if (std::find(fmts.begin(), fmts.end(), p) != fmts.end()) {
            fmt = p;
            break;
        }
    if (!s.sample_fmt.empty()) {
        const AVSampleFormat want = av_get_sample_fmt(s.sample_fmt.c_str());
        const AVSampleFormat want_p = av_get_planar_sample_fmt(want);
        for (auto f : fmts)
            if (want != AV_SAMPLE_FMT_NONE && (f == want || f == want_p)) {
                fmt = f;
                break;
            }
    }
    c->sample_fmt = fmt;
    const bool lossless = s.codec.rfind("pcm_", 0) == 0 || s.codec == "flac" || s.codec == "alac" ||
                          s.codec == "wavpack" || s.codec == "tta";
    if (!lossless && s.bitrate > 0) c->bit_rate = s.bitrate;
    if (fmt == AV_SAMPLE_FMT_S32 || fmt == AV_SAMPLE_FMT_S32P) {
        if (s.codec == "pcm_s24le" || s.codec == "pcm_s24be" || s.codec == "flac" || s.codec == "alac")
            c->bits_per_raw_sample = 24;
    }
    if (global_header) c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (codec->capabilities & AV_CODEC_CAP_EXPERIMENTAL) c->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;

    AVDictionary* opts = nullptr;
    for (const auto& [k, v] : s.options) av_dict_set(&opts, k.c_str(), v.c_str(), 0);
    int r = avcodec_open2(c, codec, &opts);
    av_dict_free(&opts);
    if (r < 0) {
        if (error) *error = std::format("не вдалося відкрити аудіокодек {}: {}", s.codec, av_error_string(r));
        return false;
    }
    frame_size_ = (codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE) || c->frame_size <= 0 ? 1024 : c->frame_size;

    // Перетворювач: стерео float 48 кГц -> формат кодека
    AVChannelLayout in_layout;
    av_channel_layout_default(&in_layout, 2);
    SwrContext* swr = nullptr;
    r = swr_alloc_set_opts2(&swr, &c->ch_layout, c->sample_fmt, c->sample_rate, &in_layout, AV_SAMPLE_FMT_FLT,
                            kMixRate, 0, nullptr);
    if (r < 0 || !swr || swr_init(swr) < 0) {
        swr_free(&swr);
        if (error) *error = "не вдалося налаштувати swresample";
        return false;
    }
    swr_.reset(swr);
    fifo_.reset(av_audio_fifo_alloc(c->sample_fmt, c->ch_layout.nb_channels, frame_size_ * 4));
    frame_ = make_frame();
    pkt_ = make_packet();
    return true;
}

std::string AudioEncoder::describe() const {
    if (!ctx_) return "—";
    return std::format("{} {} Гц {} кан.{}", s_.codec, ctx_->sample_rate, ctx_->ch_layout.nb_channels,
                       ctx_->bit_rate > 0 ? std::format(" {} кбіт/с", ctx_->bit_rate / 1000) : std::string());
}

bool AudioEncoder::push(const float* in, size_t frames, const PacketSink& sink, std::string* error) {
    if (!ctx_) return false;
    const int out_max = swr_get_out_samples(swr_.get(), static_cast<int>(frames));
    if (out_max > 0) {
        uint8_t** out = nullptr;
        int linesize = 0;
        if (av_samples_alloc_array_and_samples(&out, &linesize, ctx_->ch_layout.nb_channels, out_max, ctx_->sample_fmt, 0) < 0) {
            if (error) *error = "немає пам'яті для аудіо";
            return false;
        }
        const uint8_t* in_planes[1] = {reinterpret_cast<const uint8_t*>(in)};
        const int got = swr_convert(swr_.get(), out, out_max, in ? in_planes : nullptr, static_cast<int>(frames));
        if (got > 0) av_audio_fifo_write(fifo_.get(), reinterpret_cast<void**>(out), got);
        av_freep(&out[0]);
        av_freep(&out);
        if (got < 0) {
            if (error) *error = "помилка swresample";
            return false;
        }
    }
    return encode_from_fifo(false, sink, error);
}

bool AudioEncoder::send(AVFrame* f, const PacketSink& sink, std::string* error) {
    int r = avcodec_send_frame(ctx_.get(), f);
    if (r < 0 && r != AVERROR_EOF) {
        if (error) *error = "помилка кодування аудіо: " + av_error_string(r);
        return false;
    }
    for (;;) {
        r = avcodec_receive_packet(ctx_.get(), pkt_.get());
        if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) return true;
        if (r < 0) {
            if (error) *error = "помилка аудіопакета: " + av_error_string(r);
            return false;
        }
        const bool ok = sink(pkt_.get());
        av_packet_unref(pkt_.get());
        if (!ok) return false;
    }
}

bool AudioEncoder::encode_from_fifo(bool final_flush, const PacketSink& sink, std::string* error) {
    for (;;) {
        const int avail = av_audio_fifo_size(fifo_.get());
        if (avail <= 0) return true;
        if (avail < frame_size_ && !final_flush) return true;
        const int n = std::min(avail, frame_size_);
        av_frame_unref(frame_.get());
        frame_->nb_samples = n;
        frame_->format = ctx_->sample_fmt;
        frame_->sample_rate = ctx_->sample_rate;
        av_channel_layout_copy(&frame_->ch_layout, &ctx_->ch_layout);
        if (av_frame_get_buffer(frame_.get(), 0) < 0) {
            if (error) *error = "немає пам'яті для аудіокадру";
            return false;
        }
        av_audio_fifo_read(fifo_.get(), reinterpret_cast<void**>(frame_->data), n);
        // Останній неповний кадр для кодеків з фіксованим розміром — доповнюємо тишею
        if (n < frame_size_ && ctx_->frame_size > 0 &&
            !(ctx_->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE) &&
            !(ctx_->codec->capabilities & AV_CODEC_CAP_SMALL_LAST_FRAME)) {
            FramePtr padded = make_frame();
            padded->nb_samples = frame_size_;
            padded->format = ctx_->sample_fmt;
            padded->sample_rate = ctx_->sample_rate;
            av_channel_layout_copy(&padded->ch_layout, &ctx_->ch_layout);
            av_frame_get_buffer(padded.get(), 0);
            av_samples_set_silence(padded->data, 0, frame_size_, ctx_->ch_layout.nb_channels, ctx_->sample_fmt);
            av_samples_copy(padded->data, frame_->data, 0, 0, n, ctx_->ch_layout.nb_channels, ctx_->sample_fmt);
            padded->pts = next_pts_;
            next_pts_ += frame_size_;
            if (!send(padded.get(), sink, error)) return false;
            continue;
        }
        frame_->pts = next_pts_;
        next_pts_ += n;
        if (!send(frame_.get(), sink, error)) return false;
    }
}

bool AudioEncoder::flush(const PacketSink& sink, std::string* error) {
    if (!ctx_) return true;
    // Злити залишок з swresample
    if (!push(nullptr, 0, sink, error)) return false;
    if (!encode_from_fifo(true, sink, error)) return false;
    return send(nullptr, sink, error);
}

} // namespace gmdr::media
