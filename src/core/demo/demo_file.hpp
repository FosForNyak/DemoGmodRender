// =============================================================================
//  demo_file.hpp — читання файлу .dem (рушій Source; підпис "GMODEMO" у сучасному
//  Garry's Mod або "HL2DEMO" у старіших версіях і в інших іграх на Source).
//
//  Структура файлу:
//    [заголовок 1072 байти]
//    далі послідовність команд:  u8 cmd, i32 tick, [дані команди]
//
//  Garry's Mod: demo protocol 3, network protocol 24 (гілка Source 2013).
//  Команди:
//    1 dem_signon / 2 dem_packet : democmdinfo(76 байт) + seq_in + seq_out
//                                  + i32 довжина + мережеві повідомлення
//    3 dem_synctick              : без даних
//    4 dem_consolecmd            : i32 довжина + рядок
//    5 dem_usercmd               : i32 outgoing_seq + i32 довжина + дані
//    6 dem_datatables            : i32 довжина + дані
//    7 dem_stop                  : кінець запису
//    8 dem_stringtables          : i32 довжина + дані
// =============================================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "../util/mapped_file.hpp"

namespace gmdr::demo {

struct DemoHeader {
    std::string stamp;               // "GMODEMO" (сучасний GMod) або "HL2DEMO"
    int32_t     demo_protocol = 0;   // 3 для GMod
    int32_t     network_protocol = 0;// 24 для GMod
    std::string server_name;
    std::string client_name;         // ім'я гравця, що записував
    std::string map_name;
    std::string game_dir;            // "garrysmod"
    float       playback_time = 0.0f;
    int32_t     playback_ticks = 0;
    int32_t     playback_frames = 0;
    int32_t     signon_length = 0;
};

enum class DemoCmd : uint8_t {
    Signon = 1,
    Packet = 2,
    SyncTick = 3,
    ConsoleCmd = 4,
    UserCmd = 5,
    DataTables = 6,
    Stop = 7,
    StringTables = 8,
};

struct DemoCommand {
    DemoCmd        cmd = DemoCmd::Stop;
    int32_t        tick = 0;
    size_t         file_offset = 0;   // позиція початку команди у файлі
    const uint8_t* data = nullptr;    // корисні дані (мережеві повідомлення тощо)
    size_t         size = 0;
    int32_t        seq_in = 0, seq_out = 0;   // лише для Signon/Packet
};

class DemoFile {
public:
    // Відкриває файл (відображенням у пам'ять, а якщо не вийде — читає цілком).
    // Кидає std::runtime_error.
    explicit DemoFile(const std::filesystem::path& path);
    // Для тестів: з готового буфера.
    explicit DemoFile(std::vector<uint8_t> bytes);

    const DemoHeader& header() const { return header_; }
    size_t            file_size() const { return size_; }

    // Ітерація по командах. Повертає false у кінці файлу або при помилці
    // (тоді error() містить опис; обрізаний у кінці файл — не помилка).
    bool        next(DemoCommand& out);
    void        rewind();
    size_t      position() const { return pos_; }
    const std::string& error() const { return error_; }
    bool        reached_stop() const { return reached_stop_; }

    static constexpr size_t kHeaderSize = 1072;
    static constexpr size_t kCmdInfoSize = 76;

private:
    void parse_header();

    std::vector<uint8_t> owned_;    // якщо файл прочитано в пам'ять
    MappedFile           mapped_;   // якщо відображено
    const uint8_t*       data_ = nullptr;
    size_t               size_ = 0;
    DemoHeader           header_;
    size_t               pos_ = kHeaderSize;
    std::string          error_;
    bool                 reached_stop_ = false;
};

} // namespace gmdr::demo
