#include "encode_session.hpp"

#include "../util/file_util.hpp"
#include "../util/log.hpp"
#include "edit_package.hpp"
#include "../util/strings.hpp"
#include "../util/i18n.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>

namespace gmdr::render {

using media::kMixRate;
using Clock = std::chrono::steady_clock;

namespace {
double ms_since(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }
int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}
void atomic_add(std::atomic<double>& a, double v) {
    double cur = a.load();
    while (!a.compare_exchange_weak(cur, cur + v)) {}
}

// Частина паралельного рендеру: пакети відео з готового файлу
class PartReader {
public:
    PartReader() = default;
    PartReader(const PartReader&) = delete;
    PartReader& operator=(const PartReader&) = delete;
    ~PartReader() {
        if (ctx_) avformat_close_input(&ctx_);
    }
    bool open(const std::string& path, std::string* error) {
        int r = avformat_open_input(&ctx_, path.c_str(), nullptr, nullptr);
        if (r >= 0) r = avformat_find_stream_info(ctx_, nullptr);
        if (r < 0) {
            if (error) *error = trf("не вдалося відкрити частину {}: {}", path, media::av_error_string(r));
            return false;
        }
        stream_ = av_find_best_stream(ctx_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (stream_ < 0) {
            if (error) *error = trf("у частині {} немає відео", path);
            return false;
        }
        return true;
    }
    const AVCodecParameters* par() const { return ctx_->streams[stream_]->codecpar; }
    AVRational time_base() const { return ctx_->streams[stream_]->time_base; }
    int64_t    position() const { return ctx_->pb ? std::max<int64_t>(0, avio_tell(ctx_->pb)) : 0; }
    // Наступний пакет відео; false — кінець частини
    bool next(AVPacket* p) {
        while (av_read_frame(ctx_, p) >= 0) {
            if (p->stream_index == stream_) return true;
            av_packet_unref(p);
        }
        return false;
    }

private:
    AVFormatContext* ctx_ = nullptr;
    int              stream_ = -1;
};

// Потік для пакетів частин: параметри кодека — з першої частини
int add_copy_stream(media::Muxer& m, const std::string& first_part, AVRational fps, std::string* desc, std::string* error) {
    PartReader r;
    if (!r.open(first_part, error)) return -1;
    const int st = m.add_stream_copy(r.par(), AVRational{fps.den, fps.num}, fps, "GMod demo");
    if (st < 0 && error) *error = tr("не вдалося створити потік відео для склеювання");
    if (desc) {
        const AVCodecDescriptor* d = avcodec_descriptor_get(r.par()->codec_id);
        *desc = trf("{} {}×{} — частини без перекодування", d ? d->name : "?", r.par()->width, r.par()->height);
    }
    return st;
}
} // namespace

// ================================ PreviewSink ======================================
void PreviewSink::set(int w, int h, std::vector<uint8_t>&& rgba) {
    std::lock_guard lock(mutex_);
    frame_.width = w;
    frame_.height = h;
    frame_.rgba = std::move(rgba);
    ++frame_.serial;
}

bool PreviewSink::get_if_newer(uint64_t have_serial, PreviewFrame& out) const {
    std::lock_guard lock(mutex_);
    if (frame_.serial == 0 || frame_.serial == have_serial) return false;
    out = frame_;
    return true;
}

// =============================== EncodeSession =====================================
EncodeSession::EncodeSession(EncodeSettings s, ThreadPool* pool, PreviewSink* preview)
    : s_(std::move(s)), pool_(pool), preview_(preview) {}

EncodeSession::~EncodeSession() {
    if (started_ && !finished_) abort();
    if (worker_.joinable()) {
        {
            std::lock_guard lock(q_mutex_);
            worker_stop_ = true;
            queue_.clear();
        }
        q_cv_.notify_all();
        worker_.join();
    }
    sws_freeContext(preview_sws_);
}

double EncodeSession::video_seconds() const {
    return static_cast<double>(frames_out_.load()) * s_.video.fps.den / static_cast<double>(s_.video.fps.num);
}

bool EncodeSession::game_audio_opened() const {
    std::lock_guard lock(audio_mutex_);
    return game_input_ && game_input_->has_file();
}

std::string EncodeSession::audio_description() const {
    if (side_wav_) return tr("WAV поруч із кадрами");
    if (audio_encoders_.empty()) return tr("без звуку");
    return std::format("{} × {}", audio_encoders_.size(), audio_encoders_.front()->describe());
}

PipelineStats EncodeSession::stats() const {
    PipelineStats st;
    st.subframes = subframes_in_;
    st.frames = frames_out_.load();
    if (st.subframes > 0) st.blend_ms = blend_ms_ / static_cast<double>(st.subframes);
    if (st.frames > 0) {
        st.convert_ms = convert_ms_.load() / static_cast<double>(st.frames);
        st.encode_ms = encode_ms_.load() / static_cast<double>(st.frames);
        st.audio_ms = audio_ms_.load() / static_cast<double>(st.frames);
    }
    {
        std::lock_guard lock(q_mutex_);
        st.queue = static_cast<int>(queue_.size());
    }
    return st;
}

bool EncodeSession::begin(int frame_w, int frame_h, const AudioSourcesSpec& spec, std::string* error) {
    frame_w_ = frame_w;
    frame_h_ = frame_h;
    {
        // Папка для вихідного файлу може ще не існувати
        std::error_code ec;
        const std::filesystem::path parent = path_from_utf8(s_.output_path).parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent, ec);
    }
    if (!muxer_.open(s_.output_path, s_.container, error)) return false;
    const bool global_header = muxer_.needs_global_header();
    if (copy_mode()) {
        // Склеювання: пакети готових частин, кодер відео не потрібен
        video_stream_ = add_copy_stream(muxer_, s_.video_parts.front(), s_.video.fps, &video_desc_, error);
        if (video_stream_ < 0) return false;
        log_info("{}", trf("Відео: {}", video_desc_));
    } else {
        if (!muxer_.supports_codec(s_.video.codec)) {
            if (error) *error = trf("контейнер '{}' не підтримує кодек {} — оберіть інший формат файлу",
                                            muxer_.format()->name, s_.video.codec);
            return false;
        }
        if (!video_.open(s_.video, frame_w, frame_h, global_header, error)) return false;
        video_stream_ = muxer_.add_stream(video_.context(), "GMod demo");
        video_desc_ = video_.describe();
        log_info("{}", trf("Відео: {}", video_desc_));

        // Motion blur: 16-бітне змішування, якщо вихід >8 біт
        const bool high_depth = media::pix_fmt_bit_depth(video_.output_pix_fmt()) > 8;
        blender_ = std::make_unique<frames::MotionBlender>(s_.motion_blur_samples, s_.shutter_degrees, high_depth, pool_);
        if (s_.motion_blur_samples > 1)
            log_info("{}", trf("Motion blur: {} під-кадрів, затвор {:.0f}° (усереднюється {})", s_.motion_blur_samples,
                     s_.shutter_degrees, blender_->used_samples()));
    }
    speed_ = spec.speed > 0 ? spec.speed : 1.0;

