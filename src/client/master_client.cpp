#include "client/master_client.h"
#include "master.grpc.pb.h"
#include <grpcpp/client_context.h>
#include <chrono>

namespace fluxcache {

MasterClient::MasterClient(ChannelPool* pool, const std::string& master_address,
                           int deadline_sec, const RetryPolicy& retry_policy)
    : pool_(pool),
      master_address_(master_address),
      deadline_sec_(deadline_sec),
      retry_policy_(retry_policy) {}

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
  RetryPolicy p = retry_policy_;
  p.is_idempotent = true;
  return ExecuteWithRetry<proto::GetHashRingResponse>(
      p, [this]() { return DoGetHashRing(pool_, master_address_, deadline_sec_); });
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
  RetryPolicy p = retry_policy_;
  p.is_idempotent = true;
  return ExecuteWithRetry<proto::GetFileInfoResponse>(p, [this, path]() {
    return DoGetFileInfo(pool_, master_address_, deadline_sec_, path);
  });
}

StatusOr<proto::CreateFileResponse> MasterClient::CreateFile(
    const std::string& path) {
  auto channel = pool_->GetChannel(master_address_);
  if (!channel) {
    return Status::Unavailable("failed to get channel for master");
  }

  fluxcache::proto::MasterService::Stub stub(channel);
  grpc::ClientContext ctx;
  ctx.set_deadline(std::chrono::system_clock::now() +
                   std::chrono::seconds(deadline_sec_));

  proto::CreateFileRequest req;
  req.set_path(path);
  proto::CreateFileResponse resp;

  auto grpc_status = stub.CreateFile(&ctx, req, &resp);

  if (grpc_status.ok()) {
    return resp;
  }

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
  auto channel = pool_->GetChannel(master_address_);
  if (!channel) {
    return Status::Unavailable("failed to get channel for master");
  }

  fluxcache::proto::MasterService::Stub stub(channel);
  grpc::ClientContext ctx;
  ctx.set_deadline(std::chrono::system_clock::now() +
                   std::chrono::seconds(deadline_sec_));

  proto::CompleteFileRequest req;
  req.set_inode_id(inode_id);
  req.set_size(size);
  if (ufs_mtime_ms.has_value()) {
    req.set_ufs_mtime_ms(*ufs_mtime_ms);
  }
  proto::CompleteFileResponse resp;

  auto grpc_status = stub.CompleteFile(&ctx, req, &resp);

  if (grpc_status.ok()) {
    return Status::OK();
  }

  if (grpc_status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED ||
      grpc_status.error_code() == grpc::StatusCode::UNAVAILABLE) {
    return Status::Unavailable(grpc_status.error_message().c_str());
  }
  if (grpc_status.error_code() == grpc::StatusCode::NOT_FOUND) {
    return Status::NotFound(grpc_status.error_message().c_str());
  }
  return Status::IOError(grpc_status.error_message().c_str());
}

Status MasterClient::Mount(const std::string& path,
                           const std::string& ufs_uri) {
  auto channel = pool_->GetChannel(master_address_);
  if (!channel) {
    return Status::Unavailable("failed to get channel for master");
  }

  fluxcache::proto::MasterService::Stub stub(channel);
  grpc::ClientContext ctx;
  ctx.set_deadline(std::chrono::system_clock::now() +
                   std::chrono::seconds(deadline_sec_));

  proto::MountRequest req;
  req.set_path(path);
  req.set_ufs_uri(ufs_uri);
  proto::MountResponse resp;

  auto grpc_status = stub.Mount(&ctx, req, &resp);
  if (!grpc_status.ok()) {
    return Status::IOError(grpc_status.error_message().c_str());
  }
  return Status::OK();
}

Status MasterClient::Unmount(const std::string& path) {
  auto channel = pool_->GetChannel(master_address_);
  if (!channel) {
    return Status::Unavailable("failed to get channel for master");
  }

  fluxcache::proto::MasterService::Stub stub(channel);
  grpc::ClientContext ctx;
  ctx.set_deadline(std::chrono::system_clock::now() +
                   std::chrono::seconds(deadline_sec_));

  proto::UnmountRequest req;
  req.set_path(path);
  proto::UnmountResponse resp;

  auto grpc_status = stub.Unmount(&ctx, req, &resp);
  if (!grpc_status.ok()) {
    if (grpc_status.error_code() == grpc::StatusCode::NOT_FOUND) {
      return Status::NotFound(grpc_status.error_message().c_str());
    }
    if (grpc_status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED ||
        grpc_status.error_code() == grpc::StatusCode::UNAVAILABLE) {
      return Status::Unavailable(grpc_status.error_message().c_str());
    }
    return Status::IOError(grpc_status.error_message().c_str());
  }
  return Status::OK();
}

StatusOr<std::vector<std::string>> MasterClient::ListMounts() {
  auto channel = pool_->GetChannel(master_address_);
  if (!channel) {
    return Status::Unavailable("failed to get channel for master");
  }

  fluxcache::proto::MasterService::Stub stub(channel);
  grpc::ClientContext ctx;
  ctx.set_deadline(std::chrono::system_clock::now() +
                   std::chrono::seconds(deadline_sec_));

  proto::ListMountsRequest req;
  proto::ListMountsResponse resp;

  auto grpc_status = stub.ListMounts(&ctx, req, &resp);
  if (!grpc_status.ok()) {
    if (grpc_status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED ||
        grpc_status.error_code() == grpc::StatusCode::UNAVAILABLE) {
      return Status::Unavailable(grpc_status.error_message().c_str());
    }
    return Status::IOError(grpc_status.error_message().c_str());
  }

  std::vector<std::string> paths;
  paths.reserve(resp.paths_size());
  for (int i = 0; i < resp.paths_size(); ++i) {
    paths.push_back(resp.paths(i));
  }
  return paths;
}

StatusOr<uint64_t> MasterClient::RegisterWorker(const std::string& host,
                                                uint16_t port) {
  auto channel = pool_->GetChannel(master_address_);
  if (!channel) {
    return Status::Unavailable("failed to get channel for master");
  }

  fluxcache::proto::MasterService::Stub stub(channel);
  grpc::ClientContext ctx;
  ctx.set_deadline(std::chrono::system_clock::now() +
                   std::chrono::seconds(deadline_sec_));

  proto::RegisterWorkerRequest req;
  req.mutable_endpoint()->set_host(host);
  req.mutable_endpoint()->set_port(port);
  proto::RegisterWorkerResponse resp;

  auto grpc_status = stub.RegisterWorker(&ctx, req, &resp);
  if (!grpc_status.ok()) {
    return Status::IOError(grpc_status.error_message().c_str());
  }
  return resp.worker_id();
}

}  // namespace fluxcache
