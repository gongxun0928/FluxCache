#include "common/config/config.h"
#include "master/master_server.h"
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {

std::atomic<bool> g_shutdown_requested{false};

void SignalHandler(int sig) {
  (void)sig;
  g_shutdown_requested.store(true);
}

void InstallSignalHandlers() {
  std::signal(SIGINT, SignalHandler);
  std::signal(SIGTERM, SignalHandler);
}

void PrintUsage(const char* prog) {
  std::cerr << "Usage: " << prog << " --config <config.yaml>\n";
  std::cerr << "  config.yaml must have fluxcache root key with master.host, master.port, etc.\n";
}

}  // namespace

int main(int argc, char* argv[]) {
  std::string config_path;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--config" && i + 1 < argc) {
      config_path = argv[++i];
      break;
    }
  }

  if (config_path.empty()) {
    std::cerr << "Error: --config <path> is required\n";
    PrintUsage(argv[0]);
    return 1;
  }

  auto result = fluxcache::LoadConfig(config_path);
  if (!result.ok()) {
    std::cerr << "Error loading config: " << result.status().message() << "\n";
    return 1;
  }

  const auto& cfg = result.value();
  fluxcache::MasterServer server(cfg.master);

  if (!server.Start()) {
    std::cerr << "Error: failed to bind to " << cfg.master.host << ":"
              << cfg.master.port << "\n";
    return 1;
  }

  std::cerr << "Master listening on " << cfg.master.host << ":"
            << cfg.master.port << "\n";

  InstallSignalHandlers();

  while (!g_shutdown_requested.load(std::memory_order_relaxed)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  std::cerr << "Shutting down...\n";
  server.Shutdown();
  std::cerr << "Master stopped.\n";
  return 0;
}
