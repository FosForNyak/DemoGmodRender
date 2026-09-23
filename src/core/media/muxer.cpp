#include "muxer.hpp"

#include "../util/log.hpp"
#include "../util/strings.hpp"
#include "../util/i18n.hpp"

#include <algorithm>
#include <filesystem>
#include <format>

namespace gmdr::media {

Muxer::~Muxer() { abort(); }

bool Muxer::open(const std::string& path, const std::string& format_name, std::string* error) {
    path_ = path;
    const char* fmt_name = format_name.empty() ? nullptr : format_name.c_str();
    int r = avformat_alloc_output_context2(&fmt_ctx_, nullptr, fmt_name, path.c_str());
    if (r < 0 || !fmt_ctx_) {
        if (error) *error = trf("невідомий формат вихідного файлу '{}' (перевірте розширення)", path);
        return false;
    }
    return true;
}

void Muxer::set_format_option(const char* key, const char* value) {
    if (fmt_ctx_ && fmt_ctx_->priv_data) av_opt_set(fmt_ctx_->priv_data, key, value, 0);
}

bool Muxer::needs_global_header() const {
    return fmt_ctx_ && (fmt_ctx_->oformat->flags & AVFMT_GLOBALHEADER);
}

bool Muxer::is_image_sequence() const {
    if (!fmt_ctx_) return false;
    const std::string n = fmt_ctx_->oformat->name;
    return n == "image2" || n == "image2pipe";
}

bool Muxer::supports_codec(const std::string& encoder_name) const {
    if (!fmt_ctx_) return false;
    const AVCodec* c = avcodec_find_encoder_by_name(encoder_name.c_str());
    if (!c) return false;
    const int q = avformat_query_codec(fmt_ctx_->oformat, c->id, FF_COMPLIANCE_NORMAL);
    return q != 0;   // 1 — так, <0 — невідомо (дозволяємо спробувати)
}

int Muxer::add_stream(AVCodecContext* enc, const std::string& title, const std::string& language) {
    AVStream* st = avformat_new_stream(fmt_ctx_, nullptr);
    if (!st) return -1;
    avcodec_parameters_from_context(st->codecpar, enc);
    st->time_base = enc->time_base;
    if (enc->codec_type == AVMEDIA_TYPE_VIDEO) {
        st->avg_frame_rate = enc->framerate;
        st->r_frame_rate = enc->framerate;
        // HEVC у mp4/mov: тег hvc1 — щоб відео відкривалося на Apple/у браузерах
        const std::string fmt = fmt_ctx_->oformat->name;
        if (enc->codec_id == AV_CODEC_ID_HEVC && (fmt.find("mp4") != std::string::npos || fmt.find("mov") != std::string::npos))
            st->codecpar->codec_tag = MKTAG('h', 'v', 'c', '1');
    }
    if (!title.empty()) av_dict_set(&st->metadata, "title", title.c_str(), 0);
    if (!language.empty()) av_dict_set(&st->metadata, "language", language.c_str(), 0);
    encoders_.push_back(enc);
    return st->index;
}

int Muxer::add_stream_copy(const AVCodecParameters* par, AVRational enc_tb, AVRational framerate, const std::string& title) {
    AVStream* st = avformat_new_stream(fmt_ctx_, nullptr);
    if (!st || avcodec_parameters_copy(st->codecpar, par) < 0) return -1;
    st->codecpar->codec_tag = 0;   // тег вибере сам контейнер
    st->time_base = enc_tb;
    if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
        st->avg_frame_rate = framerate;
        st->r_frame_rate = framerate;
        const std::string fmt = fmt_ctx_->oformat->name;
        if (par->codec_id == AV_CODEC_ID_HEVC && (fmt.find("mp4") != std::string::npos || fmt.find("mov") != std::string::npos))
            st->codecpar->codec_tag = MKTAG('h', 'v', 'c', '1');
    }
    if (!title.empty()) av_dict_set(&st->metadata, "title", title.c_str(), 0);
    return st->index;
}

