// P1-12: CLI smoke tool for FluxCache.
// Subcommands: mount, unmount, ls-mounts, read, write, stat.
// Usage: fluxcache-cli --config <path> <subcommand> [args...]

#include "client/fluxcache_client.h"
#include "common/config/config.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

void PrintUsage(const char* prog) {
  std::cerr << "Usage: " << prog << " --config <config.yaml> <subcommand> [args...]\n"
            << "Subcommands:\n"
            << "  mount <path> <ufs_uri>     Mount UFS at path\n"
            << "  unmount <path>             Unmount path\n"
            << "  ls-mounts                  List all mount points\n"
            << "  read <path> [offset] [size]  Read file (default offset=0, size=all)\n"
            << "  write <path> <data>         Write data to file at offset 0\n"
            << "  stat <path>                Show file metadata (size, mtime, inode)\n";
}

int RunMount(fluxcache::FluxCacheClient& client, int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "mount requires <path> <ufs_uri>\n";
    return 1;
  }
  std::string path = argv[0];
  std::string ufs_uri = argv[1];
  auto s = client.GetMasterClient()->Mount(path, ufs_uri);
  if (!s.ok()) {
    std::cerr << "mount failed: " << s.message() << "\n";
    return 1;
  }
  std::cout << "mounted " << ufs_uri << " at " << path << "\n";
  return 0;
}

int RunUnmount(fluxcache::FluxCacheClient& client, int argc, char** argv) {
  if (argc < 1) {
    std::cerr << "unmount requires <path>\n";
    return 1;
  }
  std::string path = argv[0];
  auto s = client.GetMasterClient()->Unmount(path);
  if (!s.ok()) {
    std::cerr << "unmount failed: " << s.message() << "\n";
    return 1;
  }
  std::cout << "unmounted " << path << "\n";
  return 0;
}

int RunLsMounts(fluxcache::FluxCacheClient& client) {
  auto result = client.GetMasterClient()->ListMounts();
  if (!result.ok()) {
    std::cerr << "ls-mounts failed: " << result.status().message() << "\n";
    return 1;
  }
  for (const auto& p : result.value()) {
    std::cout << p << "\n";
  }
  return 0;
}

int RunRead(fluxcache::FluxCacheClient& client, int argc, char** argv) {
  if (argc < 1) {
    std::cerr << "read requires <path> [offset] [size]\n";
    return 1;
  }
  std::string path = argv[0];
  uint64_t offset = 0;
  uint64_t size = UINT64_MAX;  // read all by default
  if (argc >= 2) {
    try {
      offset = std::stoull(argv[1]);
    } catch (...) {
      std::cerr << "invalid offset\n";
      return 1;
    }
  }
  if (argc >= 3) {
    try {
      size = std::stoull(argv[2]);
    } catch (...) {
      std::cerr << "invalid size\n";
      return 1;
    }
  }
  auto result = client.Read(path, offset, size);
  if (!result.ok()) {
    std::cerr << "read failed: " << result.status().message() << "\n";
    return 1;
  }
  std::cout << result.value();
  return 0;
}

int RunWrite(fluxcache::FluxCacheClient& client, int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "write requires <path> <data>\n";
    return 1;
  }
  std::string path = argv[0];
  std::string data = argv[1];
  auto s = client.Write(path, 0, data);
  if (!s.ok()) {
    std::cerr << "write failed: " << s.message() << "\n";
    return 1;
  }
  std::cout << "wrote " << data.size() << " bytes to " << path << "\n";
  return 0;
}

int RunStat(fluxcache::FluxCacheClient& client, int argc, char** argv) {
  if (argc < 1) {
    std::cerr << "stat requires <path>\n";
    return 1;
  }
  std::string path = argv[0];
  auto result = client.GetMasterClient()->GetFileInfo(path);
  if (!result.ok()) {
    std::cerr << "stat failed: " << result.status().message() << "\n";
    return 1;
  }
  const auto& fi = result.value().file_info();
  std::cout << "path: " << path << "\n"
            << "inode_id: " << fi.inode_id() << "\n"
            << "size: " << fi.size() << "\n"
            << "is_directory: " << (fi.is_directory() ? "true" : "false") << "\n"
            << "ufs_mtime_ms: " << fi.ufs_mtime_ms() << "\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    PrintUsage(argv[0]);
    return 1;
  }
  if (std::string(argv[1]) != "--config") {
    std::cerr << "missing --config <path>\n";
    PrintUsage(argv[0]);
    return 1;
  }
  std::string config_path = argv[2];
  int subcmd_idx = 3;
  if (subcmd_idx >= argc) {
    std::cerr << "missing subcommand\n";
    PrintUsage(argv[0]);
    return 1;
  }
  std::string subcmd = argv[subcmd_idx];
  int subcmd_argc = argc - subcmd_idx - 1;
  char** subcmd_argv = subcmd_argc > 0 ? &argv[subcmd_idx + 1] : nullptr;

  auto cfg_result = fluxcache::LoadConfig(config_path);
  if (!cfg_result.ok()) {
    std::cerr << "config load failed: " << cfg_result.status().message() << "\n";
    return 1;
  }

  fluxcache::FluxCacheClient client(cfg_result.value().client);

  if (subcmd == "mount") {
    return RunMount(client, subcmd_argc, subcmd_argv);
  }
  if (subcmd == "unmount") {
    return RunUnmount(client, subcmd_argc, subcmd_argv);
  }
  if (subcmd == "ls-mounts") {
    return RunLsMounts(client);
  }
  if (subcmd == "read") {
    return RunRead(client, subcmd_argc, subcmd_argv);
  }
  if (subcmd == "write") {
    return RunWrite(client, subcmd_argc, subcmd_argv);
  }
  if (subcmd == "stat") {
    return RunStat(client, subcmd_argc, subcmd_argv);
  }

  std::cerr << "unknown subcommand: " << subcmd << "\n";
  PrintUsage(argv[0]);
  return 1;
}
