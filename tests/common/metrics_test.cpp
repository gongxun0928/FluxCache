#include "common/metrics/http_metrics_server.h"
#include "common/metrics/metrics_registry.h"
#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <string>

namespace fluxcache {

TEST(MetricsRegistryTest, IncCounterAndExport) {
  MetricsRegistry reg;
  reg.IncCounter("master", "GetFileInfo");
  reg.IncCounter("master", "GetFileInfo");
  reg.IncCounter("master", "GetFileInfo");
  reg.IncCounter("worker", "ReadPages");

  std::string out = reg.ExportPrometheus();

  EXPECT_TRUE(out.find("# HELP fluxcache_rpc_requests_total") != std::string::npos);
  EXPECT_TRUE(out.find("# TYPE fluxcache_rpc_requests_total counter") !=
              std::string::npos);
  EXPECT_TRUE(out.find("fluxcache_rpc_requests_total{service=\"master\",method=\"GetFileInfo\"} 3") !=
              std::string::npos);
  EXPECT_TRUE(out.find("fluxcache_rpc_requests_total{service=\"worker\",method=\"ReadPages\"} 1") !=
              std::string::npos);
}

TEST(MetricsRegistryTest, ObserveLatencyAndExport) {
  MetricsRegistry reg;
  reg.ObserveLatency("master", "GetFileInfo", 0.05);
  reg.ObserveLatency("master", "GetFileInfo", 0.12);

  std::string out = reg.ExportPrometheus();

  EXPECT_TRUE(out.find("# HELP fluxcache_rpc_duration_seconds") != std::string::npos);
  EXPECT_TRUE(out.find("# TYPE fluxcache_rpc_duration_seconds histogram") !=
              std::string::npos);
  EXPECT_TRUE(out.find("fluxcache_rpc_duration_seconds_count{service=\"master\",method=\"GetFileInfo\"} 2") !=
              std::string::npos);
  EXPECT_TRUE(out.find("fluxcache_rpc_duration_seconds_sum{service=\"master\",method=\"GetFileInfo\"}") !=
              std::string::npos);
}

TEST(HttpMetricsServerTest, StartAndServeMetrics) {
  MetricsRegistry reg;
  reg.IncCounter("master", "GetHashRing");

  HttpMetricsServer server(0, &reg);
  // Port 0 means disabled - Start should succeed but not actually listen
  ASSERT_TRUE(server.Start());
  server.Shutdown();
}

TEST(HttpMetricsServerTest, ServeMetricsOnPort) {
  MetricsRegistry reg;
  reg.IncCounter("master", "GetFileInfo");

  // Use a high port to avoid conflicts
  const uint16_t port = 19998;
  HttpMetricsServer server(port, &reg);
  ASSERT_TRUE(server.Start());

  // Connect and request GET /metrics
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(fd, 0);

  struct sockaddr_in addr {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

  int ret = connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
  ASSERT_EQ(ret, 0);

  const char* req = "GET /metrics HTTP/1.1\r\nHost: localhost\r\n\r\n";
  send(fd, req, strlen(req), 0);

  char buf[4096];
  ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
  close(fd);

  server.Shutdown();

  ASSERT_GT(n, 0);
  buf[n] = '\0';
  std::string response(buf);

  EXPECT_TRUE(response.find("HTTP/1.1 200") != std::string::npos);
  EXPECT_TRUE(response.find("Content-Type: text/plain") != std::string::npos);
  EXPECT_TRUE(response.find("fluxcache_rpc_requests_total") != std::string::npos);
  EXPECT_TRUE(response.find("GetFileInfo") != std::string::npos);
}

TEST(HttpMetricsServerTest, NonMetricsPathReturns404) {
  MetricsRegistry reg;
  HttpMetricsServer server(19997, &reg);
  ASSERT_TRUE(server.Start());

  int fd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(fd, 0);

  struct sockaddr_in addr {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(19997);
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

  int ret = connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
  ASSERT_EQ(ret, 0);

  const char* req = "GET /other HTTP/1.1\r\nHost: localhost\r\n\r\n";
  send(fd, req, strlen(req), 0);

  char buf[512];
  ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
  close(fd);

  server.Shutdown();

  ASSERT_GT(n, 0);
  buf[n] = '\0';
  std::string response(buf);

  EXPECT_TRUE(response.find("404 Not Found") != std::string::npos);
}

}  // namespace fluxcache