bool Muxer::write_header(bool faststart, bool fragmented, std::string* error) {
    if (!(fmt_ctx_->oformat->flags & AVFMT_NOFILE)) {
        int r = avio_open(&fmt_ctx_->pb, path_.c_str(), AVIO_FLAG_WRITE);
        if (r < 0) {
            if (error) *error = trf("не вдалося створити файл '{}': {}", path_, av_error_string(r));
            return false;
        }
    }
    AVDictionary* opts = nullptr;
    const std::string fmt = fmt_ctx_->oformat->name;
    const bool mov_family = fmt.find("mp4") != std::string::npos || fmt.find("mov") != std::string::npos;
    if (mov_family && fragmented) {
        // Фрагментований MP4: індекс пишеться частинами після кожного ключового кадру,
        // тож обірваний файл однаково відкривається.
        av_dict_set(&opts, "movflags", "+frag_keyframe+empty_moov+default_base_moof+delay_moov", 0);
        fragmented_ = true;
    } else if (mov_family && faststart) {
        av_dict_set(&opts, "movflags", "+faststart", 0);
    }
    av_dict_set(&fmt_ctx_->metadata, "encoder", "GMod Demo Render (FFmpeg)", 0);
    int r = avformat_write_header(fmt_ctx_, &opts);
    av_dict_free(&opts);
    if (r < 0) {
        if (error) *error = tr("не вдалося записати заголовок файлу: ") + av_error_string(r);
        return false;
    }
    header_written_ = true;
    return true;
}

bool Muxer::write_packet(int stream_index, AVPacket* pkt, AVRational enc_tb) {
    std::lock_guard lock(mutex_);
    if (!header_written_ || finished_) return false;
    AVStream* st = fmt_ctx_->streams[stream_index];
    av_packet_rescale_ts(pkt, enc_tb, st->time_base);
    pkt->stream_index = stream_index;
    const int r = av_interleaved_write_frame(fmt_ctx_, pkt);
    if (r < 0) {
        error_ = tr("помилка запису у файл: ") + av_error_string(r);
        return false;
    }
    return true;
}

bool Muxer::finish(std::string* error) {
    std::lock_guard lock(mutex_);
    if (!fmt_ctx_ || finished_) return true;
    finished_ = true;
    int r = 0;
    if (header_written_) r = av_write_trailer(fmt_ctx_);
    if (!(fmt_ctx_->oformat->flags & AVFMT_NOFILE) && fmt_ctx_->pb) avio_closep(&fmt_ctx_->pb);
    avformat_free_context(fmt_ctx_);
    fmt_ctx_ = nullptr;
    if (r < 0) {
        if (error) *error = tr("помилка завершення файлу: ") + av_error_string(r);
        return false;
    }
    return true;
}

void Muxer::abort() {
    std::lock_guard lock(mutex_);
    if (!fmt_ctx_) return;
    if (header_written_ && !finished_) av_write_trailer(fmt_ctx_);   // щоб файл можна було відкрити
    if (!(fmt_ctx_->oformat->flags & AVFMT_NOFILE) && fmt_ctx_->pb) avio_closep(&fmt_ctx_->pb);
    avformat_free_context(fmt_ctx_);
    fmt_ctx_ = nullptr;
    finished_ = true;
}

int64_t Muxer::bytes_written() const {
    std::lock_guard lock(mutex_);
    if (!fmt_ctx_ || !fmt_ctx_->pb) return 0;
    return avio_tell(fmt_ctx_->pb);
}

bool container_supports_chapters(const std::string& path) {
    std::string ext = to_lower(path_to_utf8(path_from_utf8(path).extension()));
    return ext == ".mp4" || ext == ".m4v" || ext == ".mov" || ext == ".mkv" || ext == ".webm";
}

