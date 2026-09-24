#include "frame_pipe.hpp"

#include "../util/i18n.hpp"
#include "../util/log.hpp"
#include "../util/strings.hpp"
#include "image_decode.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <format>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace gmdr::frames {

namespace fs = std::filesystem;

namespace {
[[maybe_unused]] int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
double ms_since(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}

constexpr size_t kMinFrameBuffer = 1u << 20;   // перший кадр: далі буфер — під розмір попереднього
constexpr size_t kReadSlack = 4096;            // запас, щоб останнє читання (кінець каналу) не збільшувало буфер
constexpr int    kMaxEmptyOpens = 5;           // гра відкривала "файл" кадру, нічого не записавши
// Примірників каналу звуку, що чекають. Рушій дописує звук шматками, щоразу відкриваючи "файл"
// заново; якщо саме тоді всі примірники ще зайняті (програма не встигла знову чекати на щойно
// закритих), відкриття не вдається (ERROR_PIPE_BUSY) — і шматок звуку губиться. Примірник без
// даних майже нічого не коштує, тож їх із запасом: гра мала б відкрити звук стільки разів поспіль
// (кадр за кадром), жодного разу не давши програмі процесорного часу.
constexpr int    kWavInstances = 32;

std::string frame_name(const std::string& prefix, int64_t index, const std::string& ext) {
    return std::format("{}{:04d}{}", prefix, index, ext);   // як "%s%04d.tga" у рушії
}

// Звідки читати кадр: є дані або гра закрила свій кінець
enum class Got { Data, Empty, Stopped, Error };

// Вільне місце в буфері для наступного читання (збільшує буфер, коли треба).
size_t make_room(PixelBytes& buf, size_t size) {
    if (buf.size() < size + kDecodePadding + 1) buf.resize(std::max(buf.size() * 2, size + kMinFrameBuffer));
    return std::min<size_t>(buf.size() - size - kDecodePadding, 64u << 20);
}

#ifdef _WIN32
std::string pipe_root() {
    // Змінна середовища — для перевірки інших форм назви на конкретній збірці гри
    if (const char* e = std::getenv("GMDR_PIPE_ROOT"); e && *e) {
        std::string r = e;
        if (r.back() != '\\' && r.back() != '/') r += '\\';
        return r;
    }
    // \\?\ — без нормалізації шляху: у "\\.\pipe" рушій міг би прибрати "\." як зайву частину шляху
    return "\\\\?\\pipe\\";
}

// Сервер створює канал за канонічною назвою; гра відкриває \\?\pipe\... — це той самий канал.
std::wstring server_name(const std::string& name) { return L"\\\\.\\pipe\\" + path_from_utf8(name).wstring(); }

struct Overlapped {
    OVERLAPPED ov{};
    Overlapped() { ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr); }
    ~Overlapped() {
        if (ov.hEvent) CloseHandle(ov.hEvent);
    }
    Overlapped(const Overlapped&) = delete;
    Overlapped& operator=(const Overlapped&) = delete;
    void reset() {
        HANDLE e = ov.hEvent;
        ov = OVERLAPPED{};
        ov.hEvent = e;
        ResetEvent(e);
    }
    bool signaled() const { return WaitForSingleObject(ov.hEvent, 0) == WAIT_OBJECT_0; }
};

HANDLE create_pipe(const std::wstring& name, bool first, DWORD max_instances, DWORD in_buffer) {
    // Дуплекс: рушій може відкрити "файл" і на читання-запис ("r+b"); віддалені клієнти — ні
    const DWORD open_mode = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0);
    return CreateNamedPipeW(name.c_str(), open_mode, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                            max_instances, 0, in_buffer, 0, nullptr);
}

// Один примірник каналу кадру
struct FrameEnd {
    HANDLE     pipe = INVALID_HANDLE_VALUE;
    Overlapped ov;
    bool       pending = false;     // чекаємо на з'єднання
    bool       connected = false;   // гра відкрила "файл" кадру

