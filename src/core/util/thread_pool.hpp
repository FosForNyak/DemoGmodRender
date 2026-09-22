// =============================================================================
//  thread_pool.hpp — пул потоків для використання всіх ядер процесора.
//
//  Використання:
//      ThreadPool pool(8);
//      auto fut = pool.submit([]{ return 42; });
//      pool.parallel_for(0, height, [&](int y0, int y1){ ... обробити рядки ... });
//
//  parallel_for ділить діапазон на шматки; викликаючий потік теж працює,
//  тому parallel_for можна безпечно викликати навіть з задач цього ж пулу.
// =============================================================================
#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

namespace gmdr {

class ThreadPool {
public:
    explicit ThreadPool(unsigned threads = 0) {
        if (threads == 0) threads = std::max(1u, std::thread::hardware_concurrency());
        workers_.reserve(threads);
        for (unsigned i = 0; i < threads; ++i)
            workers_.emplace_back([this] { worker_loop(); });
    }

    ~ThreadPool() {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        cv_.notify_all();
        for (auto& t : workers_)
            if (t.joinable()) t.join();
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    unsigned size() const { return static_cast<unsigned>(workers_.size()); }

    template <class F>
    auto submit(F&& f) -> std::future<std::invoke_result_t<F>> {
        using R = std::invoke_result_t<F>;
        auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(f));
        std::future<R> fut = task->get_future();
        {
            std::lock_guard lock(mutex_);
            queue_.emplace_back([task] { (*task)(); });
        }
        cv_.notify_one();
        return fut;
    }

    // Виконати fn(begin_chunk, end_chunk) для всього [begin, end), паралельно.
    template <class F>
    void parallel_for(int begin, int end, F&& fn, int min_chunk = 16) {
        const int total = end - begin;
        if (total <= 0) return;
        const int parts = std::max(1, std::min<int>(static_cast<int>(size()) + 1, total / std::max(1, min_chunk)));
        if (parts <= 1) {
            fn(begin, end);
            return;
        }
        struct Shared {
            std::atomic<int> next{0};
            std::atomic<int> done{0};
            std::mutex       m;
            std::condition_variable cv;
            std::exception_ptr error;
        };
        auto shared = std::make_shared<Shared>();
        const int chunk = (total + parts - 1) / parts;
        auto run_chunks = [shared, begin, end, chunk, parts, &fn]() {
            for (;;) {
                const int i = shared->next.fetch_add(1);
                if (i >= parts) break;
                const int b = begin + i * chunk;
                const int e = std::min(end, b + chunk);
                try {
                    if (b < e) fn(b, e);
                } catch (...) {
                    std::lock_guard lock(shared->m);
                    if (!shared->error) shared->error = std::current_exception();
                }
                if (shared->done.fetch_add(1) + 1 == parts) {
                    std::lock_guard lock(shared->m);
                    shared->cv.notify_all();
                }
            }
        };
        // Помічники з пулу (не більше ніж частин - 1).
        const int helpers = std::min<int>(static_cast<int>(size()), parts - 1);
        {
            std::lock_guard lock(mutex_);
            for (int i = 0; i < helpers; ++i) queue_.emplace_front(run_chunks);
        }
        cv_.notify_all();
        run_chunks();   // викликаючий потік теж працює
        std::unique_lock lock(shared->m);
        shared->cv.wait(lock, [&] { return shared->done.load() >= parts; });
        if (shared->error) std::rethrow_exception(shared->error);
    }

private:
    void worker_loop() {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock lock(mutex_);
                cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
                if (stopping_ && queue_.empty()) return;
                job = std::move(queue_.front());
                queue_.pop_front();
            }
            job();
        }
    }

    std::vector<std::thread>          workers_;
    std::deque<std::function<void()>> queue_;
    std::mutex                        mutex_;
    std::condition_variable           cv_;
    bool                              stopping_ = false;
};

} // namespace gmdr
