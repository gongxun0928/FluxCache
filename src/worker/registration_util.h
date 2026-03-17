#pragma once

#include <optional>
#include <string>

namespace fluxcache {

std::optional<std::string> ExtractLeaderAddress(const std::string& message);

}  // namespace fluxcache