    FrameEnd() = default;
    FrameEnd(const FrameEnd&) = delete;
    FrameEnd& operator=(const FrameEnd&) = delete;
    ~FrameEnd() {
        if (pipe == INVALID_HANDLE_VALUE) return;
        if (pending) {
            DWORD n = 0;
            CancelIoEx(pipe, &ov.ov);
            GetOverlappedResult(pipe, &ov.ov, &n, TRUE);
        }
        CloseHandle(pipe);
    }
    // Чекати, поки гра відкриє канал
    bool arm(std::string* error) {
        ov.reset();
        pending = connected = false;
        if (ConnectNamedPipe(pipe, &ov.ov)) {
            connected = true;
            return true;
        }
        const DWORD e = GetLastError();
        if (e == ERROR_IO_PENDING) pending = true;
        else if (e == ERROR_PIPE_CONNECTED || e == ERROR_NO_DATA) connected = true;   // гра встигла першою
        else {
            if (error) *error = trf("не вдалося чекати на канал кадру (код {})", e);
            return false;
        }
        return true;
    }
};

// Канал одного номера кадру — два примірники: якщо гра відкриє "файл" і закриє, нічого не
// записавши (перевірка, чи він є), наступне відкриття одразу знайде другий, що чекає
struct FrameSlot {
    std::vector<std::unique_ptr<FrameEnd>> ends;

    bool add_end(const std::string& name, std::string* error) {
        auto e = std::make_unique<FrameEnd>();
        // Без буфера: запис кадру в грі завершується, лише коли програма його прочитала, —
        // інакше гра могла б забігти за номери, для яких канали ще не створено
        e->pipe = create_pipe(server_name(name), ends.empty(), 2, 0);
        if (e->pipe == INVALID_HANDLE_VALUE) {
            if (error) *error = trf("не вдалося створити канал кадру (код {})", GetLastError());
            return false;
        }
        if (!e->arm(error)) return false;
        ends.push_back(std::move(e));
        return true;
    }
    FrameEnd* connected() const {
        for (auto& e : ends)
            if (e->connected) return e.get();
        return nullptr;
    }
};

// Примірник каналу звуку <prefix>.wav: рушій відкриває його багато разів (дописує шматками)
struct WavPipe {
    enum class St { Listening, Connected, Reading, Failed };
    HANDLE               pipe = INVALID_HANDLE_VALUE;
    Overlapped           ov;
    St                   st = St::Failed;
    std::vector<uint8_t> buf = std::vector<uint8_t>(64 * 1024);

    WavPipe() = default;
    WavPipe(const WavPipe&) = delete;
    WavPipe& operator=(const WavPipe&) = delete;
    ~WavPipe() {
        if (pipe == INVALID_HANDLE_VALUE) return;
        if (st == St::Listening || st == St::Reading) {
            DWORD n = 0;
            CancelIoEx(pipe, &ov.ov);
            GetOverlappedResult(pipe, &ov.ov, &n, TRUE);
        }
        CloseHandle(pipe);
    }
    // Чекати, поки гра відкриє звук. true — гра вже з'єдналася (тоді пакета завершення не буде)
    bool arm() {
        ov.reset();
        if (ConnectNamedPipe(pipe, &ov.ov)) {
            st = St::Listening;   // завершилось одразу — пакет завершення однаково прийде
            return false;
        }
        const DWORD e = GetLastError();
        if (e == ERROR_IO_PENDING) {
            st = St::Listening;
            return false;
        }
        if (e == ERROR_PIPE_CONNECTED || e == ERROR_NO_DATA) {
            st = St::Connected;
            return true;
        }
        st = St::Failed;
        return false;
    }
};
#endif
} // namespace

bool frame_pipes_supported() {
#if defined(_WIN32) || defined(__linux__)
    return true;
#else
    return false;
#endif
}

std::string pipe_movie_name(const std::string& dir_for_game, const std::string& prefix) {
#ifdef _WIN32
    (void)dir_for_game;
    return pipe_root() + prefix;
#else
    return dir_for_game.empty() ? prefix : dir_for_game + "/" + prefix;
#endif
}

bool is_windows_pipe_name(const std::string& path) {
    if (path.size() < 9) return false;
    const std::string head = to_lower(replace_all(path.substr(0, 9), "/", "\\"));
    return head == "\\\\?\\pipe\\" || head == "\\\\.\\pipe\\";
}

