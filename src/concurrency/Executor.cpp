#include "afina/concurrency/Executor.h"

namespace Afina {
namespace Concurrency {
Executor::Executor(size_t low_watermark, size_t hight_watermark, size_t max_queue_size, size_t idle_time)
    : low_watermark(low_watermark), hight_watermark(hight_watermark), max_queue_size(max_queue_size),
      state(Executor::State::kRun), idle_time(std::chrono::milliseconds(idle_time)) {
    for (int i = 0; i < low_watermark; i++) {
        auto t = std::thread([this]() { perform(this); });
        t.detach();
        threads.emplace(std::make_pair(t.get_id(), std::move(t)));
    }
}

void perform(Executor *executor) {
    while (executor->state != Executor::State::kStopped) {
        std::unique_lock<std::mutex> l(executor->mutex);
        executor->spare_count++;

        while (true) {
            auto until = std::chrono::high_resolution_clock::now() + executor->idle_time;
            while (std::chrono::high_resolution_clock::now() < until && executor->tasks.empty() &&
                   executor->state == Executor::State::kRun) {
                executor->empty_condition.wait_until(l, until);
            }

            if (!executor->tasks.empty())
                break;

            if (executor->state != Executor::State::kRun) {
                executor->threads.erase(std::this_thread::get_id());
                if (executor->threads.empty()) {
                    executor->state = Executor::State::kStopped;
                    l.unlock();

                    executor->empty_condition.notify_one(); //Спросить!
                }
                return;
            }

            if (executor->threads.size() == executor->low_watermark)
                continue;

            // Redundant thread
            executor->threads.erase(std::this_thread::get_id());
            executor->spare_count--;
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
    state = State::kStopping;
    empty_condition.notify_all();
    while (await && state == State::kStopping) {
        empty_condition.wait(lock);
    }
}

Executor::~Executor() { Stop(true); }
} // namespace Concurrency
} // namespace Afina
