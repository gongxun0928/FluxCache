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

  static const std::map<std::string, std::pair<std::string, std::string>>
      kMetricMeta{
          {"fluxcache_tier_hits_total", {"Cache hits by tier", "counter"}},
          {"fluxcache_tier_misses_total", {"Cache misses", "counter"}},
          {"fluxcache_promotions_total", {"Page promotions to faster tier", "counter"}},
          {"fluxcache_evictions_total", {"Page evictions/demotions", "counter"}},
          {"fluxcache_ufs_reads_total", {"UFS read operations", "counter"}},
          {"fluxcache_ufs_read_duration_seconds", {"UFS read latency", "histogram"}},
          {"fluxcache_client_retries_total", {"Client RPC retries", "counter"}},
          {"fluxcache_client_breaker_opens_total", {"Circuit breaker opens", "counter"}},
          {"fluxcache_active_workers", {"Active workers in ring", "gauge"}},
          {"stale_read_total", {"Stale reads on UFS timeout degradation", "counter"}},
      };
  for (const auto& [name, ptr] : named_counters_) {
    if (ptr) {
      auto meta = kMetricMeta.find(name);
      if (meta != kMetricMeta.end()) {
        out << "# HELP " << name << " " << meta->second.first << "\n";
        out << "# TYPE " << name << " " << meta->second.second << "\n";
      }
      out << name << " " << ptr->load(std::memory_order_relaxed) << "\n";
    }
  }
  for (const auto& [key, ptr] : labeled_counters_) {
    if (ptr) {
      auto meta = kMetricMeta.find(key.name);
      if (meta != kMetricMeta.end()) {
        out << "# HELP " << key.name << " " << meta->second.first << "\n";
        out << "# TYPE " << key.name << " " << meta->second.second << "\n";
      }
      out << key.name << "{" << key.label_key << "=\"" << key.label_value
          << "\"} " << ptr->load(std::memory_order_relaxed) << "\n";
    }
  }
  for (const auto& [name, ptr] : gauges_) {
    if (ptr) {
      auto meta = kMetricMeta.find(name);
      if (meta != kMetricMeta.end()) {
        out << "# HELP " << name << " " << meta->second.first << "\n";
        out << "# TYPE " << name << " " << meta->second.second << "\n";
      }
      out << name << " " << ptr->load(std::memory_order_relaxed) << "\n";
    }
  }
  for (const auto& [name, samples] : histogram_samples_) {
    if (samples.empty()) continue;
    auto meta = kMetricMeta.find(name);
    if (meta != kMetricMeta.end()) {
      out << "# HELP " << name << " " << meta->second.first << "\n";
      out << "# TYPE " << name << " " << meta->second.second << "\n";
    }
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
      out << name << "_bucket{le=\"" << kBucketBounds[i] << "\"} " << cum << "\n";
    }
    out << name << "_bucket{le=\"+Inf\"} " << count << "\n";
    out << name << "_sum " << sum << "\n";
    out << name << "_count " << count << "\n";
  }

  return out.str();
}

void MetricsRegistry::IncCounter(const std::string& name) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = named_counters_.find(name);
  if (it == named_counters_.end()) {
    it = named_counters_
             .emplace(name, std::make_unique<std::atomic<uint64_t>>(0))
             .first;
  }
  it->second->fetch_add(1, std::memory_order_relaxed);
}

void MetricsRegistry::IncCounter(const std::string& name,
                                 const std::string& label_key,
                                 const std::string& label_value) {
  NamedKey key{name, label_key, label_value};
  std::lock_guard<std::mutex> lock(mu_);
  auto it = labeled_counters_.find(key);
  if (it == labeled_counters_.end()) {
    it = labeled_counters_
             .emplace(key, std::make_unique<std::atomic<uint64_t>>(0))
             .first;
  }
  it->second->fetch_add(1, std::memory_order_relaxed);
}

void MetricsRegistry::IncDegradationCounter(const std::string& name) {
  IncCounter(name);
}

void MetricsRegistry::SetGauge(const std::string& name, uint64_t value) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = gauges_.find(name);
  if (it == gauges_.end()) {
    it = gauges_.emplace(name, std::make_unique<std::atomic<uint64_t>>(0))
             .first;
  }
  it->second->store(value, std::memory_order_relaxed);
}

void MetricsRegistry::ObserveHistogram(const std::string& name,
                                       double seconds) {
  std::lock_guard<std::mutex> lock(mu_);
  histogram_samples_[name].push_back(seconds);
}

}  // namespace fluxcache
