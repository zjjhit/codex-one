#include "config.hpp"

#include <fstream>
#include <regex>
#include <stdexcept>

namespace sae {

static int parse_int(const std::string &value) {
  return std::stoi(unquote(value));
}

static bool parse_bool(const std::string &value) {
  auto v = unquote(value);
  std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return std::tolower(c); });
  return v == "true" || v == "1" || v == "yes";
}

std::string Config::validate() const {
  if (sip_port < 1 || sip_port > 65535) return "sip_port must be between 1 and 65535";
  if (http_port < 1 || http_port > 65535) return "http_port must be between 1 and 65535";
  if (registrar_port < 1 || registrar_port > 65535) return "registrar_port must be between 1 and 65535";
  if (sip_transport != "udp" && sip_transport != "tcp") return "sip_transport must be udp or tcp";
  if (daily_answer_limit < 0 || daily_answer_limit > 100000) return "daily_answer_limit must be 0-100000";
  if (answer_rate_percent < 0 || answer_rate_percent > 100) return "answer_rate_percent must be 0-100";
  if (min_hold_seconds < 1 || max_hold_seconds < 1) return "hold seconds must be positive";
  if (min_hold_seconds > max_hold_seconds) return "min_hold_seconds cannot exceed max_hold_seconds";
  if (concurrency_limit < 1) return "concurrency_limit must be positive";
  if (rtp_port_start < 1 || rtp_port_end > 65535 || rtp_port_start > rtp_port_end) return "invalid RTP port range";
  if ((rtp_port_end - rtp_port_start + 1) < concurrency_limit) return "RTP port range must cover concurrency_limit";
  if (called_prefix.empty()) return "called_prefix cannot be empty";
  return "";
}

std::string Config::to_json() const {
  std::ostringstream out;
  out << "{"
      << "\"sip_ip\":\"" << json_escape(sip_ip) << "\","
      << "\"sip_port\":" << sip_port << ","
      << "\"sip_transport\":\"" << json_escape(sip_transport) << "\","
      << "\"called_prefix\":\"" << json_escape(called_prefix) << "\","
      << "\"daily_answer_limit\":" << daily_answer_limit << ","
      << "\"answer_rate_percent\":" << answer_rate_percent << ","
      << "\"min_hold_seconds\":" << min_hold_seconds << ","
      << "\"max_hold_seconds\":" << max_hold_seconds << ","
      << "\"concurrency_limit\":" << concurrency_limit << ","
      << "\"log_retention_days\":" << log_retention_days << ","
      << "\"rtp_port_start\":" << rtp_port_start << ","
      << "\"rtp_port_end\":" << rtp_port_end << ","
      << "\"enable_registration\":" << (enable_registration ? "true" : "false") << ","
      << "\"registrar_host\":\"" << json_escape(registrar_host) << "\","
      << "\"registrar_port\":" << registrar_port << ","
      << "\"registration_user\":\"" << json_escape(registration_user) << "\","
      << "\"http_ip\":\"" << json_escape(http_ip) << "\","
      << "\"http_port\":" << http_port << ","
      << "\"log_dir\":\"" << json_escape(log_dir) << "\""
      << "}";
  return out.str();
}

ConfigStore::ConfigStore(std::string path) : path_(std::move(path)) {}

Config ConfigStore::load() {
  std::lock_guard<std::mutex> lock(mutex_);
  Config next;
  std::ifstream in(path_);
  std::string line;
  while (std::getline(in, line)) {
    auto hash = line.find('#');
    if (hash != std::string::npos) line = line.substr(0, hash);
    auto colon = line.find(':');
    if (colon == std::string::npos) continue;
    apply(next, trim(line.substr(0, colon)), trim(line.substr(colon + 1)));
  }
  auto error = next.validate();
  if (!error.empty()) throw std::runtime_error(error);
  config_ = next;
  return config_;
}

