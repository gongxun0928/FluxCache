#include "common/metrics/metrics_registry.h"
#include <iomanip>
#include <sstream>

namespace fluxcache {

namespace {
constexpr double kBucketBounds[] = {0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5,
                                    1.0, 2.5, 5.0, 10.0};
constexpr size_t kNumBuckets =
    sizeof(kBucketBounds) / sizeof(kBucketBounds[0]);
}  // namespace

void MetricsRegistry::IncCounter(const std::string& service,
                                 const std::string& method) {
  CounterKey key{service, method};
  std::lock_guard<std::mutex> lock(mu_);
  counters_[key].fetch_add(1, std::memory_order_relaxed);
}

void MetricsRegistry::ObserveLatency(const std::string& service,
                                     const std::string& method,
                                     double seconds) {
  CounterKey key{service, method};
  std::lock_guard<std::mutex> lock(mu_);
  latency_samples_[key].push_back(seconds);
}

std::string MetricsRegistry::ExportPrometheus() const {
  std::ostringstream out;
  out << std::fixed << std::setprecision(6);

  std::lock_guard<std::mutex> lock(mu_);

  // Counter: fluxcache_rpc_requests_total
  out << "# HELP fluxcache_rpc_requests_total Total number of RPC requests.\n";
  out << "# TYPE fluxcache_rpc_requests_total counter\n";
  for (const auto& [key, counter] : counters_) {
    uint64_t val = counter.load(std::memory_order_relaxed);
    out << "fluxcache_rpc_requests_total{service=\"" << key.service
        << "\",method=\"" << key.method << "\"} " << val << "\n";
  }

  // Histogram: fluxcache_rpc_duration_seconds
  out << "# HELP fluxcache_rpc_duration_seconds RPC request duration in seconds.\n";
  out << "# TYPE fluxcache_rpc_duration_seconds histogram\n";
  for (const auto& [key, samples] : latency_samples_) {
    if (samples.empty()) continue;

    std::vector<uint64_t> bucket_counts(kNumBuckets + 1, 0);
    double sum = 0;
    for (double s : samples) {
      sum += s;
      size_t i = 0;
      for (; i < kNumBuckets && s > kBucketBounds[i]; ++i) {}
      bucket_counts[i]++;
    }
    uint64_t count = samples.size();

    for (size_t i = 0; i < kNumBuckets; ++i) {
      uint64_t cum = 0;
      for (size_t j = 0; j <= i; ++j) cum += bucket_counts[j];
      out << "fluxcache_rpc_duration_seconds_bucket{service=\"" << key.service
          << "\",method=\"" << key.method << "\",le=\"" << kBucketBounds[i]
          << "\"} " << cum << "\n";
    }
    out << "fluxcache_rpc_duration_seconds_bucket{service=\"" << key.service
        << "\",method=\"" << key.method << "\",le=\"+Inf\"} " << count << "\n";
    out << "fluxcache_rpc_duration_seconds_sum{service=\"" << key.service
        << "\",method=\"" << key.method << "\"} " << sum << "\n";
    out << "fluxcache_rpc_duration_seconds_count{service=\"" << key.service
        << "\",method=\"" << key.method << "\"} " << count << "\n";
  }

  return out.str();
}

}  // namespace fluxcache
