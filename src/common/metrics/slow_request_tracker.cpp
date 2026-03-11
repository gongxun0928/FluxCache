#include "common/metrics/slow_request_tracker.h"
#include <iomanip>
#include <sstream>

namespace fluxcache {

namespace {
constexpr double kBucketBounds[] = {0.5, 1.0, 2.0, 5.0, 10.0};
constexpr size_t kNumBuckets = sizeof(kBucketBounds) / sizeof(kBucketBounds[0]);
constexpr size_t kMaxSamples = 1000;
}  // namespace

SlowRequestTracker::SlowRequestTracker(double threshold_sec,
                                       size_t max_entries)
    : threshold_sec_(threshold_sec), max_entries_(max_entries) {}

void SlowRequestTracker::Record(const std::string& service,
                                const std::string& method,
                                double duration_sec,
                                const std::string& extra_info) {
  if (duration_sec <= threshold_sec_) return;

  total_count_.fetch_add(1, std::memory_order_relaxed);

  SlowRequestEntry e;
  e.service = service;
  e.method = method;
  e.duration_sec = duration_sec;
  e.extra_info = extra_info;

  std::lock_guard<std::mutex> lock(mu_);
  entries_.push_front(e);
  if (entries_.size() > max_entries_) {
    entries_.pop_back();
  }
  if (duration_samples_.size() < kMaxSamples) {
    duration_samples_.push_back(duration_sec);
  }
}

std::vector<SlowRequestEntry> SlowRequestTracker::GetRecentSlowRequests(
    size_t max_count) const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<SlowRequestEntry> out;
  size_t n = std::min(max_count, entries_.size());
  for (size_t i = 0; i < n; ++i) {
    out.push_back(entries_[i]);
  }
  return out;
}

std::string SlowRequestTracker::ExportPrometheusFragment() const {
  std::ostringstream out;
  out << std::fixed << std::setprecision(6);

  uint64_t count = total_count_.load(std::memory_order_relaxed);
  std::vector<double> samples;
  {
    std::lock_guard<std::mutex> lock(mu_);
    samples = duration_samples_;
  }

  out << "# HELP fluxcache_slow_requests_total Total slow requests (above threshold).\n";
  out << "# TYPE fluxcache_slow_requests_total counter\n";
  out << "fluxcache_slow_requests_total " << count << "\n";

  if (samples.empty()) return out.str();

  out << "# HELP fluxcache_slow_request_duration_seconds Slow request duration histogram.\n";
  out << "# TYPE fluxcache_slow_request_duration_seconds histogram\n";

  std::vector<uint64_t> bucket_counts(kNumBuckets + 1, 0);
  double sum = 0;
  for (double s : samples) {
    sum += s;
    size_t i = 0;
    for (; i < kNumBuckets && s > kBucketBounds[i]; ++i) {}
    bucket_counts[i]++;
  }
  uint64_t sample_count = samples.size();

  for (size_t i = 0; i < kNumBuckets; ++i) {
    uint64_t cum = 0;
    for (size_t j = 0; j <= i; ++j) cum += bucket_counts[j];
    out << "fluxcache_slow_request_duration_seconds_bucket{le=\""
        << kBucketBounds[i] << "\"} " << cum << "\n";
  }
  out << "fluxcache_slow_request_duration_seconds_bucket{le=\"+Inf\"} "
      << sample_count << "\n";
  out << "fluxcache_slow_request_duration_seconds_sum " << sum << "\n";
  out << "fluxcache_slow_request_duration_seconds_count " << sample_count
      << "\n";

  return out.str();
}

std::string SlowRequestTracker::FormatDebug() const {
  auto recent = GetRecentSlowRequests(100);
  std::ostringstream out;
  out << "slow_requests_count=" << recent.size() << "\n";
  for (const auto& e : recent) {
    out << e.service << " " << e.method << " " << std::fixed
        << std::setprecision(3) << e.duration_sec << "s";
    if (!e.extra_info.empty()) {
      out << " " << e.extra_info;
    }
    out << "\n";
  }
  return out.str();
}

}  // namespace fluxcache
