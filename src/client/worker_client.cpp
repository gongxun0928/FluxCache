#include "client/worker_client.h"
#include "worker.grpc.pb.h"
#include <grpcpp/client_context.h>
#include <chrono>

namespace fluxcache {

WorkerClient::WorkerClient(ChannelPool* pool,
                          const std::string& worker_address, int deadline_sec,
                          const RetryPolicy& retry_policy)
    : pool_(pool),
      worker_address_(worker_address),
      deadline_sec_(deadline_sec),
      retry_policy_(retry_policy) {}

namespace {

Status DoReadPages(fluxcache::ChannelPool* pool,
                  const std::string& worker_address, int deadline_sec,
                  const fluxcache::proto::ReadPagesRequest& request,
                  fluxcache::proto::ReadPagesResponse* response) {
  auto channel = pool->GetChannel(worker_address);
  if (!channel) {
    return fluxcache::Status::Unavailable("failed to get channel for worker");
  }

  fluxcache::proto::WorkerService::Stub stub(channel);
  grpc::ClientContext ctx;
  ctx.set_deadline(std::chrono::system_clock::now() +
                   std::chrono::seconds(deadline_sec));

  auto grpc_status = stub.ReadPages(&ctx, request, response);

  if (grpc_status.ok()) {
    return fluxcache::Status::OK();
  }
  if (grpc_status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED ||
      grpc_status.error_code() == grpc::StatusCode::UNAVAILABLE) {
    return fluxcache::Status::Unavailable(grpc_status.error_message().c_str());
  }
  return fluxcache::Status::IOError(grpc_status.error_message().c_str());
}

}  // namespace

Status WorkerClient::ReadPages(const proto::ReadPagesRequest& request,
                               proto::ReadPagesResponse* response) {
  RetryPolicy p = retry_policy_;
  p.is_idempotent = true;
  return ExecuteWithRetry(p, [this, &request, response]() {
    return DoReadPages(pool_, worker_address_, deadline_sec_, request,
                       response);
  });
}

Status WorkerClient::WritePages(const proto::WritePagesRequest& request,
                                proto::WritePagesResponse* response) {
  auto channel = pool_->GetChannel(worker_address_);
  if (!channel) {
    return Status::Unavailable("failed to get channel for worker");
  }

  fluxcache::proto::WorkerService::Stub stub(channel);
  grpc::ClientContext ctx;
  ctx.set_deadline(std::chrono::system_clock::now() +
                   std::chrono::seconds(deadline_sec_));

  auto grpc_status = stub.WritePages(&ctx, request, response);

  if (grpc_status.ok()) {
    return Status::OK();
  }
  if (grpc_status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED ||
      grpc_status.error_code() == grpc::StatusCode::UNAVAILABLE) {
    return Status::Unavailable(grpc_status.error_message().c_str());
  }
  return Status::IOError(grpc_status.error_message().c_str());
}

}  // namespace fluxcache
