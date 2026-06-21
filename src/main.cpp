#include "call_manager.hpp"
#include "config.hpp"
#include "http_server.hpp"
#include "log_store.hpp"
#include "sip_udp_engine.hpp"

#include <csignal>
#include <iostream>
#include <thread>

namespace {
std::atomic<bool> keep_running{true};

void handle_signal(int) {
  keep_running = false;
}
} // namespace

int main(int argc, char **argv) {
  std::string config_path = "config.yaml";
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--config" && i + 1 < argc) config_path = argv[++i];
  }

  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);

  try {
    sae::ConfigStore config_store(config_path);
    auto config = config_store.load();
    sae::LogStore log_store;
    log_store.open(config);
    sae::CallManager call_manager(config_store, log_store);
    sae::SipUdpEngine sip_engine(config_store, call_manager);
    call_manager.set_hangup_fn([&sip_engine](const std::string &local_call_id) {
      sip_engine.send_bye_for_local_call(local_call_id);
    });

    std::string error;
    if (!sip_engine.start(error)) {
      std::cerr << "failed to start SIP engine: " << error << "\n";
      return 2;
    }
    sae::HttpServer http_server(config_store, call_manager, log_store, sip_engine);
    if (!http_server.start(error)) {
      std::cerr << "failed to start HTTP server: " << error << "\n";
      return 3;
    }

    std::cout << "sip-answer-engine started: sip=" << config.sip_ip << ":" << config.sip_port
              << " http=" << config.http_ip << ":" << config.http_port << "\n";
    while (keep_running) {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    http_server.stop();
    sip_engine.stop();
    std::cout << "sip-answer-engine stopped\n";
    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "fatal: " << ex.what() << "\n";
    return 1;
  }
}
