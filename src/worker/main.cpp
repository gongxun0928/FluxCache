#include "common/config/config.h"
#include "master.grpc.pb.h"
#include "worker/worker_server.h"
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
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
  std::cerr << "  config.yaml must have fluxcache root key with worker.port, "
               "worker.host, etc.\n";
}

// Returns host for WorkerEndpoint. When listen host is "0.0.0.0", use
// "127.0.0.1" for advertisement so Master can reach Worker locally.
std::string AdvertiseHost(const std::string& listen_host) {
  if (listen_host == "0.0.0.0" || listen_host.empty()) {
    return "127.0.0.1";
  }
  return listen_host;
}

void HeartbeatLoop(const fluxcache::FluxCacheConfig& cfg,
                   std::atomic<uint64_t>& worker_id) {
  std::string master_addr =
      cfg.master.host + ":" + std::to_string(cfg.master.port);
  auto channel =
      grpc::CreateChannel(master_addr, grpc::InsecureChannelCredentials());
  auto stub = fluxcache::proto::MasterService::NewStub(channel);

  std::string ep_host = AdvertiseHost(cfg.worker.host);
  uint32_t ep_port = cfg.worker.port;
  uint32_t interval_ms = cfg.worker.heartbeat_interval_ms;
  if (interval_ms == 0) {
    interval_ms = 5000;
  }

  while (!g_shutdown_requested.load(std::memory_order_relaxed)) {
    fluxcache::proto::RegisterWorkerRequest req;
    auto* ep = req.mutable_endpoint();
    ep->set_host(ep_host);
    ep->set_port(ep_port);
    uint64_t wid = worker_id.load(std::memory_order_relaxed);
    if (wid > 0) {
      ep->set_worker_id(wid);
    }

    fluxcache::proto::RegisterWorkerResponse resp;
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() +
                     std::chrono::seconds(5));
    auto status = stub->RegisterWorker(&ctx, req, &resp);

    if (status.ok()) {
      uint64_t new_id = resp.worker_id();
      worker_id.store(new_id, std::memory_order_relaxed);
    } else {
      if (status.error_code() == grpc::StatusCode::NOT_FOUND) {
        worker_id.store(0, std::memory_order_relaxed);
      }
      std::cerr << "[heartbeat] Master unreachable: " << status.error_message()
                << " (will retry)" << std::endl;
    }

    for (uint32_t i = 0; i < interval_ms && !g_shutdown_requested.load(std::memory_order_relaxed);
         i += 100) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
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
  if (cfg.worker.port == 0) {
    std::cerr << "Error: fluxcache.worker.port is required and must be > 0\n";
    return 1;
  }

  fluxcache::WorkerServer server(cfg.worker);

  if (!server.Start()) {
    std::cerr << "Error: failed to bind to " << cfg.worker.host << ":"
              << cfg.worker.port << "\n";
    return 1;
  }

  std::cerr << "Worker listening on " << cfg.worker.host << ":"
            << cfg.worker.port << "\n";

  InstallSignalHandlers();

  std::atomic<uint64_t> worker_id{0};
  std::thread heartbeat_thread(HeartbeatLoop, std::cref(cfg), std::ref(worker_id));

  while (!g_shutdown_requested.load(std::memory_order_relaxed)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  std::cerr << "Shutting down...\n";
  heartbeat_thread.join();
  server.Shutdown();
  std::cerr << "Worker stopped.\n";
  return 0;
}
