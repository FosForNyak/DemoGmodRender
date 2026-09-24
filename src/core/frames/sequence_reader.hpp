// =============================================================================
//  sequence_reader.hpp — читання послідовності кадрів з папки
//  (f0000.tga, f0001.tga, ...) у правильному порядку, паралельно на всіх ядрах.
//
//  "Живий" режим: гра ще записує кадри. Файл N вважається дописаним, коли
//  з'явився файл N+1 (рушій пише кадри строго по черзі) або гра завершила
//  запис. Прочитані файли можна одразу видаляти — так на диску ніколи не
//  лежить більше кількох десятків кадрів навіть для годинного відео.
//
//  Нові файли шукаються не обходом усієї папки, а перевіркою кількох
//  наступних очікуваних імен (імена йдуть по порядку). Повне сканування —
//  лише зрідка, як запасний варіант.
// =============================================================================
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "../util/thread_pool.hpp"
#include "frame_source.hpp"
#include "image.hpp"

namespace gmdr::frames {

struct SequenceOptions {
    std::filesystem::path dir;
    std::string           prefix;                 // напр. "f" для f0000.tga
    std::vector<std::string> extensions = {".tga", ".jpg", ".jpeg", ".png", ".bmp"};
    bool                  live = false;           // файли ще створюються
    bool                  delete_after_read = false;
    int                   decode_threads = 0;     // 0 — автоматично
    int                   max_buffered = 12;      // скільки кадрів тримати декодованими
    int64_t               first_index = -1;       // з якого номера почати (-1 — з найменшого)
    bool                  keep_yuv = true;        // JPEG віддавати в YUV (без перетворення в RGB)
};

class FrameSequenceReader final : public FrameSource {
public:
    explicit FrameSequenceReader(SequenceOptions opt);
    ~FrameSequenceReader() override;
    FrameSequenceReader(const FrameSequenceReader&) = delete;
    FrameSequenceReader& operator=(const FrameSequenceReader&) = delete;

    // Гра завершила запис: більше нових файлів не буде.
    void set_producer_done() override;
    bool producer_done() const override { return producer_done_.load(); }

    // Наступний кадр за порядком. nullopt — кінець послідовності або скасування.
    std::optional<Image> next(const std::atomic<bool>* cancel = nullptr);

    // Те саме, але з обмеженням часу очікування.
    Wait next_for(Image& out, int timeout_ms) override;

    int64_t  pending_files() const override;        // файлів на диску, ще не взятих в обробку
    uint64_t pending_bytes() const override;
    int64_t  delivered() const override { return delivered_.load(); }
    int64_t  skipped() const override { return skipped_.load(); }
    bool     saw_any_file() const override { return saw_any_.load(); }
    std::string last_error() const override;
    ReaderStats stats() const override;

    // Розібрати ім'я файлу: prefix + число + розширення. Повертає номер або -1.
    static int64_t parse_index(const std::string& filename, const std::string& prefix,
                               const std::vector<std::string>& exts);

private:
    struct Known {
        std::filesystem::path path;
        uint64_t              size = 0;
        int64_t               stable_since_ms = 0;
        int                   retries = 0;
    };

    void dispatcher_loop();
    void scan_directory();
    void probe_expected();
    void add_found(int64_t idx, const std::filesystem::path& p, uint64_t size, int64_t now);
    void try_dispatch();
    void decode_task(int64_t index, std::filesystem::path path, std::string ext);
    void retry_deletions();

    SequenceOptions            opt_;
    std::unique_ptr<ThreadPool> pool_;

    mutable std::mutex         mutex_;
    std::condition_variable    cv_ready_;       // з'явився декодований кадр
    std::condition_variable    cv_dispatch_;    // є місце для нових завдань
    std::map<int64_t, Known>   known_;          // знайдені, ще не взяті файли
    std::map<int64_t, Image>   ready_;          // декодовані кадри, що чекають видачі
    std::set<int64_t>          failed_;         // номери, які не вдалося прочитати
    std::vector<std::filesystem::path> to_delete_;
    int64_t                    next_dispatch_ = -1;
    int64_t                    next_deliver_ = -1;
    int                        in_flight_ = 0;
    int64_t                    gap_since_ms_ = 0;
    std::string                last_error_;
    // Шаблон імені, дізнаний з першого знайденого файлу: prefix + номер (digits_ цифр) + ext_
    int                        digits_ = 0;
    std::string                ext_;
    int64_t                    last_full_scan_ms_ = 0;
    ReaderStats                stats_;

    std::atomic<bool>          producer_done_{false};
    std::atomic<bool>          stop_{false};
    std::atomic<int64_t>       delivered_{0};
    std::atomic<int64_t>       skipped_{0};
    std::atomic<bool>          saw_any_{false};
    std::thread                dispatcher_;
};

} // namespace gmdr::frames
