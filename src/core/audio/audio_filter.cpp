#include "audio_filter.hpp"

#include "../media/ffmpeg_util.hpp"

extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
}

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <format>

namespace gmdr::audio {

using media::kMixRate;

AudioFilterChain::~AudioFilterChain() {
    avfilter_graph_free(&graph_);
    av_frame_free(&frame_);
}

bool AudioFilterChain::open(const std::string& chain, int inputs, int channels, std::string* error) {
    avfilter_graph_free(&graph_);
    src_.clear();
    sink_ = nullptr;
    chain_ = chain;
    channels_ = channels;
    pushed_.assign(static_cast<size_t>(inputs), 0);
    out_.clear();
    out_start_ = 0;
    got_output_ = false;
    AVFilterInOut* outs = nullptr;   // виходи наших abuffer = входи ланцюжка
    AVFilterInOut* ins = nullptr;    // вхід abuffersink = вихід ланцюжка
    auto fail = [&](const char* what, int r) {
        if (error)
            *error = std::format("фільтр звуку «{}»: {}{}", chain, what, r < 0 ? " (" + media::av_error_string(r) + ")" : "");
        avfilter_inout_free(&outs);
        avfilter_inout_free(&ins);
        avfilter_graph_free(&graph_);
        src_.clear();
        sink_ = nullptr;
        return false;
    };
    if (!frame_) frame_ = av_frame_alloc();
    graph_ = avfilter_graph_alloc();
    if (!graph_ || !frame_) return fail("немає пам'яті", 0);
    graph_->nb_threads = 1;   // графів може бути багато (по одному на голос) — без власних потоків
    const char* layout = channels == 1 ? "mono" : "stereo";
    const std::string args =
        std::format("time_base=1/{0}:sample_rate={0}:sample_fmt=flt:channel_layout={1}", kMixRate, layout);
    AVFilterInOut** tail = &outs;
    for (int i = 0; i < inputs; ++i) {
        const std::string name = std::format("in{}", i);
        AVFilterContext* src = nullptr;
        const int r = avfilter_graph_create_filter(&src, avfilter_get_by_name("abuffer"), name.c_str(), args.c_str(),
                                                   nullptr, graph_);
        if (r < 0) return fail("abuffer", r);
        src_.push_back(src);
        AVFilterInOut* io = avfilter_inout_alloc();
        if (!io) return fail("немає пам'яті", 0);
        io->name = av_strdup(name.c_str());
        io->filter_ctx = src;
        io->pad_idx = 0;
        io->next = nullptr;
        *tail = io;
        tail = &io->next;
    }
    int r = avfilter_graph_create_filter(&sink_, avfilter_get_by_name("abuffersink"), "out", nullptr, nullptr, graph_);
    if (r < 0) return fail("abuffersink", r);
    ins = avfilter_inout_alloc();
    if (!ins) return fail("немає пам'яті", 0);
    ins->name = av_strdup("out");
    ins->filter_ctx = sink_;
    ins->pad_idx = 0;
    ins->next = nullptr;
    // Формат виходу = формат входу. Задаємо його фільтром aformat, а не параметрами
    // abuffersink: їхні назви змінювались між версіями FFmpeg.
    const std::string desc = std::format("{}{},aformat=sample_fmts=flt:sample_rates={}:channel_layouts={}[out]",
                                         inputs == 1 ? "[in0]" : "", chain, kMixRate, layout);
    r = avfilter_graph_parse_ptr(graph_, desc.c_str(), &ins, &outs, nullptr);
    if (r < 0) return fail("не розібрано", r);
    r = avfilter_graph_config(graph_, nullptr);
    if (r < 0) return fail("не налаштовано", r);
    avfilter_inout_free(&outs);
    avfilter_inout_free(&ins);
    return true;
}

bool AudioFilterChain::push(int input, const float* data, size_t frames, std::string* error) {
    if (!graph_) return false;
    if (frames == 0) return true;
    AVFrame* f = av_frame_alloc();
    if (!f) return false;
    f->format = AV_SAMPLE_FMT_FLT;
    f->sample_rate = kMixRate;
    av_channel_layout_default(&f->ch_layout, channels_);
    f->nb_samples = static_cast<int>(frames);
    f->pts = pushed_[static_cast<size_t>(input)];
    int r = av_frame_get_buffer(f, 0);
    if (r >= 0) {
        std::memcpy(f->data[0], data, frames * static_cast<size_t>(channels_) * sizeof(float));
        r = av_buffersrc_add_frame_flags(src_[static_cast<size_t>(input)], f, 0);   // забирає дані кадру
    }
    av_frame_free(&f);
    if (r < 0) {
        if (error) *error = std::format("фільтр звуку «{}»: {}", chain_, media::av_error_string(r));
        return false;
    }
    pushed_[static_cast<size_t>(input)] += static_cast<int64_t>(frames);
    return drain(error);
}

bool AudioFilterChain::finish(std::string* error) {
    if (!graph_) return false;
    for (AVFilterContext* src : src_) (void)av_buffersrc_add_frame_flags(src, nullptr, 0);   // кінець потоку
    return drain(error);
}

bool AudioFilterChain::drain(std::string* error) {
    for (;;) {
        const int r = av_buffersink_get_frame(sink_, frame_);
        if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) return true;
        if (r < 0) {
            if (error) *error = std::format("фільтр звуку «{}»: {}", chain_, media::av_error_string(r));
            return false;
        }
        int64_t pos = out_end();
        if (frame_->pts != AV_NOPTS_VALUE) {
            const int64_t p = av_rescale_q(frame_->pts, av_buffersink_get_time_base(sink_), AVRational{1, kMixRate});
            // Мітка часу — для першого кадру і справжніх розривів. Тремтіння округлення на кілька
            // семплів (aresample після loudnorm) пропускаємо, інакше були б клацання.
            if (!got_output_ || std::llabs(p - pos) > 32) pos = p;
        }
        place(pos, reinterpret_cast<const float*>(frame_->data[0]), static_cast<size_t>(frame_->nb_samples));
        got_output_ = true;
        av_frame_unref(frame_);
    }
}

