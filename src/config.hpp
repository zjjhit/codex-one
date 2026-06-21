#pragma once

#include "common.hpp"

namespace sae {

struct Config {
  std::string sip_ip = "0.0.0.0";
  int sip_port = 5060;
  std::string sip_transport = "udp";
  std::string called_prefix = "888";
  int daily_answer_limit = 100000;
  int answer_rate_percent = 100;
  int min_hold_seconds = 1;
  int max_hold_seconds = 15;
  int concurrency_limit = 1000;
  int log_retention_days = 30;
  int rtp_port_start = 40000;
  int rtp_port_end = 42000;
  bool enable_registration = false;
  std::string registrar_host;
  int registrar_port = 5060;
  std::string registration_user;
  std::string registration_password;
  std::string http_ip = "0.0.0.0";
  int http_port = 8080;
  std::string log_dir = "logs";

  std::string validate() const;
  std::string to_json() const;
};

class ConfigStore {
public:
  explicit ConfigStore(std::string path);
  Config load();
  void save(const Config &config);
  Config current() const;
  bool update_from_json(const std::string &json_body, std::string &error);
  bool reload(std::string &error);

private:
  std::string path_;
  mutable std::mutex mutex_;
  Config config_;

  static void apply(Config &config, const std::string &key, const std::string &value);
};

} // namespace sae
