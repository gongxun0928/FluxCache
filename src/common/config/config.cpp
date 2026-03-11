#include "common/config/config.h"
#include <yaml-cpp/yaml.h>

namespace fluxcache {

namespace {

Status RequireString(const YAML::Node& node, const std::string& path,
                     std::string* out) {
  if (!node || !node.IsDefined()) {
    return Status::InvalidArgument(("missing required field: " + path).c_str());
  }
  if (!node.IsScalar()) {
    return Status::InvalidArgument(
        ("field must be string: " + path).c_str());
  }
  try {
    *out = node.as<std::string>();
    return Status::OK();
  } catch (const YAML::BadConversion&) {
    return Status::InvalidArgument(
        ("field must be string: " + path).c_str());
  }
}

Status RequireUint16(const YAML::Node& node, const std::string& path,
                    uint16_t* out) {
  if (!node || !node.IsDefined()) {
    return Status::InvalidArgument(("missing required field: " + path).c_str());
  }
  try {
    int val = node.as<int>();
    if (val < 0 || val > 65535) {
      return Status::InvalidArgument(
          ("port out of range [0,65535]: " + path).c_str());
    }
    *out = static_cast<uint16_t>(val);
    return Status::OK();
  } catch (const YAML::BadConversion&) {
    return Status::InvalidArgument(
        ("field must be integer: " + path).c_str());
  }
}

void OptionalString(const YAML::Node& node, const std::string& default_val,
                    std::string* out) {
  if (node && node.IsDefined() && node.IsScalar()) {
    try {
      *out = node.as<std::string>();
      return;
    } catch (const YAML::BadConversion&) {
    }
  }
  *out = default_val;
}

void OptionalUint16(const YAML::Node& node, uint16_t default_val,
                    uint16_t* out) {
  if (node && node.IsDefined()) {
    try {
      int val = node.as<int>();
      if (val >= 0 && val <= 65535) {
        *out = static_cast<uint16_t>(val);
        return;
      }
    } catch (const YAML::BadConversion&) {
    }
  }
  *out = default_val;
}

void OptionalUint32(const YAML::Node& node, uint32_t default_val,
                    uint32_t* out) {
  if (node && node.IsDefined()) {
    try {
      int val = node.as<int>();
      if (val >= 0) {
        *out = static_cast<uint32_t>(val);
        return;
      }
    } catch (const YAML::BadConversion&) {
    }
  }
  *out = default_val;
}

void OptionalSizeT(const YAML::Node& node, size_t default_val, size_t* out) {
  if (node && node.IsDefined()) {
    try {
      int val = node.as<int>();
      if (val > 0) {
        *out = static_cast<size_t>(val);
        return;
      }
    } catch (const YAML::BadConversion&) {
    }
  }
  *out = default_val;
}

void OptionalInt(const YAML::Node& node, int default_val, int* out) {
  if (node && node.IsDefined()) {
    try {
      int val = node.as<int>();
      if (val >= 0) {
        *out = val;
        return;
      }
    } catch (const YAML::BadConversion&) {
    }
  }
  *out = default_val;
}

}  // namespace

StatusOr<FluxCacheConfig> LoadConfig(const std::string& path) {
  YAML::Node root;
  try {
    root = YAML::LoadFile(path);
  } catch (const YAML::BadFile& e) {
    return Status::IOError(e.what());
  } catch (const YAML::ParserException& e) {
    return Status::InvalidArgument(e.what());
  }

  if (!root || !root.IsMap()) {
    return Status::InvalidArgument("config root must be a map");
  }

  YAML::Node fluxcache = root["fluxcache"];
  if (!fluxcache || !fluxcache.IsMap()) {
    return Status::InvalidArgument("missing or invalid 'fluxcache' root key");
  }

  FluxCacheConfig cfg;

  // master
  YAML::Node master = fluxcache["master"];
  if (!master || !master.IsMap()) {
    return Status::InvalidArgument("missing or invalid 'fluxcache.master'");
  }
  if (auto s = RequireString(master["host"], "fluxcache.master.host",
                             &cfg.master.host);
      !s.ok()) {
    return s;
  }
  if (auto s = RequireUint16(master["port"], "fluxcache.master.port",
                             &cfg.master.port);
      !s.ok()) {
    return s;
  }
  OptionalString(master["db_path"], "./fluxcache_meta", &cfg.master.db_path);
  OptionalUint16(master["metrics_port"], 0, &cfg.master.metrics_port);

  // worker
  YAML::Node worker = fluxcache["worker"];
  if (!worker || !worker.IsMap()) {
    return Status::InvalidArgument("missing or invalid 'fluxcache.worker'");
  }
  if (auto s = RequireString(worker["data_dir"], "fluxcache.worker.data_dir",
                             &cfg.worker.data_dir);
      !s.ok()) {
    return s;
  }
  OptionalString(worker["metastore_path"], "", &cfg.worker.metastore_path);
  OptionalString(worker["host"], "0.0.0.0", &cfg.worker.host);
  OptionalUint16(worker["port"], 0, &cfg.worker.port);
  OptionalUint32(worker["heartbeat_interval_ms"], 5000,
                 &cfg.worker.heartbeat_interval_ms);
  OptionalUint16(worker["metrics_port"], 0, &cfg.worker.metrics_port);
  OptionalString(worker["eviction_policy"], "lru", &cfg.worker.eviction_policy);

  // client
  YAML::Node client = fluxcache["client"];
  if (!client || !client.IsMap()) {
    return Status::InvalidArgument("missing or invalid 'fluxcache.client'");
  }
  if (auto s = RequireString(client["master_host"],
                             "fluxcache.client.master_host",
                             &cfg.client.master_host);
      !s.ok()) {
    return s;
  }
  if (auto s = RequireUint16(client["master_port"],
                             "fluxcache.client.master_port",
                             &cfg.client.master_port);
      !s.ok()) {
    return s;
  }
  OptionalSizeT(client["channel_pool_size"], 4, &cfg.client.channel_pool_size);
  OptionalInt(client["retry_max_attempts"], 3, &cfg.client.retry_max_attempts);
  OptionalInt(client["retry_initial_delay_ms"], 50,
               &cfg.client.retry_initial_delay_ms);
  OptionalInt(client["timeout_master_read_sec"], 10,
               &cfg.client.timeout_master_read_sec);
  OptionalInt(client["timeout_master_write_sec"], 30,
               &cfg.client.timeout_master_write_sec);
  OptionalInt(client["timeout_worker_read_sec"], 10,
               &cfg.client.timeout_worker_read_sec);
  OptionalInt(client["timeout_worker_write_sec"], 30,
               &cfg.client.timeout_worker_write_sec);
  if (client["circuit_breaker_enabled"] && client["circuit_breaker_enabled"].IsDefined()) {
    try {
      cfg.client.circuit_breaker_enabled =
          client["circuit_breaker_enabled"].as<bool>();
    } catch (const YAML::BadConversion&) {}
  }
  OptionalInt(client["circuit_breaker_failure_threshold"], 5,
               &cfg.client.circuit_breaker_failure_threshold);
  if (client["circuit_breaker_error_rate_threshold"] &&
      client["circuit_breaker_error_rate_threshold"].IsDefined()) {
    try {
      double v = client["circuit_breaker_error_rate_threshold"].as<double>();
      if (v > 0 && v <= 1.0) {
        cfg.client.circuit_breaker_error_rate_threshold = v;
      }
    } catch (const YAML::BadConversion&) {}
  }
  OptionalInt(client["circuit_breaker_open_duration_ms"], 30000,
               &cfg.client.circuit_breaker_open_duration_ms);
  OptionalInt(client["circuit_breaker_half_open_probes"], 1,
               &cfg.client.circuit_breaker_half_open_probes);

  // ufs
  YAML::Node ufs = fluxcache["ufs"];
  if (!ufs || !ufs.IsMap()) {
    return Status::InvalidArgument("missing or invalid 'fluxcache.ufs'");
  }
  if (auto s = RequireString(ufs["type"], "fluxcache.ufs.type", &cfg.ufs.type);
      !s.ok()) {
    return s;
  }
  if (auto s = RequireString(ufs["path"], "fluxcache.ufs.path", &cfg.ufs.path);
      !s.ok()) {
    return s;
  }

  if (cfg.ufs.type != "localfs") {
    return Status::InvalidArgument(
        "fluxcache.ufs.type must be 'localfs' in Phase 1");
  }

  return cfg;
}

}  // namespace fluxcache
