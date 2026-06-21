#include "rtp_session.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

namespace sae {

void RtpPortPool::reset(int start, int end) {
  std::lock_guard<std::mutex> lock(mutex_);
  start_ = start;
  end_ = end;
  free_.clear();
  used_.clear();
  for (int p = end_; p >= start_; --p) free_.push_back(p);
}

int RtpPortPool::acquire() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (free_.empty()) return 0;
  int port = free_.back();
  free_.pop_back();
  used_.push_back(port);
  return port;
}

void RtpPortPool::release(int port) {
  if (port <= 0) return;
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = std::find(used_.begin(), used_.end(), port);
  if (it == used_.end()) return;
  used_.erase(it);
  free_.push_back(port);
}

size_t RtpPortPool::in_use() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return used_.size();
}

size_t RtpPortPool::available() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return free_.size();
}

RtpSession::~RtpSession() {
  stop();
}

bool RtpSession::start(int local_port, const std::string &remote_ip, int remote_port, std::string &error) {
  local_port_ = local_port;
  fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (fd_ < 0) {
    error = std::strerror(errno);
    return false;
  }
  sockaddr_in local{};
  local.sin_family = AF_INET;
  local.sin_port = htons(static_cast<uint16_t>(local_port));
  local.sin_addr.s_addr = INADDR_ANY;
  if (bind(fd_, reinterpret_cast<sockaddr *>(&local), sizeof(local)) < 0) {
    error = std::strerror(errno);
    ::close(fd_);
    fd_ = -1;
    return false;
  }
  remote_.sin_family = AF_INET;
  remote_.sin_port = htons(static_cast<uint16_t>(remote_port > 0 ? remote_port : 6000));
  if (inet_pton(AF_INET, remote_ip.c_str(), &remote_.sin_addr) != 1) {
    remote_.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  }
  running_ = true;
  worker_ = std::thread([this] { loop(); });
  return true;
}

void RtpSession::stop() {
  running_ = false;
  if (worker_.joinable()) worker_.join();
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

void RtpSession::loop() {
  uint16_t sequence = 0;
  uint32_t timestamp = 0;
  const uint32_t ssrc = 0x53414532;
  while (running_) {
    unsigned char packet[172]{};
    packet[0] = 0x80;
    packet[1] = 0x00;
    packet[2] = static_cast<unsigned char>(sequence >> 8);
    packet[3] = static_cast<unsigned char>(sequence & 0xff);
    packet[4] = static_cast<unsigned char>(timestamp >> 24);
    packet[5] = static_cast<unsigned char>((timestamp >> 16) & 0xff);
    packet[6] = static_cast<unsigned char>((timestamp >> 8) & 0xff);
    packet[7] = static_cast<unsigned char>(timestamp & 0xff);
    packet[8] = static_cast<unsigned char>(ssrc >> 24);
    packet[9] = static_cast<unsigned char>((ssrc >> 16) & 0xff);
    packet[10] = static_cast<unsigned char>((ssrc >> 8) & 0xff);
    packet[11] = static_cast<unsigned char>(ssrc & 0xff);
    std::fill(packet + 12, packet + sizeof(packet), 0xff);
    sendto(fd_, packet, sizeof(packet), 0, reinterpret_cast<const sockaddr *>(&remote_), sizeof(remote_));
    ++sequence;
    timestamp += 160;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
}

} // namespace sae
