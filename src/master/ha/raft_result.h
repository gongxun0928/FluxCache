#pragma once

#ifdef FLUXCACHE_ENABLE_RAFT

#include "common/status.h"
#include "libnuraft/nuraft.hxx"
#include <optional>

namespace fluxcache {

struct RaftApplyResult {
  StatusCode code = StatusCode::kOk;
  std::optional<uint64_t> value;
};

inline nuraft::ptr<nuraft::buffer> SerializeRaftApplyResult(
    StatusCode code, std::optional<uint64_t> value = std::nullopt) {
  const size_t size = 1 + (value.has_value() ? sizeof(uint64_t) : 0);
  auto result = nuraft::buffer::alloc(size);
  nuraft::buffer_serializer bs(result);
  bs.put_u8(static_cast<uint8_t>(code));
  if (value.has_value()) bs.put_u64(*value);
  return result;
}

inline std::optional<RaftApplyResult> ParseRaftApplyResult(
    const nuraft::ptr<nuraft::buffer>& buffer, bool has_value = false) {
  if (!buffer) return std::nullopt;
  auto copy = buffer;
  copy->pos(0);
  nuraft::buffer_serializer bs(copy);

  RaftApplyResult result;
  result.code = static_cast<StatusCode>(bs.get_u8());
  if (has_value) result.value = bs.get_u64();
  return result;
}

}  // namespace fluxcache

#endif  // FLUXCACHE_ENABLE_RAFT
