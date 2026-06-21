#pragma once

#include "call_manager.hpp"
#include "rtp_session.hpp"

#include <memory>
#include <netinet/in.h>
#include <thread>

namespace sae {

class SipUdpEngine {
public:
  SipUdpEngine(ConfigStore &config_store, CallManager &call_manager);
  ~SipUdpEngine();
  bool start(std::string &error);
  void stop();
  bool listening() const { return running_; }
  void send_bye_for_local_call(const std::string &local_call_id);
  std::string runtime_json() const;
  size_t active_dialogs() const;
  size_t rtp_ports_in_use() const;
  size_t rtp_ports_available() const;

private:
  struct Dialog {
    std::string local_call_id;
    std::string sip_call_id;
    std::string via;
    std::string from;
    std::string to;
    std::string contact;
    std::string invite_cseq_number = "1";
    int local_rtp_port = 0;
    std::string remote_rtp_ip;
    int remote_rtp_port = 0;
    sockaddr_in remote{};
  };

  ConfigStore &config_store_;
  CallManager &call_manager_;
  std::atomic<bool> running_{false};
  int fd_ = -1;
  std::thread thread_;
  mutable std::mutex mutex_;
  RtpPortPool rtp_ports_;
  std::unordered_map<std::string, Dialog> by_local_call_;
  std::unordered_map<std::string, std::string> local_by_sip_call_;
  std::unordered_map<std::string, std::shared_ptr<RtpSession>> rtp_sessions_;

  void loop();
  void handle_packet(const std::string &packet, const sockaddr_in &remote);
  void send_response(const sockaddr_in &remote, int code, const std::string &reason, const Dialog &dialog, const std::string &body = "");
  void send_raw(const sockaddr_in &remote, const std::string &message);
  void close_dialog_resources(const std::string &local_call_id);
  static std::string header(const std::string &packet, const std::string &name);
  static std::string request_uri_user(const std::string &request_line);
  static std::string cseq_number(const std::string &cseq);
  static int sdp_audio_port(const std::string &packet);
  static std::string sdp_connection_ip(const std::string &packet, const std::string &fallback);
  static std::string contact_uri(const std::string &contact);
};

} // namespace sae