    // ---- Звук ----
    // Уповільнення/прискорення: звук гри, голоси й мікрофон записані в часі демо — розтягуємо
    // їх (atempo, висота тону та сама) до часу відео; або відео без звуку
    const bool tempo = std::abs(spec.speed - 1.0) > 1e-6;
    if (s_.audio_enabled && tempo && spec.speed_mute) log_info("{}", trf("Швидкість ×{:g}: відео без звуку", spec.speed));
    else if (s_.audio_enabled && tempo) log_info("{}", trf("Швидкість ×{:g}: звук розтягнуто (atempo), висота тону та сама", spec.speed));
    if (s_.audio_enabled && !(tempo && spec.speed_mute)) {
        std::vector<std::unique_ptr<audio::AudioInput>> inputs;
        // Джерело в часі демо -> у часі відео. Джерело переходить у володіння обгортки: мікшер
        // відкидає прочитане за позицією у відео, а джерелу потрібна позиція в демо.
        auto in_video_time = [&](audio::AudioInput* in, bool mono) -> audio::AudioInput* {
            if (!tempo || !in) return in;
            auto it = std::find_if(inputs.begin(), inputs.end(), [&](const auto& p) { return p.get() == in; });
            if (it == inputs.end()) return in;
            auto f = std::make_unique<audio::FilteredInput>(
                in->name(), std::vector<std::vector<audio::TrackSource>>{{{in, 1.0f}}}, mono);
            std::string ferr;
            if (!f->open(audio::tempo_filter(spec.speed), nullptr, &ferr)) {
                log_warn("{}", trf("{}: не вдалося змінити темп ({}) — звук без розтягування", in->name(), ferr));
                return in;
            }
            f->set_input_rate(spec.speed);
            f->own(std::move(*it));
            inputs.erase(it);
            audio::AudioInput* out = f.get();
            inputs.push_back(std::move(f));
            return out;
        };
        std::vector<audio::AudioTrackPlan> tracks;
        audio::AudioTrackPlan mix;
        mix.title = tr("Мікс");
        audio::AudioInput* game = nullptr;
        audio::AudioInput* mic = nullptr;
        std::vector<audio::AudioInput*> voices;
        std::vector<float> voice_gains;
        std::vector<std::string> voice_keys;   // гравець кожного голосу (для окремих WAV)
        if (spec.game_audio) {
            auto g = std::make_unique<audio::GameAudioInput>(spec.game_wav, spec.game_wav_live, spec.game_offset);
            g->set_read_ahead(spec.game_read_ahead);
            if (!spec.game_segments.empty()) {
                // Кілька WAV: перший — одразу (з його місцем у часі), решта — коли до них дійде відео
                g->start_segment(spec.game_segments.front().first,
                                 std::llround(spec.game_segments.front().second * kMixRate));
                segments_.assign(spec.game_segments.begin() + 1, spec.game_segments.end());
            }
            game_input_ = g.get();
            game = g.get();
            inputs.push_back(std::move(g));
            game = in_video_time(game, false);
        }
        for (size_t i = 0; i < spec.voices.size(); ++i) {
            const audio::VoiceCleanup* cl = i < spec.voice_cleanup.size() ? &spec.voice_cleanup[i] : nullptr;
            const float gain = spec.voice_gain * (i < spec.voice_gains.size() ? spec.voice_gains[i] : 1.0f) *
                               (cl ? cl->level : 1.0f);
            if (gain <= 0.0f) continue;   // вимкнений гравець
            auto vi = std::make_unique<audio::VoiceInput>(spec.voices[i], spec.voice_origin_sample, spec.voice_delay);
            audio::AudioInput* v = vi.get();
            if (cl && cl->denoise && cl->profile.valid()) {
                // Шумодав (afftdn) і гейт тиші між фразами — з порогами саме цього гравця
                auto f = std::make_unique<audio::FilteredInput>(
                    vi->name(), std::vector<std::vector<audio::TrackSource>>{{{vi.get(), 1.0f}}}, true);
                const audio::GateParams gp = audio::gate_for(cl->profile);
                std::string ferr;
                bool ok = f->open(audio::denoise_filter(cl->profile.noise_db), &gp, &ferr);
                if (!ok) {
                    log_warn("{}", trf("{}: без afftdn ({}), лише гейт", vi->name(), ferr));
                    ok = f->open({}, &gp, &ferr);
                }
                if (ok) {
                    log_info("{}", trf("  {}: шумодав, фон {:.0f} дБ, мова {:.0f} дБ, гейт від {:.0f} дБ", vi->name(),
                             cl->profile.noise_db, cl->profile.speech_db, gp.open_db));
                    f->own(std::move(vi));
                    v = f.get();
                    inputs.push_back(std::move(f));
                }
            }
            if (vi) inputs.push_back(std::move(vi));
            v = in_video_time(v, true);
            voices.push_back(v);
            voice_gains.push_back(gain);
            voice_keys.push_back(spec.voices[i]->key);
        }
        if (!spec.mic_file.empty()) {
            auto m = std::make_unique<audio::FileAudioInput>(spec.mic_file, spec.mic_offset);
            if (!m->ok()) {
                log_warn("{}", trf("Файл мікрофона не додано: {}", m->error()));
            } else {
                mic = m.get();
                inputs.push_back(std::move(m));
                mic = in_video_time(mic, false);
            }
        }
        // Гра стихає, коли хтось говорить: другий вхід компресора — сума голосів
        audio::AudioInput* game_in_mix = game;
        if (game && spec.duck_game && (!voices.empty() || mic)) {
            std::vector<audio::TrackSource> sidechain;
            for (size_t i = 0; i < voices.size(); ++i) sidechain.push_back({voices[i], voice_gains[i]});
            if (mic) sidechain.push_back({mic, spec.mic_gain});
            auto d = std::make_unique<audio::FilteredInput>(
                tr("Гра (стихає під голоси)"), std::vector<std::vector<audio::TrackSource>>{{{game, 1.0f}}, sidechain}, false);
            std::string ferr;
            if (d->open(audio::duck_filter(), nullptr, &ferr)) {
                game_in_mix = d.get();
                inputs.push_back(std::move(d));
            } else {
                log_warn("{}", trf("Гра не стихатиме під голоси: {}", ferr));
            }
        }
        if (game_in_mix) mix.sources.push_back({game_in_mix, spec.game_gain});
        for (size_t i = 0; i < voices.size(); ++i) mix.sources.push_back({voices[i], voice_gains[i]});
        if (mic) mix.sources.push_back({mic, spec.mic_gain});
        mix.post_filter = audio::loudness_filter(spec.loudness_target);
        if (!mix.post_filter.empty()) log_info("{}", trf("Гучність міксу — {:.0f} LUFS (EBU R128, loudnorm)", spec.loudness_target));
        if (!mix.sources.empty()) {
            tracks.push_back(mix);
            const bool stems = !s_.stems_dir.empty();
            std::vector<std::pair<std::string, std::string>> kinds = {{"mix", ""}};   // що в кожній доріжці
            if (s_.separate_tracks || stems) {
                if (game) {
                    tracks.push_back({tr("Гра"), {{game, spec.game_gain}}});
                    kinds.push_back({"game", ""});
                }
                for (size_t i = 0; i < voices.size(); ++i) {
                    tracks.push_back({voices[i]->name(), {{voices[i], voice_gains[i]}}});
                    kinds.push_back({"voice", voice_keys[i]});
                }
                if (mic) {
                    tracks.push_back({tr("Мікрофон"), {{mic, spec.mic_gain}}});
                    kinds.push_back({"mic", ""});
                }
            }
            if (muxer_.is_image_sequence()) {
                // У послідовність зображень звук не вбудувати — пишемо WAV поруч
                std::filesystem::path out = path_from_utf8(s_.output_path);
                std::filesystem::path wav = out.parent_path() / "audio.wav";
                side_wav_ = std::make_unique<audio::WavWriter>();
                if (!side_wav_->open(wav, kMixRate, 2, audio::WavWriter::Format::Int16, error)) return false;
                log_info("{}", trf("Звук буде збережено окремо: {}", path_to_utf8(wav)));
            } else {
                // У файл — лише мікс, або всі доріжки ("окремі доріжки для монтажу")
                const size_t encoded = s_.separate_tracks ? tracks.size() : 1;
                for (size_t i = 0; i < encoded; ++i) {
                    const auto& t = tracks[i];
                    auto enc = std::make_unique<media::AudioEncoder>();
                    if (!enc->open(s_.audio, global_header, error)) return false;
                    if (!muxer_.supports_codec(s_.audio.codec)) {
                        if (error) *error = trf("контейнер '{}' не підтримує аудіокодек {}",
                                                        muxer_.format()->name, s_.audio.codec);
                        return false;
                    }
                    audio_streams_.push_back(muxer_.add_stream(enc->context(), t.title));
                    audio_encoders_.push_back(std::move(enc));
                }
                log_info("{}", trf("Звук: {} доріжк(и), {}", audio_encoders_.size(), audio_encoders_.front()->describe()));
            }
            if (stems) {
                // Пакет для монтажу: кожне джерело — окремий WAV 24 біт, від першого кадру відео
                const std::filesystem::path dir = path_from_utf8(s_.stems_dir);
                std::error_code ec;
                std::filesystem::create_directories(dir, ec);
                stem_wavs_.resize(tracks.size());
                for (size_t i = 1; i < tracks.size(); ++i) {
                    const std::filesystem::path p =
                        dir / path_from_utf8(std::format("{:02} {}.wav", i, safe_file_name(tracks[i].title)));
                    auto w = std::make_unique<audio::WavWriter>();
                    if (!w->open(p, kMixRate, 2, audio::WavWriter::Format::Int24, error)) return false;
                    stem_wavs_[i] = std::move(w);
                    stem_files_.push_back({tracks[i].title, path_to_utf8(p), kinds[i].first, kinds[i].second});
                }
                log_info("{}", trf("Пакет для монтажу: {} окремих WAV у {}", stem_files_.size(), s_.stems_dir));
            }
            for (const auto& t : tracks) {
                std::string names;
                for (const auto& src : t.sources) names += (names.empty() ? "" : " + ") + src.input->name();
                log_info("{}", trf("  Доріжка «{}»: {}", t.title, names));
            }
            mixer_ = std::make_unique<audio::AudioMixer>(std::move(inputs), std::move(tracks));
        } else {
            log_info("{}", trf("Звук: немає джерел — відео буде без звуку"));
        }
    }
    // Додаткові версії: помилка в одній не зупиняє основний рендер
    for (const auto& x : s_.extras) {
        auto e = std::make_unique<Extra>();
        e->cfg = x;
        std::string xerr;
        if (open_extra(*e, mixer_ != nullptr, &xerr)) {
            log_info("{}", trf("Додаткова версія «{}»: {} → {}", x.label, e->video.describe(), x.output_path));
            extras_.push_back(std::move(e));
        } else {
            e->muxer.abort();
            std::error_code ec;
            std::filesystem::remove(path_from_utf8(x.output_path), ec);
            log_warn("{}", trf("Додаткову версію «{}» пропущено: {}", x.label, xerr));
        }
    }
    if (!muxer_.write_header(s_.faststart, s_.crash_safe, error)) return false;
    if (s_.crash_safe && muxer_.is_fragmented())
        log_info("{}", trf("Файл пишеться фрагментами: навіть якщо гра чи ПК впадуть, уже записане відео відкриється"));
    started_ = true;
    if (!copy_mode()) worker_ = std::thread([this] { worker_loop(); });
    return true;
}

