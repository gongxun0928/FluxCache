#include "worker/cache/eviction_policy_factory.h"
#include "worker/cache/lru_policy.h"

namespace fluxcache {

std::unique_ptr<EvictionPolicy> CreateEvictionPolicy(EvictionPolicyType type) {
  switch (type) {
    case EvictionPolicyType::kLRU:
      return std::make_unique<LruPolicy>();
    case EvictionPolicyType::kLFU:
      return nullptr;  // P2-04
    default:
      return nullptr;
  }
}

}  // namespace fluxcache