void AudioFilterChain::place(int64_t pos, const float* data, size_t frames) {
    const size_t ch = static_cast<size_t>(channels_);
    int64_t end = out_end();
    if (pos > end + 60LL * kMixRate) pos = end;   // дивна мітка часу — просто продовжуємо
    if (pos > end) {
        out_.resize(out_.size() + static_cast<size_t>(pos - end) * ch, 0.0f);
        end = pos;
    }
    // Те, що вже викинуто, пропускаємо; перекриття перезаписуємо
    int64_t skip = std::max<int64_t>(0, out_start_ - pos);
    if (skip >= static_cast<int64_t>(frames)) return;
    const int64_t at = pos + skip;
    const size_t overlap = static_cast<size_t>(std::min<int64_t>(end - at, static_cast<int64_t>(frames) - skip));
    std::memcpy(out_.data() + static_cast<size_t>(at - out_start_) * ch, data + static_cast<size_t>(skip) * ch,
                overlap * ch * sizeof(float));
    const size_t rest_from = static_cast<size_t>(skip) + overlap;
    out_.insert(out_.end(), data + rest_from * ch, data + frames * ch);
}

void AudioFilterChain::discard_before(int64_t pos) {
    const int64_t n = std::min(pos, out_end()) - out_start_;
    if (n <= 0) return;
    out_.erase(out_.begin(), out_.begin() + static_cast<ptrdiff_t>(n * channels_));
    out_start_ += n;
}

std::string denoise_filter(double noise_db) {
    const int nf = static_cast<int>(std::lround(std::clamp(noise_db, -80.0, -20.0)));
    return std::format("afftdn=nr=12:nf={}:tn=1", nf);
}

std::string duck_filter() {
    return "[in0][in1]sidechaincompress=threshold=0.02:ratio=5:attack=20:release=400:knee=3";
}

std::string loudness_filter(double target_lufs) {
    if (!(target_lufs < 0) || target_lufs < -70) return {};
    // loudnorm усередині працює на 192 кГц — повертаємо 48 кГц
    return std::format("loudnorm=I={:.1f}:TP=-1.5:LRA=11,aresample={}", target_lufs, kMixRate);
}

} // namespace gmdr::audio
