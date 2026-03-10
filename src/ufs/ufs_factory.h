#pragma once

#include "ufs/ufs.h"

#include <memory>
#include <string>

namespace fluxcache {

Status CreateUFS(const std::string& scheme, const std::string& authority,
                 std::unique_ptr<UFS>* out);

}  // namespace fluxcache
