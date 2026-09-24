// =============================================================================
//  frame_pipe.hpp — кадри з гри прямо в пам'ять програми, без файлів на диску.
//
//  startmovie рушія Source пише кожен кадр окремим файлом <назва>0000.tga,
//  <назва>0001.tga ...: відкриває, записує цілим і закриває, строго по черзі.
//  Якщо дати startmovie назву в просторі іменованих каналів Windows
//  (\\?\pipe\gmdr_<id>_), кожне таке "відкриття файлу" з'єднується з каналом,
//  який програма створила заздалегідь, і кадр іде з гри прямо в її буфер.
//  Жодного файлу кадру, жодного запису на диск. Номер кадру — у назві каналу,
//  тож порядок той самий, що й у файлів.
//
//  Звук (<назва>.wav) рушій пише так само — у канал; програма дописує його у
//  звичайний WAV у тимчасовій папці (dir/<prefix>.wav), тож звук гри,
//  перезапуски після збою і дописування працюють як і з файлами.
//  На Linux той самий прийом — FIFO (mkfifo) з іменами кадрів у тимчасовій
//  папці; WAV гра пише звичайним файлом поруч.
//
//  Для гри це її штатний startmovie і звичайний запис файлу: без ін'єкцій,
//  хуків, змін у файлах гри чи в захисті — тож і з RTXLauncher/Remix
//  (кадр однаково бере сам рушій). Канали чекають наперед на кілька номерів;
//  кадр готовий, коли гра закрила свій кінець каналу. Якщо програма не встигає,
//  гра просто чекає на записі кадру — зворотний тиск без паузи процесу.
// =============================================================================
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include "../util/thread_pool.hpp"
#include "frame_source.hpp"

namespace gmdr::frames {

struct PipeOptions {
    std::filesystem::path dir;             // тимчасова папка рендеру: FIFO кадрів (Linux) і WAV гри
    std::string           prefix;          // як у startmovie: <prefix>0000.tga
    std::string           ext = ".tga";    // розширення кадрів, яке додає рушій: .tga або .jpg
    bool                  audio = true;    // Windows: приймати і звук <prefix>.wav (у dir/<prefix>.wav)
    int                   decode_threads = 0;   // 0 — автоматично
    int                   max_buffered = 12;    // кадрів у пам'яті; далі гра чекає на записі
    int                   lookahead = 6;        // скільки номерів кадрів чекають наперед
    bool                  keep_yuv = true;      // JPEG віддавати в YUV
};

// Чи вміє ця система передавати кадри каналом (Windows, Linux).
bool frame_pipes_supported();

// Назва для startmovie, за якою рушій писатиме в канали, а не у файли.
// dir_for_game — папка кадрів відносно garrysmod/ (там FIFO на Linux).
// Windows: \\?\pipe\<prefix> (корінь можна змінити змінною GMDR_PIPE_ROOT).
std::string pipe_movie_name(const std::string& dir_for_game, const std::string& prefix);

// Назва — у просторі іменованих каналів Windows (\\?\pipe\..., \\.\pipe\...).
bool is_windows_pipe_name(const std::string& path);

class FramePipeReader final : public FrameSource {
public:
    explicit FramePipeReader(PipeOptions opt);
    ~FramePipeReader() override;
    FramePipeReader(const FramePipeReader&) = delete;
    FramePipeReader& operator=(const FramePipeReader&) = delete;

    // Канали створено; інакше — last_error() (тоді кадри мають іти файлами).
    bool ok() const { return ok_; }
    // Гра вже писала звук у канал (Windows; на Linux WAV — звичайний файл, завжди true).
    bool audio_connected() const { return audio_connected_.load(); }
    // Скільки байтів кадрів прийшло каналом (для журналу).
    uint64_t bytes_received() const { return bytes_received_.load(); }

    // Гра завершила запис (або її закрито): дочитати те, що вже в каналах, і дописати звук.
    void set_producer_done() override;
    bool producer_done() const override { return producer_done_.load(); }
    Wait next_for(Image& out, int timeout_ms) override;

    int64_t     pending_files() const override;
    uint64_t    pending_bytes() const override { return 0; }
    int64_t     delivered() const override { return delivered_.load(); }
    int64_t     skipped() const override { return skipped_.load(); }
    bool        saw_any_file() const override { return connected_.load(); }
    std::string last_error() const override;
    ReaderStats stats() const override;

    // Ім'я каналу чи FIFO кадру index (для тестів і імітатора гри).
    std::string frame_path(int64_t index) const;

private:
    struct Io;   // канали — своє для кожної ОС (frame_pipe.cpp)

    void io_loop();
    void audio_loop();
    // Потік каналів: кадр index прийнято (size байтів у bytes).
    void frame_received(int64_t index, PixelBytes&& bytes, size_t size, double read_ms);
    void frame_failed(int64_t index, const std::string& why);
    // Потік каналів чекає, поки в пам'яті звільниться місце. false — зупинка.
    bool wait_for_room();
    void decode_task(int64_t index, PixelBytes bytes, size_t size);
    void set_error(const std::string& e);

    PipeOptions                 opt_;
    std::unique_ptr<Io>         io_;
    std::unique_ptr<ThreadPool> pool_;
    bool                        ok_ = false;

    mutable std::mutex          mutex_;
    std::condition_variable     cv_ready_;   // з'явився декодований кадр (або кінець)
    std::condition_variable     cv_room_;    // звільнилося місце в пам'яті
    std::map<int64_t, Image>    ready_;
    std::set<int64_t>           failed_;
    int                         in_flight_ = 0;   // прийнято і декодується
    bool                        receiving_ = false;
    int64_t                     next_deliver_ = 0;
    bool                        io_done_ = false; // потік каналів закінчив (після set_producer_done)
    std::string                 last_error_;
    ReaderStats                 stats_;
    size_t                      last_size_ = 0;   // розмір попереднього кадру — під нього буфер

    std::atomic<bool>           producer_done_{false};
    std::atomic<bool>           stop_{false};
    std::atomic<bool>           connected_{false};
    std::atomic<bool>           audio_connected_{false};
    std::atomic<int>            audio_clients_{0};
    std::atomic<int64_t>        audio_last_data_ms_{0};
    std::atomic<int64_t>        delivered_{0};
    std::atomic<int64_t>        skipped_{0};
    std::atomic<uint64_t>       bytes_received_{0};
    std::thread                 io_thread_;
    std::thread                 audio_thread_;
};

} // namespace gmdr::frames
