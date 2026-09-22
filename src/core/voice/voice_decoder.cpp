#include "voice_decoder.hpp"

#include "../audio/wav.hpp"
#include "../media/audio_encoder.hpp"
#include "../media/ffmpeg_util.hpp"
#include "../media/muxer.hpp"
#include "../util/log.hpp"
#include "../util/strings.hpp"
#include "steam_voice.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <format>
#include <map>

namespace gmdr::voice {

std::string SpeakerTrack::display_name() const {
    std::string n = name.empty() ? std::format("Гравець #{}", slot) : name;
    if (steamid64) n += " (" + format_steamid(steamid64) + ")";
    return n;
}

int opus_packet_samples_48k(const uint8_t* d, size_t n) {
    if (!d || n < 1) return 0;
    const int toc = d[0];
    const int config = toc >> 3;
    int frame = 0;   // семплів 48 кГц в одному кадрі
    if (config < 12) {        // SILK: 10/20/40/60 мс
        static const int s[4] = {480, 960, 1920, 2880};
        frame = s[config & 3];
    } else if (config < 16) { // Hybrid: 10/20 мс
        frame = (config & 1) ? 960 : 480;
    } else {                  // CELT: 2.5/5/10/20 мс
        static const int c[4] = {120, 240, 480, 960};
        frame = c[config & 3];
    }
    int count = 1;
    switch (toc & 3) {
    case 0: count = 1; break;
    case 1:
    case 2: count = 2; break;
    default: count = n >= 2 ? (d[1] & 0x3F) : 0; break;
    }
    const int total = frame * count;
    return (total <= 0 || total > 5760) ? 0 : total;   // пакет Opus — не довше 120 мс
}

namespace {

// Декодер Opus на базі FFmpeg (вбудований декодер "opus"), вихід 48 кГц моно float.
class OpusDecoder {
public:
    OpusDecoder() { open(); }

    bool ok() const { return ctx_ != nullptr; }

    void reset() {
        if (ctx_) avcodec_flush_buffers(ctx_.get());
    }

    // Декодувати один пакет Opus і дописати семпли в out. Повертає false при помилці.
    bool decode(const uint8_t* data, size_t size, std::vector<float>& out) {
        if (!ctx_) return false;
        pkt_->data = const_cast<uint8_t*>(data);
        pkt_->size = static_cast<int>(size);
        int r = avcodec_send_packet(ctx_.get(), pkt_.get());
        pkt_->data = nullptr;
        pkt_->size = 0;
        if (r < 0) return false;
        bool got = false;
        while ((r = avcodec_receive_frame(ctx_.get(), frame_.get())) >= 0) {
            append_frame(out);
            got = true;
            av_frame_unref(frame_.get());
        }
        return got || r == AVERROR(EAGAIN);
    }

private:
    void open() {
        const AVCodec* codec = avcodec_find_decoder_by_name("opus");   // вбудований декодер FFmpeg
        if (!codec) codec = avcodec_find_decoder(AV_CODEC_ID_OPUS);
        if (!codec) {
            log_error("У цій збірці FFmpeg немає декодера Opus — голос не буде декодовано");
            return;
        }
        media::CodecCtxPtr ctx(avcodec_alloc_context3(codec));
        ctx->sample_rate = kVoiceRate;
        av_channel_layout_default(&ctx->ch_layout, 1);
        ctx->pkt_timebase = AVRational{1, kVoiceRate};
        const int r = avcodec_open2(ctx.get(), codec, nullptr);
        if (r < 0) {
            log_error("Не вдалося відкрити декодер Opus: {}", media::av_error_string(r));
            return;
        }
        ctx_ = std::move(ctx);
        pkt_ = media::make_packet();
        frame_ = media::make_frame();
    }