bool remux_file(const std::string& path, bool faststart, std::string* error, const std::vector<ChapterMark>* chapters) {
    // Пишемо поруч у тимчасовий файл, потім замінюємо оригінал
    const std::string tmp = path + ".remux" + path_to_utf8(path_from_utf8(path).extension());
    AVFormatContext* in = nullptr;
    int r = avformat_open_input(&in, path.c_str(), nullptr, nullptr);
    if (r < 0) {
        if (error) *error = tr("не вдалося відкрити файл для переупаковки: ") + av_error_string(r);
        return false;
    }
    InputCtxPtr in_guard(in);
    if ((r = avformat_find_stream_info(in, nullptr)) < 0) {
        if (error) *error = tr("не вдалося прочитати файл: ") + av_error_string(r);
        return false;
    }
    AVFormatContext* out = nullptr;
    avformat_alloc_output_context2(&out, nullptr, nullptr, tmp.c_str());
    if (!out) {
        if (error) *error = tr("невідомий формат файлу");
        return false;
    }
    struct OutGuard {
        AVFormatContext* c;
        ~OutGuard() {
            if (!c) return;
            if (!(c->oformat->flags & AVFMT_NOFILE) && c->pb) avio_closep(&c->pb);
            avformat_free_context(c);
        }
    } out_guard{out};
    std::vector<int> map(in->nb_streams, -1);
    for (unsigned i = 0; i < in->nb_streams; ++i) {
        const AVStream* is = in->streams[i];
        const auto type = is->codecpar->codec_type;
        if (type != AVMEDIA_TYPE_VIDEO && type != AVMEDIA_TYPE_AUDIO && type != AVMEDIA_TYPE_SUBTITLE) continue;
        AVStream* os = avformat_new_stream(out, nullptr);
        if (!os || avcodec_parameters_copy(os->codecpar, is->codecpar) < 0) {
            if (error) *error = tr("не вдалося скопіювати параметри потоку");
            return false;
        }
        os->codecpar->codec_tag = is->codecpar->codec_tag;
        os->time_base = is->time_base;
        os->avg_frame_rate = is->avg_frame_rate;
        os->r_frame_rate = is->r_frame_rate;
        os->disposition = is->disposition;
        av_dict_copy(&os->metadata, is->metadata, 0);
        map[i] = os->index;
    }
    av_dict_copy(&out->metadata, in->metadata, 0);
    // Розділи: нові або ті, що вже були у файлі. Пам'ять звільнить avformat_free_context.
    std::vector<ChapterMark> copied;
    if (!chapters) {
        for (unsigned i = 0; i < in->nb_chapters; ++i) {
            const AVChapter* c = in->chapters[i];
            const AVDictionaryEntry* t = av_dict_get(c->metadata, "title", nullptr, 0);
            copied.push_back({c->start * av_q2d(c->time_base), c->end * av_q2d(c->time_base), t ? t->value : ""});
        }
        chapters = &copied;
    }
    if (!chapters->empty()) {
        out->chapters = static_cast<AVChapter**>(av_calloc(chapters->size(), sizeof(AVChapter*)));
        if (!out->chapters) {
            if (error) *error = tr("недостатньо пам'яті");
            return false;
        }
        for (const auto& c : *chapters) {
            auto* ch = static_cast<AVChapter*>(av_mallocz(sizeof(AVChapter)));
            if (!ch) break;
            ch->id = static_cast<int64_t>(out->nb_chapters) + 1;
            ch->time_base = AVRational{1, 1000};
            ch->start = std::llround(c.start * 1000.0);
            ch->end = std::max(ch->start + 1, static_cast<int64_t>(std::llround(c.end * 1000.0)));
            av_dict_set(&ch->metadata, "title", c.title.c_str(), 0);
            out->chapters[out->nb_chapters++] = ch;
        }
    }
    if ((r = avio_open(&out->pb, tmp.c_str(), AVIO_FLAG_WRITE)) < 0) {
        if (error) *error = tr("не вдалося створити тимчасовий файл: ") + av_error_string(r);
        return false;
    }
    AVDictionary* opts = nullptr;
    if (faststart) av_dict_set(&opts, "movflags", "+faststart", 0);
    r = avformat_write_header(out, &opts);
    av_dict_free(&opts);
    if (r < 0) {
        if (error) *error = tr("не вдалося записати заголовок: ") + av_error_string(r);
        return false;
    }
    PacketPtr pkt = make_packet();
    while ((r = av_read_frame(in, pkt.get())) >= 0) {
        const int si = pkt->stream_index;
        if (si < 0 || static_cast<size_t>(si) >= map.size() || map[si] < 0) {
            av_packet_unref(pkt.get());
            continue;
        }
        av_packet_rescale_ts(pkt.get(), in->streams[si]->time_base, out->streams[map[si]]->time_base);
        pkt->stream_index = map[si];
        pkt->pos = -1;
        const int w = av_interleaved_write_frame(out, pkt.get());
        av_packet_unref(pkt.get());
        if (w < 0) {
            if (error) *error = tr("помилка запису: ") + av_error_string(w);
            return false;
        }
    }
    if ((r = av_write_trailer(out)) < 0) {
        if (error) *error = tr("помилка завершення файлу: ") + av_error_string(r);
        return false;
    }
    avio_closep(&out->pb);
    in_guard.reset();
    std::error_code ec;
    std::filesystem::rename(path_from_utf8(tmp), path_from_utf8(path), ec);
    if (ec) {
        std::filesystem::remove(path_from_utf8(path), ec);
        std::filesystem::rename(path_from_utf8(tmp), path_from_utf8(path), ec);
    }
    if (ec) {
        if (error) *error = tr("не вдалося замінити файл: ") + ec.message();
        return false;
    }
    return true;
}

