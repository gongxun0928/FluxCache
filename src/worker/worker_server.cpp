#include "worker/worker_server.h"
#include "worker/worker_service_impl.h"
#include "common/metrics/http_metrics_server.h"
#include "common/metrics/metrics_registry.h"
#include "common/metrics/slow_request_tracker.h"
#include "common/types.h"
#include "worker/cache/eviction_policy_factory.h"
#include "worker/cache/hotspot_tracker.h"
#include "worker/cache/tier_evictor.h"
#include "worker/meta/meta_store.h"
#include "worker/storage/memory_tier.h"
#include "worker/storage/ssd_tier.h"
#include <chrono>
#include <grpcpp/grpcpp.h>

namespace fluxcache {

namespace {

EvictionPolicyType ParseEvictionPolicyType(const std::string& s) {
  if (s == "lfu") return EvictionPolicyType::kLFU;
  return EvictionPolicyType::kLRU;  // default
}

}  // namespace

WorkerServer::WorkerServer(const WorkerConfig& config) : config_(config) {
  tier_manager_ = std::make_unique<TierManager>();
  tier_manager_->AddTier(std::make_unique<MemoryTier>(256 * kPageSize));  // 256MB

  MetaStore* meta_ptr = nullptr;
  EvictionPolicy* eviction_ptr = nullptr;
  if (!config_.data_dir.empty()) {
    tier_manager_->AddTier(std::make_unique<SsdTier>(
        config_.data_dir + "/ssd", kSsdCapacity));
    meta_store_ = std::make_unique<MetaStore>();
    std::string metastore_path = config_.metastore_path.empty()
                                     ? config_.data_dir + "/metastore"
                                     : config_.metastore_path;
    if (meta_store_->Open(metastore_path)) {
      meta_ptr = meta_store_.get();
      eviction_policy_ =
          CreateEvictionPolicy(ParseEvictionPolicyType(config_.eviction_policy));
      eviction_ptr = eviction_policy_.get();
    }
  }

  page_store_ = std::make_unique<PageStore>(tier_manager_.get(), kPageSize,
                                           meta_ptr, eviction_ptr);
  if (meta_ptr) {
    page_store_->RecoverFromMetaStore();
  }
  if (config_.metrics_port > 0) {
    metrics_registry_ = std::make_unique<MetricsRegistry>();
    slow_request_tracker_ =
        std::make_unique<SlowRequestTracker>(1.0);  // 1s threshold
    hotspot_tracker_ = std::make_unique<HotspotTracker>(100);

    metrics_registry_->RegisterPrometheusExporter([this]() {
      return slow_request_tracker_->ExportPrometheusFragment();
    });
    metrics_registry_->RegisterPrometheusExporter([this]() {
      return hotspot_tracker_->ExportPrometheusFragment();
    });

    service_impl_ = std::make_unique<WorkerServiceImpl>(
        page_store_.get(), kPageSize, kBlockSize, metrics_registry_.get(),
        config_.allow_stale_read_on_ufs_timeout,
        slow_request_tracker_.get(), hotspot_tracker_.get());
  } else {
    service_impl_ = std::make_unique<WorkerServiceImpl>(
        page_store_.get(), kPageSize, kBlockSize, nullptr,
        config_.allow_stale_read_on_ufs_timeout);
  }

  if (meta_ptr && eviction_ptr) {
    tier_evictor_ = std::make_unique<TierEvictor>(
        page_store_.get(), tier_manager_.get(), meta_ptr, eviction_ptr, 0.9,
        metrics_registry_ ? metrics_registry_.get() : nullptr);
    tier_evictor_->SetBeforeEvictCallback(
        [this](PageId id, const std::string& data) {
          return service_impl_->TryWriteBackToUfs(id, data);
        });
  }

  if (config_.metrics_port > 0) {
    http_metrics_server_ =
        std::make_unique<HttpMetricsServer>(config_.metrics_port,
                                            metrics_registry_.get());
    http_metrics_server_->RegisterDebugEndpoint(
        "/debug/slow-requests",
        [this]() { return slow_request_tracker_->FormatDebug(); });
    http_metrics_server_->RegisterDebugEndpoint(
        "/debug/hot-pages",
        [this]() { return hotspot_tracker_->FormatDebug(); });
  }
}

WorkerServer::~WorkerServer() {
  Shutdown();
}

bool WorkerServer::Start() {
  if (http_metrics_server_ && !http_metrics_server_->Start()) {
    return false;
  }
  if (tier_evictor_) {
    eviction_stop_.store(false);
    eviction_thread_ = std::thread(&WorkerServer::EvictionLoop, this);
  }
  std::string addr = config_.host + ":" + std::to_string(config_.port);
  ::grpc::ServerBuilder builder;
  builder.AddListeningPort(addr, ::grpc::InsecureServerCredentials());
  builder.RegisterService(service_impl_.get());
  server_ = builder.BuildAndStart();
  return server_ != nullptr;
}

void WorkerServer::EvictionLoop() {
  while (!eviction_stop_.load(std::memory_order_relaxed)) {
    if (tier_evictor_) {
      tier_evictor_->EvictOne();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

void WorkerServer::Shutdown() {
  eviction_stop_.store(true);
  if (eviction_thread_.joinable()) {
    eviction_thread_.join();
  }
  if (http_metrics_server_) {
    http_metrics_server_->Shutdown();
  }
  if (server_) {
    server_->Shutdown();
    server_->Wait();
    server_.reset();
  }
}

}  // namespace fluxcache
