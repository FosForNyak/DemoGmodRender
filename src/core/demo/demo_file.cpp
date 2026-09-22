#include "demo_file.hpp"

#include "../util/file_util.hpp"

#include <cstring>
#include <format>
#include <stdexcept>

namespace gmdr::demo {

const char* demo_cmd_name(DemoCmd c) {
    switch (c) {
    case DemoCmd::Signon: return "dem_signon";
    case DemoCmd::Packet: return "dem_packet";
    case DemoCmd::SyncTick: return "dem_synctick";
    case DemoCmd::ConsoleCmd: return "dem_consolecmd";
    case DemoCmd::UserCmd: return "dem_usercmd";
    case DemoCmd::DataTables: return "dem_datatables";
    case DemoCmd::Stop: return "dem_stop";
    case DemoCmd::StringTables: return "dem_stringtables";
    }
    return "dem_unknown";
}

namespace {
int32_t rd_i32(const uint8_t* p) {
    int32_t v;
    std::memcpy(&v, p, 4);   // файл little-endian, як і x86
    return v;
}
float rd_f32(const uint8_t* p) {
    float v;
    std::memcpy(&v, p, 4);
    return v;
}
std::string rd_fixed_string(const uint8_t* p, size_t max) {
    size_t n = 0;
    while (n < max && p[n] != 0) ++n;
    return std::string(reinterpret_cast<const char*>(p), n);
}
// Для повідомлення про помилку: лише друковані ASCII-символи
std::string printable(const std::string& s) {
    std::string out;
    for (unsigned char c : s) out += (c >= 32 && c < 127) ? static_cast<char>(c) : '?';
    return out;
}
} // namespace

DemoFile::DemoFile(const std::filesystem::path& path) {
    std::string err;
    if (mapped_.open(path, &err)) {
        data_ = mapped_.data();
        size_ = mapped_.size();
    } else {
        auto data = read_file_bytes(path, &err);
        if (!data) throw std::runtime_error("Не вдалося прочитати демо: " + err);
        owned_ = std::move(*data);
        data_ = owned_.data();
        size_ = owned_.size();
    }
    parse_header();
}

DemoFile::DemoFile(std::vector<uint8_t> bytes) : owned_(std::move(bytes)) {
    data_ = owned_.data();
    size_ = owned_.size();
    parse_header();
}

void DemoFile::parse_header() {
    if (size_ < kHeaderSize) throw std::runtime_error("Файл занадто малий для демо-запису (.dem)");
    const uint8_t* p = data_;
    header_.stamp = rd_fixed_string(p, 8);
    // Сучасний Garry's Mod пише власний підпис "GMODEMO"; решта формату — як у Source 2013.
    if (header_.stamp != "HL2DEMO" && header_.stamp != "GMODEMO")
        throw std::runtime_error(std::format("Це не демо рушія Source / Garry's Mod (підпис файлу: \"{}\", "
                                             "очікувався GMODEMO або HL2DEMO)", printable(header_.stamp)));
    header_.demo_protocol = rd_i32(p + 8);
    header_.network_protocol = rd_i32(p + 12);
    header_.server_name = rd_fixed_string(p + 16, 260);
    header_.client_name = rd_fixed_string(p + 276, 260);
    header_.map_name = rd_fixed_string(p + 536, 260);
    header_.game_dir = rd_fixed_string(p + 796, 260);
    header_.playback_time = rd_f32(p + 1056);
    header_.playback_ticks = rd_i32(p + 1060);
    header_.playback_frames = rd_i32(p + 1064);
    header_.signon_length = rd_i32(p + 1068);
    if (header_.demo_protocol < 2 || header_.demo_protocol > 3)
        throw std::runtime_error(std::format(
            "Непідтримувана версія демо-протоколу {} (Garry's Mod використовує 3)", header_.demo_protocol));
}

void DemoFile::rewind() {
    pos_ = kHeaderSize;
    error_.clear();
    reached_stop_ = false;
}

bool DemoFile::next(DemoCommand& out) {
    if (reached_stop_) return false;
    const size_t n = size_;
    // Мінімум: 1 байт команди + 4 байти тіка
    if (pos_ + 5 > n) return false;   // кінець (можливо, обрізаний файл)
    const uint8_t* p = data_;
    out = DemoCommand{};
    out.file_offset = pos_;
    const uint8_t cmd = p[pos_];
    out.tick = rd_i32(p + pos_ + 1);
    pos_ += 5;

    auto need = [&](size_t count) -> bool {
        if (pos_ + count > n) {
            // Файл обірвався посеред команди — таке буває, якщо гра впала під час запису.
            pos_ = n;
            return false;
        }
        return true;
    };
    auto read_len_block = [&]() -> bool {
        if (!need(4)) return false;
        const int32_t len = rd_i32(p + pos_);
        pos_ += 4;
        if (len < 0) {
            error_ = std::format("від'ємна довжина блоку на позиції {}", pos_ - 4);
            pos_ = n;
            return false;
        }
        if (!need(static_cast<size_t>(len))) return false;
        out.data = p + pos_;
        out.size = static_cast<size_t>(len);
        pos_ += static_cast<size_t>(len);
        return true;
    };

    switch (cmd) {
    case 1:
    case 2:
        out.cmd = cmd == 1 ? DemoCmd::Signon : DemoCmd::Packet;
        if (!need(kCmdInfoSize + 8)) return false;
        pos_ += kCmdInfoSize;
        out.seq_in = rd_i32(p + pos_);
        out.seq_out = rd_i32(p + pos_ + 4);
        pos_ += 8;
        return read_len_block();
    case 3:
        out.cmd = DemoCmd::SyncTick;
        return true;
    case 4:
        out.cmd = DemoCmd::ConsoleCmd;
        return read_len_block();
    case 5:
        out.cmd = DemoCmd::UserCmd;
        if (!need(4)) return false;
        pos_ += 4;   // outgoing sequence
        return read_len_block();
    case 6:
        out.cmd = DemoCmd::DataTables;
        return read_len_block();
    case 7:
        out.cmd = DemoCmd::Stop;
        reached_stop_ = true;
        return true;
    case 8:
        out.cmd = DemoCmd::StringTables;
        return read_len_block();
    default:
        error_ = std::format("невідома команда демо {} на позиції {}", cmd, out.file_offset);
        pos_ = n;
        return false;
    }
}

} // namespace gmdr::demo
