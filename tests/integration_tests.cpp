#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include "gtest/gtest.h"
#include "httplib.h"
#include "http_api.hpp"
#include "service.hpp"
#include "sqlite_repository.hpp"
#include "nlohmann/json.hpp"

TEST(IntegrationTests, IngestAggregateAndUnknownSensorHandling) {

  const std::string db_path = "/tmp/info_getter_integration.db";
  std::remove(db_path.c_str());

  info_getter::SqliteWeatherRepository repo(db_path);
  std::string error;
  ASSERT_TRUE(repo.init_schema(&error));
  info_getter::WeatherService service(repo);

  httplib::Server server;
  info_getter::HttpApi api(server, service);
  api.register_routes();

  const int port = 19090;
  std::thread server_thread([&]() {
    server.listen("127.0.0.1", port);
  });

  httplib::Client client("127.0.0.1", port);
  client.set_connection_timeout(2, 0);
  client.set_read_timeout(2, 0);

  // Wait for the server to start accepting connections (retry with backoff).
  bool server_ready = false;
  for (int attempt = 0; attempt < 20; ++attempt) {
    auto probe = client.Get("/health_probe_nonexistent");
    if (probe) {
      server_ready = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  ASSERT_TRUE(server_ready);

  const std::string ingest_body =
      R"({"sensorId":"sensor-1","observedAt":"2026-03-21T00:00:00Z","metrics":{"temperature":21.0,"humidity":50.0,"windSpeed":5.0}})";
  const auto ingest_response = client.Post("/api/v1/readings", ingest_body, "application/json");
  ASSERT_TRUE(static_cast<bool>(ingest_response));
  if (ingest_response) {
    EXPECT_EQ(ingest_response->status, 201);
  }

  const std::string query_body =
      R"({"sensorIds":["sensor-1"],"metrics":["temperature","humidity"],"aggregations":["avg"],"dateRange":{"from":"2026-03-20T00:00:00Z","to":"2026-03-21T00:00:00Z"}})";
  const auto query_response = client.Post("/api/v1/queries/aggregate", query_body, "application/json");
  ASSERT_TRUE(static_cast<bool>(query_response));
  if (query_response) {
    EXPECT_EQ(query_response->status, 200);
    const auto json = nlohmann::json::parse(query_response->body);
    EXPECT_EQ(json["window"]["mode"].get<std::string>(), "explicit");
    EXPECT_EQ(static_cast<int>(json["results"].size()), 2);
  }

  const std::string bad_sensor_query_body =
      R"({"sensorIds":["sensor-404"],"metrics":["temperature"],"aggregations":["avg"],"dateRange":{"from":"2026-03-20T00:00:00Z","to":"2026-03-21T00:00:00Z"}})";
  const auto bad_sensor_response = client.Post("/api/v1/queries/aggregate", bad_sensor_query_body, "application/json");
  ASSERT_TRUE(static_cast<bool>(bad_sensor_response));
  if (bad_sensor_response) {
    EXPECT_EQ(bad_sensor_response->status, 400);
    const auto json = nlohmann::json::parse(bad_sensor_response->body);
    EXPECT_EQ(json["error"]["code"].get<std::string>(), "INVALID_SENSOR_IDS");
  }

  server.stop();
  server_thread.join();
}
