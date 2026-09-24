#include "dubbing.hpp"

#include "../dub/dub_mix.hpp"
#include "../dub/voice_library.hpp"
#include "../media/muxer.hpp"
#include "../util/file_util.hpp"
#include "../util/i18n.hpp"
#include "../util/log.hpp"
#include "../util/secret.hpp"
#include "../util/strings.hpp"
#include "subtitles.hpp"

#include <algorithm>
#include <format>
#include <map>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace gmdr::render {

namespace fs = std::filesystem;

namespace {
double speed_of(const RenderSettings& s) { return s.speed > 0 ? std::clamp(s.speed, 0.1, 16.0) : 1.0; }

bool is_mov(const std::string& path) {
    const std::string e = to_lower(path_to_utf8(path_from_utf8(path).extension()));
    return e == ".mp4" || e == ".m4v" || e == ".mov";
}

std::string language_name(const std::string& code) {
    const translate::Language* l = translate::find_language(code);
    return l ? l->native : code;
}

std::string iso3_of(const std::string& code) {
    const translate::Language* l = translate::find_language(code);
    return l ? l->iso3 : std::string();
}

// Назва озвученої доріжки: «Deutsch (озвучення ШІ)» — щоб глядач бачив, що голос синтезовано
std::string dub_track_title(const std::string& lang) { return trf("{} (озвучення ШІ)", language_name(lang)); }

// Файл поруч із відео: <назва><suffix>
fs::path beside(const RenderSettings& s, const std::string& suffix) {
    const fs::path out = path_from_utf8(s.output_path);
    return out.parent_path() / path_from_utf8(path_to_utf8(out.stem()) + suffix);
}

std::string source_language(const RenderSettings& s) {
    return s.whisper_language.empty() || s.whisper_language == "auto" ? "auto" : s.whisper_language;
}

// Репліка, що потрапила у відео
struct Cue {
    const speech::Line* line;
    double              start, end;   // с відео
};

std::vector<Cue> cues_in_video(const RenderSettings& s, const std::vector<speech::Line>& lines,
                               const std::vector<std::string>& keys, double origin, double duration) {
    const double sp = speed_of(s);
    std::vector<Cue> out;
    for (const auto& l : lines) {
        if (!keys.empty() && std::find(keys.begin(), keys.end(), l.speaker_key) == keys.end()) continue;
        if (speech::is_noise_text(l.text)) continue;
        const double a = (l.start + s.voice_delay - origin) / sp, b = (l.end + s.voice_delay - origin) / sp;
        if (b <= 0 || a >= duration) continue;
        out.push_back({&l, a, b});
    }
    return out;
}

bool encoder_available(const std::string& name) { return avcodec_find_encoder_by_name(name.c_str()) != nullptr; }
} // namespace

// ============================ Налаштування ==============================================
std::vector<std::string> dub_language_list(const RenderSettings& s) {
    std::vector<std::string> out;
    for (const auto& c : split(s.dub_languages, ',')) {
        const translate::Language* l = translate::find_language(trim(c));
        if (l && std::find(out.begin(), out.end(), l->code) == out.end()) out.push_back(l->code);
    }
    return out;
}

bool translation_requested(const RenderSettings& s) { return (s.translate_subtitles || s.dub) && !dub_language_list(s).empty(); }

bool dub_output(const RenderSettings& s, const std::string& what) {
    for (const auto& x : split(s.dub_outputs, ','))
        if (trim(x) == what) return true;
    return false;
}

bool dub_needs_stems(const RenderSettings& s) {
    return s.dub && s.audio && !dub_language_list(s).empty() &&
           (dub_output(s, "tracks") || dub_output(s, "videos") || dub_output(s, "audio"));
}

fs::path dub_work_dir(const RenderSettings& s) { return beside(s, ".dub_tmp"); }

std::string* translator_key_field(RenderSettings& s, const std::string& provider) {
    if (provider == "deepl") return &s.deepl_key;
    if (provider == "google") return &s.google_key;
    if (provider == "libre") return &s.libre_key;
    if (provider == "openai") return &s.openai_key;
    return nullptr;
}

const std::string* translator_key_field(const RenderSettings& s, const std::string& provider) {
    return translator_key_field(const_cast<RenderSettings&>(s), provider);
}

translate::Config translator_config(const RenderSettings& s) {
    translate::Config c;
    c.provider = s.translator;
    c.url = s.translator_url;
    c.model = s.translator_model;
    if (const std::string* k = translator_key_field(s, s.translator)) c.key = unprotect_secret(*k);
    return c;
}

