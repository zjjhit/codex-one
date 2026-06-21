#include "http_server.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace sae {

HttpServer::HttpServer(ConfigStore &config_store, CallManager &call_manager, LogStore &log_store, SipUdpEngine &sip_engine)
    : config_store_(config_store), call_manager_(call_manager), log_store_(log_store), sip_engine_(sip_engine) {}

HttpServer::~HttpServer() {
  stop();
}

bool HttpServer::start(std::string &error) {
  auto config = config_store_.current();
  fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd_ < 0) {
    error = std::strerror(errno);
    return false;
  }
  int yes = 1;
  setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(config.http_port));
  if (inet_pton(AF_INET, config.http_ip.c_str(), &addr.sin_addr) != 1) {
    addr.sin_addr.s_addr = INADDR_ANY;
  }
  if (bind(fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
    error = std::strerror(errno);
    ::close(fd_);
    fd_ = -1;
    return false;
  }
  if (listen(fd_, 128) < 0) {
    error = std::strerror(errno);
    ::close(fd_);
    fd_ = -1;
    return false;
  }
  running_ = true;
  thread_ = std::thread([this] { loop(); });
  return true;
}

void HttpServer::stop() {
  running_ = false;
  if (fd_ >= 0) {
    ::shutdown(fd_, SHUT_RDWR);
    ::close(fd_);
    fd_ = -1;
  }
  if (thread_.joinable()) thread_.join();
}

void HttpServer::loop() {
  while (running_) {
    int client = accept(fd_, nullptr, nullptr);
    if (client < 0) continue;
    std::thread([this, client] { handle_client(client); }).detach();
  }
}

void HttpServer::handle_client(int client) {
  std::string req;
  char buf[4096];
  ssize_t n;
  while ((n = recv(client, buf, sizeof(buf), 0)) > 0) {
    req.append(buf, static_cast<size_t>(n));
    auto headers_end = req.find("\r\n\r\n");
    if (headers_end != std::string::npos) {
      auto cl_pos = req.find("Content-Length:");
      size_t content_length = 0;
      if (cl_pos != std::string::npos) {
        auto end = req.find("\r\n", cl_pos);
        content_length = static_cast<size_t>(std::stoul(trim(req.substr(cl_pos + 15, end - cl_pos - 15))));
      }
      if (req.size() >= headers_end + 4 + content_length) break;
    }
  }
  std::istringstream in(req);
  std::string method, target, version;
  in >> method >> target >> version;
  auto body_pos = req.find("\r\n\r\n");
  std::string body = body_pos == std::string::npos ? "" : req.substr(body_pos + 4);
  std::string path = target.substr(0, target.find('?'));
  std::string out;

  if (method == "GET" && path == "/health") {
    auto c = config_store_.current();
    std::ostringstream b;
    b << "{\"status\":\"ok\",\"sip_listening\":" << (sip_engine_.listening() ? "true" : "false")
      << ",\"registration_enabled\":" << (c.enable_registration ? "true" : "false")
      << ",\"registration_status\":\"" << (c.enable_registration ? "not_implemented_in_udp_engine" : "disabled") << "\"}";
    out = response(200, b.str());
  } else if (method == "GET" && path == "/metrics") {
    out = response(200, call_manager_.metrics().to_json());
  } else if (method == "GET" && path == "/config") {
    out = response(200, config_store_.current().to_json());
  } else if (method == "PUT" && path == "/config") {
    std::string error;
    if (config_store_.update_from_json(body, error)) {
      out = response(200, config_store_.current().to_json());
    } else {
      out = response(400, "{\"error\":\"" + json_escape(error) + "\"}");
    }
  } else if (method == "GET" && path == "/calls") {
    auto params = query_params(target);
    size_t limit = 50;
    if (params.count("limit")) limit = static_cast<size_t>(std::max(1, std::stoi(params["limit"])));
    out = response(200, log_store_.query_json(limit));
  } else if (method == "POST" && path == "/admin/reload") {
    std::string error;
    if (config_store_.reload(error)) out = response(200, "{\"ok\":true}");
    else out = response(400, "{\"error\":\"" + json_escape(error) + "\"}");
  } else if (method == "POST" && path == "/admin/reset-daily-counter") {
    call_manager_.reset_daily_counter();
    out = response(200, "{\"ok\":true}");
  } else {
    out = response(404, "{\"error\":\"not found\"}");
  }
  send(client, out.data(), out.size(), 0);
  ::close(client);
}

std::string HttpServer::response(int code, const std::string &body, const std::string &content_type) {
  std::string reason = code == 200 ? "OK" : code == 400 ? "Bad Request" : "Not Found";
  std::ostringstream out;
  out << "HTTP/1.1 " << code << " " << reason << "\r\n"
      << "Content-Type: " << content_type << "\r\n"
      << "Content-Length: " << body.size() << "\r\n"
      << "Connection: close\r\n\r\n"
      << body;
  return out.str();
}

std::map<std::string, std::string> HttpServer::query_params(const std::string &target) {
  std::map<std::string, std::string> params;
  auto q = target.find('?');
  if (q == std::string::npos) return params;
  std::istringstream in(target.substr(q + 1));
  std::string pair;
  while (std::getline(in, pair, '&')) {
    auto eq = pair.find('=');
    if (eq != std::string::npos) params[pair.substr(0, eq)] = pair.substr(eq + 1);
  }
  return params;
}

} // namespace sae
