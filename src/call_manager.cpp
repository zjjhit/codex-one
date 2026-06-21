#include "call_manager.hpp"

#include <iostream>

namespace sae {

CallManager::CallManager(ConfigStore &config_store, LogStore &log_store)
    : config_store_(config_store), log_store_(log_store) {}

CallManager::~CallManager() {
  scheduler_.stop();
}

void CallManager::set_hangup_fn(HangupFn fn) {
  std::lock_guard<std::mutex> lock(mutex_);
  hangup_fn_ = std::move(fn);
}

CallDecision CallManager::reject_locked(const IncomingCall &incoming, const Config &config, const std::string &status, int code, const std::string &reason) {
  rejected_++;
  if (status == "rejected_prefix_mismatch") rejected_prefix_mismatch_++;
  else if (status == "rejected_probability") rejected_probability_++;
  else if (status == "rejected_daily_limit") rejected_daily_limit_++;
  else if (status == "abnormal_terminated") rejected_concurrency_limit_++;

  CallRecord record;
  record.call_id = random_id();
  record.sip_call_id = incoming.sip_call_id;
  record.called_number = incoming.called_number;
  record.line_id = incoming.called_number;
  record.status = status;
  record.call_time = Clock::now();
  record.hangup_time = Clock::now();
  record.daily_answer_count = daily_answer_count_;
  record.answer_rate_percent = config.answer_rate_percent;
  record.error = reason;
  record.sip_ip = config.sip_ip;
  record.sip_port = config.sip_port;
  record.called_prefix = config.called_prefix;
  log_store_.append(record);
  return CallDecision{false, code, reason, record.call_id, 0};
}

CallDecision CallManager::on_invite(const IncomingCall &incoming) {
  Config config = config_store_.current();
  std::lock_guard<std::mutex> lock(mutex_);
  rollover_day_locked();
  total_calls_++;

  if (draining_) {
    return reject_locked(incoming, config, "abnormal_terminated", 486, "service is draining");
  }
  if (!starts_with(incoming.called_number, config.called_prefix)) {
    return reject_locked(incoming, config, "rejected_prefix_mismatch", 404, "called prefix mismatch");
  }
  if (sessions_.size() >= static_cast<size_t>(config.concurrency_limit)) {
    return reject_locked(incoming, config, "abnormal_terminated", 486, "concurrency limit reached");
  }
  if (daily_answer_count_ >= config.daily_answer_limit) {
    return reject_locked(incoming, config, "rejected_daily_limit", 486, "daily answer limit reached");
  }
  static thread_local std::mt19937 rng{std::random_device{}()};
  std::uniform_int_distribution<int> pct(1, 100);
  if (pct(rng) > config.answer_rate_percent) {
    return reject_locked(incoming, config, "rejected_probability", 486, "answer probability miss");
  }

  std::uniform_int_distribution<int> hold(config.min_hold_seconds, config.max_hold_seconds);
  Session session;
  session.record.call_id = random_id();
  session.record.sip_call_id = incoming.sip_call_id;
  session.record.called_number = incoming.called_number;
  session.record.line_id = incoming.called_number;
  session.record.status = "answered";
  session.record.call_time = Clock::now();
  session.record.answer_time = Clock::now();
  session.record.planned_hold_seconds = hold(rng);
  session.record.daily_answer_count = ++daily_answer_count_;
  session.record.answer_rate_percent = config.answer_rate_percent;
  session.record.sip_ip = config.sip_ip;
  session.record.sip_port = config.sip_port;
  session.record.called_prefix = config.called_prefix;
  auto answer_ms = std::chrono::duration_cast<std::chrono::milliseconds>(session.record.answer_time - session.invite_received).count();
  answer_latency_sum_ms_ += static_cast<uint64_t>(std::max<int64_t>(0, answer_ms));
  answered_++;
  auto local_call_id = session.record.call_id;
  auto hold_seconds = session.record.planned_hold_seconds;
  sessions_.emplace(local_call_id, std::move(session));
  peak_concurrent_ = std::max<uint64_t>(peak_concurrent_, sessions_.size());
  schedule_hangup(local_call_id, hold_seconds);
  return CallDecision{true, 200, "OK", local_call_id, hold_seconds};
}

void CallManager::on_ack(const std::string &) {}

void CallManager::on_remote_cancel(const std::string &local_call_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  close_session_locked(local_call_id, "remote_cancelled", "");
}

void CallManager::on_remote_hangup(const std::string &local_call_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  close_session_locked(local_call_id, "remote_hangup", "");
}

void CallManager::on_local_hangup_sent(const std::string &local_call_id, bool ok, const std::string &error) {
  std::lock_guard<std::mutex> lock(mutex_);
  close_session_locked(local_call_id, ok ? "answered" : "hangup_failed", error);
}

MetricsSnapshot CallManager::metrics() const {
  std::lock_guard<std::mutex> lock(mutex_);
  MetricsSnapshot s;
  s.total_calls = total_calls_;
  s.answered = answered_;
  s.rejected = rejected_;
  s.abnormal = abnormal_;
  s.current_concurrent = sessions_.size();
  s.peak_concurrent = peak_concurrent_;
  s.rejected_prefix_mismatch = rejected_prefix_mismatch_;
  s.rejected_probability = rejected_probability_;
  s.rejected_daily_limit = rejected_daily_limit_;
  s.rejected_concurrency_limit = rejected_concurrency_limit_;
  s.log_queue_depth = log_store_.queue_depth();
  s.log_dropped = log_store_.dropped();
  s.timer_pending = scheduler_.pending();
  s.avg_answer_ms = answered_ ? static_cast<double>(answer_latency_sum_ms_) / answered_ : 0;
  s.avg_hangup_ms = hangup_latency_count_ ? static_cast<double>(hangup_latency_sum_ms_) / hangup_latency_count_ : 0;
  s.daily_answer_count = daily_answer_count_;
  s.daily_date = daily_date_;
  return s;
}

void CallManager::reset_daily_counter() {
  std::lock_guard<std::mutex> lock(mutex_);
  daily_answer_count_ = 0;
  daily_date_ = today_utc();
}

void CallManager::set_draining(bool draining) {
  std::lock_guard<std::mutex> lock(mutex_);
  draining_ = draining;
}

bool CallManager::draining() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return draining_;
}

