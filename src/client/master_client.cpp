#include "client/master_client.h"
#include "common/metrics/metrics_registry.h"
#include "master.grpc.pb.h"
#include "worker/registration_util.h"
#include <chrono>
#include <grpcpp/client_context.h>

namespace fluxcache {

MasterClient::MasterClient(ChannelPool* pool, const std::string& master_address,
                           const ResilienceConfig& resilience_config,
                           const RetryPolicy& retry_policy,
                           CircuitBreaker* circuit_breaker,
                           MetricsRegistry* metrics)
    : pool_(pool),
      master_address_(master_address),
      resilience_config_(resilience_config),
      retry_policy_(retry_policy),
      circuit_breaker_(circuit_breaker),
      metrics_(metrics) {}

std::string MasterClient::GetMasterAddress() const {
  std::lock_guard<std::mutex> lock(master_address_mu_);
  return master_address_;
}

bool MasterClient::TryFollowLeaderHint(const grpc::Status& status) {
  if (status.error_code() != grpc::StatusCode::UNAVAILABLE) {
    return false;
  }
  auto leader = ExtractLeaderAddress(status.error_message());
  if (!leader.has_value() || leader->empty()) {
    return false;
  }
  std::lock_guard<std::mutex> lock(master_address_mu_);
  master_address_ = *leader;
  return true;
}

namespace {

StatusOr<fluxcache::proto::GetHashRingResponse> DoGetHashRing(
    fluxcache::ChannelPool* pool, const std::string& address, int deadline_sec) {
  auto channel = pool->GetChannel(address);
  if (!channel) {
    return fluxcache::Status::Unavailable("failed to get channel for master");
  }

  fluxcache::proto::MasterService::Stub stub(channel);
  grpc::ClientContext ctx;
  ctx.set_deadline(std::chrono::system_clock::now() +
                   std::chrono::seconds(deadline_sec));

  fluxcache::proto::GetHashRingRequest req;
  fluxcache::proto::GetHashRingResponse resp;

  auto grpc_status = stub.GetHashRing(&ctx, req, &resp);

  if (grpc_status.ok()) {
    return resp;
  }

  if (grpc_status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED ||
      grpc_status.error_code() == grpc::StatusCode::UNAVAILABLE) {
    return fluxcache::Status::Unavailable(grpc_status.error_message().c_str());
  }
  return fluxcache::Status::IOError(grpc_status.error_message().c_str());
}

}  // namespace

StatusOr<proto::GetHashRingResponse> MasterClient::GetHashRing() {
  if (circuit_breaker_ && !circuit_breaker_->AllowRequest()) {
    return Status::Unavailable("circuit breaker open");
  }
  int deadline = resilience_config_.TimeoutSec(OpType::kMasterRead);
  RetryPolicy p = retry_policy_;
  p.is_idempotent = true;
  auto result = ExecuteWithRetry<proto::GetHashRingResponse>(
      p, [this, deadline]() {
        return DoGetHashRing(pool_, GetMasterAddress(), deadline);
      },
      metrics_, "master");
  if (circuit_breaker_) {
    result.ok() ? circuit_breaker_->RecordSuccess()
                : circuit_breaker_->RecordFailure();
  }
  return result;
}

namespace {

StatusOr<fluxcache::proto::GetFileInfoResponse> DoGetFileInfo(
    fluxcache::ChannelPool* pool, const std::string& address, int deadline_sec,
    const std::string& path) {
  auto channel = pool->GetChannel(address);
  if (!channel) {
    return fluxcache::Status::Unavailable("failed to get channel for master");
  }

  fluxcache::proto::MasterService::Stub stub(channel);
  grpc::ClientContext ctx;
  ctx.set_deadline(std::chrono::system_clock::now() +
                   std::chrono::seconds(deadline_sec));

  fluxcache::proto::GetFileInfoRequest req;
  req.set_path(path);
  fluxcache::proto::GetFileInfoResponse resp;

  auto grpc_status = stub.GetFileInfo(&ctx, req, &resp);

  if (grpc_status.ok()) {
    return resp;
  }

  if (grpc_status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED ||
      grpc_status.error_code() == grpc::StatusCode::UNAVAILABLE) {
    return fluxcache::Status::Unavailable(grpc_status.error_message().c_str());
  }
  if (grpc_status.error_code() == grpc::StatusCode::NOT_FOUND) {
    return fluxcache::Status::NotFound(grpc_status.error_message().c_str());
  }
  return fluxcache::Status::IOError(grpc_status.error_message().c_str());
}

}  // namespace

StatusOr<proto::GetFileInfoResponse> MasterClient::GetFileInfo(
    const std::string& path) {
  if (circuit_breaker_ && !circuit_breaker_->AllowRequest()) {
    return Status::Unavailable("circuit breaker open");
  }
  int deadline = resilience_config_.TimeoutSec(OpType::kMasterRead);
  RetryPolicy p = retry_policy_;
  p.is_idempotent = true;
  auto result = ExecuteWithRetry<proto::GetFileInfoResponse>(
      p, [this, path, deadline]() {
        return DoGetFileInfo(pool_, GetMasterAddress(), deadline, path);
      },
      metrics_, "master");
  if (circuit_breaker_) {
    result.ok() ? circuit_breaker_->RecordSuccess()
                : circuit_breaker_->RecordFailure();
  }
  return result;
}

StatusOr<proto::CreateFileResponse> MasterClient::CreateFile(
    const std::string& path) {
  if (circuit_breaker_ && !circuit_breaker_->AllowRequest()) {
    return Status::Unavailable("circuit breaker open");
  }
  int deadline = resilience_config_.TimeoutSec(OpType::kMasterWrite);
  proto::CreateFileRequest req;
  req.set_path(path);
  proto::CreateFileResponse resp;
  auto invoke = [&](const std::string& address) {
    auto channel = pool_->GetChannel(address);
    if (!channel) {
      return grpc::Status(grpc::StatusCode::UNAVAILABLE,
                          "failed to get channel for master");
    }
    fluxcache::proto::MasterService::Stub stub(channel);
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() +
                     std::chrono::seconds(deadline));
    return stub.CreateFile(&ctx, req, &resp);
  };

