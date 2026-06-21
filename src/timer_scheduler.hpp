#pragma once

#include "common.hpp"

#include <condition_variable>
#include <functional>
#include <thread>

namespace sae {

class TimerScheduler {
public:
  using Task = std::function<void()>;

  TimerScheduler();
  ~TimerScheduler();
  TimerScheduler(const TimerScheduler &) = delete;
  TimerScheduler &operator=(const TimerScheduler &) = delete;

  void schedule(TimePoint due, Task task);
  size_t pending() const;
  void stop();

private:
  struct Item {
    TimePoint due;
    uint64_t sequence = 0;
    Task task;
  };

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::vector<Item> items_;
  std::thread worker_;
  bool running_ = true;
  uint64_t next_sequence_ = 0;

  void loop();
};

} // namespace sae
