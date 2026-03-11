#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace fluxcache {

/// Thread-safe registry for Prometheus-style metrics.
/// Supports counters, gauges, and latency histograms.
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

  /// Increment named counter (no labels). Low-overhead: atomic.
  void IncCounter(const std::string& name);

  /// Increment named counter with one label. Low-overhead: atomic.
  void IncCounter(const std::string& name, const std::string& label_key,
                 const std::string& label_value);

  /// Increment degradation counter (alias for IncCounter).
  void IncDegradationCounter(const std::string& name);

  /// Set gauge value. Used for active_workers etc.
  void SetGauge(const std::string& name, uint64_t value);

  /// Record value for named histogram (e.g. UFS read latency).
  void ObserveHistogram(const std::string& name, double seconds);

  /// Export all metrics in Prometheus text format.
  std::string ExportPrometheus() const;

  /// Register an extra Prometheus exporter. Called during ExportPrometheus().
  /// Use for SlowRequestTracker, HotspotTracker, etc.
  void RegisterPrometheusExporter(std::function<std::string()> fn);

 private:
  struct CounterKey {
    std::string service;
    std::string method;
    bool operator<(const CounterKey& o) const {
      if (service != o.service) return service < o.service;
      return method < o.method;
    }
  };

  struct NamedKey {
    std::string name;
    std::string label_key;
    std::string label_value;
    bool operator<(const NamedKey& o) const {
      if (name != o.name) return name < o.name;
      if (label_key != o.label_key) return label_key < o.label_key;
      return label_value < o.label_value;
    }
  };

  mutable std::mutex mu_;
  std::map<CounterKey, std::atomic<uint64_t>> counters_;
  std::map<CounterKey, std::vector<double>> latency_samples_;

  std::map<std::string, std::unique_ptr<std::atomic<uint64_t>>> named_counters_;
  std::map<NamedKey, std::unique_ptr<std::atomic<uint64_t>>> labeled_counters_;
  std::map<std::string, std::unique_ptr<std::atomic<uint64_t>>> gauges_;
  std::map<std::string, std::vector<double>> histogram_samples_;
  std::vector<std::function<std::string()>> extra_exporters_;
};

}  // namespace fluxcache
