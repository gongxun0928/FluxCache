add_library(fluxcache_common STATIC
  hash_ring.cpp
  status.cpp
  config/config.cpp
  metrics/metrics_registry.cpp
  metrics/slow_request_tracker.cpp
  metrics/http_metrics_server.cpp
  rpc/channel_pool.cpp
  rpc/retry_policy.cpp
  rpc/resilience_config.cpp
  rpc/circuit_breaker.cpp
)
target_include_directories(fluxcache_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(fluxcache_common PUBLIC
  yaml-cpp::yaml-cpp
  gRPC::grpc++
)