    void append_frame(std::vector<float>& out) {
        const AVFrame* f = frame_.get();
        const int n = f->nb_samples;
        const int ch = f->ch_layout.nb_channels > 0 ? f->ch_layout.nb_channels : 1;
        const auto fmt = static_cast<AVSampleFormat>(f->format);
        const size_t base = out.size();
        out.resize(base + static_cast<size_t>(n), 0.0f);
        // Змішуємо канали в моно (зазвичай канал один)
        for (int c = 0; c < ch; ++c) {
            for (int i = 0; i < n; ++i) {
                float v = 0.0f;
                switch (fmt) {
                case AV_SAMPLE_FMT_FLTP: v = reinterpret_cast<const float*>(f->extended_data[c])[i]; break;
                case AV_SAMPLE_FMT_FLT: v = reinterpret_cast<const float*>(f->extended_data[0])[i * ch + c]; break;
                case AV_SAMPLE_FMT_S16P: v = reinterpret_cast<const int16_t*>(f->extended_data[c])[i] / 32768.0f; break;
                case AV_SAMPLE_FMT_S16: v = reinterpret_cast<const int16_t*>(f->extended_data[0])[i * ch + c] / 32768.0f; break;
                default: break;
                }
                out[base + static_cast<size_t>(i)] += v / static_cast<float>(ch);
            }
        }
    }

