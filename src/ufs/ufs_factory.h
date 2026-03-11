#pragma once

#include "ufs/ufs.h"

#include <memory>
#include <string>

namespace fluxcache {

class FakeUfs;

Status CreateUFS(const std::string& scheme, const std::string& authority,
                 std::unique_ptr<UFS>* out);

// Test-only: register FakeUfs for "fake://<authority>". Must be called before
// CreateUFS("fake", authority).
void RegisterFakeUfsForTest(const std::string& authority,
                            std::unique_ptr<FakeUfs> ufs);

}  // namespace fluxcache