// ============================== Канали ОС ======================================
// Спільний цикл (io_loop) працює через кілька дій: створити канали наперед, дочекатися, поки
// гра відкриє котрийсь, прочитати кадр до кінця, знову чекати (гра відкрила і нічого не
// записала), закрити.
#ifdef _WIN32
struct FramePipeReader::Io {
    PipeOptions                                   opt;
    std::map<int64_t, std::unique_ptr<FrameSlot>> slots;
    std::vector<std::unique_ptr<WavPipe>>         wav;
    HANDLE                                        wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);   // set_producer_done
    // Звук: порт завершення — пакети в ньому стоять у тому порядку, в якому гра відкривала канал
    HANDLE                                        port = nullptr;

    ~Io() {
        slots.clear();
        wav.clear();   // скасувати й дочекатися операцій раніше, ніж закриється порт
        if (wake) CloseHandle(wake);
        if (port) CloseHandle(port);
    }

    std::string path(int64_t index) const {
        return path_to_utf8(fs::path(server_name(frame_name(opt.prefix, index, opt.ext))));
    }

    // Канали для номерів [from, to)
    bool ensure(int64_t from, int64_t to, std::string* error) {
        for (int64_t i = from; i < to; ++i) {
            if (slots.count(i)) continue;
            auto s = std::make_unique<FrameSlot>();
            const std::string name = frame_name(opt.prefix, i, opt.ext);
            if (!s->add_end(name, error) || !s->add_end(name, error)) return false;
            slots.emplace(i, std::move(s));
        }
        return true;
    }

    bool open(const PipeOptions& o, std::string* error) {
        opt = o;
        if (!wake) {
            if (error) *error = tr("не вдалося створити подію");
            return false;
        }
        if (!ensure(0, opt.lookahead, error)) return false;
        if (opt.audio) {
            port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
            if (!port) {
                if (error) *error = tr("не вдалося створити подію");
                return false;
            }
            const std::wstring name = server_name(opt.prefix + ".wav");
            for (int i = 0; i < kWavInstances; ++i) {
                auto w = std::make_unique<WavPipe>();
                w->pipe = create_pipe(name, i == 0, PIPE_UNLIMITED_INSTANCES, 64 * 1024);
                if (w->pipe == INVALID_HANDLE_VALUE || !CreateIoCompletionPort(w->pipe, port, static_cast<ULONG_PTR>(i), 0)) {
                    if (error) *error = trf("не вдалося створити канал звуку (код {})", GetLastError());
                    return false;
                }
                wav.push_back(std::move(w));
            }
        }
        return true;
    }

    // Найменший номер кадру, чий канал гра вже відкрила (-1 — жодного)
    int64_t wait_ready(int timeout_ms) {
        auto first_connected = [&]() -> int64_t {
            for (auto& [i, s] : slots)
                if (s->connected()) return i;
            return -1;
        };
        if (first_connected() < 0) {
            std::vector<HANDLE> h;
            for (auto& [i, s] : slots)
                for (auto& e : s->ends)
                    if (e->pending && h.size() < MAXIMUM_WAIT_OBJECTS - 1) h.push_back(e->ov.ov.hEvent);
            h.push_back(wake);
            WaitForMultipleObjects(static_cast<DWORD>(h.size()), h.data(), FALSE, static_cast<DWORD>(timeout_ms));
        }
        for (auto& [i, s] : slots)
            for (auto& e : s->ends) {
                if (!e->pending || !e->ov.signaled()) continue;
                e->pending = false;
                DWORD n = 0;
                if (GetOverlappedResult(e->pipe, &e->ov.ov, &n, FALSE)) {
                    e->connected = true;
                } else {
                    const DWORD err = GetLastError();
                    if (err == ERROR_PIPE_CONNECTED || err == ERROR_NO_DATA) e->connected = true;
                    else e->arm(nullptr);
                }
            }
        return first_connected();
    }

    int64_t highest_ready() const {
        for (auto it = slots.rbegin(); it != slots.rend(); ++it)
            if (it->second->connected()) return it->first;
        return -1;
    }

    Got read(int64_t index, PixelBytes& buf, size_t& size, const std::atomic<bool>& stopping, std::string* error) {
        auto it = slots.find(index);
        FrameEnd* end = it == slots.end() ? nullptr : it->second->connected();
        if (!end) {
            if (error) *error = tr("немає каналу кадру");
            return Got::Error;
        }
        FrameEnd& s = *end;
        for (;;) {
            const DWORD want = static_cast<DWORD>(make_room(buf, size));
            s.ov.reset();
            DWORD got = 0;
            BOOL r = ReadFile(s.pipe, buf.data() + size, want, nullptr, &s.ov.ov);
            DWORD e = r ? ERROR_SUCCESS : GetLastError();
            if (r || e == ERROR_IO_PENDING) {
                if (!r) {
                    while (WaitForSingleObject(s.ov.ov.hEvent, 50) == WAIT_TIMEOUT) {
                        if (!stopping.load()) continue;
                        CancelIoEx(s.pipe, &s.ov.ov);
                        GetOverlappedResult(s.pipe, &s.ov.ov, &got, TRUE);
                        return Got::Stopped;
                    }
                }
                r = GetOverlappedResult(s.pipe, &s.ov.ov, &got, FALSE);
                e = r ? ERROR_SUCCESS : GetLastError();
            }
            size += got;
            if (r || e == ERROR_MORE_DATA) continue;
            // Гра закрила свій кінець: кадр записано повністю
            if (e == ERROR_BROKEN_PIPE || e == ERROR_PIPE_NOT_CONNECTED || e == ERROR_NO_DATA)
                return size > 0 ? Got::Data : Got::Empty;
            if (error) *error = trf("помилка читання каналу кадру (код {})", e);
            return Got::Error;
        }
    }

    // Гра відкрила і закрила, нічого не записавши: цей примірник — на заміну новим (другий уже чекає)
    void rearm(int64_t index) {
        auto it = slots.find(index);
        if (it == slots.end()) return;
        auto& ends = it->second->ends;
        for (size_t k = 0; k < ends.size(); ++k)
            if (ends[k]->connected) {
                ends.erase(ends.begin() + static_cast<ptrdiff_t>(k));
                break;
            }
        it->second->add_end(frame_name(opt.prefix, index, opt.ext), nullptr);
    }

    void drop(int64_t index) { slots.erase(index); }
    void wake_up() { SetEvent(wake); }
};
#else
struct FifoSlot {
    std::string path;
    int         fd = -1;
    bool        ready = false;   // гра відкрила FIFO (є дані або вже закрила)

