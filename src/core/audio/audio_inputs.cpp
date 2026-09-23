#include "audio_inputs.hpp"

#include "../util/log.hpp"
#include "../util/strings.hpp"
#include "../util/i18n.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <format>

namespace gmdr::audio {

using media::kMixRate;

// ============================ GameAudioInput ====================================
GameAudioInput::GameAudioInput(std::filesystem::path wav_path, bool live, double offset_seconds)
    : path_(std::move(wav_path)), live_(live),
      offset_(static_cast<int64_t>(std::llround(offset_seconds * kMixRate))) {}

void GameAudioInput::pull() {
    if (!opened_) {
        std::error_code ec;
        if (!std::filesystem::exists(path_, ec)) {
            if (exact_path_) return;
            // Запасний варіант: будь-який .wav у тій самій папці (якщо гра назвала файл інакше)
            bool found = false;
            for (std::filesystem::directory_iterator it(path_.parent_path(), ec), end; !ec && it != end; it.increment(ec)) {
                if (ends_with_i(path_to_utf8(it->path().filename()), ".wav")) {
                    log_info("{}", trf("Звук гри знайдено у файлі {}", path_to_utf8(it->path().filename())));
                    path_ = it->path();
                    found = true;
                    break;
                }
            }
            if (!found) return;
        }
        std::string err;
        if (!reader_.open(path_, live_ && !finished_, &err)) return;   // заголовок ще не записаний
        SwrContext* swr = nullptr;
        AVChannelLayout in_l, out_l;
        av_channel_layout_default(&in_l, reader_.channels());
        av_channel_layout_default(&out_l, 2);
        if (swr_alloc_set_opts2(&swr, &out_l, AV_SAMPLE_FMT_FLT, kMixRate, &in_l, AV_SAMPLE_FMT_FLT,
                                reader_.sample_rate(), 0, nullptr) < 0 ||
            swr_init(swr) < 0) {
            swr_free(&swr);
            log_warn("{}", trf("Не вдалося налаштувати перетворення звуку гри"));
            return;
        }
        swr_.reset(swr);
        opened_ = true;
        log_info("{}", trf("Звук гри: {} Гц, {} кан., {} біт", reader_.sample_rate(), reader_.channels(), reader_.bits_per_sample()));
    }
    if (finished_) reader_.set_live(false);
    // Читаємо все, що вже записано
    for (;;) {
        const size_t chunk = 8192;
        raw_.resize(chunk * static_cast<size_t>(reader_.channels()));
        const size_t got = reader_.read(raw_.data(), chunk);
        if (got == 0) break;
        const uint8_t* in_planes[1] = {reinterpret_cast<const uint8_t*>(raw_.data())};
        const int out_max = swr_get_out_samples(swr_.get(), static_cast<int>(got));
        const size_t base = buf_.size();
        buf_.resize(base + static_cast<size_t>(std::max(0, out_max)) * 2);
        uint8_t* out_planes[1] = {reinterpret_cast<uint8_t*>(buf_.data() + base)};
        const int conv = swr_convert(swr_.get(), out_planes, out_max, in_planes, static_cast<int>(got));
        buf_.resize(base + static_cast<size_t>(std::max(0, conv)) * 2);
        produced_ += std::max(0, conv);
        if (got < chunk) break;
    }
    if (finished_ && !drained_ && swr_) {
        // Злити затримку ресемплера
        const int out_max = swr_get_out_samples(swr_.get(), 0) + 64;
        const size_t base = buf_.size();
        buf_.resize(base + static_cast<size_t>(out_max) * 2);
        uint8_t* out_planes[1] = {reinterpret_cast<uint8_t*>(buf_.data() + base)};
        const int conv = swr_convert(swr_.get(), out_planes, out_max, nullptr, 0);
        buf_.resize(base + static_cast<size_t>(std::max(0, conv)) * 2);
        produced_ += std::max(0, conv);
        drained_ = true;
    }
}

int64_t GameAudioInput::available() {
    if (finished_) {
        pull();
        return INT64_MAX;   // після кінця — тиша
    }
    pull();
    // Після перезапуску гри новий WAV ще не з'явився — доступне те, що лишилось від старого
    if (!opened_) return exact_path_ ? std::max<int64_t>(0, produced_ + offset_) : 0;
    // Позиція у часі відео = час джерела + offset
    return produced_ + offset_ < 0 ? 0 : produced_ + offset_;
}

void GameAudioInput::mix(int64_t pos, float* out, size_t frames, float gain) {
    const int64_t buf_frames = static_cast<int64_t>(buf_.size() / 2);
    for (size_t i = 0; i < frames; ++i) {
        const int64_t src = pos + static_cast<int64_t>(i) - offset_ - buf_start_;
        if (src < 0 || src >= buf_frames) continue;
        out[i * 2] += buf_[static_cast<size_t>(src) * 2] * gain;
        out[i * 2 + 1] += buf_[static_cast<size_t>(src) * 2 + 1] * gain;
    }
}

void GameAudioInput::discard_before(int64_t pos) {
    const int64_t src = pos - offset_ - buf_start_;
    const int64_t buf_frames = static_cast<int64_t>(buf_.size() / 2);
    if (src <= 48000 || buf_frames == 0) return;   // тримаємо невеликий запас
    const int64_t drop = std::min(src - 4800, buf_frames);
    if (drop <= 0) return;
    buf_.erase(buf_.begin(), buf_.begin() + static_cast<ptrdiff_t>(drop * 2));
    buf_start_ += drop;
}

void GameAudioInput::set_finished() {
    finished_ = true;
    pull();
}

void GameAudioInput::start_segment(std::filesystem::path wav_path, int64_t source_pos) {
    if (opened_) pull();   // дочитати все, що встигло записатися до збою
    const int64_t had = produced_;
    if (source_pos < buf_start_) {
        buf_.clear();
        buf_start_ = source_pos;
    } else {
        buf_.resize(static_cast<size_t>(source_pos - buf_start_) * 2, 0.0f);   // обрізати хвіст або доповнити тишею
    }
    produced_ = source_pos;
    log_debug("Звук гри: новий файл з позиції {:.3f} с ({} {:.0f} мс)", source_pos / static_cast<double>(kMixRate),
              had > source_pos ? "відкинуто" : "тиші", std::abs(had - source_pos) * 1000.0 / kMixRate);
    path_ = std::move(wav_path);
    swr_.reset();
    opened_ = drained_ = finished_ = false;
    exact_path_ = true;
}

// ============================== VoiceInput ======================================
VoiceInput::VoiceInput(const voice::SpeakerTrack* track, int64_t origin_sample, double extra_delay_seconds)
    : track_(track), origin_(origin_sample - static_cast<int64_t>(std::llround(extra_delay_seconds * kMixRate))),
      stream_(std::make_unique<voice::VoiceStream>(*track)) {}

void VoiceInput::mix(int64_t pos, float* out, size_t frames, float gain) {
    static_assert(voice::kVoiceRate == media::kMixRate, "частоти голосу і змішування мають збігатися");
    if (pos != cache_pos_ || cache_.size() != frames) {
        cache_.assign(frames, 0.0f);
        stream_->mix_into(pos + origin_, frames, cache_.data());
        cache_pos_ = pos;
    }
    for (size_t i = 0; i < frames; ++i) {
        const float v = cache_[i] * gain;
        out[i * 2] += v;
        out[i * 2 + 1] += v;
    }
}

// ============================= FileAudioInput ===================================
FileAudioInput::FileAudioInput(const std::filesystem::path& path, double offset_seconds)
    : offset_(static_cast<int64_t>(std::llround(offset_seconds * kMixRate))) {
    name_ = path_to_utf8(path.filename());
    const std::string p = path_to_utf8(path);
    int r = avformat_open_input(&fmt_, p.c_str(), nullptr, nullptr);
    if (r < 0) {
        error_ = tr("не вдалося відкрити аудіофайл: ") + media::av_error_string(r);
        return;
    }
    avformat_find_stream_info(fmt_, nullptr);
    const AVCodec* codec = nullptr;
    stream_ = av_find_best_stream(fmt_, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (stream_ < 0 || !codec) {
        error_ = tr("у файлі немає аудіопотоку");
        return;
    }
    dec_.reset(avcodec_alloc_context3(codec));
    avcodec_parameters_to_context(dec_.get(), fmt_->streams[stream_]->codecpar);
    if (avcodec_open2(dec_.get(), codec, nullptr) < 0) {
        error_ = tr("не вдалося відкрити декодер аудіофайлу");
        return;
    }
    if (fmt_->duration > 0) duration_ = fmt_->duration / static_cast<double>(AV_TIME_BASE);
    AVChannelLayout out_l;
    av_channel_layout_default(&out_l, 2);
    AVChannelLayout in_l;
    if (dec_->ch_layout.nb_channels > 0) av_channel_layout_copy(&in_l, &dec_->ch_layout);
    else av_channel_layout_default(&in_l, 2);
    SwrContext* swr = nullptr;
    if (swr_alloc_set_opts2(&swr, &out_l, AV_SAMPLE_FMT_FLT, kMixRate, &in_l, dec_->sample_fmt, dec_->sample_rate, 0,
                            nullptr) < 0 ||
        swr_init(swr) < 0) {
        swr_free(&swr);
        av_channel_layout_uninit(&in_l);
        error_ = tr("не вдалося налаштувати ресемплер для аудіофайлу");
        return;
    }
    av_channel_layout_uninit(&in_l);
    swr_.reset(swr);
    pkt_ = media::make_packet();
    frame_ = media::make_frame();
    ok_ = true;
}

FileAudioInput::~FileAudioInput() {
    if (fmt_) avformat_close_input(&fmt_);
}

bool FileAudioInput::decode_more() {
    if (!ok_ || eof_) return false;
    for (;;) {
        int r = avcodec_receive_frame(dec_.get(), frame_.get());
        if (r == 0) {
            const int out_max = swr_get_out_samples(swr_.get(), frame_->nb_samples);
            const size_t base = buf_.size();
            buf_.resize(base + static_cast<size_t>(out_max) * 2);
            uint8_t* out_planes[1] = {reinterpret_cast<uint8_t*>(buf_.data() + base)};
            const int conv = swr_convert(swr_.get(), out_planes, out_max,
                                         const_cast<const uint8_t**>(frame_->extended_data), frame_->nb_samples);
            buf_.resize(base + static_cast<size_t>(std::max(0, conv)) * 2);
            av_frame_unref(frame_.get());
            return true;
        }
        if (r == AVERROR_EOF) {
            eof_ = true;
            return false;
        }
        // потрібен наступний пакет
        r = av_read_frame(fmt_, pkt_.get());
        if (r < 0) {
            avcodec_send_packet(dec_.get(), nullptr);   // злити декодер
            continue;
        }
        if (pkt_->stream_index == stream_) avcodec_send_packet(dec_.get(), pkt_.get());
        av_packet_unref(pkt_.get());
    }
}

void FileAudioInput::mix(int64_t pos, float* out, size_t frames, float gain) {
    if (!ok_) return;
    // Позиція у "часі файлу"
    const int64_t a = pos - offset_;
    const int64_t b = a + static_cast<int64_t>(frames);
    if (b <= 0) return;
    while (buf_start_ + static_cast<int64_t>(buf_.size() / 2) < b && decode_more()) {
        // Якщо файл починається набагато раніше (від'ємний зсув) — не тримаємо зайве
        const int64_t have = static_cast<int64_t>(buf_.size() / 2);
        if (buf_start_ + have < a - kMixRate) {
            buf_start_ += have;
            buf_.clear();
        }
    }
    const int64_t have = static_cast<int64_t>(buf_.size() / 2);
    for (size_t i = 0; i < frames; ++i) {
        const int64_t src = a + static_cast<int64_t>(i) - buf_start_;
        if (src < 0 || src >= have) continue;
        out[i * 2] += buf_[static_cast<size_t>(src) * 2] * gain;
        out[i * 2 + 1] += buf_[static_cast<size_t>(src) * 2 + 1] * gain;
    }
}

void FileAudioInput::discard_before(int64_t pos) {
    const int64_t src = pos - offset_ - buf_start_;
    const int64_t have = static_cast<int64_t>(buf_.size() / 2);
    if (src <= kMixRate || have == 0) return;
    const int64_t drop = std::min(src - 4800, have);
    buf_.erase(buf_.begin(), buf_.begin() + static_cast<ptrdiff_t>(drop * 2));
    buf_start_ += drop;
}

// ============================== FilteredInput ===================================
FilteredInput::FilteredInput(std::string name, std::vector<std::vector<TrackSource>> inputs, bool mono)
    : name_(std::move(name)), inputs_(std::move(inputs)), ch_(mono ? 1 : 2) {}

bool FilteredInput::open(const std::string& chain, const GateParams* gate, std::string* error) {
    chain_.reset();
    if (!chain.empty()) {
        auto c = std::make_unique<AudioFilterChain>();
        if (!c->open(chain, static_cast<int>(inputs_.size()), static_cast<int>(ch_), error)) return false;
        chain_ = std::move(c);
    }
    if (gate && ch_ == 1) gate_ = std::make_unique<NoiseGate>(*gate);
    return true;
}

void FilteredInput::fail(const std::string& why) {
    if (failed_) return;
    failed_ = true;
    log_warn("{}", trf("Обробку «{}» вимкнено ({}) — далі без неї", name_, why));
}

int64_t FilteredInput::source_available() {
    int64_t a = INT64_MAX;
    for (const auto& in : inputs_)
        for (const auto& s : in) a = std::min(a, s.input->available());
    return a;
}

int64_t FilteredInput::ready_end() const {
    return gate_ ? gated_end_ : buf_start_ + static_cast<int64_t>(buf_.size() / ch_);
}

void FilteredInput::feed_chunk(size_t n) {
    for (size_t i = 0; i < inputs_.size(); ++i) {
        tmp_.assign(n * 2, 0.0f);
        for (const auto& s : inputs_[i]) s.input->mix(fed_, tmp_.data(), n, s.gain);
        const float* data = tmp_.data();
        if (ch_ == 1) {
            mono_.resize(n);
            for (size_t k = 0; k < n; ++k) mono_[k] = tmp_[k * 2];
            data = mono_.data();
        }
        if (chain_) {
            std::string err;
            if (!chain_->push(static_cast<int>(i), data, n, &err)) return fail(err);
        } else if (i == 0) {
            buf_.insert(buf_.end(), data, data + n * ch_);
        }
    }
    fed_ += static_cast<int64_t>(n);
    for (auto& o : owned_) o->discard_before(fed_);
    if (chain_) {
        // Новий результат фільтрів — у свій буфер (за позиціями)
        const int64_t have = buf_start_ + static_cast<int64_t>(buf_.size() / ch_);
        const int64_t end = chain_->out_end() + origin_;
        if (end > have) {
            const int64_t from = std::max(have, chain_->out_start() + origin_);
            if (from > have) buf_.resize(buf_.size() + static_cast<size_t>(from - have) * ch_, 0.0f);
            buf_.insert(buf_.end(), chain_->out_at(from - origin_), chain_->out_at(end - origin_));
        }
        chain_->discard_before(end - origin_);
    }
    if (gate_) {
        const int64_t can = buf_start_ + static_cast<int64_t>(buf_.size()) - static_cast<int64_t>(gate_->lookahead());
        if (can > gated_end_) {
            gate_->process(buf_.data() + (gated_end_ - buf_start_), static_cast<size_t>(can - gated_end_));
            gated_end_ = can;
        }
    }
}

void FilteredInput::produce_until(int64_t end, int64_t avail) {
    while (!failed_ && ready_end() < end && fed_ < avail) {
        feed_chunk(static_cast<size_t>(std::min<int64_t>(4096, avail - fed_)));
        // Джерело нескінченне (тиша після кінця), а фільтр нічого не віддає — щось не так
        if (avail == INT64_MAX && fed_ > static_cast<int64_t>(static_cast<double>(end) * std::max(1.0, input_rate_)) +
                                             10LL * kMixRate)
            fail(tr("фільтр не віддає звук"));
    }
}

int64_t FilteredInput::available() {
    const int64_t a = source_available();
    if (failed_ || a == INT64_MAX) return a;
    produce_until(INT64_MAX, a);
    return failed_ ? a : ready_end();
}

void FilteredInput::mix(int64_t pos, float* out, size_t frames, float gain) {
    const int64_t end = pos + static_cast<int64_t>(frames);
    if (!failed_) produce_until(end, source_available());
    if (failed_) {
        for (const auto& s : inputs_[0]) s.input->mix(pos, out, frames, gain * s.gain);
        return;
    }
    const int64_t a = std::max(pos, buf_start_);
    const int64_t b = std::min(end, ready_end());
    for (int64_t p = a; p < b; ++p) {
        const float* s = buf_.data() + static_cast<size_t>(p - buf_start_) * ch_;
        const size_t i = static_cast<size_t>(p - pos) * 2;
        out[i] += s[0] * gain;
        out[i + 1] += s[ch_ - 1] * gain;
    }
}

void FilteredInput::discard_before(int64_t pos) {
    const int64_t drop = std::min(pos, ready_end()) - buf_start_;
    if (drop < kMixRate / 2) return;   // не зсуваємо буфер щоразу
    buf_.erase(buf_.begin(), buf_.begin() + static_cast<ptrdiff_t>(static_cast<size_t>(drop) * ch_));
    buf_start_ += drop;
}

void FilteredInput::set_finished() {
    for (auto& o : owned_) o->set_finished();
}

// ================================ AudioMixer ====================================
AudioMixer::AudioMixer(std::vector<std::unique_ptr<AudioInput>> inputs, std::vector<AudioTrackPlan> tracks)
    : inputs_(std::move(inputs)), tracks_(std::move(tracks)) {
    post_.resize(tracks_.size());
    emitted_.assign(tracks_.size(), 0);
    for (size_t t = 0; t < tracks_.size(); ++t) {
        if (tracks_[t].post_filter.empty()) continue;
        auto c = std::make_unique<AudioFilterChain>();
        std::string err;
        if (c->open(tracks_[t].post_filter, 1, 2, &err)) post_[t] = std::move(c);
        else log_warn("{}", trf("Доріжка «{}» — без фільтра: {}", tracks_[t].title, err));
    }
}

void AudioMixer::log_post_failure(size_t t, const std::string& err) const {
    log_warn("{}", trf("Фільтр доріжки «{}» вимкнено: {}", tracks_[t].title, err));
}

int64_t AudioMixer::ready_until() {
    int64_t r = INT64_MAX;
    for (auto& in : inputs_) r = std::min(r, in->available());
    return r;
}

void AudioMixer::set_finished() {
    for (auto& in : inputs_) in->set_finished();
}

void AudioMixer::soft_limit(float* b, size_t n) {
    // М'яке обмеження піків: до 0.9 — без змін, далі плавно до 1.0 (без "хрипу" від кліпінгу)
    for (size_t i = 0; i < n; ++i) {
        float& x = b[i];
        const float a = std::fabs(x);
        if (a > 0.9f) {
            const float y = 0.9f + 0.1f * std::tanh((a - 0.9f) / 0.1f);
            x = x < 0 ? -y : y;
        }
    }
}

} // namespace gmdr::audio
