#include "client/fluxcache_client.h"
#include "master.pb.h"
#include <sstream>

namespace fluxcache {

FluxCacheClient::FluxCacheClient(const ClientConfig& config) {
  std::ostringstream oss;
  oss << config.master_host << ":" << config.master_port;
  master_address_ = oss.str();
  master_client_ =
      std::make_unique<MasterClient>(&pool_, master_address_, 10);
}

Status FluxCacheClient::RefreshRing() {
  auto result = master_client_->GetHashRing();
  if (!result.ok()) {
    return result.status();
  }
  cached_ring_.Update(result.value());
  ring_fetched_ = true;
  return Status::OK();
}

StatusOr<WorkerId> FluxCacheClient::GetWorkerForBlock(BlockId block_id) {
  if (!ring_fetched_) {
    auto s = RefreshRing();
    if (!s.ok()) {
      return s;
    }
  }

  WorkerId wid = cached_ring_.GetWorker(block_id);
  if (wid == 0) {
    return Status::NotFound("no worker in ring");
  }
  return wid;
}

StatusOr<std::unique_ptr<WorkerClient>> FluxCacheClient::GetWorkerClient(
    WorkerId worker_id) {
  if (!ring_fetched_) {
    auto s = RefreshRing();
    if (!s.ok()) {
      return s;
    }
  }

  std::string addr = cached_ring_.GetWorkerAddress(worker_id);
  if (addr.empty()) {
    return Status::NotFound("worker not in ring");
  }

  return std::make_unique<WorkerClient>(&pool_, addr, 10);
}

void FluxCacheClient::SetRingForTest(const proto::GetHashRingResponse& resp) {
  cached_ring_.Update(resp);
  ring_fetched_ = true;
}

}  // namespace fluxcache
