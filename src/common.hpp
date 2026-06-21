#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace sae {

using Clock = std::chrono::system_clock;
using TimePoint = std::chrono::time_point<Clock>;

inline std::string trim(std::string value) {
  auto not_space = [](unsigned char c) { return !std::isspace(c); };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
  value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
  return value;
}

inline std::string unquote(std::string value) {
  value = trim(value);
  if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                            (value.front() == '\'' && value.back() == '\''))) {
    return value.substr(1, value.size() - 2);
  }
  return value;
}

inline bool starts_with(const std::string &value, const std::string &prefix) {
  return value.rfind(prefix, 0) == 0;
}

inline std::string json_escape(const std::string &value) {
  std::ostringstream out;
  for (char c : value) {
    switch (c) {
    case '\\': out << "\\\\"; break;
    case '"': out << "\\\""; break;
    case '\n': out << "\\n"; break;
    case '\r': out << "\\r"; break;
    case '\t': out << "\\t"; break;
    default: out << c; break;
    }
  }
  return out.str();
}

inline std::string iso_time(TimePoint tp = Clock::now()) {
  auto t = Clock::to_time_t(tp);
  std::tm tm{};
  gmtime_r(&t, &tm);
  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}

inline std::string today_utc() {
  auto t = Clock::to_time_t(Clock::now());
  std::tm tm{};
  gmtime_r(&t, &tm);
  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%d");
  return out.str();
}

inline std::string random_id() {
  static std::atomic<uint64_t> seq{0};
  static thread_local std::mt19937_64 rng{std::random_device{}()};
  std::ostringstream out;
  out << std::hex << Clock::now().time_since_epoch().count() << "-" << rng() << "-" << seq++;
  return out.str();
}

} // namespace sae