bool EncodeSession::open_extra(Extra& e, bool with_audio, std::string* error) {
    std::error_code ec;
    const std::filesystem::path parent = path_from_utf8(e.cfg.output_path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, ec);
    if (!e.muxer.open(e.cfg.output_path, e.cfg.container, error)) return false;
    if (!e.muxer.supports_codec(e.cfg.video.codec)) {
        if (error) *error = trf("контейнер '{}' не підтримує кодек {}", e.muxer.format()->name, e.cfg.video.codec);
        return false;
    }
    const bool global_header = e.muxer.needs_global_header();
    if (!e.cfg.video_parts.empty()) {
        e.video_stream = add_copy_stream(e.muxer, e.cfg.video_parts.front(), e.cfg.video.fps, nullptr, error);
        if (e.video_stream < 0) return false;
    } else {
        if (!e.video.open(e.cfg.video, frame_w_, frame_h_, global_header, error)) return false;
        e.video_stream = e.muxer.add_stream(e.video.context(), "GMod demo");
    }
    if (with_audio && !e.cfg.audio.codec.empty()) {
        if (!e.muxer.supports_codec(e.cfg.audio.codec)) {
            if (error) *error = trf("контейнер '{}' не підтримує аудіокодек {}", e.muxer.format()->name,
                                            e.cfg.audio.codec);
            return false;
        }
        e.audio = std::make_unique<media::AudioEncoder>();
        if (!e.audio->open(e.cfg.audio, global_header, error)) return false;
        e.audio_stream = e.muxer.add_stream(e.audio->context(), tr("Мікс"));
    }
    return e.muxer.write_header(true, false, error);
}

