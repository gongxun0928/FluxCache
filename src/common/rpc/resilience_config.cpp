#include "common/rpc/resilience_config.h"

namespace fluxcache {

int ResilienceConfig::TimeoutSec(OpType op) const {
  switch (op) {
    case OpType::kMasterRead:
      return timeout_master_read_sec > 0 ? timeout_master_read_sec : 10;
    case OpType::kMasterWrite:
      return timeout_master_write_sec > 0 ? timeout_master_write_sec : 30;
    case OpType::kWorkerRead:
      return timeout_worker_read_sec > 0 ? timeout_worker_read_sec : 10;
    case OpType::kWorkerWrite:
      return timeout_worker_write_sec > 0 ? timeout_worker_write_sec : 30;
  }
  return 10;
}

ResilienceConfig ResilienceConfigFromClientConfig(const ClientConfig& cfg) {
  ResilienceConfig r;
  r.timeout_master_read_sec =
      cfg.timeout_master_read_sec > 0 ? cfg.timeout_master_read_sec : 10;
  r.timeout_master_write_sec =
      cfg.timeout_master_write_sec > 0 ? cfg.timeout_master_write_sec : 30;
  r.timeout_worker_read_sec =
      cfg.timeout_worker_read_sec > 0 ? cfg.timeout_worker_read_sec : 10;
  r.timeout_worker_write_sec =
      cfg.timeout_worker_write_sec > 0 ? cfg.timeout_worker_write_sec : 30;
  r.circuit_breaker_enabled = cfg.circuit_breaker_enabled;
  r.circuit_breaker_failure_threshold =
      cfg.circuit_breaker_failure_threshold > 0
          ? cfg.circuit_breaker_failure_threshold
          : 5;
  r.circuit_breaker_error_rate_threshold =
      cfg.circuit_breaker_error_rate_threshold > 0 &&
              cfg.circuit_breaker_error_rate_threshold <= 1.0
          ? cfg.circuit_breaker_error_rate_threshold
          : 0.5;
  r.circuit_breaker_open_duration_ms =
      cfg.circuit_breaker_open_duration_ms > 0
          ? cfg.circuit_breaker_open_duration_ms
          : 30000;
  r.circuit_breaker_half_open_probes =
      cfg.circuit_breaker_half_open_probes > 0
          ? cfg.circuit_breaker_half_open_probes
          : 1;
  return r;
}

}  // namespace fluxcache
