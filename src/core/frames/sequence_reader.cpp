#include "sequence_reader.hpp"

#include "../util/file_util.hpp"
#include "../util/log.hpp"
#include "../util/strings.hpp"
#include "image_decode.hpp"
#include "../util/i18n.hpp"

#include <algorithm>
#include <chrono>
#include <format>

namespace gmdr::frames {

namespace fs = std::filesystem;

namespace {
int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
double ms_since(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}
} // namespace

int64_t FrameSequenceReader::parse_index(const std::string& name, const std::string& prefix,
                                         const std::vector<std::string>& exts) {
    if (name.size() <= prefix.size() || !starts_with_i(name, prefix)) return -1;
    std::string ext_found;
    for (const auto& e : exts) {
        if (ends_with_i(name, e)) {
            ext_found = e;
            break;
        }
    }
    if (ext_found.empty()) return -1;
    const std::string digits = name.substr(prefix.size(), name.size() - prefix.size() - ext_found.size());
    if (digits.empty() || digits.size() > 12) return -1;
    for (char c : digits)
        if (c < '0' || c > '9') return -1;
    return std::stoll(digits);
}

FrameSequenceReader::FrameSequenceReader(SequenceOptions opt) : opt_(std::move(opt)) {
    unsigned threads = opt_.decode_threads > 0 ? static_cast<unsigned>(opt_.decode_threads)
                                               : std::max(2u, std::thread::hardware_concurrency() / 2);
    pool_ = std::make_unique<ThreadPool>(threads);
    next_dispatch_ = opt_.first_index;
    next_deliver_ = opt_.first_index;
    dispatcher_ = std::thread([this] { dispatcher_loop(); });
}

FrameSequenceReader::~FrameSequenceReader() {
    stop_ = true;
    cv_dispatch_.notify_all();
    cv_ready_.notify_all();
    if (dispatcher_.joinable()) dispatcher_.join();
    pool_.reset();   // дочекатися завдань декодування
    retry_deletions();
}

void FrameSequenceReader::set_producer_done() {
    producer_done_ = true;
    cv_dispatch_.notify_all();
}

int64_t FrameSequenceReader::pending_files() const {
    std::lock_guard lock(mutex_);
    return static_cast<int64_t>(known_.size()) + in_flight_;
}

uint64_t FrameSequenceReader::pending_bytes() const {
    std::lock_guard lock(mutex_);
    uint64_t total = 0;
    for (const auto& [i, k] : known_) total += k.size;
    return total;
}

std::string FrameSequenceReader::last_error() const {
    std::lock_guard lock(mutex_);
    return last_error_;
}

ReaderStats FrameSequenceReader::stats() const {
    std::lock_guard lock(mutex_);
    return stats_;
}

// Викликається під замком mutex_.
void FrameSequenceReader::add_found(int64_t idx, const fs::path& p, uint64_t size, int64_t t) {
    if (next_dispatch_ >= 0 && idx < next_dispatch_) return;   // вже оброблений (видаляється)
    auto it = known_.find(idx);
    if (it == known_.end()) {
        known_[idx] = Known{p, size, t, 0};
        saw_any_ = true;
    } else if (it->second.size != size) {
        it->second.size = size;
        it->second.stable_since_ms = t;
    }
}

void FrameSequenceReader::scan_directory() {
    std::error_code ec;
    if (!fs::exists(opt_.dir, ec)) return;
    struct Found {
        int64_t     idx;
        fs::path    path;
        uint64_t    size;
        int         digits;
        std::string ext;
    };
    std::vector<Found> found;
    for (fs::directory_iterator it(opt_.dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code ec2;
        if (!it->is_regular_file(ec2)) continue;
        const std::string name = path_to_utf8(it->path().filename());
        const int64_t idx = parse_index(name, opt_.prefix, opt_.extensions);
        if (idx < 0) continue;
        const std::string ext = path_to_utf8(it->path().extension());
        const uint64_t size = it->file_size(ec2);
        found.push_back({idx, it->path(), ec2 ? 0 : size,
                         static_cast<int>(name.size() - opt_.prefix.size() - ext.size()), ext});
    }
    const int64_t t = now_ms();
    std::lock_guard lock(mutex_);
    for (auto& f : found) {
        add_found(f.idx, f.path, f.size, t);
        if (digits_ == 0) {
            digits_ = f.digits;
            ext_ = f.ext;
        }
    }
}

void FrameSequenceReader::probe_expected() {
    // Перевіряємо лише наступні номери: next_dispatch_ (оновити розмір) і далі, поки файли є.
    int64_t from;
    int limit;
    {
        std::lock_guard lock(mutex_);
        if (digits_ == 0 || next_dispatch_ < 0) return;
        from = next_dispatch_;
        limit = opt_.max_buffered + static_cast<int>(pool_->size()) + 4;
    }
    const int64_t t = now_ms();
    for (int n = 0; n < limit && !stop_; ++n) {
        const int64_t idx = from + n;
        const fs::path p = opt_.dir / path_from_utf8(std::format("{}{:0{}}{}", opt_.prefix, idx, digits_, ext_));
        std::error_code ec;
        const uint64_t size = fs::file_size(p, ec);
        if (ec) {
            if (n == 0) continue;   // очікуваний файл ще не з'явився — наступні теж навряд чи є
            break;
        }
        std::lock_guard lock(mutex_);
        add_found(idx, p, size, t);
    }
}

void FrameSequenceReader::try_dispatch() {
    std::unique_lock lock(mutex_);
    const int max_in_pipeline = opt_.max_buffered + static_cast<int>(pool_->size());
    const bool done = producer_done_.load();
    while (!known_.empty()) {
        if (next_dispatch_ < 0) {
            // Початок послідовності — найменший знайдений номер.
            // У живому режимі чекаємо другий файл (або кінець запису), щоб не схопити перший файл посеред запису.
            next_dispatch_ = known_.begin()->first;
            if (next_deliver_ < 0) next_deliver_ = next_dispatch_;
        }
        if (in_flight_ + static_cast<int>(ready_.size()) >= max_in_pipeline) return;
        auto it = known_.find(next_dispatch_);
        if (it == known_.end()) {
            // Пропуск у нумерації: є файли з більшими номерами, а потрібного немає.
            const auto higher = known_.upper_bound(next_dispatch_);
            if (higher == known_.end()) return;   // просто чекаємо наступний файл
            if (gap_since_ms_ == 0) gap_since_ms_ = now_ms();
            if (done || !opt_.live || now_ms() - gap_since_ms_ > 3000) {
                log_warn("{}", trf("Кадр №{} відсутній — пропускаю", next_dispatch_));
                failed_.insert(next_dispatch_);
                ++next_dispatch_;
                ++skipped_;
                gap_since_ms_ = 0;
                cv_ready_.notify_all();
                continue;
            }
            return;
        }
        gap_since_ms_ = 0;
        // Чи дописаний файл?
        bool complete = !opt_.live || done || known_.upper_bound(next_dispatch_) != known_.end();
        if (!complete && now_ms() - it->second.stable_since_ms > 1500 && it->second.size > 0) {
            // Розмір не змінюється 1.5 с — перевіримо вміст (напр. гра "призупинена")
            if (auto bytes = read_file_bytes(it->second.path)) {
                const std::string ext = path_to_utf8(it->second.path.extension());
                complete = image_file_complete(bytes->data(), bytes->size(), ext);
            }
            it->second.stable_since_ms = now_ms();   // наступна перевірка — не раніше ніж за 1.5 с
        }
        if (!complete) return;
        const int64_t idx = next_dispatch_;
        fs::path path = it->second.path;
        known_.erase(it);
        ++next_dispatch_;
        ++in_flight_;
        const std::string ext = path_to_utf8(path.extension());
        lock.unlock();
        pool_->submit([this, idx, path, ext] { decode_task(idx, path, ext); });
        lock.lock();
    }
}

void FrameSequenceReader::decode_task(int64_t index, fs::path path, std::string ext) {
    std::string err;
    Image img;
    bool ok = false;
    bool deleted = false;
    double read_ms = 0, decode_ms = 0;
    // Повторні спроби лише для тимчасових проблем: файл заблоковано (антивірус)
    // або ще не повністю записаний на диск. Пошкоджений файл — одразу пропускаємо.
    int incomplete = 0;
    for (int attempt = 0; attempt < 30 && !stop_; ++attempt) {
        FrameFileRead rd;
        const auto t0 = std::chrono::steady_clock::now();
        const ReadStatus st = read_frame_file(path, ext, opt_.delete_after_read, rd);
        read_ms += ms_since(t0);
        if (st == ReadStatus::Missing) {
            err = tr("файл зник");
            break;
        }
        if (st != ReadStatus::Ok) {
            err = rd.error;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        if (!rd.complete) {
            err = rd.size == 0 ? tr("порожній файл") : tr("файл обрізаний");
            if (++incomplete > 5) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        deleted = rd.deleted;
        img.index = index;
        const auto t1 = std::chrono::steady_clock::now();
        ok = decode_image_buffer(std::move(rd.bytes), rd.size, ext, img, &err, opt_.keep_yuv);
        decode_ms += ms_since(t1);
        break;
    }
    if (opt_.delete_after_read && !deleted) {
        std::error_code ec;
        if (!fs::remove(path, ec) && fs::exists(path, ec)) {
            std::lock_guard lock(mutex_);
            to_delete_.push_back(path);
        }
    }
    {
        std::lock_guard lock(mutex_);
        --in_flight_;
        stats_.read_ms += read_ms;
        stats_.decode_ms += decode_ms;
        if (ok) {
            ++stats_.frames;
            img.index = index;
            ready_.emplace(index, std::move(img));
        } else {
            failed_.insert(index);
            ++skipped_;
            last_error_ = trf("кадр {}: {}", path_to_utf8(path.filename()), err);
            log_warn("{}", trf("Не вдалося прочитати {}", last_error_));
        }
    }
    cv_ready_.notify_all();
    cv_dispatch_.notify_all();
}

void FrameSequenceReader::retry_deletions() {
    std::vector<fs::path> list;
    {
        std::lock_guard lock(mutex_);
        list.swap(to_delete_);
    }
    std::vector<fs::path> still;
    for (auto& p : list) {
        std::error_code ec;
        fs::remove(p, ec);
        if (fs::exists(p, ec)) still.push_back(p);
    }
    if (!still.empty()) {
        std::lock_guard lock(mutex_);
        to_delete_.insert(to_delete_.end(), still.begin(), still.end());
    }
}

void FrameSequenceReader::dispatcher_loop() {
    int64_t last_delete_retry = now_ms();
    while (!stop_) {
        const int64_t t = now_ms();
        bool know_pattern;
        {
            std::lock_guard lock(mutex_);
            know_pattern = digits_ > 0 && next_dispatch_ >= 0;
        }
        // Не живий режим: усі файли вже є — достатньо одного сканування.
        // Живий: повне сканування, поки не знаємо шаблону імен, і зрідка як запасний варіант
        // (раптом гра пропустила номер або почала з несподіваного).
        const bool full = opt_.live ? (!know_pattern || t - last_full_scan_ms_ > 1000) : last_full_scan_ms_ == 0;
        if (full) {
            scan_directory();
            last_full_scan_ms_ = t;
        } else if (opt_.live) {
            probe_expected();
        }
        try_dispatch();
        if (now_ms() - last_delete_retry > 2000) {
            retry_deletions();
            last_delete_retry = now_ms();
        }
        cv_ready_.notify_all();
        std::unique_lock lock(mutex_);
        cv_dispatch_.wait_for(lock, std::chrono::milliseconds(opt_.live ? 5 : 2));
    }
}

FrameSequenceReader::Wait FrameSequenceReader::next_for(Image& out, int timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(0, timeout_ms));
    std::unique_lock lock(mutex_);
    for (;;) {
        if (stop_) return Wait::End;
        // Пропускаємо номери, які не вдалося прочитати
        while (next_deliver_ >= 0 && failed_.count(next_deliver_)) {
            failed_.erase(next_deliver_);
            ++next_deliver_;
        }
        if (next_deliver_ >= 0) {
            auto it = ready_.find(next_deliver_);
            if (it != ready_.end()) {
                out = std::move(it->second);
                ready_.erase(it);
                ++next_deliver_;
                ++delivered_;
                lock.unlock();
                cv_dispatch_.notify_all();
                return Wait::Frame;
            }
        }
        // Кінець: запис завершено і все знайдене оброблено
        const bool nothing_left = known_.empty() && in_flight_ == 0 && ready_.empty();
        if ((producer_done_ || !opt_.live) && nothing_left) {
            // Останнє сканування, щоб не пропустити файли, що з'явилися в останню мить
            lock.unlock();
            scan_directory();
            try_dispatch();
            lock.lock();
            if (known_.empty() && in_flight_ == 0 && ready_.empty()) return Wait::End;
            continue;
        }
        if (std::chrono::steady_clock::now() >= deadline) return Wait::Timeout;
        cv_ready_.wait_until(lock, std::min(deadline, std::chrono::steady_clock::now() + std::chrono::milliseconds(20)));
    }
}

std::optional<Image> FrameSequenceReader::next(const std::atomic<bool>* cancel) {
    Image img;
    for (;;) {
        if (cancel && cancel->load()) return std::nullopt;
        switch (next_for(img, 100)) {
        case Wait::Frame: return img;
        case Wait::End: return std::nullopt;
        case Wait::Timeout: break;
        }
    }
}

} // namespace gmdr::frames