    FifoSlot() = default;
    FifoSlot(const FifoSlot&) = delete;
    FifoSlot& operator=(const FifoSlot&) = delete;
    ~FifoSlot() {
        if (fd >= 0) ::close(fd);
        if (!path.empty()) ::unlink(path.c_str());
    }
    // Кінець для читання відкрито наперед (без блокування): тоді гра відкриває FIFO на запис одразу
    // Новий кінець відкривається раніше, ніж закривається старий: гра, що саме відкрила FIFO на
    // запис, ні на мить не лишається без читача (інакше отримала б SIGPIPE)
    bool reopen() {
        const int nfd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd >= 0) ::close(fd);
        fd = nfd;
        ready = false;
        if (fd < 0) return false;
#ifdef F_SETPIPE_SZ
        // Найменший буфер (сторінка): кадр не вміщується в нього, тож гра чекає на записі, поки
        // програма не прочитає, і не забігає за номери, для яких FIFO ще не створено
        ::fcntl(fd, F_SETPIPE_SZ, 4096);
#endif
        return true;
    }
};

struct FramePipeReader::Io {
    PipeOptions                                  opt;
    std::map<int64_t, std::unique_ptr<FifoSlot>> slots;

    std::string path(int64_t index) const { return path_to_utf8(opt.dir / path_from_utf8(frame_name(opt.prefix, index, opt.ext))); }

    // FIFO для номерів [from, to)
    bool ensure(int64_t from, int64_t to, std::string* error) {
        for (int64_t i = from; i < to; ++i) {
            if (slots.count(i)) continue;
            auto s = std::make_unique<FifoSlot>();
            const std::string p = path(i);
            ::unlink(p.c_str());   // залишок попереднього запуску
            if (::mkfifo(p.c_str(), 0600) != 0) {
                if (error) *error = trf("не вдалося створити FIFO {}: {}", p, std::strerror(errno));
                return false;
            }
            s->path = p;
            if (!s->reopen()) {
                if (error) *error = trf("не вдалося відкрити FIFO {}: {}", p, std::strerror(errno));
                return false;
            }
            slots.emplace(i, std::move(s));
        }
        return true;
    }