void ConfigStore::save(const Config &config) {
  auto error = config.validate();
  if (!error.empty()) throw std::runtime_error(error);
  std::ofstream out(path_, std::ios::trunc);
  out << "sip_ip: " << config.sip_ip << "\n"
      << "sip_port: " << config.sip_port << "\n"
      << "sip_transport: " << config.sip_transport << "\n"
      << "called_prefix: \"" << config.called_prefix << "\"\n"
      << "daily_answer_limit: " << config.daily_answer_limit << "\n"
      << "answer_rate_percent: " << config.answer_rate_percent << "\n"
      << "min_hold_seconds: " << config.min_hold_seconds << "\n"
      << "max_hold_seconds: " << config.max_hold_seconds << "\n"
      << "concurrency_limit: " << config.concurrency_limit << "\n"
      << "log_retention_days: " << config.log_retention_days << "\n"
      << "rtp_port_start: " << config.rtp_port_start << "\n"
      << "rtp_port_end: " << config.rtp_port_end << "\n"
      << "enable_registration: " << (config.enable_registration ? "true" : "false") << "\n"
      << "registrar_host: \"" << config.registrar_host << "\"\n"
      << "registrar_port: " << config.registrar_port << "\n"
      << "registration_user: \"" << config.registration_user << "\"\n"
      << "registration_password: \"" << config.registration_password << "\"\n"
      << "http_ip: " << config.http_ip << "\n"
      << "http_port: " << config.http_port << "\n"
      << "log_dir: " << config.log_dir << "\n";
}

Config ConfigStore::current() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return config_;
}

bool ConfigStore::update_from_json(const std::string &json_body, std::string &error) {
  std::lock_guard<std::mutex> lock(mutex_);
  Config next = config_;
  std::regex pair("\"([A-Za-z0-9_]+)\"\\s*:\\s*(\"[^\"]*\"|true|false|-?[0-9]+)");
  for (auto it = std::sregex_iterator(json_body.begin(), json_body.end(), pair); it != std::sregex_iterator(); ++it) {
    apply(next, (*it)[1].str(), (*it)[2].str());
  }
  error = next.validate();
  if (!error.empty()) return false;
  try {
    save(next);
  } catch (const std::exception &ex) {
    error = ex.what();
    return false;
  }
  config_ = next;
  return true;
}

bool ConfigStore::reload(std::string &error) {
  try {
    load();
    return true;
  } catch (const std::exception &ex) {
    error = ex.what();
    return false;
  }
}

void ConfigStore::apply(Config &c, const std::string &key, const std::string &value) {
  if (key == "sip_ip") c.sip_ip = unquote(value);
  else if (key == "sip_port") c.sip_port = parse_int(value);
  else if (key == "sip_transport") c.sip_transport = unquote(value);
  else if (key == "called_prefix") c.called_prefix = unquote(value);
  else if (key == "daily_answer_limit") c.daily_answer_limit = parse_int(value);
  else if (key == "answer_rate_percent") c.answer_rate_percent = parse_int(value);
  else if (key == "min_hold_seconds") c.min_hold_seconds = parse_int(value);
  else if (key == "max_hold_seconds") c.max_hold_seconds = parse_int(value);
  else if (key == "concurrency_limit") c.concurrency_limit = parse_int(value);
  else if (key == "log_retention_days") c.log_retention_days = parse_int(value);
  else if (key == "rtp_port_start") c.rtp_port_start = parse_int(value);
  else if (key == "rtp_port_end") c.rtp_port_end = parse_int(value);
  else if (key == "enable_registration") c.enable_registration = parse_bool(value);
  else if (key == "registrar_host") c.registrar_host = unquote(value);
  else if (key == "registrar_port") c.registrar_port = parse_int(value);
  else if (key == "registration_user") c.registration_user = unquote(value);
  else if (key == "registration_password") c.registration_password = unquote(value);
  else if (key == "http_ip") c.http_ip = unquote(value);
  else if (key == "http_port") c.http_port = parse_int(value);
  else if (key == "log_dir") c.log_dir = unquote(value);
}

} // namespace sae
