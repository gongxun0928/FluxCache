// P2-09: C++ SDK MVP example.
// Demonstrates Create -> Open -> Write -> Read -> Stat -> Delete.
// Usage: sdk_example <master_host> <master_port> [path]
// Example: sdk_example 127.0.0.1 9090 /mnt/test.txt

#include "client/sdk/fluxcache_sdk.h"
#include "client/sdk/types.h"
#include <iostream>
#include <string>

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "Usage: " << argv[0] << " <master_host> <master_port> [path]\n"
              << "Example: " << argv[0] << " 127.0.0.1 9090 /mnt/test.txt\n";
    return 1;
  }

  fluxcache::SDKConfig config;
  config.master_host = argv[1];
  config.master_port = static_cast<uint16_t>(std::stoi(argv[2]));
  std::string path = argc >= 4 ? argv[3] : "/mnt/sdk_example.txt";

  auto sdk_result = fluxcache::FluxCacheSDK::Create(config);
  if (!sdk_result.ok()) {
    std::cerr << "SDK Create failed: " << sdk_result.status().message() << "\n";
    return 1;
  }
  auto sdk = std::move(sdk_result.value());

  // Create
  auto create_status = sdk->Create(path);
  if (!create_status.ok()) {
    if (create_status.code() == fluxcache::StatusCode::kAlreadyExists) {
      std::cout << "File already exists, continuing...\n";
    } else {
      std::cerr << "Create failed: " << create_status.message() << "\n";
      return 1;
    }
  } else {
    std::cout << "Created " << path << "\n";
  }

  // Open
  auto open_result = sdk->Open(path, fluxcache::OpenMode::kReadWrite);
  if (!open_result.ok()) {
    std::cerr << "Open failed: " << open_result.status().message() << "\n";
    return 1;
  }
  auto handle = std::move(open_result.value());
  std::cout << "Opened " << path << "\n";

  // Write
  const std::string data = "Hello, FluxCache SDK!";
  auto write_status = handle->Write(0, data);
  if (!write_status.ok()) {
    std::cerr << "Write failed: " << write_status.message() << "\n";
    return 1;
  }
  std::cout << "Wrote " << data.size() << " bytes\n";

  // Close before Read (optional; we can also read through handle)
  handle->Close();
  handle.reset();

  // Re-open for read
  open_result = sdk->Open(path, fluxcache::OpenMode::kReadOnly);
  if (!open_result.ok()) {
    std::cerr << "Re-open failed: " << open_result.status().message() << "\n";
    return 1;
  }
  handle = std::move(open_result.value());

  // Read
  char buf[256] = {};
  auto read_result = handle->Read(buf, 0, sizeof(buf));
  if (!read_result.ok()) {
    std::cerr << "Read failed: " << read_result.status().message() << "\n";
    return 1;
  }
  std::cout << "Read " << read_result.value() << " bytes: "
            << std::string(buf, read_result.value()) << "\n";

  handle->Close();
  handle.reset();

  // Stat
  auto stat_result = sdk->Stat(path);
  if (!stat_result.ok()) {
    std::cerr << "Stat failed: " << stat_result.status().message() << "\n";
    return 1;
  }
  const auto& fi = stat_result.value();
  std::cout << "Stat: inode=" << fi.inode_id << " size=" << fi.size
            << " is_dir=" << fi.is_directory << " mtime_ms=" << fi.ufs_mtime_ms
            << "\n";

  // Delete (server may not implement DeleteFile yet in MVP)
  auto delete_status = sdk->Delete(path);
  if (delete_status.ok()) {
    std::cout << "Deleted " << path << "\n";
  } else {
    std::cout << "Delete skipped (server may not implement yet): "
              << delete_status.message() << "\n";
  }

  return 0;
}