    bool open(const PipeOptions& o, std::string* error) {
        opt = o;
        std::error_code ec;
        fs::create_directories(opt.dir, ec);
        return ensure(0, opt.lookahead, error);
    }

    int64_t wait_ready(int timeout_ms) {
        std::vector<pollfd> fds;
        std::vector<int64_t> idx;
        for (auto& [i, s] : slots) {
            fds.push_back({s->fd, POLLIN, 0});
            idx.push_back(i);
        }
        if (fds.empty()) {
            if (timeout_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
            return -1;
        }
        if (::poll(fds.data(), fds.size(), timeout_ms) < 0) return -1;
        int64_t first = -1;
        for (size_t k = 0; k < fds.size(); ++k) {
            auto& s = *slots[idx[k]];
            s.ready = s.ready || (fds[k].revents & (POLLIN | POLLHUP | POLLERR)) != 0;
            if (s.ready && first < 0) first = idx[k];
        }
        return first;
    }

    int64_t highest_ready() const {
        for (auto it = slots.rbegin(); it != slots.rend(); ++it)
            if (it->second->ready) return it->first;
        return -1;
    }

    Got read(int64_t index, PixelBytes& buf, size_t& size, const std::atomic<bool>& stopping, std::string* error) {
        auto it = slots.find(index);
        if (it == slots.end()) {
            if (error) *error = tr("немає каналу кадру");
            return Got::Error;
        }
        const int fd = it->second->fd;
        for (;;) {
            const size_t want = make_room(buf, size);
            const ssize_t n = ::read(fd, buf.data() + size, want);
            if (n > 0) {
                size += static_cast<size_t>(n);
                continue;
            }
            if (n == 0) return size > 0 ? Got::Data : Got::Empty;   // гра закрила свій кінець
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                pollfd p{fd, POLLIN, 0};
                while (::poll(&p, 1, 50) == 0)
                    if (stopping.load()) return Got::Stopped;
                continue;
            }
            if (error) *error = trf("помилка читання FIFO: {}", std::strerror(errno));
            return Got::Error;
        }
    }

    // Гра відкрила і закрила FIFO: відкриваємо заново (інакше poll і далі бачив би "закрито")
    void rearm(int64_t index) {
        auto it = slots.find(index);
        if (it != slots.end()) it->second->reopen();
    }

    void drop(int64_t index) { slots.erase(index); }
    void wake_up() {}
};
#endif

// ============================== Читач ============================================
FramePipeReader::FramePipeReader(PipeOptions opt) : opt_(std::move(opt)), io_(std::make_unique<Io>()) {
    if (!opt_.ext.empty() && opt_.ext.front() != '.') opt_.ext = "." + opt_.ext;
    opt_.lookahead = std::clamp(opt_.lookahead, 2, 32);
    opt_.max_buffered = std::max(1, opt_.max_buffered);
    std::string err;
    if (!frame_pipes_supported()) {
        last_error_ = tr("канали для кадрів не підтримуються в цій системі");
        return;
    }
    if (!io_->open(opt_, &err)) {
        last_error_ = err;
        io_.reset();
        return;
    }
    ok_ = true;
#ifdef _WIN32
    audio_connected_ = !opt_.audio;
    audio_via_pipe_ = opt_.audio;
#else
    audio_connected_ = true;   // WAV гра пише звичайним файлом поруч
#endif
    const unsigned threads = opt_.decode_threads > 0 ? static_cast<unsigned>(opt_.decode_threads)
                                                     : std::max(2u, std::thread::hardware_concurrency() / 2);
    pool_ = std::make_unique<ThreadPool>(threads);
    io_thread_ = std::thread([this] { io_loop(); });
#ifdef _WIN32
    if (opt_.audio) audio_thread_ = std::thread([this] { audio_loop(); });
#endif
}

FramePipeReader::~FramePipeReader() {
    stop_ = true;
    if (io_) {
        io_->wake_up();
#ifdef _WIN32
        if (io_->port) PostQueuedCompletionStatus(io_->port, 0, static_cast<ULONG_PTR>(-1), nullptr);
#endif
    }
    cv_room_.notify_all();
    cv_ready_.notify_all();
    if (io_thread_.joinable()) io_thread_.join();
    if (audio_thread_.joinable()) audio_thread_.join();
    pool_.reset();   // дочекатися декодування
    io_.reset();     // закрити канали (FIFO — видалити)
}