bool probe_media_file(const std::string& path, MediaFileInfo& out, std::string* error) {
    out = MediaFileInfo{};
    AVFormatContext* in = nullptr;
    int r = avformat_open_input(&in, path.c_str(), nullptr, nullptr);
    if (r < 0) {
        if (error) *error = av_error_string(r);
        return false;
    }
    InputCtxPtr guard(in);
    avformat_find_stream_info(in, nullptr);
    out.chapters = static_cast<int>(in->nb_chapters);
    for (unsigned i = 0; i < in->nb_streams; ++i) {
        const AVStream* st = in->streams[i];
        double dur = st->duration > 0 ? st->duration * av_q2d(st->time_base) : 0;
        if (dur <= 0 && in->duration > 0) dur = in->duration / static_cast<double>(AV_TIME_BASE);
        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && !out.has_video) {
            out.has_video = true;
            out.video_seconds = dur;
            out.video_frames = st->nb_frames;
        } else if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            ++out.audio_streams;
            out.audio_seconds = std::max(out.audio_seconds, dur);
        }
    }
    return true;
}

int container_supports(const std::string& ext, const std::string& encoder_name) {
    const std::string fake = "x." + ext;
    const AVOutputFormat* fmt = av_guess_format(nullptr, fake.c_str(), nullptr);
    const AVCodec* c = avcodec_find_encoder_by_name(encoder_name.c_str());
    if (!fmt || !c) return 0;
    const int q = avformat_query_codec(fmt, c->id, FF_COMPLIANCE_NORMAL);
    return q > 0 ? 1 : q == 0 ? 0 : -1;
}

std::vector<std::string> containers_for_codec(const std::string& enc) {
    auto has = [&](const char* s) { return enc.find(s) != std::string::npos; };
    if (has("prores") || enc == "dnxhd" || enc == "qtrle") return {"mov", "mkv"};
    if (enc == "ffv1" || enc == "utvideo" || enc == "huffyuv" || enc == "ffvhuff" || enc == "magicyuv") return {"mkv", "avi", "nut"};
    if (has("vp9") || has("libvpx") || has("av1")) return {"mkv", "webm", "mp4"};
    if (enc == "png" || enc == "tiff" || enc == "exr" || enc == "bmp" || enc == "mjpeg" || enc == "targa")
        return {"png-seq", "mkv", "mov"};
    return {"mp4", "mkv", "mov"};
}

} // namespace gmdr::media
