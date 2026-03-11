#pragma once

#include "common/config/config.h"

namespace fluxcache {

enum class OpType {
  kMasterRead,
  kMasterWrite,
  kWorkerRead,
  kWorkerWrite,
};

struct ResilienceConfig {
  int timeout_master_read_sec = 10;
  int timeout_master_write_sec = 30;
  int timeout_worker_read_sec = 10;
  int timeout_worker_write_sec = 30;
  bool circuit_breaker_enabled = true;
  int circuit_breaker_failure_threshold = 5;
  double circuit_breaker_error_rate_threshold = 0.5;
  int circuit_breaker_open_duration_ms = 30000;
  int circuit_breaker_half_open_probes = 1;

  int TimeoutSec(OpType op) const;
};

ResilienceConfig ResilienceConfigFromClientConfig(const ClientConfig& cfg);

}  // namespace fluxcache