void EncodeSession::fail_extra(Extra& e, const std::string& error) {
    if (!e.ok.exchange(false)) return;
    log_warn("{}", trf("Додаткова версія «{}» не вдалася: {} — основне відео пишеться далі", e.cfg.label, error));
    e.muxer.abort();
    std::error_code ec;
    std::filesystem::remove(path_from_utf8(e.cfg.output_path), ec);
}

std::vector<std::string> EncodeSession::finished_extras() const {
    std::vector<std::string> out;
    for (const auto& e : extras_)
        if (e->ok && e->finished) out.push_back(e->cfg.output_path);
    return out;
}

bool EncodeSession::encode_video_frame(const frames::Image& img, std::string* error) {
    const int64_t pts = frames_out_.load();
    const double conv0 = video_.convert_ms(), enc0 = video_.encode_ms();
    const bool ok = video_.encode(img, pts, [&](AVPacket* p) {
        return muxer_.write_packet(video_stream_, p, video_.context()->time_base);
    }, error);
    atomic_add(convert_ms_, video_.convert_ms() - conv0);
    atomic_add(encode_ms_, video_.encode_ms() - enc0);
    if (!ok) {
        if (error && error->empty()) *error = muxer_.last_error();
        return false;
    }
    for (auto& e : extras_) {
        if (!e->ok) continue;
        const double c0 = e->video.convert_ms(), e0 = e->video.encode_ms();
        std::string xerr;
        if (!e->video.encode(img, pts, [&](AVPacket* p) {
                return e->muxer.write_packet(e->video_stream, p, e->video.context()->time_base);
            }, &xerr))
            fail_extra(*e, xerr.empty() ? e->muxer.last_error() : xerr);
        atomic_add(convert_ms_, e->video.convert_ms() - c0);
        atomic_add(encode_ms_, e->video.encode_ms() - e0);
    }
    ++frames_out_;
    return true;
}

