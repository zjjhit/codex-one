#pragma once

#include "config.hpp"
#include "log_store.hpp"

#include <functional>
#include <set>

namespace sae {

struct IncomingCall {
  std::string sip_call_id;
  std::string called_number;
  std::string from;
  std::string to;
  std::string via;
  std::string cseq;
  std::string remote_ip;
  int remote_port = 0;
};

struct CallDecision {
  bool answer = false;
  int reject_code = 486;
  std::string reason;
  std::string local_call_id;
  int hold_seconds = 0;
};

class CallManager {
public:
  using HangupFn = std::function<void(const std::string &)>;

  CallManager(ConfigStore &config_store, LogStore &log_store);
  void set_hangup_fn(HangupFn fn);
  CallDecision on_invite(const IncomingCall &incoming);
  void on_ack(const std::string &local_call_id);
  void on_remote_cancel(const std::string &local_call_id);
  void on_remote_hangup(const std::string &local_call_id);
  void on_local_hangup_sent(const std::string &local_call_id, bool ok, const std::string &error = "");
  MetricsSnapshot metrics() const;
  void reset_daily_counter();

private:
  struct Session {
    CallRecord record;
    TimePoint invite_received = Clock::now();
    bool closed = false;
  };

  mutable std::mutex mutex_;
  ConfigStore &config_store_;
  LogStore &log_store_;
  HangupFn hangup_fn_;
  std::unordered_map<std::string, Session> sessions_;
  uint64_t total_calls_ = 0;
  uint64_t answered_ = 0;
  uint64_t rejected_ = 0;
  uint64_t abnormal_ = 0;
  uint64_t answer_latency_sum_ms_ = 0;
  uint64_t hangup_latency_sum_ms_ = 0;
  uint64_t hangup_latency_count_ = 0;
  int daily_answer_count_ = 0;
  std::string daily_date_ = today_utc();

  void rollover_day_locked();
  void close_session_locked(const std::string &local_call_id, const std::string &status, const std::string &error);
  void schedule_hangup(const std::string &local_call_id, int seconds);
};

} // namespace sae
