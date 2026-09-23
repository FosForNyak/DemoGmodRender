#include "zip_writer.hpp"

#include <array>
#include <ctime>

#include "file_util.hpp"
#include "strings.hpp"
#include "i18n.hpp"

namespace gmdr {

uint32_t crc32(const uint8_t* data, size_t size, uint32_t crc) {
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

namespace {
void put16(std::string& b, uint32_t v) {
    b += static_cast<char>(v & 0xFF);
    b += static_cast<char>((v >> 8) & 0xFF);
}
void put32(std::string& b, uint32_t v) {
    put16(b, v & 0xFFFF);
    put16(b, v >> 16);
}
constexpr uint16_t kUtf8Names = 0x0800;
constexpr uint16_t kVersion = 20;   // 2.0: достатньо для "stored"
} // namespace

ZipWriter::~ZipWriter() {
    if (open_) close(nullptr);
}

bool ZipWriter::open(const std::filesystem::path& path, std::string* error) {
    f_.open(path, std::ios::binary | std::ios::trunc);
    if (!f_) {
        if (error) *error = tr("не вдалося створити ") + path_to_utf8(path);
        return false;
    }
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    dos_time_ = static_cast<uint16_t>((tm.tm_hour << 11) | (tm.tm_min << 5) | (tm.tm_sec / 2));
    dos_date_ = static_cast<uint16_t>(((tm.tm_year - 80) << 9) | ((tm.tm_mon + 1) << 5) | tm.tm_mday);
    open_ = true;
    return true;
}

bool ZipWriter::add(const std::string& name, std::string_view data) {
    if (!open_ || data.size() > 0xFFFFFFFFu) return false;   // без ZIP64: до 4 ГБ
    Entry e;
    e.name = name;
    e.crc = crc32(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    e.size = static_cast<uint32_t>(data.size());
    e.offset = static_cast<uint32_t>(f_.tellp());
    std::string h;
    put32(h, 0x04034b50);
    put16(h, kVersion);
    put16(h, kUtf8Names);
    put16(h, 0);   // без стиснення
    put16(h, dos_time_);
    put16(h, dos_date_);
    put32(h, e.crc);
    put32(h, e.size);
    put32(h, e.size);
    put16(h, static_cast<uint32_t>(name.size()));
    put16(h, 0);
    h += name;
    f_.write(h.data(), static_cast<std::streamsize>(h.size()));
    f_.write(data.data(), static_cast<std::streamsize>(data.size()));
    entries_.push_back(std::move(e));
    return static_cast<bool>(f_);
}

bool ZipWriter::add_file(const std::string& name, const std::filesystem::path& src) {
    auto bytes = read_file_bytes(src);
    if (!bytes) return false;
    return add(name, std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
}

bool ZipWriter::close(std::string* error) {
    if (!open_) return true;
    open_ = false;
    const uint32_t cd_offset = static_cast<uint32_t>(f_.tellp());
    std::string cd;
    for (const auto& e : entries_) {
        put32(cd, 0x02014b50);
        put16(cd, kVersion);   // створено
        put16(cd, kVersion);   // потрібно для розпакування
        put16(cd, kUtf8Names);
        put16(cd, 0);
        put16(cd, dos_time_);
        put16(cd, dos_date_);
        put32(cd, e.crc);
        put32(cd, e.size);
        put32(cd, e.size);
        put16(cd, static_cast<uint32_t>(e.name.size()));
        put16(cd, 0);   // extra
        put16(cd, 0);   // коментар
        put16(cd, 0);   // диск
        put16(cd, 0);   // внутрішні атрибути
        put32(cd, 0);   // зовнішні атрибути
        put32(cd, e.offset);
        cd += e.name;
    }
    std::string end;
    put32(end, 0x06054b50);
    put16(end, 0);
    put16(end, 0);
    put16(end, static_cast<uint32_t>(entries_.size()));
    put16(end, static_cast<uint32_t>(entries_.size()));
    put32(end, static_cast<uint32_t>(cd.size()));
    put32(end, cd_offset);
    put16(end, 0);
    f_.write(cd.data(), static_cast<std::streamsize>(cd.size()));
    f_.write(end.data(), static_cast<std::streamsize>(end.size()));
    f_.close();
    if (!f_) {
        if (error) *error = tr("не вдалося дописати архів");
        return false;
    }
    return true;
}

} // namespace gmdr