void EncodeSession::worker_loop() {
    for (;;) {
        frames::Image img;
        {
            std::unique_lock lock(q_mutex_);
            q_cv_.wait(lock, [&] { return !queue_.empty() || worker_stop_; });
            if (queue_.empty()) return;   // зупинка, черга порожня
            img = std::move(queue_.front());
            queue_.pop_front();
        }
        q_space_cv_.notify_all();
        std::string err;
        bool ok = encode_video_frame(img, &err);
        if (ok) {
            capture_preview(img);
            img.release();   // буфер кадру — назад у пул, поки кодуємо звук
            ok = pump_audio(&err);
        }
        if (!ok) {
            {
                std::lock_guard lock(q_mutex_);
                worker_failed_ = true;
                worker_error_ = err.empty() ? tr("помилка кодування") : err;
                queue_.clear();
            }
            q_space_cv_.notify_all();
            return;
        }
    }
}

bool EncodeSession::enqueue(frames::Image&& img, std::string* error) {
    std::unique_lock lock(q_mutex_);
    q_space_cv_.wait(lock, [&] { return queue_.size() < queue_max_ || worker_failed_ || worker_stop_; });
    if (worker_failed_) {
        if (error) *error = worker_error_;
        return false;
    }
    queue_.push_back(std::move(img));
    lock.unlock();
    q_cv_.notify_one();
    return true;
}