dub::TtsConfig tts_config(const RenderSettings& s) {
    dub::TtsConfig c;
    c.engine = s.tts_engine;
    c.device = s.tts_device;
    c.python = s.tts_python;
    c.elevenlabs_key = unprotect_secret(s.elevenlabs_key);
    if (!s.elevenlabs_model.empty()) c.elevenlabs_model = s.elevenlabs_model;
    return c;
}

bool clone_allowed(const RenderSettings& s) { return s.tts_clone && s.tts_clone_ack; }

const std::vector<PublishTemplate>& publish_templates() {
    static const std::vector<PublishTemplate> k = {
        {"youtube", "YouTube",
         N_("Основне відео як є; для кожної мови — озвучення .mp3 (YouTube Studio → «Мови» → «Дубляж») і субтитри .srt, "
            "гучність -14 LUFS. Поруч — підказка, що куди завантажити."),
         true, "audio", "mp3", -14},
        {"tracks", N_("Одне відео, кілька мов"),
         N_("Озвучення — додатковими доріжками з мітками мови в тому самому файлі (VLC, mpv, Plex, Jellyfin, Kodi), "
            "плюс субтитри .srt."),
         true, "tracks", "", 0},
        {"shorts", "Shorts / TikTok / Reels",
         N_("Окреме відео для кожної мови з озвученням замість оригіналу — ці сервіси показують лише одну доріжку."),
         false, "videos", "", -14},
        {"discord", "Discord / Telegram",
         N_("Окреме відео для кожної мови: месенджери грають лише першу доріжку."), false, "videos", "", 0},
        {"editing", N_("Для монтажу"),
         N_("Озвучення кожної мови — окремим WAV від першого кадру, плюс субтитри .srt: у програмі монтажу кладіть під відео."),
         true, "audio", "wav", 0},
    };
    return k;
}

const PublishTemplate* find_template(const std::string& id) {
    for (const auto& t : publish_templates())
        if (id == t.id) return &t;
    return nullptr;
}

void apply_template(RenderSettings& s, const PublishTemplate& t) {
    s.dub_template = t.id;
    s.translate_subtitles = t.subtitles;
    s.dub_outputs = t.outputs;
    if (*t.audio_format) s.dub_audio_format = t.audio_format;
    if (t.loudness != 0) s.loudness_target = t.loudness;
}

// ============================ Бібліотека голосів ========================================
int collect_voice_samples(const RenderSettings& s, const std::vector<const voice::SpeakerTrack*>& speakers,
                          const speech::Transcript& t) {
    if (!s.voice_library_auto || !s.tts_clone_ack) return 0;
    const std::string demo = path_to_utf8(path_from_utf8(s.demo_path).stem());
    int total = 0;
    for (const voice::SpeakerTrack* tr_ : speakers) {
        if (!tr_ || !dub::persistent_key(tr_->key)) continue;
        dub::VoiceProfile p = dub::load_profile(tr_->key).value_or(dub::VoiceProfile{});
        p.key = tr_->key;
        const int n = dub::collect_samples(p, dub::profile_dir(tr_->key), *tr_, t.lines, demo, t.language);
        if (n <= 0) continue;
        std::string err;
        if (!dub::save_profile(p, &err)) log_warn("{}", trf("Бібліотека голосів: {}", err));
        total += n;
    }
    if (total > 0) log_info("{}", trf("Бібліотека голосів: додано зразків — {}", total));
    return total;
}

// ============================ Переклад ==================================================
namespace {
// Перекласти репліки; nullopt — не вдалося (у журналі)
std::optional<std::vector<std::string>> translate_cues(const RenderSettings& s, const std::vector<Cue>& cues,
                                                       const std::string& lang, const StageFn& stage,
                                                       const std::atomic<bool>* cancel, std::string* error) {
    std::vector<std::string> texts;
    for (const auto& c : cues) texts.push_back(trim(c.line->text));
    const translate::Config cfg = translator_config(s);
    return translate::translate(cfg, texts, source_language(s), lang,
                                [&](double f) { if (stage) stage(trf("переклад: {}", language_name(lang)), f); }, cancel, error);
}

std::string translated_srt(const RenderSettings& s, const std::vector<Cue>& cues, const std::vector<std::string>& text,
                           const std::vector<std::string>& keys, double origin, double duration) {
    std::vector<speech::Line> lines;
    for (size_t i = 0; i < cues.size(); ++i) {
        speech::Line l = *cues[i].line;
        l.text = text[i];
        if (!trim(l.text).empty()) lines.push_back(std::move(l));
    }
    return make_transcript_srt(lines, keys, origin, duration, s.voice_delay, speed_of(s));
}
} // namespace

