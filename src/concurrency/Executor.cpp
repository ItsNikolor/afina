#include "afina/concurrency/Executor.h"

namespace Afina {
namespace Concurrency {
Executor::Executor(size_t low_watermark, size_t hight_watermark, size_t max_queue_size, size_t idle_time)
    : low_watermark(low_watermark), hight_watermark(hight_watermark), max_queue_size(max_queue_size),
      state(Executor::State::kRun), idle_time(std::chrono::milliseconds(idle_time)), threads_count(low_watermark),
      busy_count(low_watermark) {
    for (int i = 0; i < low_watermark; i++) {
        auto t = std::thread([this]() { perform(this); });
        t.detach();
    }
}

void perform(Executor *executor) {
    while (true) {
        std::unique_lock<std::mutex> l(executor->mutex);

        if (executor->tasks.empty()) {
            executor->busy_count--;
        }

        while (true) {
            auto until = std::chrono::high_resolution_clock::now() + executor->idle_time;
            while (executor->tasks.empty() && executor->state == Executor::State::kRun) {
                if (executor->empty_condition.wait_until(l, until) == std::cv_status::timeout) {
                    break;
                }
            }

            if (!executor->tasks.empty()) {
                break;
            }

            if (executor->state != Executor::State::kRun) {
                executor->threads_count--;
                if (executor->threads_count == 0) {
                    executor->state = Executor::State::kStopped;
                    l.unlock();

                    executor->stop_condition.notify_all();
                }
                l.unlock();
                executor->empty_condition.notify_one();
                return;
            }

            if (executor->threads_count == executor->low_watermark)
                continue;

            // Redundant thread
            executor->threads_count--;
            return;
        }

        auto f = executor->tasks.back();
        executor->tasks.pop_back();
        l.unlock();

        f();
    }
}

void Executor::Stop(bool await) {
    std::unique_lock<std::mutex> lock(mutex);
    if (threads_count != 0) {
        state = State::kStopping;
    } else {
        state = State::kStopped;
    }
    empty_condition.notify_one();
    while (await && threads_count) {
        stop_condition.wait(lock);
    }
}

Executor::~Executor() { Stop(true); }
} // namespace Concurrency
} // namespace Afina
