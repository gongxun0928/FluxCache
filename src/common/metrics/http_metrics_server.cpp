#include "common/metrics/http_metrics_server.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <sstream>
#include <string>

namespace fluxcache {

namespace {

const char kResponse200[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/plain; version=0.0.4; charset=utf-8\r\n"
    "Connection: close\r\n\r\n";

const char kResponse404[] =
    "HTTP/1.1 404 Not Found\r\n"
    "Content-Length: 0\r\n"
    "Connection: close\r\n\r\n";

/// Parse first line of HTTP request. Returns true if GET /metrics.
bool IsGetMetrics(const char* buf, size_t len) {
  if (len < 15) return false;  // "GET /metrics " is 14 chars
  if (std::strncmp(buf, "GET ", 4) != 0) return false;
  if (std::strncmp(buf + 4, "/metrics", 8) != 0) return false;
  char c = buf[12];
  return c == ' ' || c == '?' || c == '\r' || c == '\n';
}

}  // namespace

HttpMetricsServer::HttpMetricsServer(uint16_t port, MetricsRegistry* registry)
    : port_(port), registry_(registry) {}

HttpMetricsServer::~HttpMetricsServer() {
  Shutdown();
}

bool HttpMetricsServer::Start() {
  if (port_ == 0) return true;  // Disabled

  listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd_ < 0) return false;

  int opt = 1;
  if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }

  struct sockaddr_in addr {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port_);
  if (bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr),
          sizeof(addr)) < 0) {
    close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }

  if (listen(listen_fd_, 8) < 0) {
    close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }

  running_ = true;
  thread_ = std::make_unique<std::thread>(&HttpMetricsServer::ServeLoop, this);
  return true;
}

void HttpMetricsServer::Shutdown() {
  running_ = false;
  if (listen_fd_ >= 0) {
    int fd = listen_fd_;
    listen_fd_ = -1;
    close(fd);
  }
  if (thread_ && thread_->joinable()) {
    thread_->join();
    thread_.reset();
  }
}

void HttpMetricsServer::ServeLoop() {
  char buf[512];
  while (running_ && listen_fd_ >= 0) {
    struct sockaddr_in client_addr {};
    socklen_t len = sizeof(client_addr);
    int client_fd = accept(listen_fd_,
                          reinterpret_cast<struct sockaddr*>(&client_addr),
                          &len);
    if (client_fd < 0) {
      if (running_) continue;
      break;
    }

    ssize_t n = recv(client_fd, buf, sizeof(buf) - 1, 0);
    if (n > 0) {
      buf[n] = '\0';
      if (IsGetMetrics(buf, static_cast<size_t>(n))) {
        std::string body = registry_->ExportPrometheus();
        send(client_fd, kResponse200, strlen(kResponse200), 0);
        send(client_fd, body.data(), body.size(), 0);
      } else {
        send(client_fd, kResponse404, strlen(kResponse404), 0);
      }
    }
    close(client_fd);
  }
}

}  // namespace fluxcache
