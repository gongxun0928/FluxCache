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

}  // namespace fluxcache