bool EncodeSession::stop_worker(std::string* error) {
    if (worker_.joinable()) {
        {
            std::lock_guard lock(q_mutex_);
            worker_stop_ = true;
        }
        q_cv_.notify_all();
        worker_.join();
    }
    std::lock_guard lock(q_mutex_);
    if (worker_failed_) {
        if (error) *error = worker_error_;
        return false;
    }
    return true;
}

void EncodeSession::draw_overlay(frames::Image& img) {
    const int64_t n = blended_frames_++;
    if (overlay_) overlay_->draw(img, static_cast<double>(n) * s_.video.fps.den / s_.video.fps.num);
}

bool EncodeSession::push_subframe(frames::Image&& img, std::string* error) {
    if (!started_) {
        if (error) *error = tr("сесію кодування не запущено");
        return false;
    }
    {
        std::lock_guard lock(q_mutex_);
        if (worker_failed_) {
            if (error) *error = worker_error_;
            return false;
        }
    }
    ++subframes_in_;
    const auto t0 = Clock::now();
    auto out = blender_->push(std::move(img));
    if (s_.motion_blur_samples > 1) blend_ms_ += ms_since(t0);
    if (!out) return true;
    draw_overlay(*out);
    return enqueue(std::move(*out), error);
}

// Викликається під audio_mutex_.
bool EncodeSession::produce_audio(int64_t until, std::string* error, bool final) {
    if (!mixer_) return true;
    bool ok = true;
    auto sink = [&](size_t track, const float* data, size_t frames) {
        if (!ok) return;
        if (track == 0) {
            for (auto& e : extras_) {
                if (!e->ok || !e->audio) continue;
                std::string xerr;
                const AVRational tb = e->audio->context()->time_base;
                if (!e->audio->push(data, frames, [&](AVPacket* p) { return e->muxer.write_packet(e->audio_stream, p, tb); },
                                    &xerr))
                    fail_extra(*e, xerr);
            }
        }
        if (track < stem_wavs_.size() && stem_wavs_[track]) stem_wavs_[track]->write(data, frames);
        if (side_wav_) {
            if (track == 0) side_wav_->write(data, frames);
            return;
        }
        if (track >= audio_encoders_.size()) return;   // доріжка лише для окремого WAV
        auto& enc = audio_encoders_[track];
        const int stream = audio_streams_[track];
        const AVRational tb = enc->context()->time_base;
        if (!enc->push(data, frames, [&](AVPacket* p) { return muxer_.write_packet(stream, p, tb); }, error)) ok = false;
    };
    mixer_->produce(until, sink);
    if (final) mixer_->flush(until, sink);   // хвіст фільтрів доріжок (loudnorm тримає 3 с)
    return ok;
}

void EncodeSession::start_due_segments() {
    if (!game_input_) return;
    const double v = video_seconds();
    while (!segments_.empty() && segments_.front().second / speed_ <= v + 1e-9) {
        game_input_->start_segment(segments_.front().first, std::llround(segments_.front().second * kMixRate));
        segments_.erase(segments_.begin());
    }
}

bool EncodeSession::pump_audio(std::string* error) {
    std::lock_guard lock(audio_mutex_);
    if (!mixer_) return true;
    start_due_segments();
    const auto t0 = Clock::now();
    const int64_t target = static_cast<int64_t>(std::llround(video_seconds() * kMixRate));
    const int64_t until = std::min(target, mixer_->ready_until());
    if (until <= mixer_->position()) return true;
    const bool ok = produce_audio(until, error);
    atomic_add(audio_ms_, ms_since(t0));
    return ok;
}

