// P2-06: FUSE mount entry point for FluxCache.
// Usage: fluxcache-fuse -o master=HOST -o port=PORT -o root=PATH mount_point
// Or: fluxcache-fuse --master HOST --port PORT --root PATH mount_point

#define FUSE_USE_VERSION 31

#include "client/fuse/fuse_ops.h"
#include "client/sdk/fluxcache_sdk.h"
#include "client/sdk/types.h"
#include <fuse.h>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

namespace {

struct FuseOptions {
  char master_host[256] = "";
  char port_str[32] = "";
  char root[256] = "/mnt";
  int show_help = 0;
};

#define OPT(t, m) \
  { t, offsetof(FuseOptions, m), 1 }

const struct fuse_opt fc_opt_spec[] = {
    FUSE_OPT_KEY("master=%s", 1),
    FUSE_OPT_KEY("port=%s", 2),
    FUSE_OPT_KEY("root=%s", 3),
    OPT("-h", show_help),
    OPT("--help", show_help),
    FUSE_OPT_END,
};

int fc_opt_proc(void* data, const char* arg, int key,
                struct fuse_args* outargs) {
  (void)outargs;
  auto* opts = static_cast<FuseOptions*>(data);
  if (!arg) return 1;
  if (key == 1) {
    std::strncpy(opts->master_host, arg, sizeof(opts->master_host) - 1);
    opts->master_host[sizeof(opts->master_host) - 1] = '\0';
    return 0;
  }
  if (key == 2) {
    std::strncpy(opts->port_str, arg, sizeof(opts->port_str) - 1);
    opts->port_str[sizeof(opts->port_str) - 1] = '\0';
    return 0;
  }
  if (key == 3) {
    std::strncpy(opts->root, arg, sizeof(opts->root) - 1);
    opts->root[sizeof(opts->root) - 1] = '\0';
    return 0;
  }
  return 1;
}

void show_help(const char* prog) {
  std::cerr << "Usage: " << prog
            << " [options] mount_point\n\n"
               "FluxCache FUSE options:\n"
               "  -o master=HOST    Master host (required)\n"
               "  -o port=PORT      Master port (required)\n"
               "  -o root=PATH      Logical root path (default: /mnt)\n"
               "  -h, --help        Show this help\n\n"
               "Example:\n"
               "  fluxcache-fuse -o master=127.0.0.1 -o port=9090 /tmp/fc\n";
}

}  // namespace

int main(int argc, char* argv[]) {
  FuseOptions opts;

  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
  if (fuse_opt_parse(&args, &opts, fc_opt_spec, fc_opt_proc) == -1) {
    return 1;
  }

  if (opts.show_help) {
    show_help(argv[0]);
    fuse_opt_add_arg(&args, "--help");
    args.argv[0][0] = '\0';
    struct fuse_operations empty_ops = {};
    fuse_main(args.argc, args.argv, &empty_ops, nullptr);
    fuse_opt_free_args(&args);
    return 0;
  }

  if (opts.master_host[0] == '\0') {
    std::cerr << "Error: master host required (-o master=HOST)\n";
    fuse_opt_free_args(&args);
    return 1;
  }
  uint16_t port = static_cast<uint16_t>(std::atoi(opts.port_str));
  if (port == 0) {
    std::cerr << "Error: master port required (-o port=PORT)\n";
    fuse_opt_free_args(&args);
    return 1;
  }

  fluxcache::SDKConfig config;
  config.master_host = opts.master_host;
  config.master_port = port;

  auto sdk_result = fluxcache::FluxCacheSDK::Create(config);
  if (!sdk_result.ok()) {
    std::cerr << "SDK init failed: " << sdk_result.status().message() << "\n";
    fuse_opt_free_args(&args);
    return 1;
  }
  auto sdk = std::move(sdk_result.value());

  fluxcache::FuseContext ctx;
  ctx.root = opts.root[0] != '\0' ? opts.root : "/mnt";
  ctx.sdk = sdk.get();

  struct fuse_operations ops;
  fluxcache::RegisterFluxCacheFuseOps(&ops);

  int ret = fuse_main(args.argc, args.argv, &ops, &ctx);
  fuse_opt_free_args(&args);
  return ret;
}
