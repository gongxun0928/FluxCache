#include "worker/registration_util.h"

namespace fluxcache {

std::optional<std::string> ExtractLeaderAddress(const std::string& message) {
  const std::string marker = "leader=";
  size_t pos = message.find(marker);
  if (pos == std::string::npos) return std::nullopt;
  pos += marker.size();
  if (pos >= message.size()) return std::nullopt;

  size_t end = pos;
  while (end < message.size() && message[end] != ';' && message[end] != ' ' &&
         message[end] != '\n' && message[end] != '\r') {
    ++end;
  }
  if (end == pos) return std::nullopt;
  return message.substr(pos, end - pos);
}

}  // namespace fluxcache