bool EncodeSession::copy_video(const std::atomic<bool>* cancel, const std::function<void(double)>& progress,
                               std::string* error) {
    if (!started_ || !copy_mode()) return true;
    const AVRational enc_tb{s_.video.fps.den, s_.video.fps.num};
    uint64_t total_bytes = 0, done_bytes = 0;   // прогрес — за позицією читання у файлах частин
    for (const auto& p : s_.video_parts) total_bytes += file_size_or_zero(path_from_utf8(p));
    auto report = [&](uint64_t done) {
        if (progress) progress(total_bytes ? std::min(1.0, static_cast<double>(done) / static_cast<double>(total_bytes)) : 0.0);
    };
    // pts і dts частини — у номерах кадрів від її початку, зсунуті на кадри попередніх частин
    // (як у звичайному рендері: кадр n має pts n у часі 1/FPS)
    auto shift = [&](AVPacket* p, AVRational tb, int64_t base) {
        const auto frame = [&](int64_t t) {
            if (t == AV_NOPTS_VALUE) return t;
            return base + av_rescale_q_rnd(t, tb, enc_tb, static_cast<AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        };
        p->pts = frame(p->pts);
        p->dts = frame(p->dts);
        p->duration = 1;
        p->pos = -1;
    };
    struct PacketDel {
        void operator()(AVPacket* p) const { av_packet_free(&p); }
    };
    using Packet = std::unique_ptr<AVPacket, PacketDel>;
    Packet pkt(av_packet_alloc());
    int64_t base = 0;
    auto last_progress = Clock::now();
    for (size_t k = 0; k < s_.video_parts.size(); ++k) {
        PartReader in;
        if (!in.open(s_.video_parts[k], error)) return false;
        // Додаткові версії цієї частини: пакет наперед, щоб писати їх упереміш з основним відео
        struct ExtraIn {
            Extra*     e = nullptr;
            PartReader r;
            Packet     pending{av_packet_alloc()};
            bool       has = false;
            int64_t    written = 0;
        };
        std::vector<std::unique_ptr<ExtraIn>> xs;
        for (auto& e : extras_) {
            if (!e->ok || k >= e->cfg.video_parts.size()) continue;
            auto x = std::make_unique<ExtraIn>();
            x->e = e.get();
            std::string xerr;
            if (!x->r.open(e->cfg.video_parts[k], &xerr)) {
                fail_extra(*e, xerr);
                continue;
            }
            x->has = x->r.next(x->pending.get());
            xs.push_back(std::move(x));
        }
        auto write_extras = [&](int64_t upto) {   // скільки пакетів (кадрів) має бути в кожній версії
            for (auto& x : xs) {
                while (x->has && x->e->ok && x->written < upto) {
                    shift(x->pending.get(), x->r.time_base(), base);
                    if (!x->e->muxer.write_packet(x->e->video_stream, x->pending.get(), enc_tb))
                        fail_extra(*x->e, x->e->muxer.last_error());
                    av_packet_unref(x->pending.get());
                    ++x->written;
                    x->has = x->r.next(x->pending.get());
                }
            }
        };
        // Обмеження частини (дописування після збою): кадри від ключового кадру limit і далі — ні
        const int64_t limit =
            k < s_.video_part_frames.size() && s_.video_part_frames[k] > 0 ? s_.video_part_frames[k] : INT64_MAX;
        int64_t part_frames = 0, part_packets = 0;
        while (in.next(pkt.get())) {
            if (cancel && *cancel) {
                av_packet_unref(pkt.get());
                if (error) *error = tr("скасовано");
                return false;
            }
            shift(pkt.get(), in.time_base(), base);
            if (limit != INT64_MAX && pkt->pts != AV_NOPTS_VALUE && pkt->pts - base >= limit) {
                const bool key = (pkt->flags & AV_PKT_FLAG_KEY) != 0;
                av_packet_unref(pkt.get());
                if (key) break;   // далі — наступні групи кадрів (GOP), їх не беремо
                continue;
            }
            if (pkt->pts != AV_NOPTS_VALUE) part_frames = std::max(part_frames, pkt->pts - base + 1);
            if (!muxer_.write_packet(video_stream_, pkt.get(), enc_tb)) {
                av_packet_unref(pkt.get());
                if (error) *error = muxer_.last_error();
                return false;
            }
            av_packet_unref(pkt.get());
            ++part_packets;
            frames_out_ = std::max(frames_out_.load(), base + part_frames);
            write_extras(part_packets);
            if (!pump_audio(error)) return false;
            if (Clock::now() - last_progress > std::chrono::milliseconds(200)) {
                last_progress = Clock::now();
                report(done_bytes + static_cast<uint64_t>(in.position()));
            }
        }
        write_extras(limit == INT64_MAX ? INT64_MAX : part_packets);
        done_bytes += file_size_or_zero(path_from_utf8(s_.video_parts[k]));
        report(done_bytes);
        log_debug("Склеювання: частина {} — {} кадрів ({})", k + 1, part_frames, s_.video_parts[k]);
        base += part_frames;
        frames_out_ = base;
    }
    return pump_audio(error);
}

void EncodeSession::game_audio_finished() {
    std::lock_guard lock(audio_mutex_);
    if (game_input_) game_input_->set_finished();
}

void EncodeSession::game_audio_new_segment(const std::filesystem::path& wav, double demo_seconds) {
    std::lock_guard lock(audio_mutex_);
    if (game_input_) game_input_->start_segment(wav, std::llround(std::max(0.0, demo_seconds) * kMixRate));
}

bool EncodeSession::finish(std::string* error) {
    if (!started_ || finished_) return true;
    // Незавершена група motion blur
    if (blender_) {
        if (auto last = blender_->flush()) {
            draw_overlay(*last);
            if (!enqueue(std::move(*last), error)) return false;
        }
    }
    if (!stop_worker(error)) return false;
    if (!copy_mode() &&
        !video_.flush([&](AVPacket* p) { return muxer_.write_packet(video_stream_, p, video_.context()->time_base); },
                      error))
        return false;
    for (auto& e : extras_) {
        std::string xerr;
        if (e->ok && e->cfg.video_parts.empty() && !e->video.flush([&](AVPacket* p) {
                return e->muxer.write_packet(e->video_stream, p, e->video.context()->time_base);
            }, &xerr))
            fail_extra(*e, xerr);
    }
    {
        std::lock_guard lock(audio_mutex_);
        if (mixer_) {
            mixer_->set_finished();
            const int64_t total = static_cast<int64_t>(std::llround(video_seconds() * kMixRate));
            if (!produce_audio(total, error, true)) return false;
            for (size_t i = 0; i < audio_encoders_.size(); ++i) {
                const int stream = audio_streams_[i];
                const AVRational tb = audio_encoders_[i]->context()->time_base;
                if (!audio_encoders_[i]->flush([&](AVPacket* p) { return muxer_.write_packet(stream, p, tb); }, error))
                    return false;
            }
            if (side_wav_) side_wav_->close(error);
            for (auto& w : stem_wavs_)
                if (w) w->close(nullptr);
            for (auto& e : extras_) {
                std::string xerr;
                const AVRational tb = e->audio ? e->audio->context()->time_base : AVRational{1, 1};
                if (e->ok && e->audio &&
                    !e->audio->flush([&](AVPacket* p) { return e->muxer.write_packet(e->audio_stream, p, tb); }, &xerr))
                    fail_extra(*e, xerr);
            }
        }
    }
    for (auto& e : extras_) {
        std::string xerr;
        if (!e->ok) continue;
        if (e->muxer.finish(&xerr)) e->finished = true;
        else fail_extra(*e, xerr);
    }
    finished_ = true;
    return muxer_.finish(error);
}

void EncodeSession::abort() {
    {
        std::lock_guard lock(q_mutex_);
        worker_stop_ = true;
        queue_.clear();
    }
    q_cv_.notify_all();
    q_space_cv_.notify_all();
    if (worker_.joinable()) worker_.join();
    finished_ = true;
    muxer_.abort();
    for (auto& e : extras_) e->muxer.abort();
    std::lock_guard lock(audio_mutex_);
    if (side_wav_) side_wav_->close(nullptr);
    for (auto& w : stem_wavs_)
        if (w) w->close(nullptr);
}

void EncodeSession::capture_preview(const frames::Image& img) {
    if (!preview_) return;
    const int64_t t = now_ms();
    if (t - preview_last_ms_ < 250) return;   // 4 кадри на секунду достатньо
    preview_last_ms_ = t;
    // Зменшена копія, що вміщується в 480×270 (з тими самими пропорціями)
    const double k = std::min({1.0, 480.0 / img.width, 270.0 / img.height});
    const int pw = std::max(2, static_cast<int>(img.width * k)) & ~1;
    const int ph = std::max(2, static_cast<int>(img.height * k)) & ~1;
    const AVPixelFormat in_fmt = media::image_pix_fmt(img.layout);
    preview_sws_ = sws_getCachedContext(preview_sws_, img.width, img.height, in_fmt, pw, ph, AV_PIX_FMT_RGBA,
                                        SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
    if (!preview_sws_) return;
    if (frames::is_yuv(img.layout)) {
        const int* in = sws_getCoefficients(img.bt709 ? SWS_CS_ITU709 : SWS_CS_ITU601);
        sws_setColorspaceDetails(preview_sws_, in, img.full_range ? 1 : 0, sws_getCoefficients(SWS_CS_ITU709), 1, 0,
                                 1 << 16, 1 << 16);
    }
    std::vector<uint8_t> rgba(static_cast<size_t>(pw) * static_cast<size_t>(ph) * 4);
    const uint8_t* src[4] = {nullptr, nullptr, nullptr, nullptr};
    int src_stride[4] = {0, 0, 0, 0};
    for (int p = 0; p < img.planes(); ++p) {
        src[p] = img.row(p, 0);
        src_stride[p] = img.stride[p];
    }
    uint8_t* dst[4] = {rgba.data(), nullptr, nullptr, nullptr};
    const int dst_stride[4] = {pw * 4, 0, 0, 0};
    sws_scale(preview_sws_, src, src_stride, 0, img.height, dst, dst_stride);
    preview_->set(pw, ph, std::move(rgba));
}

} // namespace gmdr::render
