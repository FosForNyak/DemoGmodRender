#include "wav.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <system_error>

namespace gmdr::audio {

std::FILE* open_file_utf8(const std::filesystem::path& p, const char* mode) {
#ifdef _WIN32
    std::wstring wmode(mode, mode + std::strlen(mode));
    // _wfsopen з _SH_DENYNO дозволяє читати файл, який відкрила гра на запис
    return _wfsopen(p.c_str(), wmode.c_str(), 0x40 /* _SH_DENYNO */);
#else
    return std::fopen(p.c_str(), mode);
#endif
}

namespace {
uint32_t rd32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
uint16_t rd16(const uint8_t* p) { uint16_t v; std::memcpy(&v, p, 2); return v; }

int64_t file_size_now(std::FILE* f, const std::filesystem::path& p) {
    std::error_code ec;
    const auto s = std::filesystem::file_size(p, ec);
    if (!ec) return static_cast<int64_t>(s);
    (void)f;
    return 0;
}

bool seek64(std::FILE* f, int64_t off) {
#ifdef _WIN32
    return _fseeki64(f, off, SEEK_SET) == 0;
#else
    return fseeko(f, static_cast<off_t>(off), SEEK_SET) == 0;
#endif
}
} // namespace

WavReader::~WavReader() {
    if (file_) std::fclose(file_);
}

bool WavReader::open(const std::filesystem::path& path, bool live, std::string* error) {
    if (file_) std::fclose(file_);
    file_ = open_file_utf8(path, "rb");
    path_ = path;
    live_ = live;
    header_ok_ = false;
    frames_read_ = 0;
    if (!file_) {
        if (error) *error = "не вдалося відкрити WAV";
        return false;
    }
    return parse_header(error);
}

bool WavReader::parse_header(std::string* error) {
    const int64_t size = file_size_now(file_, path_);
    if (size < 44) {
        if (error) *error = "WAV ще порожній";
        return false;
    }
    std::vector<uint8_t> head(static_cast<size_t>(std::min<int64_t>(size, 1 << 16)));
    seek64(file_, 0);
    if (std::fread(head.data(), 1, head.size(), file_) != head.size()) {
        if (error) *error = "помилка читання заголовка WAV";
        return false;
    }
    if (std::memcmp(head.data(), "RIFF", 4) != 0 || std::memcmp(head.data() + 8, "WAVE", 4) != 0) {
        if (error) *error = "це не WAV (RIFF/WAVE)";
        return false;
    }
    size_t pos = 12;
    bool have_fmt = false;
    while (pos + 8 <= head.size()) {
        const uint8_t* ck = head.data() + pos;
        const uint32_t len = rd32(ck + 4);
        if (std::memcmp(ck, "fmt ", 4) == 0) {
            if (pos + 8 + 16 > head.size()) break;
            const uint16_t tag = rd16(ck + 8);
            channels_ = rd16(ck + 10);
            rate_ = static_cast<int>(rd32(ck + 12));
            bits_ = rd16(ck + 22);
            float_ = (tag == 3);
            if (tag == 0xFFFE && len >= 40 && pos + 8 + 40 <= head.size()) {
                // WAVE_FORMAT_EXTENSIBLE: підформат у GUID
                float_ = rd16(ck + 8 + 24) == 3;
            }
            have_fmt = true;
        } else if (std::memcmp(ck, "data", 4) == 0) {
            if (!have_fmt || channels_ <= 0 || rate_ <= 0 ||
                (bits_ != 8 && bits_ != 16 && bits_ != 24 && bits_ != 32)) {
                if (error) *error = "непідтримуваний формат WAV";
                return false;
            }
            data_offset_ = static_cast<int64_t>(pos + 8);
            data_size_declared_ = len;
            header_ok_ = true;
            seek64(file_, data_offset_);
            return true;
        }
        pos += 8 + len + (len & 1);
        if (len > (1u << 30)) break;   // дивний розмір — ймовірно ще не виправлений заголовок
    }
    if (error) *error = "у WAV немає блоку data (можливо, ще не записано)";
    return false;
}

int64_t WavReader::frames_available() {
    if (!file_) return 0;
    if (!header_ok_) {
        if (!parse_header(nullptr)) return 0;
    }
    const int64_t frame_bytes = static_cast<int64_t>(channels_) * (bits_ / 8);
    int64_t data_bytes = file_size_now(file_, path_) - data_offset_;
    // Якщо запис завершено і заголовок коректний — довіряємо заголовку
    if (!live_ && data_size_declared_ > 0 && data_size_declared_ <= data_bytes) data_bytes = data_size_declared_;
    if (data_bytes < 0) data_bytes = 0;
    return data_bytes / frame_bytes;
}

size_t WavReader::read(float* out, size_t max_frames) {
    if (!is_open() && !(file_ && parse_header(nullptr))) return 0;
    const int64_t avail = frames_available() - frames_read_;
    if (avail <= 0) return 0;
    const size_t n = static_cast<size_t>(std::min<int64_t>(avail, static_cast<int64_t>(max_frames)));
    const size_t bps = static_cast<size_t>(bits_ / 8);
    const size_t bytes = n * bps * static_cast<size_t>(channels_);
    raw_.resize(bytes);
    // Позиціонуємося явно: між читаннями файл міг рости
    seek64(file_, data_offset_ + frames_read_ * static_cast<int64_t>(bps) * channels_);
    const size_t got = std::fread(raw_.data(), 1, bytes, file_);
    const size_t frames = got / (bps * static_cast<size_t>(channels_));
    const size_t samples = frames * static_cast<size_t>(channels_);
    const uint8_t* p = raw_.data();
    for (size_t i = 0; i < samples; ++i) {
        float v = 0.0f;
        switch (bits_) {
        case 8: v = (static_cast<int>(p[i]) - 128) / 128.0f; break;
        case 16: { int16_t s; std::memcpy(&s, p + i * 2, 2); v = s / 32768.0f; break; }
        case 24: {
            int32_t s = (p[i * 3] | (p[i * 3 + 1] << 8) | (p[i * 3 + 2] << 16));
            if (s & 0x800000) s |= ~0xFFFFFF;
            v = static_cast<float>(s) / 8388608.0f;
            break;
        }
        case 32:
            if (float_) { std::memcpy(&v, p + i * 4, 4); }
            else { int32_t s; std::memcpy(&s, p + i * 4, 4); v = static_cast<float>(s / 2147483648.0); }
            break;
        }
        out[i] = v;
    }
    frames_read_ += static_cast<int64_t>(frames);
    return frames;
}

// ---------------------------------------------------------------------------
WavWriter::~WavWriter() { close(nullptr); }

bool WavWriter::open(const std::filesystem::path& path, int rate, int channels, Format fmt, std::string* error) {
    close(nullptr);
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    file_ = open_file_utf8(path, "wb");
    if (!file_) {
        if (error) *error = "не вдалося створити WAV";
        return false;
    }
    rate_ = rate;
    channels_ = channels;
    fmt_ = fmt;
    frames_ = 0;
    write_header();
    return true;
}

void WavWriter::write_header() {
    const int bits = fmt_ == Format::Int16 ? 16 : fmt_ == Format::Int24 ? 24 : 32;
    const uint16_t tag = fmt_ == Format::Float32 ? 3 : 1;
    const uint32_t block = static_cast<uint32_t>(channels_ * bits / 8);
    const uint64_t data_bytes64 = static_cast<uint64_t>(frames_) * block;
    const uint32_t data_bytes = data_bytes64 > 0xFFFFFFF0ull ? 0xFFFFFFF0u : static_cast<uint32_t>(data_bytes64);
    uint8_t h[44];
    auto w32 = [&](int off, uint32_t v) { std::memcpy(h + off, &v, 4); };
    auto w16 = [&](int off, uint16_t v) { std::memcpy(h + off, &v, 2); };
    std::memcpy(h, "RIFF", 4);
    w32(4, 36 + data_bytes);
    std::memcpy(h + 8, "WAVEfmt ", 8);
    w32(16, 16);
    w16(20, tag);
    w16(22, static_cast<uint16_t>(channels_));
    w32(24, static_cast<uint32_t>(rate_));
    w32(28, static_cast<uint32_t>(rate_) * block);
    w16(32, static_cast<uint16_t>(block));
    w16(34, static_cast<uint16_t>(bits));
    std::memcpy(h + 36, "data", 4);
    w32(40, data_bytes);
    seek64(file_, 0);
    std::fwrite(h, 1, 44, file_);
}

bool WavWriter::write(const float* in, size_t frames) {
    if (!file_) return false;
    const size_t samples = frames * static_cast<size_t>(channels_);
    const int bps = fmt_ == Format::Int16 ? 2 : fmt_ == Format::Int24 ? 3 : 4;
    buf_.resize(samples * static_cast<size_t>(bps));
    uint8_t* p = buf_.data();
    for (size_t i = 0; i < samples; ++i) {
        const float v = std::clamp(in[i], -1.0f, 1.0f);
        switch (fmt_) {
        case Format::Int16: {
            const int16_t s = static_cast<int16_t>(std::lrint(v * 32767.0f));
            std::memcpy(p + i * 2, &s, 2);
            break;
        }
        case Format::Int24: {
            const int32_t s = static_cast<int32_t>(std::lrint(v * 8388607.0f));
            p[i * 3] = static_cast<uint8_t>(s & 0xFF);
            p[i * 3 + 1] = static_cast<uint8_t>((s >> 8) & 0xFF);
            p[i * 3 + 2] = static_cast<uint8_t>((s >> 16) & 0xFF);
            break;
        }
        case Format::Float32:
            std::memcpy(p + i * 4, &in[i], 4);
            break;
        }
    }
    const bool ok = std::fwrite(buf_.data(), 1, buf_.size(), file_) == buf_.size();
    frames_ += static_cast<int64_t>(frames);
    return ok;
}

bool WavWriter::close(std::string* error) {
    if (!file_) return true;
    write_header();
    const bool ok = std::fclose(file_) == 0;
    file_ = nullptr;
    if (!ok && error) *error = "помилка закриття WAV";
    return ok;
}

} // namespace gmdr::audio