  auto grpc_status = invoke(GetMasterAddress());
  if (!grpc_status.ok() && TryFollowLeaderHint(grpc_status)) {
    grpc_status = invoke(GetMasterAddress());
  }

  if (grpc_status.ok()) {
    if (circuit_breaker_) circuit_breaker_->RecordSuccess();
    return resp;
  }

  if (circuit_breaker_) circuit_breaker_->RecordFailure();
  if (grpc_status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED ||
      grpc_status.error_code() == grpc::StatusCode::UNAVAILABLE) {
    return Status::Unavailable(grpc_status.error_message().c_str());
  }
  if (grpc_status.error_code() == grpc::StatusCode::NOT_FOUND) {
    return Status::NotFound(grpc_status.error_message().c_str());
  }
  if (grpc_status.error_code() == grpc::StatusCode::ALREADY_EXISTS) {
    return Status::AlreadyExists(grpc_status.error_message().c_str());
  }
  return Status::IOError(grpc_status.error_message().c_str());
}

Status MasterClient::CompleteFile(uint64_t inode_id, uint64_t size,
                                  std::optional<int64_t> ufs_mtime_ms) {
  if (circuit_breaker_ && !circuit_breaker_->AllowRequest()) {
    return Status::Unavailable("circuit breaker open");
  }
  int deadline = resilience_config_.TimeoutSec(OpType::kMasterWrite);
  proto::CompleteFileRequest req;
  req.set_inode_id(inode_id);
  req.set_size(size);
  if (ufs_mtime_ms.has_value()) {
    req.set_ufs_mtime_ms(*ufs_mtime_ms);
  }
  proto::CompleteFileResponse resp;
  auto invoke = [&](const std::string& address) {
    auto channel = pool_->GetChannel(address);
    if (!channel) {
      return grpc::Status(grpc::StatusCode::UNAVAILABLE,
                          "failed to get channel for master");
    }
    fluxcache::proto::MasterService::Stub stub(channel);
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() +
                     std::chrono::seconds(deadline));
    return stub.CompleteFile(&ctx, req, &resp);
  };

  auto grpc_status = invoke(GetMasterAddress());
  if (!grpc_status.ok() && TryFollowLeaderHint(grpc_status)) {
    grpc_status = invoke(GetMasterAddress());
  }

  if (grpc_status.ok()) {
    if (circuit_breaker_) circuit_breaker_->RecordSuccess();
    return Status::OK();
  }

  if (circuit_breaker_) circuit_breaker_->RecordFailure();
  if (grpc_status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED ||
      grpc_status.error_code() == grpc::StatusCode::UNAVAILABLE) {
    return Status::Unavailable(grpc_status.error_message().c_str());
  }
  if (grpc_status.error_code() == grpc::StatusCode::NOT_FOUND) {
    return Status::NotFound(grpc_status.error_message().c_str());
  }
  return Status::IOError(grpc_status.error_message().c_str());
}

