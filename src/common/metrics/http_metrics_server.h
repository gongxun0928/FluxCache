#pragma once

#include "common/metrics/metrics_registry.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace fluxcache {

/// Minimal HTTP server that serves GET /metrics with Prometheus text format.
/// Also supports GET /debug/* when handlers are registered.
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

  /// Register a debug endpoint. Path must start with /debug/.
  /// Handler is invoked on GET request; must return response body.
  void RegisterDebugEndpoint(const std::string& path,
                             std::function<std::string()> handler);

  /// Port actually bound (same as constructor port when > 0).
  uint16_t port() const { return port_; }

 private:
  void ServeLoop();
  std::string HandleRequest(const char* buf, size_t len);

  uint16_t port_;
  MetricsRegistry* registry_;
  std::map<std::string, std::function<std::string()>> debug_handlers_;
  std::mutex handlers_mu_;
  std::atomic<bool> running_{false};
  int listen_fd_{-1};
  std::unique_ptr<std::thread> thread_;
};

}  // namespace fluxcache