    media::CodecCtxPtr ctx_;
    media::PacketPtr   pkt_;
    media::FramePtr    frame_;
};

struct IndexState {
    SpeakerTrack track;
    int64_t      cursor = INT64_MIN;   // де закінчився попередній звук
    int          expected_seq = -1;    // очікуваний номер наступного кадру
};

} // namespace

// ================================ Розбір ==========================================
VoiceDecodeResult decode_voice(const demo::DemoAnalysis& a, const VoiceDecodeOptions& opt,
                               const std::function<void(double)>& progress, const std::atomic<bool>* cancel) {
    VoiceDecodeResult res;
    res.total_packets = static_cast<int>(a.voice_packets.size());
    std::map<std::string, IndexState> speakers;
    std::vector<std::string> order;

    // SteamID того, хто записував (якщо відомий з userinfo)
    uint64_t local_steamid = 0;
    if (auto it = a.players.find(a.local_slot); it != a.players.end()) local_steamid = it->second.steamid64;

    const double samples_per_tick = static_cast<double>(a.tick_interval) * kVoiceRate;
    const int64_t latency = static_cast<int64_t>(std::llround(opt.latency * kVoiceRate));
    const int64_t jitter = static_cast<int64_t>(std::llround(
        std::max(opt.jitter_tolerance, 3.0 * a.tick_interval) * kVoiceRate));

    SteamVoicePacket parsed;
    size_t idx = 0;
    for (const auto& vp : a.voice_packets) {
        ++idx;
        if (cancel && cancel->load()) break;
        if (progress && (idx % 4096 == 0)) progress(static_cast<double>(idx) / std::max<size_t>(1, a.voice_packets.size()));

        // Порожні пакети гра пише для гравця, який записував демо (його голос сервер не пересилає).
        if (vp.data.empty()) {
            ++res.empty_packets;
            continue;
        }
        std::string err;
        const bool parsed_ok = parse_steam_voice(vp.data.data(), vp.data.size(), parsed, &err);
        if (!parsed_ok || !parsed.crc_ok) {
            ++res.bad_packets;
            if (res.bad_packets <= 3)
                log_debug("Голосовий пакет (тік {}, слот {}) не схожий на Steam Voice: {} [{}]", vp.tick, vp.client,
                          parsed_ok ? "CRC не збігається" : err, hex_dump(vp.data.data(), vp.data.size(), 16));
            if (!parsed_ok) continue;
        }

        const bool steamid_ok = (parsed.steamid64 >> 32) == 0x01100001u;
        const std::string key = steamid_ok ? std::format("steam:{}", parsed.steamid64) : std::format("slot:{}", vp.client);
        auto [it, inserted] = speakers.try_emplace(key);
        IndexState& st = it->second;
        SpeakerTrack& tr = st.track;
        if (inserted) {
            order.push_back(key);
            tr.key = key;
            tr.slot = vp.client;
            tr.steamid64 = steamid_ok ? parsed.steamid64 : 0;
            // Ім'я: спершу шукаємо за SteamID, потім за слотом
            for (const auto& [slot, pi] : a.players)
                if (tr.steamid64 && pi.steamid64 == tr.steamid64) tr.name = pi.name;
            if (tr.name.empty()) tr.name = a.player_name(vp.client);
            tr.is_local = (vp.client == a.local_slot) || (local_steamid != 0 && tr.steamid64 == local_steamid);
        }
        ++tr.packets;

        // Події цього пакета
        const auto first_event = static_cast<uint32_t>(tr.events.size());
        int64_t packet_samples = 0;
        auto add_silence = [&](int64_t n) {
            if (n <= 0) return;
            VoiceEvent e;
            e.kind = VoiceEvent::Silence;
            e.samples = static_cast<uint32_t>(n);
            tr.events.push_back(e);
            packet_samples += n;
        };
        for (const auto& op : parsed.ops) {
            if (op.kind == SteamVoiceOp::Kind::Silence) {
                // тиша задана у семплах "рідної" частоти потоку
                const double ratio = static_cast<double>(kVoiceRate) / std::max(8000, parsed.sample_rate);
                add_silence(static_cast<int64_t>(op.silence_samples * ratio));
                continue;
            }
            if (op.kind == SteamVoiceOp::Kind::Unsupported) {
                ++tr.unsupported_ops;
                continue;
            }
            for (const auto& fr : op.frames) {
                if (fr.reset) {   // кінець фрази: скинути декодер
                    VoiceEvent e;
                    e.kind = VoiceEvent::Reset;
                    tr.events.push_back(e);
                    st.expected_seq = -1;
                    continue;
                }
                if (st.expected_seq >= 0) {
                    if (fr.seq < st.expected_seq) {   // потік почався заново
                        VoiceEvent e;
                        e.kind = VoiceEvent::Reset;
                        tr.events.push_back(e);
                    } else if (fr.seq > st.expected_seq) {   // втрачені кадри — заповнюємо тишею
                        const int lost = std::min(static_cast<int>(fr.seq) - st.expected_seq, 10);
                        tr.lost_frames += lost;
                        add_silence(static_cast<int64_t>(lost) * (kVoiceRate / 50));
                    }
                }
                st.expected_seq = fr.seq + 1;
                ++tr.frames;
                int n = opus_packet_samples_48k(fr.data, fr.size);
                if (n <= 0) n = kVoiceRate / 50;   // Steam Voice використовує кадри по 20 мс
                VoiceEvent e;
                e.kind = VoiceEvent::Frame;
                e.samples = static_cast<uint32_t>(n);
                e.data_offset = static_cast<uint32_t>(tr.blob.size());
                e.data_size = static_cast<uint32_t>(fr.size);
                tr.blob.insert(tr.blob.end(), fr.data, fr.data + fr.size);
                tr.events.push_back(e);
                packet_samples += n;
            }
        }
        const auto event_count = static_cast<uint32_t>(tr.events.size()) - first_event;
        if (packet_samples == 0) {
            // Пакет без звуку (лише "скинути декодер") — приєднуємо до поточного відрізка
            if (!tr.segments.empty()) tr.segments.back().event_count += event_count;
            else tr.events.resize(first_event);
            continue;
        }

        // Розміщення на часовій шкалі
        const int64_t arrival = static_cast<int64_t>(std::llround(vp.tick * samples_per_tick)) + latency;
        const bool new_segment = tr.segments.empty() || st.cursor == INT64_MIN || arrival > st.cursor + jitter;
        if (new_segment) {
            VoiceSegment seg;
            seg.start = std::max<int64_t>(0, arrival);
            // Якщо новий відрізок налазить на попередній (буває при перезапуску потоку) — ставимо після
            if (!tr.segments.empty()) seg.start = std::max(seg.start, tr.segments.back().end());
            seg.length = packet_samples;
            seg.first_event = first_event;
            seg.event_count = event_count;
            tr.segments.push_back(seg);
        } else {
            auto& seg = tr.segments.back();
            seg.length += packet_samples;
            seg.event_count += event_count;
        }
        st.cursor = tr.segments.back().end();
        tr.seconds += static_cast<double>(packet_samples) / kVoiceRate;
    }

    for (const auto& key : order) {
        SpeakerTrack& t = speakers[key].track;
        t.events.shrink_to_fit();
        t.blob.shrink_to_fit();
        res.speakers.push_back(std::move(t));
    }
    // Спочатку власний голос, далі — за тривалістю мовлення
    std::stable_sort(res.speakers.begin(), res.speakers.end(), [](const SpeakerTrack& x, const SpeakerTrack& y) {
        if (x.is_local != y.is_local) return x.is_local;
        return x.seconds > y.seconds;
    });
    if (res.bad_packets > 0)
        res.warnings.push_back(std::format("{} з {} голосових пакетів мають невідомий формат або пошкоджені",
                                           res.bad_packets, res.total_packets));
    int unsupported = 0;
    for (auto& s : res.speakers) unsupported += s.unsupported_ops;
    if (unsupported > 0)
        res.warnings.push_back(std::format("{} блоків голосу у старих кодеках (не Opus) пропущено", unsupported));
    if (progress) progress(1.0);
    return res;
}

// ============================ Потокове декодування ===================================
struct VoiceStream::Impl {
    explicit Impl(const SpeakerTrack& t) : track(t) {}

