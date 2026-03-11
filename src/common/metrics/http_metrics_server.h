#pragma once

#include "common/metrics/metrics_registry.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

namespace fluxcache {

/// Minimal HTTP server that serves GET /metrics with Prometheus text format.
/// Runs in a background thread. metrics_port=0 means do not start.
class HttpMetricsServer {
 public:
  /// registry must outlive this server.
  explicit HttpMetricsServer(uint16_t port, MetricsRegistry* registry);
  ~HttpMetricsServer();

  HttpMetricsServer(const HttpMetricsServer&) = delete;
  HttpMetricsServer& operator=(const HttpMetricsServer&) = delete;

  /// Start the server in a background thread. Returns false if bind fails.
  bool Start();
  /// Stop the server. Safe to call multiple times.
  void Shutdown();

  /// Port actually bound (same as constructor port when > 0).
  uint16_t port() const { return port_; }

 private:
  void ServeLoop();

  uint16_t port_;
  MetricsRegistry* registry_;
  std::atomic<bool> running_{false};
  int listen_fd_{-1};
  std::unique_ptr<std::thread> thread_;
};

}  // namespace fluxcache
