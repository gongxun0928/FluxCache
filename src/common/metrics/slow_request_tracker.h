#pragma once

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace fluxcache {

/// Record of a single slow request.
struct SlowRequestEntry {
  std::string service;
  std::string method;
  double duration_sec = 0;
  std::string extra_info;
};

/// Tracks RPC requests that exceed a duration threshold.
/// Thread-safe. Used for /debug/slow-requests and Prometheus metrics.
class SlowRequestTracker {
 public:
  /// @param threshold_sec Requests with duration > this are recorded.
  /// @param max_entries Maximum entries to retain (ring buffer). Default 100.
  explicit SlowRequestTracker(double threshold_sec,
                              size_t max_entries = 100);

  /// Record a slow request. Only stored if duration_sec > threshold_.
  void Record(const std::string& service, const std::string& method,
              double duration_sec, const std::string& extra_info);

  /// Get recent slow requests, newest first. At most max_count returned.
  std::vector<SlowRequestEntry> GetRecentSlowRequests(size_t max_count) const;

  /// Export Prometheus fragment for fluxcache_slow_requests_total and
  /// fluxcache_slow_request_duration_seconds.
  std::string ExportPrometheusFragment() const;

  /// Human-readable format for /debug/slow-requests.
  std::string FormatDebug() const;

 private:
  double threshold_sec_;
  size_t max_entries_;
  mutable std::mutex mu_;
  std::deque<SlowRequestEntry> entries_;
  std::atomic<uint64_t> total_count_{0};
  std::vector<double> duration_samples_;  // for histogram, capped
};

}  // namespace fluxcache