    const SpeakerTrack& track;
    OpusDecoder         dec;
    size_t              seg = SIZE_MAX;   // відрізок, який зараз декодуємо
    uint32_t            next_event = 0;   // наступна подія для декодування
    int64_t             buf_start = 0;    // позиція buf[0] на часовій шкалі
    std::vector<float>  buf;              // декодований звук поточного відрізка від buf_start
    std::vector<float>  tmp;
    size_t              hint = 0;         // перший відрізок, що може перетинатися з запитом
    int                 errors = 0;

    int64_t decoded_end() const { return buf_start + static_cast<int64_t>(buf.size()); }

    void start_segment(size_t k) {
        const VoiceSegment& s = track.segments[k];
        // Якщо попередній відрізок декодовано до кінця — стан декодера продовжується
        // (як у грі); інакше починаємо з чистого декодера.
        const bool continuous = seg != SIZE_MAX && k == seg + 1 && next_event == s.first_event;
        if (!continuous) dec.reset();
        seg = k;
        next_event = s.first_event;
        buf.clear();
        buf_start = s.start;
    }

    void decode_event(const VoiceEvent& e) {
        switch (e.kind) {
        case VoiceEvent::Reset: dec.reset(); return;
        case VoiceEvent::Silence: buf.insert(buf.end(), e.samples, 0.0f); return;
        case VoiceEvent::Frame: {
            tmp.clear();
            const bool ok = dec.ok() && e.data_offset + static_cast<size_t>(e.data_size) <= track.blob.size() &&
                            dec.decode(track.blob.data() + e.data_offset, e.data_size, tmp);
            if (!ok) ++errors;
            // Тривалість кадру відома наперед — рівно стільки й дописуємо,
            // тож розміщення на часовій шкалі не залежить від декодера.
            tmp.resize(e.samples, 0.0f);
            buf.insert(buf.end(), tmp.begin(), tmp.end());
            return;
        }
        default: return;
        }
    }

    // Забезпечити декодований звук відрізка k до позиції upto.
    void ensure(size_t k, int64_t from, int64_t upto) {
        const VoiceSegment& s = track.segments[k];
        if (seg != k || from < buf_start) start_segment(k);
        const uint32_t last = s.first_event + s.event_count;
        while (decoded_end() < upto && next_event < last) decode_event(track.events[next_event++]);
        if (decoded_end() < upto) buf.resize(static_cast<size_t>(upto - buf_start), 0.0f);
    }

