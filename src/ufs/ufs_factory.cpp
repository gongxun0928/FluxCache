#include "ufs/ufs_factory.h"
#include "ufs/fake_ufs.h"
#include "ufs/local_ufs.h"

#include <mutex>
#include <unordered_map>

namespace fluxcache {

namespace {

std::mutex g_fake_registry_mu;
std::unordered_map<std::string, std::unique_ptr<FakeUfs>>* g_fake_registry =
    nullptr;

}  // namespace

void RegisterFakeUfsForTest(const std::string& authority,
                            std::unique_ptr<FakeUfs> ufs) {
  std::lock_guard<std::mutex> lock(g_fake_registry_mu);
  if (!g_fake_registry) g_fake_registry = new std::unordered_map<std::string, std::unique_ptr<FakeUfs>>();
  (*g_fake_registry)[authority] = std::move(ufs);
}

Status CreateUFS(const std::string& scheme, const std::string& authority,
                 std::unique_ptr<UFS>* out) {
  if (!out) return Status::InvalidArgument(nullptr);

  if (scheme == "local" || scheme == "file" || scheme == "localfs") {
    *out = std::make_unique<LocalUFS>(authority);
    return Status::OK();
  }
  if (scheme == "fake") {
    std::lock_guard<std::mutex> lock(g_fake_registry_mu);
    if (g_fake_registry) {
      auto it = g_fake_registry->find(authority);
      if (it != g_fake_registry->end()) {
        std::unique_ptr<UFS> clone = it->second->Clone();
        if (clone) {
          *out = std::move(clone);
          return Status::OK();
        }
      }
    }
    return Status::NotFound("CreateUFS: fake authority not registered");
  }
  return Status::InvalidArgument(nullptr);
}

}  // namespace fluxcache
