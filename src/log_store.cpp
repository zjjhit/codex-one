#include "log_store.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace sae {

static std::string nullable_time(TimePoint tp) {
  return tp.time_since_epoch().count() == 0 ? "" : iso_time(tp);
}

std::string CallRecord::to_json() const {
  std::ostringstream out;
  out << "{"
      << "\"call_id\":\"" << json_escape(call_id) << "\","
      << "\"sip_call_id\":\"" << json_escape(sip_call_id) << "\","
      << "\"called_number\":\"" << json_escape(called_number) << "\","
      << "\"line_id\":\"" << json_escape(line_id) << "\","
      << "\"call_time\":\"" << nullable_time(call_time) << "\","
      << "\"answer_time\":\"" << nullable_time(answer_time) << "\","
      << "\"planned_hold_seconds\":" << planned_hold_seconds << ","
      << "\"actual_hold_ms\":" << actual_hold_ms << ","
      << "\"hangup_time\":\"" << nullable_time(hangup_time) << "\","
      << "\"status\":\"" << json_escape(status) << "\","
      << "\"daily_answer_count\":" << daily_answer_count << ","
      << "\"answer_rate_percent\":" << answer_rate_percent << ","
      << "\"error\":\"" << json_escape(error) << "\","
      << "\"sip_ip\":\"" << json_escape(sip_ip) << "\","
      << "\"sip_port\":" << sip_port << ","
      << "\"called_prefix\":\"" << json_escape(called_prefix) << "\""
      << "}";
  return out.str();
}

std::string MetricsSnapshot::to_json() const {
  std::ostringstream out;
  out << "{"
      << "\"total_calls\":" << total_calls << ","
      << "\"answered\":" << answered << ","
      << "\"rejected\":" << rejected << ","
      << "\"abnormal\":" << abnormal << ","
      << "\"current_concurrent\":" << current_concurrent << ","
      << "\"peak_concurrent\":" << peak_concurrent << ","
      << "\"rejected_prefix_mismatch\":" << rejected_prefix_mismatch << ","
      << "\"rejected_probability\":" << rejected_probability << ","
      << "\"rejected_daily_limit\":" << rejected_daily_limit << ","
      << "\"rejected_concurrency_limit\":" << rejected_concurrency_limit << ","
      << "\"log_queue_depth\":" << log_queue_depth << ","
      << "\"log_dropped\":" << log_dropped << ","
      << "\"timer_pending\":" << timer_pending << ","
      << "\"rtp_ports_in_use\":" << rtp_ports_in_use << ","
      << "\"rtp_ports_available\":" << rtp_ports_available << ","
      << "\"avg_answer_ms\":" << avg_answer_ms << ","
      << "\"avg_hangup_ms\":" << avg_hangup_ms << ","
      << "\"daily_answer_count\":" << daily_answer_count << ","
      << "\"daily_date\":\"" << json_escape(daily_date) << "\""
      << "}";
  return out.str();
}

LogStore::LogStore() = default;

LogStore::~LogStore() {
  stop();
#if SAE_HAVE_SQLITE
  if (db_) sqlite3_close(db_);
#endif
}

void LogStore::open(const Config &config) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::filesystem::create_directories(config.log_dir);
  jsonl_path_ = config.log_dir + "/calls-" + today_utc() + ".jsonl";
  if (!running_) {
    running_ = true;
    worker_ = std::thread([this] { worker_loop(); });
  }
#if SAE_HAVE_SQLITE
  if (db_) sqlite3_close(db_);
  db_ = nullptr;
  auto db_path = config.log_dir + "/calls.db";
  if (sqlite3_open(db_path.c_str(), &db_) == SQLITE_OK) {
    const char *sql =
        "CREATE TABLE IF NOT EXISTS calls ("
        "call_id TEXT PRIMARY KEY, sip_call_id TEXT, called_number TEXT, line_id TEXT,"
        "call_time TEXT, answer_time TEXT, planned_hold_seconds INTEGER, actual_hold_ms INTEGER,"
        "hangup_time TEXT, status TEXT, daily_answer_count INTEGER, answer_rate_percent INTEGER,"
        "error TEXT, sip_ip TEXT, sip_port INTEGER, called_prefix TEXT);";
    char *err = nullptr;
    sqlite3_exec(db_, sql, nullptr, nullptr, &err);
    if (err) sqlite3_free(err);
  }
#endif
}

void LogStore::append(const CallRecord &record) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    recent_.push_back(record);
    if (recent_.size() > 1000) recent_.erase(recent_.begin(), recent_.begin() + 100);
    if (queue_.size() >= 10000) {
      dropped_++;
      return;
    }
    queue_.push_back(record);
  }
  cv_.notify_one();
}

void LogStore::write_record(const CallRecord &record) {
  std::ofstream out(jsonl_path_, std::ios::app);
  out << record.to_json() << "\n";
#if SAE_HAVE_SQLITE
  if (db_) {
    const char *sql = "INSERT OR REPLACE INTO calls VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
      auto bind_text = [&](int idx, const std::string &v) { sqlite3_bind_text(stmt, idx, v.c_str(), -1, SQLITE_TRANSIENT); };
      bind_text(1, record.call_id);
      bind_text(2, record.sip_call_id);
      bind_text(3, record.called_number);
      bind_text(4, record.line_id);
      bind_text(5, nullable_time(record.call_time));
      bind_text(6, nullable_time(record.answer_time));
      sqlite3_bind_int(stmt, 7, record.planned_hold_seconds);
      sqlite3_bind_int(stmt, 8, record.actual_hold_ms);
      bind_text(9, nullable_time(record.hangup_time));
      bind_text(10, record.status);
      sqlite3_bind_int(stmt, 11, record.daily_answer_count);
      sqlite3_bind_int(stmt, 12, record.answer_rate_percent);
      bind_text(13, record.error);
      bind_text(14, record.sip_ip);
      sqlite3_bind_int(stmt, 15, record.sip_port);
      bind_text(16, record.called_prefix);
      sqlite3_step(stmt);
    }
    sqlite3_finalize(stmt);
  }
#endif
}

void LogStore::worker_loop() {
  std::unique_lock<std::mutex> lock(mutex_);
  while (running_ || !queue_.empty()) {
    cv_.wait(lock, [this] { return !running_ || !queue_.empty(); });
    std::vector<CallRecord> batch;
    batch.swap(queue_);
    lock.unlock();
    for (const auto &record : batch) write_record(record);
    lock.lock();
  }
}

std::string LogStore::query_json(size_t limit) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::ostringstream out;
  out << "{\"items\":[";
  size_t count = 0;
  for (auto it = recent_.rbegin(); it != recent_.rend() && count < limit; ++it, ++count) {
    if (count) out << ",";
    out << it->to_json();
  }
  out << "]}";
  return out.str();
}

size_t LogStore::queue_depth() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return queue_.size();
}

uint64_t LogStore::dropped() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return dropped_;
}

void LogStore::stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_) return;
    running_ = false;
  }
  cv_.notify_all();
  if (worker_.joinable()) worker_.join();
}

} // namespace sae
