#pragma once

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace fluxcache {

/// Thread-safe registry for Prometheus-style metrics.
/// Supports counters and simple latency histograms.
class MetricsRegistry {
 public:
  MetricsRegistry() = default;
  ~MetricsRegistry() = default;

  MetricsRegistry(const MetricsRegistry&) = delete;
  MetricsRegistry& operator=(const MetricsRegistry&) = delete;

  /// Increment RPC request counter for service/method.
  void IncCounter(const std::string& service, const std::string& method);

  /// Record RPC latency in seconds (for histogram).
  void ObserveLatency(const std::string& service, const std::string& method,
                     double seconds);

  /// Export all metrics in Prometheus text format.
  std::string ExportPrometheus() const;

 private:
  struct CounterKey {
    std::string service;
    std::string method;
    bool operator<(const CounterKey& o) const {
      if (service != o.service) return service < o.service;
      return method < o.method;
    }
  };

  mutable std::mutex mu_;
  std::map<CounterKey, std::atomic<uint64_t>> counters_;
  std::map<CounterKey, std::vector<double>> latency_samples_;
};

}  // namespace fluxcache
