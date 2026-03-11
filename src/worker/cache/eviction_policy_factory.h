#pragma once

#include "worker/cache/eviction_policy.h"

#include <memory>

namespace fluxcache {

std::unique_ptr<EvictionPolicy> CreateEvictionPolicy(EvictionPolicyType type);

}  // namespace fluxcache