Status MasterClient::DeleteFile(const std::string& path) {
  if (circuit_breaker_ && !circuit_breaker_->AllowRequest()) {
    return Status::Unavailable("circuit breaker open");
  }
  int deadline = resilience_config_.TimeoutSec(OpType::kMasterWrite);
  proto::DeleteFileRequest req;
  req.set_path(path);
  proto::DeleteFileResponse resp;
  auto invoke = [&](const std::string& address) {
    auto channel = pool_->GetChannel(address);
    if (!channel) {
      return grpc::Status(grpc::StatusCode::UNAVAILABLE,
                          "failed to get channel for master");
    }
    fluxcache::proto::MasterService::Stub stub(channel);
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() +
                     std::chrono::seconds(deadline));
    return stub.DeleteFile(&ctx, req, &resp);
  };

  auto grpc_status = invoke(GetMasterAddress());
  if (!grpc_status.ok() && TryFollowLeaderHint(grpc_status)) {
    grpc_status = invoke(GetMasterAddress());
  }

  if (grpc_status.ok()) {
    if (circuit_breaker_) circuit_breaker_->RecordSuccess();
    return Status::OK();
  }

  if (circuit_breaker_) circuit_breaker_->RecordFailure();
  if (grpc_status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED ||
      grpc_status.error_code() == grpc::StatusCode::UNAVAILABLE) {
    return Status::Unavailable(grpc_status.error_message().c_str());
  }
  if (grpc_status.error_code() == grpc::StatusCode::NOT_FOUND) {
    return Status::NotFound(grpc_status.error_message().c_str());
  }
  if (grpc_status.error_code() == grpc::StatusCode::UNIMPLEMENTED) {
    return Status::Unavailable("DeleteFile not implemented on server");
  }
  return Status::IOError(grpc_status.error_message().c_str());
}

Status MasterClient::Mount(const std::string& path,
                           const std::string& ufs_uri) {
  if (circuit_breaker_ && !circuit_breaker_->AllowRequest()) {
    return Status::Unavailable("circuit breaker open");
  }
  int deadline = resilience_config_.TimeoutSec(OpType::kMasterWrite);
  proto::MountRequest req;
  req.set_path(path);
  req.set_ufs_uri(ufs_uri);
  proto::MountResponse resp;
  auto invoke = [&](const std::string& address) {
    auto channel = pool_->GetChannel(address);
    if (!channel) {
      return grpc::Status(grpc::StatusCode::UNAVAILABLE,
                          "failed to get channel for master");
    }
    fluxcache::proto::MasterService::Stub stub(channel);
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() +
                     std::chrono::seconds(deadline));
    return stub.Mount(&ctx, req, &resp);
  };

  auto grpc_status = invoke(GetMasterAddress());
  if (!grpc_status.ok() && TryFollowLeaderHint(grpc_status)) {
    grpc_status = invoke(GetMasterAddress());
  }
  if (!grpc_status.ok()) {
    if (circuit_breaker_) circuit_breaker_->RecordFailure();
    if (grpc_status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED ||
        grpc_status.error_code() == grpc::StatusCode::UNAVAILABLE) {
      return Status::Unavailable(grpc_status.error_message().c_str());
    }
    return Status::IOError(grpc_status.error_message().c_str());
  }
  if (circuit_breaker_) circuit_breaker_->RecordSuccess();
  return Status::OK();
}

