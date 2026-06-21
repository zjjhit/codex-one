#include "timer_scheduler.hpp"

namespace sae {

TimerScheduler::TimerScheduler() {
  worker_ = std::thread([this] { loop(); });
}

TimerScheduler::~TimerScheduler() {
  stop();
}

void TimerScheduler::schedule(TimePoint due, Task task) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    items_.push_back(Item{due, next_sequence_++, std::move(task)});
  }
  cv_.notify_one();
}

size_t TimerScheduler::pending() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return items_.size();
}

void TimerScheduler::stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_) return;
    running_ = false;
  }
  cv_.notify_all();
  if (worker_.joinable()) worker_.join();
}

void TimerScheduler::loop() {
  std::unique_lock<std::mutex> lock(mutex_);
  while (running_) {
    if (items_.empty()) {
      cv_.wait(lock, [this] { return !running_ || !items_.empty(); });
      continue;
    }

    auto next = std::min_element(items_.begin(), items_.end(), [](const Item &a, const Item &b) {
      if (a.due == b.due) return a.sequence < b.sequence;
      return a.due < b.due;
    });
    auto due = next->due;
    cv_.wait_until(lock, due, [this, due] {
      if (!running_) return true;
      auto earlier = std::min_element(items_.begin(), items_.end(), [](const Item &a, const Item &b) {
        if (a.due == b.due) return a.sequence < b.sequence;
        return a.due < b.due;
      });
      return earlier != items_.end() && earlier->due < due;
    });
    if (!running_) break;
    if (Clock::now() < due) continue;

    std::vector<Task> due_tasks;
    auto now = Clock::now();
    auto it = items_.begin();
    while (it != items_.end()) {
      if (it->due <= now) {
        due_tasks.push_back(std::move(it->task));
        it = items_.erase(it);
      } else {
        ++it;
      }
    }
    lock.unlock();
    for (auto &task : due_tasks) {
      if (task) task();
    }
    lock.lock();
  }
}

} // namespace sae
