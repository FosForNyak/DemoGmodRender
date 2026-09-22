// =============================================================================
//  wav.hpp — читання і запис WAV.
//
//  WavReader вміє читати файл, який ЩЕ ЗАПИСУЄТЬСЯ грою (startmovie пише
//  звук паралельно з кадрами, а розміри в заголовку виправляє лише в кінці).
//  Тому розмір даних визначаємо за реальним розміром файлу.
// =============================================================================
#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace gmdr::audio {

class WavReader {
public:
    WavReader() = default;
    ~WavReader();
    WavReader(const WavReader&) = delete;
    WavReader& operator=(const WavReader&) = delete;

    // live = true: файл ще росте, розмір беремо з файлової системи.
    bool open(const std::filesystem::path& path, bool live, std::string* error = nullptr);
    // Повторна спроба прочитати заголовок (якщо на момент open його ще не було).
    bool is_open() const { return file_ != nullptr && header_ok_; }

    int sample_rate() const { return rate_; }
    int channels() const { return channels_; }
    int bits_per_sample() const { return bits_; }
    bool is_float() const { return float_; }

    // Скільки ЦІЛИХ кадрів (семпл × канали) доступно зараз від початку даних.
    int64_t frames_available();
    int64_t frames_read() const { return frames_read_; }

    // Прочитати до max_frames кадрів у float (interleaved). Повертає прочитане.
    size_t read(float* out, size_t max_frames);

    void set_live(bool live) { live_ = live; }

private:
    bool parse_header(std::string* error);

    std::FILE*            file_ = nullptr;
    std::filesystem::path path_;
    bool                  live_ = false;
    bool                  header_ok_ = false;
    int                   rate_ = 0, channels_ = 0, bits_ = 0;
    bool                  float_ = false;
    int64_t               data_offset_ = 0;
    int64_t               data_size_declared_ = 0;
    int64_t               frames_read_ = 0;
    std::vector<uint8_t>  raw_;
};

class WavWriter {
public:
    enum class Format { Int16, Int24, Float32 };
    WavWriter() = default;
    ~WavWriter();
    bool open(const std::filesystem::path& path, int rate, int channels, Format fmt, std::string* error = nullptr);
    bool write(const float* interleaved, size_t frames);
    bool close(std::string* error = nullptr);
    int64_t frames_written() const { return frames_; }

private:
    void write_header();
    std::FILE* file_ = nullptr;
    int        rate_ = 0, channels_ = 0;
    Format     fmt_ = Format::Int16;
    int64_t    frames_ = 0;
    std::vector<uint8_t> buf_;
};

std::FILE* open_file_utf8(const std::filesystem::path& p, const char* mode);

} // namespace gmdr::audio