void CallManager::rollover_day_locked() {
  auto today = today_utc();
  if (today != daily_date_) {
    daily_date_ = today;
    daily_answer_count_ = 0;
  }
}

void CallManager::close_session_locked(const std::string &local_call_id, const std::string &status, const std::string &error) {
  auto it = sessions_.find(local_call_id);
  if (it == sessions_.end() || it->second.closed) return;
  auto &session = it->second;
  session.closed = true;
  session.record.status = status;
  session.record.error = error;
  session.record.hangup_time = Clock::now();
  if (status != "answered" && status != "remote_hangup" && status != "remote_cancelled") abnormal_++;
  if (session.record.answer_time.time_since_epoch().count()) {
    auto actual = std::chrono::duration_cast<std::chrono::milliseconds>(session.record.hangup_time - session.record.answer_time).count();
    session.record.actual_hold_ms = static_cast<int>(std::max<int64_t>(0, actual));
    auto planned = session.record.answer_time + std::chrono::seconds(session.record.planned_hold_seconds);
    auto hangup_latency = std::chrono::duration_cast<std::chrono::milliseconds>(session.record.hangup_time - planned).count();
    if (hangup_latency >= 0) {
      hangup_latency_sum_ms_ += static_cast<uint64_t>(hangup_latency);
      hangup_latency_count_++;
    }
  }
  log_store_.append(session.record);
  sessions_.erase(it);
}

void CallManager::schedule_hangup(const std::string &local_call_id, int seconds) {
  scheduler_.schedule(Clock::now() + std::chrono::seconds(seconds), [this, local_call_id] {
    HangupFn fn;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      fn = hangup_fn_;
    }
    if (fn) fn(local_call_id);
  });
}

} // namespace sae
