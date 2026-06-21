#include "sip_udp_engine.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace sae {

SipUdpEngine::SipUdpEngine(ConfigStore &config_store, CallManager &call_manager)
    : config_store_(config_store), call_manager_(call_manager) {}

SipUdpEngine::~SipUdpEngine() {
  stop();
}

bool SipUdpEngine::start(std::string &error) {
  auto config = config_store_.current();
  if (config.sip_transport != "udp") {
    error = "current embedded engine supports udp only";
    return false;
  }
  rtp_ports_.reset(config.rtp_port_start, config.rtp_port_end);
  fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (fd_ < 0) {
    error = std::strerror(errno);
    return false;
  }
  int yes = 1;
  setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(config.sip_port));
  if (inet_pton(AF_INET, config.sip_ip.c_str(), &addr.sin_addr) != 1) {
    addr.sin_addr.s_addr = INADDR_ANY;
  }
  if (bind(fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
    error = std::strerror(errno);
    ::close(fd_);
    fd_ = -1;
    return false;
  }
  running_ = true;
  thread_ = std::thread([this] { loop(); });
  return true;
}

void SipUdpEngine::stop() {
  running_ = false;
  if (fd_ >= 0) {
    ::shutdown(fd_, SHUT_RDWR);
    ::close(fd_);
    fd_ = -1;
  }
  if (thread_.joinable()) thread_.join();
  std::vector<std::shared_ptr<RtpSession>> sessions;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &entry : rtp_sessions_) sessions.push_back(entry.second);
    rtp_sessions_.clear();
    by_local_call_.clear();
    local_by_sip_call_.clear();
  }
  for (auto &session : sessions) session->stop();
}

void SipUdpEngine::loop() {
  while (running_) {
    char buf[8192];
    sockaddr_in remote{};
    socklen_t len = sizeof(remote);
    auto n = recvfrom(fd_, buf, sizeof(buf) - 1, 0, reinterpret_cast<sockaddr *>(&remote), &len);
    if (n <= 0) continue;
    buf[n] = '\0';
    handle_packet(std::string(buf, static_cast<size_t>(n)), remote);
  }
}

