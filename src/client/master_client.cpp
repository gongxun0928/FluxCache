#include "client/master_client.h"
#include "master.grpc.pb.h"
#include <grpcpp/client_context.h>
#include <chrono>

namespace fluxcache {

MasterClient::MasterClient(ChannelPool* pool, const std::string& master_address,
                           int deadline_sec)
    : pool_(pool), master_address_(master_address), deadline_sec_(deadline_sec) {
}

StatusOr<proto::GetHashRingResponse> MasterClient::GetHashRing() {
  auto channel = pool_->GetChannel(master_address_);
  if (!channel) {
    return Status::Unavailable("failed to get channel for master");
  }

  fluxcache::proto::MasterService::Stub stub(channel);
  grpc::ClientContext ctx;
  ctx.set_deadline(std::chrono::system_clock::now() +
                   std::chrono::seconds(deadline_sec_));

  proto::GetHashRingRequest req;
  proto::GetHashRingResponse resp;

  auto grpc_status = stub.GetHashRing(&ctx, req, &resp);

  if (grpc_status.ok()) {
    return resp;
  }

  if (grpc_status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED ||
      grpc_status.error_code() == grpc::StatusCode::UNAVAILABLE) {
    return Status::Unavailable(grpc_status.error_message().c_str());
  }
  return Status::IOError(grpc_status.error_message().c_str());
}

StatusOr<proto::GetFileInfoResponse> MasterClient::GetFileInfo(
    const std::string& path) {
  auto channel = pool_->GetChannel(master_address_);
  if (!channel) {
    return Status::Unavailable("failed to get channel for master");
  }

  fluxcache::proto::MasterService::Stub stub(channel);
  grpc::ClientContext ctx;
  ctx.set_deadline(std::chrono::system_clock::now() +
                   std::chrono::seconds(deadline_sec_));

  proto::GetFileInfoRequest req;
  req.set_path(path);
  proto::GetFileInfoResponse resp;

  auto grpc_status = stub.GetFileInfo(&ctx, req, &resp);

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
