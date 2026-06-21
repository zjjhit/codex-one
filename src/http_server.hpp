#pragma once

#include "call_manager.hpp"
#include "config.hpp"
#include "log_store.hpp"
#include "sip_udp_engine.hpp"

#include <thread>

namespace sae {

class HttpServer {
public:
  HttpServer(ConfigStore &config_store, CallManager &call_manager, LogStore &log_store, SipUdpEngine &sip_engine);
  ~HttpServer();
  bool start(std::string &error);
  void stop();

private:
  ConfigStore &config_store_;
  CallManager &call_manager_;
  LogStore &log_store_;
  SipUdpEngine &sip_engine_;
  std::atomic<bool> running_{false};
  int fd_ = -1;
  std::thread thread_;

  void loop();
  void handle_client(int client);
  static std::string response(int code, const std::string &body, const std::string &content_type = "application/json");
  static std::map<std::string, std::string> query_params(const std::string &target);
};

} // namespace sae
