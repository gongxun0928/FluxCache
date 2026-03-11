#include "client/worker_client.h"
#include "worker.grpc.pb.h"
#include <grpcpp/client_context.h>
#include <chrono>

namespace fluxcache {

WorkerClient::WorkerClient(ChannelPool* pool,
                          const std::string& worker_address,
                          const ResilienceConfig& resilience_config,
                          const RetryPolicy& retry_policy,
                          CircuitBreaker* circuit_breaker)
    : pool_(pool),
      worker_address_(worker_address),
      resilience_config_(resilience_config),
      retry_policy_(retry_policy),
      circuit_breaker_(circuit_breaker) {}

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

Status DoBatchReadPages(fluxcache::ChannelPool* pool,
                        const std::string& worker_address, int deadline_sec,
                        const fluxcache::proto::BatchReadPagesRequest& request,
                        fluxcache::proto::BatchReadPagesResponse* response) {
  auto channel = pool->GetChannel(worker_address);
  if (!channel) {
    return fluxcache::Status::Unavailable("failed to get channel for worker");
  }

  fluxcache::proto::WorkerService::Stub stub(channel);
  grpc::ClientContext ctx;
  ctx.set_deadline(std::chrono::system_clock::now() +
                   std::chrono::seconds(deadline_sec));

  auto grpc_status = stub.BatchReadPages(&ctx, request, response);

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
  if (circuit_breaker_ && !circuit_breaker_->AllowRequest()) {
    return Status::Unavailable("circuit breaker open");
  }
  int deadline = resilience_config_.TimeoutSec(OpType::kWorkerRead);
  RetryPolicy p = retry_policy_;
  p.is_idempotent = true;
  Status s = ExecuteWithRetry(p, [this, &request, response, deadline]() {
    return DoReadPages(pool_, worker_address_, deadline, request, response);
  });
  if (circuit_breaker_) {
    s.ok() ? circuit_breaker_->RecordSuccess()
           : circuit_breaker_->RecordFailure();
  }
  return s;
}

Status WorkerClient::BatchReadPages(
    const proto::BatchReadPagesRequest& request,
    proto::BatchReadPagesResponse* response) {
  if (circuit_breaker_ && !circuit_breaker_->AllowRequest()) {
    return Status::Unavailable("circuit breaker open");
  }
  int deadline = resilience_config_.TimeoutSec(OpType::kWorkerRead);
  RetryPolicy p = retry_policy_;
  p.is_idempotent = true;
  Status s = ExecuteWithRetry(p, [this, &request, response, deadline]() {
    return DoBatchReadPages(pool_, worker_address_, deadline, request,
                            response);
  });
  if (circuit_breaker_) {
    s.ok() ? circuit_breaker_->RecordSuccess()
           : circuit_breaker_->RecordFailure();
  }
  return s;
}

Status WorkerClient::WritePages(const proto::WritePagesRequest& request,
                                proto::WritePagesResponse* response) {
  if (circuit_breaker_ && !circuit_breaker_->AllowRequest()) {
    return Status::Unavailable("circuit breaker open");
  }
  int deadline = resilience_config_.TimeoutSec(OpType::kWorkerWrite);
  auto channel = pool_->GetChannel(worker_address_);
  if (!channel) {
    if (circuit_breaker_) circuit_breaker_->RecordFailure();
    return Status::Unavailable("failed to get channel for worker");
  }

  fluxcache::proto::WorkerService::Stub stub(channel);
  grpc::ClientContext ctx;
  ctx.set_deadline(std::chrono::system_clock::now() +
                   std::chrono::seconds(deadline));

  auto grpc_status = stub.WritePages(&ctx, request, response);

  if (grpc_status.ok()) {
    if (circuit_breaker_) circuit_breaker_->RecordSuccess();
    return Status::OK();
  }
  if (circuit_breaker_) circuit_breaker_->RecordFailure();
  if (grpc_status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED ||
      grpc_status.error_code() == grpc::StatusCode::UNAVAILABLE) {
    return Status::Unavailable(grpc_status.error_message().c_str());
  }
  return Status::IOError(grpc_status.error_message().c_str());
}

}  // namespace fluxcache
