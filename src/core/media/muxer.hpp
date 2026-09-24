// =============================================================================
//  muxer.hpp — запис закодованих потоків у контейнер (mp4, mkv, mov, webm,
//  avi, nut ... або послідовність зображень out_%06d.png).
// =============================================================================
#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "ffmpeg_util.hpp"

namespace gmdr::media {

class Muxer {
public:
    Muxer() = default;
    ~Muxer();
    Muxer(const Muxer&) = delete;
    Muxer& operator=(const Muxer&) = delete;

    // path у UTF-8. format_name — порожній (за розширенням) або "mp4", "matroska" ...
    bool open(const std::string& path_utf8, const std::string& format_name, std::string* error);
    bool needs_global_header() const;
    bool is_image_sequence() const;
    // Параметр самого формату (напр. "loop" для webp) — до write_header.
    void set_format_option(const char* key, const char* value);
    const AVOutputFormat* format() const { return fmt_ctx_ ? fmt_ctx_->oformat : nullptr; }

    // Перевірити, чи контейнер підтримує кодек.
    bool supports_codec(const std::string& encoder_name) const;

    int  add_stream(AVCodecContext* enc, const std::string& title, const std::string& language = {});
    // Потік для вже закодованих пакетів (склеювання частин без перекодування). enc_time_base —
    // у якому часі будуть pts пакетів для write_packet.
    int  add_stream_copy(const AVCodecParameters* par, AVRational enc_time_base, AVRational framerate,
                         const std::string& title);
    // fragmented: MP4/MOV пишеться фрагментами — файл відкривається, навіть якщо
    // запис обірвався (збій гри, вимкнення ПК). MKV такий і так.
    bool write_header(bool faststart, bool fragmented, std::string* error);
    bool is_fragmented() const { return fragmented_; }
    bool write_packet(int stream_index, AVPacket* pkt, AVRational enc_time_base);
    bool finish(std::string* error);
    void abort();   // закрити без трейлера (при скасуванні)

    int64_t bytes_written() const;
    const std::string& last_error() const { return error_; }

private:
    AVFormatContext*          fmt_ctx_ = nullptr;
    bool                      header_written_ = false;
    bool                      finished_ = false;
    bool                      fragmented_ = false;
    std::string               path_;
    std::string               error_;
    std::vector<int64_t>      last_dts_;   // останній записаний dts кожного потоку (у його time_base)
    mutable std::mutex        mutex_;
};

struct ChapterMark {
    double      start = 0;   // с
    double      end = 0;
    std::string title;
};
// Чи вміє контейнер (за розширенням) зберігати розділи: mp4/m4v/mov/mkv/webm.
bool container_supports_chapters(const std::string& path_utf8);

// Переупакувати файл (без перекодування) у звичайний MP4/MOV з індексом на
// початку — напр. фрагментований MP4 після рендеру. Результат замінює вхідний файл.
// chapters: записати ці розділи (інакше — скопіювати наявні з вхідного файлу).
bool remux_file(const std::string& path_utf8, bool faststart, std::string* error,
                const std::vector<ChapterMark>* chapters = nullptr);

// Зібрати новий файл з потоків кількох файлів без перекодування (озвучення: відео з
// основного файлу + звукові доріжки перекладу). Пакети йдуть упереміш за часом; розділи й
// метадані файлу — з першого входу.
struct MuxInput {
    std::string path;                  // UTF-8
    bool        video = true;          // брати відео
    bool        audio = true;          // брати звук
    bool        subtitles = true;      // брати субтитри
    std::string title;                 // назва звукових доріжок (порожньо — як була)
    std::string language;              // мова звукових доріжок, ISO 639-2 (порожньо — як була)
    int         default_audio = -1;    // 1 — доріжка звучить типово, 0 — ні, -1 — як була
};
bool mux_files(const std::vector<MuxInput>& inputs, const std::string& out_path_utf8, bool faststart, std::string* error);

// Короткі відомості про готовий файл (для перевірки результату).
struct MediaFileInfo {
    double  video_seconds = 0;
    int64_t video_frames = 0;   // 0 — невідомо
    double  audio_seconds = 0;
    int     audio_streams = 0;
    int     chapters = 0;
    bool    has_video = false;
};
bool probe_media_file(const std::string& path_utf8, MediaFileInfo& out, std::string* error);

// Номери (від 0, за частотою кадрів fps) ключових кадрів відео у файлі — напр. обірваному збоєм:
// кадри до ключового — цілі групи (GOP), тож на ньому файл можна обрізати і дописати решту.
// Порожньо — не вдалося прочитати.
std::vector<int64_t> keyframe_frames(const std::string& path_utf8, AVRational fps, std::string* error);

// Чи підтримує контейнер (за розширенням файлу, напр. "mp4") цей кодек.
// 1 — так, 0 — ні, -1 — невідомо (можна спробувати).
int container_supports(const std::string& ext, const std::string& encoder_name);

} // namespace gmdr::media