    void trim_before(int64_t pos) {
        if (pos <= buf_start) return;
        const int64_t drop = std::min<int64_t>(pos - buf_start, static_cast<int64_t>(buf.size()));
        // Невеликі шматки не вирізаємо щоразу — лише коли назбиралося
        if (drop < kVoiceRate && drop < static_cast<int64_t>(buf.size())) return;
        buf.erase(buf.begin(), buf.begin() + static_cast<ptrdiff_t>(drop));
        buf_start += drop;
    }
};

VoiceStream::VoiceStream(const SpeakerTrack& track) : impl_(std::make_unique<Impl>(track)) {}
VoiceStream::~VoiceStream() = default;

int VoiceStream::decode_errors() const { return impl_->errors; }

void VoiceStream::mix_into(int64_t from, size_t count, float* out, float gain, size_t stride) {
    Impl& m = *impl_;
    const auto& segs = m.track.segments;
    if (segs.empty() || count == 0) return;
    const int64_t a = from;
    const int64_t b = from + static_cast<int64_t>(count);
    // Запит назад у часі — шукаємо відрізок заново
    if (m.hint >= segs.size() || (m.hint > 0 && segs[m.hint - 1].end() > a) || (m.hint < segs.size() && segs[m.hint].start > b)) {
        m.hint = static_cast<size_t>(std::upper_bound(segs.begin(), segs.end(), a,
                                                      [](int64_t v, const VoiceSegment& s) { return v < s.end(); }) -
                                     segs.begin());
    }
    while (m.hint < segs.size() && segs[m.hint].end() <= a) ++m.hint;
    for (size_t k = m.hint; k < segs.size() && segs[k].start < b; ++k) {
        const VoiceSegment& s = segs[k];
        const int64_t lo = std::max(a, s.start);
        const int64_t hi = std::min(b, s.end());
        if (hi <= lo) continue;
        m.ensure(k, lo, hi);
        for (int64_t t = lo; t < hi; ++t)
            out[static_cast<size_t>(t - a) * stride] += m.buf[static_cast<size_t>(t - m.buf_start)] * gain;
        m.trim_before(hi);
    }
}

std::vector<float> decode_range(const SpeakerTrack& track, int64_t from, int64_t to) {
    std::vector<float> out(static_cast<size_t>(std::max<int64_t>(0, to - from)), 0.0f);
    if (out.empty()) return out;
    VoiceStream s(track);
    s.mix_into(from, out.size(), out.data());
    return out;
}

// ================================= Експорт ==========================================
bool export_speaker_audio(const SpeakerTrack& track, const std::filesystem::path& path, int64_t from, int64_t to,
                          std::string* error, const std::atomic<bool>* cancel,
                          const std::function<void(double)>& progress) {
    if (to < 0) to = track.end_sample();
    if (to < from) to = from;
    const bool flac = to_lower(path_to_utf8(path.extension())) == ".flac";
    VoiceStream stream(track);
    const int64_t chunk = kVoiceRate;   // по секунді
    std::vector<float> mono, stereo;
    auto should_stop = [&] { return cancel && cancel->load(); };

    if (!flac) {
        audio::WavWriter w;
        if (!w.open(path, kVoiceRate, 1, audio::WavWriter::Format::Int16, error)) return false;
        for (int64_t pos = from; pos < to; pos += chunk) {
            if (should_stop()) {
                w.close(nullptr);
                if (error) *error = "скасовано";
                return false;
            }
            const auto n = static_cast<size_t>(std::min(chunk, to - pos));
            mono.assign(n, 0.0f);
            stream.mix_into(pos, n, mono.data());
            w.write(mono.data(), n);
            if (progress && ((pos - from) / chunk) % 30 == 0) progress(static_cast<double>(pos - from) / std::max<int64_t>(1, to - from));
        }
        return w.close(error);
    }

    // FLAC: моно 16 біт, як і WAV, але без втрат стиснений (тиша займає кілька байт).
    media::Muxer mux;
    if (!mux.open(path_to_utf8(path), "flac", error)) return false;
    media::AudioEncoder enc;
    media::AudioEncoderSettings as;
    as.codec = "flac";
    as.sample_rate = kVoiceRate;
    as.channels = 1;          // вхід кодера — стерео з однаковими каналами, swresample зведе в моно
    as.sample_fmt = "s16";
    if (!enc.open(as, mux.needs_global_header(), error)) {
        mux.abort();
        return false;
    }
    const int stream_index = mux.add_stream(enc.context(), track.display_name());
    if (stream_index < 0 || !mux.write_header(false, false, error)) {
        mux.abort();
        return false;
    }
    const AVRational tb = enc.context()->time_base;
    auto sink = [&](AVPacket* p) { return mux.write_packet(stream_index, p, tb); };
    for (int64_t pos = from; pos < to; pos += chunk) {
        if (should_stop()) {
            mux.abort();
            if (error) *error = "скасовано";
            return false;
        }
        const auto n = static_cast<size_t>(std::min(chunk, to - pos));
        stereo.assign(n * 2, 0.0f);
        stream.mix_into(pos, n, stereo.data(), 1.0f, 2);
        for (size_t i = 0; i < n; ++i) stereo[i * 2 + 1] = stereo[i * 2];
        if (!enc.push(stereo.data(), n, sink, error)) {
            mux.abort();
            return false;
        }
        if (progress && ((pos - from) / chunk) % 30 == 0) progress(static_cast<double>(pos - from) / std::max<int64_t>(1, to - from));
    }
    if (!enc.flush(sink, error)) {
        mux.abort();
        return false;
    }
    return mux.finish(error);
}

} // namespace gmdr::voice
