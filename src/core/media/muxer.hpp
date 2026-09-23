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
    std::vector<AVCodecContext*> encoders_;
    bool                      header_written_ = false;
    bool                      finished_ = false;
    bool                      fragmented_ = false;
    std::string               path_;
    std::string               error_;
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

// Порада: яке розширення/контейнер для кодека (для GUI).
std::vector<std::string> containers_for_codec(const std::string& encoder_name);

// Чи підтримує контейнер (за розширенням файлу, напр. "mp4") цей кодек.
// 1 — так, 0 — ні, -1 — невідомо (можна спробувати).
int container_supports(const std::string& ext, const std::string& encoder_name);

} // namespace gmdr::media