std::vector<std::string> translate_transcript_files(const RenderSettings& s, const std::vector<speech::Line>& lines,
                                                    const std::vector<std::string>& keys, double origin, double duration,
                                                    const std::string& base_path, const StageFn& stage,
                                                    const std::atomic<bool>* cancel, std::string* error) {
    std::vector<std::string> made;
    RenderSettings plain = s;
    plain.voice_delay = 0;
    plain.speed = 1;
    const auto cues = cues_in_video(plain, lines, keys, origin, duration);
    if (cues.empty()) {
        if (error) *error = tr("у розшифровці немає реплік");
        return made;
    }
    for (const auto& lang : dub_language_list(s)) {
        std::string err;
        const auto text = translate_cues(plain, cues, lang, stage, cancel, &err);
        if (!text) {
            if (error) *error = err;
            return made;
        }
        fs::path p = path_from_utf8(base_path);
        p.replace_extension("." + lang + ".srt");
        if (!write_file_text(p, translated_srt(plain, cues, *text, keys, origin, duration), &err)) {
            if (error) *error = err;
            return made;
        }
        made.push_back(path_to_utf8(p));
    }
    return made;
}

// ============================ Переклад і озвучення після рендеру ========================
std::vector<std::string> make_translations(const RenderSettings& s, const DubSource& src, const StageFn& stage,
                                           const std::atomic<bool>* cancel) {
    std::vector<std::string> made;
    const auto langs = dub_language_list(s);
    if (langs.empty() || s.output_path.find('%') != std::string::npos) return made;
    const auto cues = cues_in_video(s, src.lines, src.keys, src.origin, src.duration);
    if (cues.empty()) {
        log_warn("{}", trf("Переклад: у фрагменті немає розпізнаних реплік — нема чого перекладати"));
        return made;
    }
    const translate::ProviderInfo* prov = translate::find_provider(s.translator);
    log_info("{}", trf("Переклад: {} реплік → {} ({})", cues.size(), s.dub_languages, prov ? tr(prov->label) : s.translator));

    // ---- Звук для озвучення: гра окремо, оригінальні голоси й мікрофон окремо ----
    const bool want_dub = s.dub;
    fs::path game_stem;
    std::vector<fs::path> original_stems;
    for (const auto& st : src.stems) {
        if (st.kind == "game") game_stem = path_from_utf8(st.path);
        else if (st.kind == "voice" || st.kind == "mic") original_stems.push_back(path_from_utf8(st.path));
    }
    bool can_dub = want_dub && !src.stems.empty();
    if (want_dub && src.stems.empty())
        log_warn("{}", trf("Озвучення пропущено: звук відео вимкнено або в ньому нічого немає"));
    const fs::path work = dub_work_dir(s) / "work";
    std::error_code ec;
    const dub::TtsConfig tcfg = tts_config(s);
    const std::string src_lang = source_language(s);
    const std::string demo_name = path_to_utf8(path_from_utf8(s.demo_path).stem());

    // ---- Голоси гравців (спільні для всіх мов) ----
    std::vector<std::string> order;
    for (const auto& c : cues)
        if (std::find(order.begin(), order.end(), c.line->speaker_key) == order.end()) order.push_back(c.line->speaker_key);
    std::vector<dub::SpeakerVoice> voices;
    std::vector<std::string> temp_cloud_voices;   // клони ElevenLabs гравців без SteamID — прибрати після озвучення
    if (can_dub) {
        fs::create_directories(work, ec);
        const bool clone = clone_allowed(s);
        if (stage) stage(tr("голоси гравців"), 0);
        for (size_t i = 0; i < order.size(); ++i) {
            const std::string& key = order[i];
            dub::SpeakerVoice v;
            v.key = key;
            const voice::SpeakerTrack* track = nullptr;
            for (const auto* t : src.speakers)
                if (t && t->key == key) track = t;
            std::string name = key;
            if (track) name = track->name.empty() ? track->display_name() : track->name;
            if (clone && track) {
                const bool keep = dub::persistent_key(key);
                dub::VoiceProfile p = keep ? dub::load_profile(key).value_or(dub::VoiceProfile{}) : dub::VoiceProfile{};
                p.key = key;
                const fs::path dir = keep ? dub::profile_dir(key) : work / "voices" / path_from_utf8(sanitize_filename(replace_all(key, ":", "_")));
                dub::collect_samples(p, dir, *track, src.lines, demo_name, src_lang);
                std::string err;
                if (keep && !dub::save_profile(p, &err)) log_warn("{}", trf("Бібліотека голосів: {}", err));
                if (auto ref = dub::make_reference(p, dir, work / "refs", 10.0)) {
                    if (tcfg.engine == "elevenlabs") {
                        if (p.elevenlabs_voice_id.empty()) {
                            if (auto id = dub::elevenlabs_clone(tcfg.elevenlabs_key, "GMDR " + p.name(), ref->files, cancel, &err)) {
                                p.elevenlabs_voice_id = *id;
                                if (keep) dub::save_profile(p, nullptr);
                                else temp_cloud_voices.push_back(*id);
                                log_info("{}", trf("Голос {}: клон ElevenLabs створено", name));
                            } else {
                                log_warn("{}", trf("Голос {}: клон не створено ({}) — готовий голос", name, err));
                            }
                        }
                        v.elevenlabs_voice_id = p.elevenlabs_voice_id;
                    } else {
                        v.ref_wav = ref->wav;
                        v.ref_text = ref->text;
                    }
                    if (!v.ref_wav.empty() || !v.elevenlabs_voice_id.empty())
                        log_info("{}", trf("Голос {}: клон зі зразків {:.1f} с", name, ref->seconds));
                } else {
                    log_info("{}", trf("Голос {}: немає чистих зразків для клонування — готовий голос", name));
                }
            }
            if (v.ref_wav.empty() && v.elevenlabs_voice_id.empty()) {
                v.instruct = dub::generic_instruct(i);
                v.elevenlabs_voice_id = !trim(s.elevenlabs_voice).empty() ? trim(s.elevenlabs_voice) : dub::generic_elevenlabs_voice(i);
            }
            voices.push_back(std::move(v));
        }
    }
    auto voice_index = [&](const std::string& key) {
        return static_cast<size_t>(std::find(order.begin(), order.end(), key) - order.begin());
    };

    // ---- Кожна мова ----
    struct Track {
        std::string lang;
        fs::path    file;   // доріжка для відео (MKA кодеком основного звуку)
    };
    std::vector<Track> tracks;
    std::vector<std::pair<std::string, fs::path>> audio_files, subtitle_files;
    const bool want_track = dub_output(s, "tracks") || dub_output(s, "videos");
    const bool want_audio = dub_output(s, "audio");
    dub::AudioFormat afmt = dub::find_audio_format(s.dub_audio_format);
    if (want_audio && !encoder_available(afmt.codec)) {
        log_warn("{}", trf("Кодек {} недоступний у цій збірці FFmpeg — окремі аудіофайли будуть FLAC", afmt.codec));
        afmt = dub::find_audio_format("flac");
    }
    for (size_t li = 0; li < langs.size(); ++li) {
        const std::string& lang = langs[li];
        if (cancel && cancel->load()) break;
        if (src_lang != "auto" && translate::base_code(src_lang) == translate::base_code(lang)) {
            log_info("{}", trf("Переклад на {} пропущено: це мова оригіналу", language_name(lang)));
            continue;
        }
        if (!translate::supports(s.translator, lang)) {
            log_warn("{}", trf("{} не перекладає на цю мову ({}) — виберіть інший сервіс", prov ? tr(prov->label) : s.translator,
                               language_name(lang)));
            continue;
        }
        std::string err;
        const auto text = translate_cues(s, cues, lang, stage, cancel, &err);
        if (!text) {
            if (!(cancel && cancel->load())) log_warn("{}", trf("Переклад на {} не вдався: {}", language_name(lang), err));
            continue;
        }
        if (s.translate_subtitles) {
            fs::path p = path_from_utf8(s.output_path);
            p.replace_extension("." + lang + ".srt");
            if (write_file_text(p, translated_srt(s, cues, *text, src.keys, src.origin, src.duration), &err)) {
                made.push_back(path_to_utf8(p));
                subtitle_files.push_back({lang, p});
                log_info("{}", trf("Субтитри ({}): {}", language_name(lang), path_to_utf8(p)));
            } else {
                log_warn("{}", trf("Не вдалося записати субтитри: {}", err));
            }
        }
        if (!can_dub || (!want_track && !want_audio)) continue;
        if (!dub::engine_supports(tcfg.engine, tcfg.elevenlabs_model, lang)) {
            log_warn("{}", trf("Озвучення {} пропущено: {} не вміє цієї мови", language_name(lang),
                               tcfg.engine == "elevenlabs" ? std::string("ElevenLabs ") + tcfg.elevenlabs_model : tcfg.engine));
            continue;
        }
        // Синтез
        const fs::path lw = work / lang;
        fs::create_directories(lw, ec);
        std::vector<dub::TtsItem> items;
        std::vector<size_t> item_cue;
        for (size_t i = 0; i < cues.size(); ++i) {
            if (trim((*text)[i]).empty()) continue;
            items.push_back({trim((*text)[i]), lang, voice_index(cues[i].line->speaker_key), lw / std::format("{:04}.wav", i)});
            item_cue.push_back(i);
        }
        const std::string what = trf("озвучення: {}", language_name(lang));
        if (stage) stage(what, 0);
        std::vector<std::string> warnings;
        const bool synth_ok = dub::synthesize(tcfg, voices, items, lw,
                                              [&](double f, const std::string& w) { if (stage) stage(w.empty() ? what : what + " — " + w, f * 0.8); },
                                              cancel, &warnings, &err);
        for (const auto& w : warnings) log_warn("{}", trf("Озвучення: {}", w));
        if (!synth_ok) {
            if (!(cancel && cancel->load())) log_warn("{}", trf("Озвучення {} не вдалося: {}", language_name(lang), err));
            // Рушій не працює — інші мови теж не вийдуть
            can_dub = false;
            continue;
        }
        // Фрази на місця
        std::vector<dub::Slot> slots;
        std::vector<std::vector<float>> pcm;
        for (size_t k = 0; k < items.size(); ++k) {
            const Cue& c = cues[item_cue[k]];
            std::vector<float> clip;
            if (fs::exists(items[k].out, ec)) {
                if (auto l = dub::load_clip(items[k].out, &err)) clip = std::move(*l);
                else log_warn("{}", trf("Озвучення: фразу «{}» не прочитано: {}", items[k].text, err));
            }
            slots.push_back({c.start, c.end, c.line->speaker_key, static_cast<double>(clip.size() / 2) / media::kMixRate});
            pcm.push_back(std::move(clip));
        }
        const auto places = dub::place_clips(slots);
        std::vector<dub::Clip> clips;
        int faster = 0, later = 0;
        for (size_t k = 0; k < slots.size(); ++k) {
            if (pcm[k].empty()) continue;
            if (places[k].tempo > 1.01) ++faster;
            if (places[k].at > slots[k].start + 0.3) ++later;
            clips.push_back({static_cast<int64_t>(std::llround(places[k].at * media::kMixRate)), dub::change_tempo(std::move(pcm[k]), places[k].tempo)});
        }
        log_info("{}", trf("Озвучення {}: {} фраз; пришвидшено {}, зсунуто пізніше {}", language_name(lang), clips.size(), faster, later));
        // Мікс і файли
        dub::MixSpec spec;
        spec.game = game_stem;
        spec.originals = original_stems;
        spec.original_gain = static_cast<float>(std::clamp(s.dub_original_volume, 0.0, 1.0));
        spec.dub_gain = static_cast<float>(std::clamp(s.voice_volume, 0.0, 4.0));
        spec.duck = s.duck_game;
        spec.loudness = s.loudness_target;
        spec.seconds = src.duration;
        std::vector<dub::MixOutput> outs;
        const std::string title = dub_track_title(lang);
        const fs::path track_file = work / (lang + ".mka");
        if (want_track) outs.push_back({path_to_utf8(track_file), "matroska", src.main_audio, title, iso3_of(lang)});
        fs::path audio_file;
        if (want_audio) {
            audio_file = beside(s, "." + lang + afmt.ext);
            media::AudioEncoderSettings a;
            a.codec = afmt.codec;
            a.bitrate = afmt.bitrate > 0 ? afmt.bitrate : src.main_audio.bitrate;
            a.sample_rate = 48000;
            outs.push_back({path_to_utf8(audio_file), "", a, title, iso3_of(lang)});
        }
        if (!dub::mix_dub(spec, std::move(clips), outs, [&](double f) { if (stage) stage(what, 0.8 + 0.2 * f); }, cancel, &err)) {
            for (const auto& o : outs) fs::remove(path_from_utf8(o.path), ec);
            if (!(cancel && cancel->load())) log_warn("{}", trf("Озвучення {}: мікс не вдався: {}", language_name(lang), err));
            continue;
        }
        if (want_track) tracks.push_back({lang, track_file});
        if (want_audio) {
            made.push_back(path_to_utf8(audio_file));
            audio_files.push_back({lang, audio_file});
            log_info("{}", trf("Озвучення ({}): {}", language_name(lang), path_to_utf8(audio_file)));
        }
    }
    for (const auto& id : temp_cloud_voices) dub::elevenlabs_delete_voice(tcfg.elevenlabs_key, id, nullptr);

    // ---- Відео з озвученням ----
    const bool faststart = s.faststart && is_mov(s.output_path);
    if (!tracks.empty() && !(cancel && cancel->load())) {
        const std::string ext = path_to_utf8(path_from_utf8(s.output_path).extension());
        if (dub_output(s, "videos")) {
            for (const auto& t : tracks) {
                if (stage) stage(trf("відео: {}", language_name(t.lang)), 0);
                const fs::path v = beside(s, "_" + t.lang + ext);
                std::string err;
                media::MuxInput mv{s.output_path, true, false, true, {}, {}, -1};
                media::MuxInput ma{path_to_utf8(t.file), false, true, false, dub_track_title(t.lang), iso3_of(t.lang), 1};
                if (media::mux_files({mv, ma}, path_to_utf8(v), faststart, &err)) {
                    made.push_back(path_to_utf8(v));
                    log_info("{}", trf("Відео з озвученням ({}): {}", language_name(t.lang), path_to_utf8(v)));
                } else {
                    log_warn("{}", trf("Відео з озвученням ({}) не записано: {}", language_name(t.lang), err));
                }
            }
        }
        if (dub_output(s, "tracks")) {
            if (stage) stage(tr("доріжки перекладу у відео"), 0);
            std::vector<media::MuxInput> in = {{s.output_path, true, true, true, {}, src_lang == "auto" ? "" : iso3_of(src_lang), 1}};
            for (const auto& t : tracks)
                in.push_back({path_to_utf8(t.file), false, true, false, dub_track_title(t.lang), iso3_of(t.lang), 0});
            std::string err;
            if (media::mux_files(in, s.output_path, faststart, &err)) {
                std::string names;
                for (const auto& t : tracks) names += (names.empty() ? "" : ", ") + language_name(t.lang);
                log_info("{}", trf("У відео додано доріжки озвучення: {}", names));
            } else {
                log_warn("{}", trf("Доріжки озвучення у відео не додано: {}", err));
            }
        }
    }

    // ---- Підказка для YouTube ----
    if (s.dub_template == "youtube" && (!audio_files.empty() || !subtitle_files.empty())) {
        const fs::path out = path_from_utf8(s.output_path);
        std::string t = trf("YouTube: {}\n\n1. Завантажте відео: {}\n", path_to_utf8(out.stem()), path_to_utf8(out.filename()));
        if (fs::exists(beside(s, ".chapters.txt"), ec))
            t += trf("   Розділи для опису — у файлі {}\n", path_to_utf8(beside(s, ".chapters.txt").filename()));
        t += tr("2. YouTube Studio → «Мови» → це відео → «Додати мову», для кожної мови:\n");
        for (const auto& lang : langs) {
            std::string a, sub;
            for (const auto& [l, p] : audio_files)
                if (l == lang) a = path_to_utf8(p.filename());
            for (const auto& [l, p] : subtitle_files)
                if (l == lang) sub = path_to_utf8(p.filename());
            if (a.empty() && sub.empty()) continue;
            t += std::format("\n   {}:\n", language_name(lang));
            if (!a.empty()) t += trf("     «Дубляж» → «Додати» → {}\n", a);
            if (!sub.empty()) t += trf("     «Субтитри» → «Додати» → «Завантажити файл» → {}\n", sub);
        }
        t += tr("\nОзвучення синтезоване (ШІ) — YouTube просить позначати такий вміст при публікації.\n");
        const fs::path p = beside(s, ".youtube.txt");
        if (write_file_text(p, t, nullptr)) made.push_back(path_to_utf8(p));
    }

    if (!s.keep_temp_files) {
        // Окремі WAV для озвучення (якщо це не пакет для монтажу) і робочі файли
        fs::remove_all(dub_work_dir(s), ec);
    }
    return made;
}

} // namespace gmdr::render