void SipUdpEngine::handle_packet(const std::string &packet, const sockaddr_in &remote) {
  auto first_end = packet.find("\r\n");
  if (first_end == std::string::npos) return;
  auto first = packet.substr(0, first_end);
  auto call_id = header(packet, "Call-ID");
  if (call_id.empty()) call_id = header(packet, "i");
  if (starts_with(first, "INVITE ")) {
    Dialog dialog;
    dialog.sip_call_id = call_id;
    dialog.via = header(packet, "Via");
    dialog.from = header(packet, "From");
    dialog.to = header(packet, "To");
    dialog.contact = header(packet, "Contact");
    dialog.invite_cseq_number = cseq_number(header(packet, "CSeq"));
    dialog.remote = remote;

    char ip[INET_ADDRSTRLEN]{};
    inet_ntop(AF_INET, &remote.sin_addr, ip, sizeof(ip));
    dialog.remote_rtp_ip = sdp_connection_ip(packet, ip);
    dialog.remote_rtp_port = sdp_audio_port(packet);

    IncomingCall incoming;
    incoming.sip_call_id = call_id;
    incoming.called_number = request_uri_user(first);
    incoming.from = dialog.from;
    incoming.to = dialog.to;
    incoming.via = dialog.via;
    incoming.cseq = header(packet, "CSeq");
    incoming.remote_ip = ip;
    incoming.remote_port = ntohs(remote.sin_port);

    send_response(remote, 100, "Trying", dialog);
    auto decision = call_manager_.on_invite(incoming);
    dialog.local_call_id = decision.local_call_id;
    if (!decision.answer) {
      send_response(remote, decision.reject_code, decision.reason, dialog);
      return;
    }

    dialog.local_rtp_port = rtp_ports_.acquire();
    if (dialog.local_rtp_port == 0) {
      call_manager_.on_local_hangup_sent(dialog.local_call_id, false, "rtp port pool exhausted");
      send_response(remote, 486, "RTP Port Exhausted", dialog);
      return;
    }
    auto rtp = std::make_shared<RtpSession>();
    std::string rtp_error;
    if (!rtp->start(dialog.local_rtp_port, dialog.remote_rtp_ip, dialog.remote_rtp_port, rtp_error)) {
      rtp_ports_.release(dialog.local_rtp_port);
      call_manager_.on_local_hangup_sent(dialog.local_call_id, false, rtp_error);
      send_response(remote, 500, "RTP Start Failed", dialog);
      return;
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      by_local_call_[dialog.local_call_id] = dialog;
      local_by_sip_call_[dialog.sip_call_id] = dialog.local_call_id;
      rtp_sessions_[dialog.local_call_id] = rtp;
    }
    send_response(remote, 180, "Ringing", dialog);
    auto config = config_store_.current();
    std::ostringstream sdp;
    sdp << "v=0\r\n"
        << "o=sip-answer-engine 0 0 IN IP4 " << config.sip_ip << "\r\n"
        << "s=sip-answer-engine\r\n"
        << "c=IN IP4 " << config.sip_ip << "\r\n"
        << "t=0 0\r\n"
        << "m=audio " << dialog.local_rtp_port << " RTP/AVP 0\r\n"
        << "a=rtpmap:0 PCMU/8000\r\n"
        << "a=sendrecv\r\n";
    send_response(remote, 200, "OK", dialog, sdp.str());
    return;
  }

  std::string local_id;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = local_by_sip_call_.find(call_id);
    if (it != local_by_sip_call_.end()) local_id = it->second;
  }
  if (local_id.empty()) return;

  if (starts_with(first, "ACK ")) {
    call_manager_.on_ack(local_id);
  } else if (starts_with(first, "BYE ")) {
    Dialog dialog;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      dialog = by_local_call_[local_id];
    }
    send_response(remote, 200, "OK", dialog);
    close_dialog_resources(local_id);
    call_manager_.on_remote_hangup(local_id);
  } else if (starts_with(first, "CANCEL ")) {
    Dialog dialog;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      dialog = by_local_call_[local_id];
    }
    send_response(remote, 200, "OK", dialog);
    close_dialog_resources(local_id);
    call_manager_.on_remote_cancel(local_id);
  }
}

void SipUdpEngine::send_bye_for_local_call(const std::string &local_call_id) {
  Dialog dialog;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = by_local_call_.find(local_call_id);
    if (it == by_local_call_.end()) return;
    dialog = it->second;
  }
  auto uri = contact_uri(dialog.contact);
  if (uri.empty()) uri = "sip:peer";
  std::ostringstream msg;
  msg << "BYE " << uri << " SIP/2.0\r\n"
      << "Via: " << dialog.via << "\r\n"
      << "From: " << dialog.to << ";tag=sae\r\n"
      << "To: " << dialog.from << "\r\n"
      << "Call-ID: " << dialog.sip_call_id << "\r\n"
      << "CSeq: 2 BYE\r\n"
      << "Max-Forwards: 70\r\n"
      << "Content-Length: 0\r\n\r\n";
  send_raw(dialog.remote, msg.str());
  close_dialog_resources(local_call_id);
  call_manager_.on_local_hangup_sent(local_call_id, true);
}

void SipUdpEngine::close_dialog_resources(const std::string &local_call_id) {
  std::shared_ptr<RtpSession> rtp;
  int port = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto dialog_it = by_local_call_.find(local_call_id);
    if (dialog_it != by_local_call_.end()) {
      port = dialog_it->second.local_rtp_port;
      local_by_sip_call_.erase(dialog_it->second.sip_call_id);
      by_local_call_.erase(dialog_it);
    }
    auto rtp_it = rtp_sessions_.find(local_call_id);
    if (rtp_it != rtp_sessions_.end()) {
      rtp = rtp_it->second;
      rtp_sessions_.erase(rtp_it);
    }
  }
  if (rtp) rtp->stop();
  rtp_ports_.release(port);
}