Status MasterClient::Unmount(const std::string& path) {
  if (circuit_breaker_ && !circuit_breaker_->AllowRequest()) {
    return Status::Unavailable("circuit breaker open");
  }
  int deadline = resilience_config_.TimeoutSec(OpType::kMasterWrite);
  proto::UnmountRequest req;
  req.set_path(path);
  proto::UnmountResponse resp;
  auto invoke = [&](const std::string& address) {
    auto channel = pool_->GetChannel(address);
    if (!channel) {
      return grpc::Status(grpc::StatusCode::UNAVAILABLE,
                          "failed to get channel for master");
    }
    fluxcache::proto::MasterService::Stub stub(channel);
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() +
                     std::chrono::seconds(deadline));
    return stub.Unmount(&ctx, req, &resp);
  };

  auto grpc_status = invoke(GetMasterAddress());
  if (!grpc_status.ok() && TryFollowLeaderHint(grpc_status)) {
    grpc_status = invoke(GetMasterAddress());
  }
  if (!grpc_status.ok()) {
    if (circuit_breaker_) circuit_breaker_->RecordFailure();
    if (grpc_status.error_code() == grpc::StatusCode::NOT_FOUND) {
      return Status::NotFound(grpc_status.error_message().c_str());
    }
    if (grpc_status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED ||
        grpc_status.error_code() == grpc::StatusCode::UNAVAILABLE) {
      return Status::Unavailable(grpc_status.error_message().c_str());
    }
    return Status::IOError(grpc_status.error_message().c_str());
  }
  if (circuit_breaker_) circuit_breaker_->RecordSuccess();
  return Status::OK();
}

StatusOr<std::vector<std::string>> MasterClient::ListMounts() {
  if (circuit_breaker_ && !circuit_breaker_->AllowRequest()) {
    return Status::Unavailable("circuit breaker open");
  }
  int deadline = resilience_config_.TimeoutSec(OpType::kMasterRead);
  auto channel = pool_->GetChannel(GetMasterAddress());
  if (!channel) {
    if (circuit_breaker_) circuit_breaker_->RecordFailure();
    return Status::Unavailable("failed to get channel for master");
  }

  fluxcache::proto::MasterService::Stub stub(channel);
  grpc::ClientContext ctx;
  ctx.set_deadline(std::chrono::system_clock::now() +
                   std::chrono::seconds(deadline));

  proto::ListMountsRequest req;
  proto::ListMountsResponse resp;

  auto grpc_status = stub.ListMounts(&ctx, req, &resp);
  if (!grpc_status.ok()) {
    if (circuit_breaker_) circuit_breaker_->RecordFailure();
    if (grpc_status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED ||
        grpc_status.error_code() == grpc::StatusCode::UNAVAILABLE) {
      return Status::Unavailable(grpc_status.error_message().c_str());
    }
    return Status::IOError(grpc_status.error_message().c_str());
  }

  if (circuit_breaker_) circuit_breaker_->RecordSuccess();
  std::vector<std::string> paths;
  paths.reserve(resp.paths_size());
  for (int i = 0; i < resp.paths_size(); ++i) {
    paths.push_back(resp.paths(i));
  }
  return paths;
}

StatusOr<uint64_t> MasterClient::RegisterWorker(const std::string& host,
                                                uint16_t port) {
  if (circuit_breaker_ && !circuit_breaker_->AllowRequest()) {
    return Status::Unavailable("circuit breaker open");
  }
  int deadline = resilience_config_.TimeoutSec(OpType::kMasterWrite);
  proto::RegisterWorkerRequest req;
  req.mutable_endpoint()->set_host(host);
  req.mutable_endpoint()->set_port(port);
  proto::RegisterWorkerResponse resp;
  auto invoke = [&](const std::string& address) {
    auto channel = pool_->GetChannel(address);
    if (!channel) {
      return grpc::Status(grpc::StatusCode::UNAVAILABLE,
                          "failed to get channel for master");
    }
    fluxcache::proto::MasterService::Stub stub(channel);
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() +
                     std::chrono::seconds(deadline));
    return stub.RegisterWorker(&ctx, req, &resp);
  };

  auto grpc_status = invoke(GetMasterAddress());
  if (!grpc_status.ok() && TryFollowLeaderHint(grpc_status)) {
    grpc_status = invoke(GetMasterAddress());
  }
  if (!grpc_status.ok()) {
    if (circuit_breaker_) circuit_breaker_->RecordFailure();
    if (grpc_status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED ||
        grpc_status.error_code() == grpc::StatusCode::UNAVAILABLE) {
      return Status::Unavailable(grpc_status.error_message().c_str());
    }
    return Status::IOError(grpc_status.error_message().c_str());
  }
  if (circuit_breaker_) circuit_breaker_->RecordSuccess();
  return resp.worker_id();
}

}  // namespace fluxcache