std::string FramePipeReader::frame_path(int64_t index) const { return io_ ? io_->path(index) : std::string(); }

void FramePipeReader::set_producer_done() {
    if (producer_done_.exchange(true)) return;
    if (io_) io_->wake_up();
    cv_room_.notify_all();
#ifdef _WIN32
    // Звук іде окремим каналом: дочекатися, поки гра допише останній шматок (канал затихне), —
    // інакше після "запис завершено" хвіст звуку ще був би в дорозі
    if (ok_ && opt_.audio) {
        const int64_t t0 = now_ms();
        while (now_ms() - t0 < 2000) {
            const int64_t quiet = now_ms() - audio_last_data_ms_.load();
            if ((audio_clients_.load() == 0 && quiet > 100) || quiet > 300) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
#endif
}

int64_t FramePipeReader::pending_files() const {
    std::lock_guard lock(mutex_);
    return static_cast<int64_t>(ready_.size()) + in_flight_ + (receiving_ ? 1 : 0);
}

std::string FramePipeReader::last_error() const {
    std::lock_guard lock(mutex_);
    return last_error_;
}

ReaderStats FramePipeReader::stats() const {
    std::lock_guard lock(mutex_);
    return stats_;
}

void FramePipeReader::set_error(const std::string& e) {
    std::lock_guard lock(mutex_);
    last_error_ = e;
}

bool FramePipeReader::wait_for_room() {
    std::unique_lock lock(mutex_);
    const int cap = opt_.max_buffered + static_cast<int>(pool_->size());
    // Пам'ять зайнята — не читаємо: гра чекає на записі кадру, доки кодер не звільнить місце.
    // Недовго: потік каналів тим часом стежить, чи не треба створити канали далі.
    const bool room = cv_room_.wait_for(lock, std::chrono::milliseconds(20), [&] {
        return stop_.load() || in_flight_ + static_cast<int>(ready_.size()) < cap;
    });
    if (!room || stop_) return false;
    receiving_ = true;
    return true;
}

void FramePipeReader::frame_received(int64_t index, PixelBytes&& bytes, size_t size, double read_ms) {
    {
        std::lock_guard lock(mutex_);
        receiving_ = false;
        ++in_flight_;
        stats_.read_ms += read_ms;
    }
    connected_ = true;
    bytes_received_ += size;
    auto data = std::make_shared<PixelBytes>(std::move(bytes));
    pool_->submit([this, index, data, size] { decode_task(index, std::move(*data), size); });
}

void FramePipeReader::frame_failed(int64_t index, const std::string& why) {
    {
        std::lock_guard lock(mutex_);
        receiving_ = false;
        failed_.insert(index);
        ++skipped_;
        last_error_ = trf("кадр {}: {}", index, why);
    }
    log_warn("{}", trf("Кадр №{} з каналу не прийнято: {}", index, why));
    cv_ready_.notify_all();
}

void FramePipeReader::decode_task(int64_t index, PixelBytes bytes, size_t size) {
    std::string err;
    Image img;
    img.index = index;
    bool ok = false;
    const auto t0 = std::chrono::steady_clock::now();
    if (!image_file_complete(bytes.data(), size, opt_.ext)) {
        // Гра закрилась посеред кадру (збій) — такий кадр не беремо, як і обрізаний файл
        err = tr("кадр обрізаний (гра не дописала його)");
        recycle_buffer(std::move(bytes));
    } else {
        ok = decode_image_buffer(std::move(bytes), size, opt_.ext, img, &err, opt_.keep_yuv);
    }
    const double decode_ms = ms_since(t0);
    {
        std::lock_guard lock(mutex_);
        --in_flight_;
        stats_.decode_ms += decode_ms;
        if (ok) {
            ++stats_.frames;
            img.index = index;
            ready_.emplace(index, std::move(img));
        } else {
            failed_.insert(index);
            ++skipped_;
            last_error_ = trf("кадр {}: {}", index, err);
        }
    }
    if (!ok) log_warn("{}", trf("Кадр №{} з каналу не прийнято: {}", index, err));
    cv_ready_.notify_all();
}

void FramePipeReader::io_loop() {
    int64_t next = 0;             // наступний очікуваний номер кадру
    std::map<int64_t, int> empties;
    bool warned = false;
    std::string err_create;
    for (;;) {
        if (stop_) break;
        const bool done = producer_done_.load();
        // Після кінця запису — лише те, що гра вже встигла записати в канали
        const int64_t idx = io_->wait_ready(done ? 0 : 50);
        if (idx < 0) {
            if (done) break;
            continue;
        }
        // Гра пише кадри строго по черзі: відкрила пізніший — попередніх уже не буде
        for (int64_t k = next; k < idx; ++k) {
            io_->drop(k);
            frame_failed(k, tr("гра його не записала"));
        }
        next = std::max(next, idx);
        // Канали наперед — і від найдальшого кадру, який гра вже відкрила: крихітний кадр (менший
        // за буфер каналу) гра допише, не чекаючи на програму, і відразу візьметься за наступний
        const int64_t ahead = std::max(next, io_->highest_ready() + 1);
        if (!io_->ensure(next, std::min(ahead + opt_.lookahead, next + 4 * opt_.lookahead), &err_create) && !warned) {
            warned = true;
            set_error(err_create);
            log_warn("{}", trf("Канал для наступних кадрів: {}", err_create));
        }
        if (!wait_for_room()) {
            if (stop_) break;
            continue;
        }
        PixelBytes buf = acquire_buffer(std::max(last_size_ + kDecodePadding + kReadSlack, kMinFrameBuffer));
        size_t size = 0;
        std::string err;
        const auto t0 = std::chrono::steady_clock::now();
        const Got got = io_->read(idx, buf, size, stop_, &err);
        if (got == Got::Stopped) {
            recycle_buffer(std::move(buf));
            break;
        }
        if (got == Got::Empty) {
            // Гра відкрила "файл" кадру і закрила, нічого не записавши (скажімо, перевірила, чи він є) —
            // чекаємо на тому самому номері
            recycle_buffer(std::move(buf));
            {
                std::lock_guard lock(mutex_);
                receiving_ = false;
            }
            if (++empties[idx] <= kMaxEmptyOpens) {
                io_->rearm(idx);
                continue;
            }
            io_->drop(idx);
            frame_failed(idx, tr("гра відкривала кадр, але нічого не записала"));
        } else if (got == Got::Error) {
            recycle_buffer(std::move(buf));
            io_->drop(idx);
            frame_failed(idx, err);
        } else {
            io_->drop(idx);
            last_size_ = size;
            frame_received(idx, std::move(buf), size, ms_since(t0));
        }
        empties.erase(idx);
        next = idx + 1;
        if (!io_->ensure(next, next + opt_.lookahead, &err_create) && !warned) {
            warned = true;
            set_error(err_create);
            log_warn("{}", trf("Канал для наступних кадрів: {}", err_create));
        }
    }
    {
        std::lock_guard lock(mutex_);
        io_done_ = true;
        receiving_ = false;
    }
    cv_ready_.notify_all();
}

void FramePipeReader::audio_loop() {
#ifdef _WIN32
    // Звук гри: усе, що рушій пише в <prefix>.wav (скільки б разів він його не відкривав), — у
    // звичайний WAV у тимчасовій папці, строго в порядку відкриттів: з'єднання — у черзі в порядку
    // пакетів порту завершення, і кожне дочитується до кінця, перш ніж братися за наступне (гра
    // закриває шматок раніше, ніж відкриває наступний). Цей WAV читає звук гри, як і з файлами.
    Io& io = *io_;
    // Знову чекати на щойно закритому примірнику треба швидше, ніж гра відкриє звук наступного разу, —
    // тож цей потік не стоїть у черзі за потоками кодування
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    const fs::path tee_path = opt_.dir / path_from_utf8(opt_.prefix + ".wav");
    std::FILE* tee = nullptr;
    bool tee_failed = false, warned = false;
    auto write_tee = [&](const uint8_t* data, size_t n) {
        audio_last_data_ms_ = now_ms();
        if (n == 0 || tee_failed) return;
        if (!tee) {
            // _SH_DENYNO: звук гри читає цей файл, поки ми його дописуємо
            tee = _wfsopen(tee_path.c_str(), L"wb", 0x40 /* _SH_DENYNO */);
            if (!tee) {
                tee_failed = true;
                log_warn("{}", trf("Не вдалося створити WAV звуку гри: {}", path_to_utf8(tee_path)));
                return;
            }
        }
        std::fwrite(data, 1, n, tee);
        std::fflush(tee);
        audio_bytes_ += n;
    };
    std::deque<size_t> order;   // з'єднані примірники в порядку з'єднання
    bool reading = false;       // читання голови черги в дорозі
    auto connected = [&](size_t i) {
        audio_connected_ = true;
        ++audio_clients_;
        audio_last_data_ms_ = now_ms();
        order.push_back(i);
    };
    auto arm = [&](size_t i) {
        if (io.wav[i]->arm()) connected(i);
        else if (io.wav[i]->st == WavPipe::St::Failed && !warned) {
            warned = true;
            log_warn("{}", trf("Канал звуку гри: помилка очікування (код {})", GetLastError()));
        }
    };
    // Гра закрила голову черги: цей примірник знову чекає
    auto finish_head = [&] {
        const size_t i = order.front();
        order.pop_front();
        reading = false;
        --audio_clients_;
        DisconnectNamedPipe(io.wav[i]->pipe);
        arm(i);
    };
    // Читати голову черги (результат прийде пакетом у порт)
    auto start_read = [&] {
        while (!reading && !order.empty()) {
            WavPipe& w = *io.wav[order.front()];
            w.ov.reset();
            w.st = WavPipe::St::Reading;
            if (ReadFile(w.pipe, w.buf.data(), static_cast<DWORD>(w.buf.size()), nullptr, &w.ov.ov) ||
                GetLastError() == ERROR_IO_PENDING) {
                reading = true;
                return;
            }
            w.st = WavPipe::St::Connected;   // гра вже закрила, даних більше нема — пакета не буде
            finish_head();
        }
    };
    // Один пакет порту: з'єднання або прочитаний шматок
    auto handle = [&](int timeout_ms) {
        start_read();
        DWORD n = 0;
        ULONG_PTR key = 0;
        OVERLAPPED* ov = nullptr;
        const BOOL ok = GetQueuedCompletionStatus(io.port, &n, &key, &ov, static_cast<DWORD>(timeout_ms));
        const DWORD err = ok ? ERROR_SUCCESS : GetLastError();
        if (!ov || key >= io.wav.size()) return false;   // час вийшов або зупинка
        WavPipe& w = *io.wav[key];
        if (w.st == WavPipe::St::Listening) {
            if (ok || err == ERROR_PIPE_CONNECTED || err == ERROR_NO_DATA) {
                w.st = WavPipe::St::Connected;
                connected(key);
            } else {
                DisconnectNamedPipe(w.pipe);
                arm(key);
            }
        } else if (w.st == WavPipe::St::Reading) {
            write_tee(w.buf.data(), n);
            reading = false;
            w.st = WavPipe::St::Connected;
            if (!ok && err != ERROR_MORE_DATA) finish_head();   // гра закрила свій кінець
        }
        return true;
    };
    for (size_t i = 0; i < io.wav.size(); ++i) arm(i);
    while (!stop_) handle(50);
    // Те, що вже прийшло, дописуємо; незавершені операції скасовуються разом із каналами
    for (int guard = 0; guard < 10000 && handle(0); ++guard) {
    }
    if (tee) std::fclose(tee);
#endif
}

FrameSource::Wait FramePipeReader::next_for(Image& out, int timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(0, timeout_ms));
    std::unique_lock lock(mutex_);
    for (;;) {
        if (stop_ || !ok_) return Wait::End;
        while (failed_.count(next_deliver_)) {
            failed_.erase(next_deliver_);
            ++next_deliver_;
        }
        auto it = ready_.find(next_deliver_);
        if (it != ready_.end()) {
            out = std::move(it->second);
            ready_.erase(it);
            ++next_deliver_;
            ++delivered_;
            lock.unlock();
            cv_room_.notify_all();
            return Wait::Frame;
        }
        // Кінець: гра закінчила, канали дочитано, усе декодоване віддано
        if (io_done_ && in_flight_ == 0 && ready_.empty()) return Wait::End;
        if (std::chrono::steady_clock::now() >= deadline) return Wait::Timeout;
        cv_ready_.wait_until(lock, std::min(deadline, std::chrono::steady_clock::now() + std::chrono::milliseconds(20)));
    }
}

} // namespace gmdr::frames
