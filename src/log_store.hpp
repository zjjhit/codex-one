#pragma once

#include "common.hpp"
#include "config.hpp"

#include <condition_variable>
#include <fstream>
#include <thread>

#if SAE_HAVE_SQLITE
#include <sqlite3.h>
#endif

namespace sae {

struct CallRecord {
  std::string call_id;
  std::string sip_call_id;
  std::string called_number;
  std::string line_id;
  std::string status;
  TimePoint call_time = Clock::now();
  TimePoint answer_time = TimePoint{};
  TimePoint hangup_time = TimePoint{};
  int planned_hold_seconds = 0;
  int actual_hold_ms = 0;
  int daily_answer_count = 0;
  int answer_rate_percent = 100;
  std::string error;
  std::string sip_ip;
  int sip_port = 5060;
  std::string called_prefix;

  std::string to_json() const;
};

struct MetricsSnapshot {
  uint64_t total_calls = 0;
  uint64_t answered = 0;
  uint64_t rejected = 0;
  uint64_t abnormal = 0;
  uint64_t current_concurrent = 0;
  uint64_t peak_concurrent = 0;
  uint64_t rejected_prefix_mismatch = 0;
  uint64_t rejected_probability = 0;
  uint64_t rejected_daily_limit = 0;
  uint64_t rejected_concurrency_limit = 0;
  uint64_t log_queue_depth = 0;
  uint64_t log_dropped = 0;
  uint64_t timer_pending = 0;
  uint64_t rtp_ports_in_use = 0;
  uint64_t rtp_ports_available = 0;
  double avg_answer_ms = 0;
  double avg_hangup_ms = 0;
  int daily_answer_count = 0;
  std::string daily_date;

  std::string to_json() const;
};

class LogStore {
public:
  LogStore();
  ~LogStore();
  void open(const Config &config);
  void append(const CallRecord &record);
  std::string query_json(size_t limit) const;
  size_t queue_depth() const;
  uint64_t dropped() const;
  void stop();

private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::string jsonl_path_;
  std::vector<CallRecord> recent_;
  std::vector<CallRecord> queue_;
  std::thread worker_;
  bool running_ = false;
  uint64_t dropped_ = 0;
#if SAE_HAVE_SQLITE
  sqlite3 *db_ = nullptr;
#endif

  void write_record(const CallRecord &record);
  void worker_loop();
};

} // namespace sae