void SipUdpEngine::send_response(const sockaddr_in &remote, int code, const std::string &reason, const Dialog &dialog, const std::string &body) {
  std::ostringstream msg;
  msg << "SIP/2.0 " << code << " " << reason << "\r\n"
      << "Via: " << dialog.via << "\r\n"
      << "From: " << dialog.from << "\r\n"
      << "To: " << dialog.to;
  if (code >= 180 && dialog.to.find("tag=") == std::string::npos) msg << ";tag=sae";
  msg << "\r\n"
      << "Call-ID: " << dialog.sip_call_id << "\r\n"
      << "CSeq: " << dialog.invite_cseq_number << " INVITE\r\n";
  if (code == 200 && !body.empty()) {
    auto config = config_store_.current();
    msg << "Contact: <sip:answer@" << config.sip_ip << ":" << config.sip_port << ">\r\n"
        << "Content-Type: application/sdp\r\n";
  }
  msg << "Content-Length: " << body.size() << "\r\n\r\n" << body;
  send_raw(remote, msg.str());
}

void SipUdpEngine::send_raw(const sockaddr_in &remote, const std::string &message) {
  if (fd_ < 0) return;
  sendto(fd_, message.data(), message.size(), 0, reinterpret_cast<const sockaddr *>(&remote), sizeof(remote));
}

std::string SipUdpEngine::runtime_json() const {
  auto config = config_store_.current();
  std::ostringstream out;
  out << "{"
      << "\"sip_listening\":" << (running_ ? "true" : "false") << ","
      << "\"active_dialogs\":" << active_dialogs() << ","
      << "\"rtp_ports_in_use\":" << rtp_ports_in_use() << ","
      << "\"rtp_ports_available\":" << rtp_ports_available() << ","
      << "\"draining\":" << (call_manager_.draining() ? "true" : "false") << ","
      << "\"sip_ip\":\"" << json_escape(config.sip_ip) << "\","
      << "\"sip_port\":" << config.sip_port
      << "}";
  return out.str();
}

size_t SipUdpEngine::active_dialogs() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return by_local_call_.size();
}

size_t SipUdpEngine::rtp_ports_in_use() const {
  return rtp_ports_.in_use();
}

size_t SipUdpEngine::rtp_ports_available() const {
  return rtp_ports_.available();
}

std::string SipUdpEngine::header(const std::string &packet, const std::string &name) {
  std::istringstream in(packet);
  std::string line;
  auto lowered = name;
  std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) { return std::tolower(c); });
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    auto colon = line.find(':');
    if (colon == std::string::npos) continue;
    auto key = line.substr(0, colon);
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return std::tolower(c); });
    if (key == lowered) return trim(line.substr(colon + 1));
  }
  return "";
}

std::string SipUdpEngine::request_uri_user(const std::string &request_line) {
  auto first_space = request_line.find(' ');
  auto second_space = request_line.find(' ', first_space + 1);
  if (first_space == std::string::npos || second_space == std::string::npos) return "";
  auto uri = request_line.substr(first_space + 1, second_space - first_space - 1);
  auto sip = uri.find("sip:");
  if (sip != std::string::npos) uri = uri.substr(sip + 4);
  auto at = uri.find('@');
  if (at != std::string::npos) uri = uri.substr(0, at);
  auto semi = uri.find(';');
  if (semi != std::string::npos) uri = uri.substr(0, semi);
  return uri;
}

std::string SipUdpEngine::cseq_number(const std::string &cseq) {
  auto pos = cseq.find(' ');
  return pos == std::string::npos ? cseq : cseq.substr(0, pos);
}

int SipUdpEngine::sdp_audio_port(const std::string &packet) {
  std::istringstream in(packet);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (starts_with(line, "m=audio ")) {
      std::istringstream fields(line.substr(8));
      int port = 0;
      fields >> port;
      return port;
    }
  }
  return 6000;
}

std::string SipUdpEngine::sdp_connection_ip(const std::string &packet, const std::string &fallback) {
  std::istringstream in(packet);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (starts_with(line, "c=IN IP4 ")) return trim(line.substr(9));
  }
  return fallback;
}

std::string SipUdpEngine::contact_uri(const std::string &contact) {
  auto start = contact.find('<');
  auto end = contact.find('>');
  if (start != std::string::npos && end != std::string::npos && end > start + 1) {
    return contact.substr(start + 1, end - start - 1);
  }
  auto semi = contact.find(';');
  return semi == std::string::npos ? trim(contact) : trim(contact.substr(0, semi));
}

} // namespace sae
