#pragma once

#include "common.hpp"
#include "config.hpp"

#include <netinet/in.h>
#include <thread>

namespace sae {

class RtpPortPool {
public:
  void reset(int start, int end);
  int acquire();
  void release(int port);
  size_t in_use() const;
  size_t available() const;

private:
  mutable std::mutex mutex_;
  int start_ = 0;
  int end_ = -1;
  std::vector<int> free_;
  std::vector<int> used_;
};

class RtpSession {
public:
  RtpSession() = default;
  ~RtpSession();
  RtpSession(const RtpSession &) = delete;
  RtpSession &operator=(const RtpSession &) = delete;

  bool start(int local_port, const std::string &remote_ip, int remote_port, std::string &error);
  void stop();
  int local_port() const { return local_port_; }

private:
  std::atomic<bool> running_{false};
  int fd_ = -1;
  int local_port_ = 0;
  sockaddr_in remote_{};
  std::thread worker_;

  void loop();
};

} // namespace sae
