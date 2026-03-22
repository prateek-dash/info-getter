#include <cstdio>
#include <string>

#include "gtest/gtest.h"
#include "json_codec.hpp"
#include "service.hpp"
#include "sqlite_repository.hpp"
#include "time_utils.hpp"

namespace {

std::string temp_db_path(const char* file) {
  return std::string("/tmp/") + file;
}

TEST(UnitTests, DecodeReadingValidation) {
  const std::string bad_json = R"({"sensorId":"s1","observedAt":"invalid","metrics":{"temperature":1,"humidity":2,"windSpeed":3}})";
  const auto parsed = info_getter::json_codec::decode_reading_request(bad_json);
  ASSERT_FALSE(parsed.has_value());
  if (!parsed.has_value()) {
    EXPECT_EQ(parsed.error().code, "INVALID_OBSERVED_AT");
  }
}

TEST(UnitTests, DecodeAggregateRejectsUnknownMetric) {
  const std::string bad_query =
      R"({"metrics":["pressure"],"aggregations":["avg"]})";
  const auto parsed = info_getter::json_codec::decode_aggregate_request(bad_query);
  ASSERT_FALSE(parsed.has_value());
  if (!parsed.has_value()) {
    EXPECT_EQ(parsed.error().code, "INVALID_METRICS");
  }
}

TEST(UnitTests, ExplicitDateRangeBounds) {
  const auto db_path = temp_db_path("info_getter_unit_bounds.db");
  std::remove(db_path.c_str());

  info_getter::SqliteWeatherRepository repo(db_path);
  std::string error;
  ASSERT_TRUE(repo.init_schema(&error));
  info_getter::WeatherService service(repo);

  info_getter::AggregateQuery query;
  query.metrics = {info_getter::Metric::kTemperature};
  query.aggregations = {info_getter::Aggregation::kAvg};
  query.date_range = info_getter::DateRange{
      .from = "2026-03-01T00:00:00Z",
      .to = "2026-03-01T12:00:00Z",
  };

  const auto result = service.query_aggregate(query);
  ASSERT_FALSE(result.has_value());
  if (!result.has_value()) {
    EXPECT_EQ(result.error().code, "INVALID_DATE_RANGE");
  }
}

TEST(UnitTests, DefaultLatestWindowAndAvg) {
  const auto db_path = temp_db_path("info_getter_unit_latest.db");
  std::remove(db_path.c_str());

  info_getter::SqliteWeatherRepository repo(db_path);
  std::string error;
  ASSERT_TRUE(repo.init_schema(&error));
  info_getter::WeatherService service(repo);

  const info_getter::Reading r1{
      .sensor_id = "sensor-1",
      .observed_at = "2026-03-20T00:00:00Z",
      .temperature = 10,
      .humidity = 50,
      .wind_speed = 4,
  };
  const info_getter::Reading r2{
      .sensor_id = "sensor-1",
      .observed_at = "2026-03-21T00:00:00Z",
      .temperature = 20,
      .humidity = 60,
      .wind_speed = 8,
  };

  EXPECT_TRUE(service.ingest_reading(r1).has_value());
  EXPECT_TRUE(service.ingest_reading(r2).has_value());

  info_getter::AggregateQuery query;
  query.sensor_ids = {"sensor-1"};
  query.metrics = {info_getter::Metric::kTemperature, info_getter::Metric::kHumidity};
  query.aggregations = {info_getter::Aggregation::kAvg};

  const auto result = service.query_aggregate(query);
  ASSERT_TRUE(result.has_value());
  if (!result.has_value()) {
    return;
  }

  EXPECT_EQ(result.value().window.mode, info_getter::QueryWindowMode::kLatest);
  EXPECT_EQ(result.value().window.to, "2026-03-21T00:00:00Z");
  EXPECT_EQ(result.value().window.from, "2026-03-20T00:00:00Z");
  EXPECT_EQ(static_cast<int>(result.value().rows.size()), 2);

  for (const auto& row : result.value().rows) {
    if (row.metric == "temperature") {
      EXPECT_NEAR(row.value, 15.0, 1e-9);
    }
    if (row.metric == "humidity") {
      EXPECT_NEAR(row.value, 55.0, 1e-9);
    }
  }
}

TEST(UnitTests, UnknownSensorIdsReturn400) {
  const auto db_path = temp_db_path("info_getter_unit_unknown_sensor.db");
  std::remove(db_path.c_str());

  info_getter::SqliteWeatherRepository repo(db_path);
  std::string error;
  ASSERT_TRUE(repo.init_schema(&error));
  info_getter::WeatherService service(repo);

  const info_getter::Reading r1{
      .sensor_id = "sensor-1",
      .observed_at = "2026-03-21T00:00:00Z",
      .temperature = 20,
      .humidity = 60,
      .wind_speed = 8,
  };
  EXPECT_TRUE(service.ingest_reading(r1).has_value());

  info_getter::AggregateQuery query;
  query.sensor_ids = {"sensor-404"};
  query.metrics = {info_getter::Metric::kTemperature};
  query.aggregations = {info_getter::Aggregation::kAvg};
  query.date_range = info_getter::DateRange{
      .from = "2026-03-20T00:00:00Z",
      .to = "2026-03-21T00:00:00Z",
  };

  const auto result = service.query_aggregate(query);
  ASSERT_FALSE(result.has_value());
  if (!result.has_value()) {
    EXPECT_EQ(result.error().code, "INVALID_SENSOR_IDS");
    EXPECT_EQ(result.error().http_status, 400);
  }
}

TEST(TimeUtilsTests, ParseValidTimestamp) {
  const auto result = info_getter::time_utils::parse_utc_seconds("2026-03-21T10:00:00Z");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(info_getter::time_utils::format_utc_seconds(*result), "2026-03-21T10:00:00Z");
}

TEST(TimeUtilsTests, ParseRejectsInvalidTimestamp) {
  EXPECT_FALSE(info_getter::time_utils::parse_utc_seconds("not-a-date").has_value());
  EXPECT_FALSE(info_getter::time_utils::parse_utc_seconds("2026-13-01T00:00:00Z").has_value());
  EXPECT_FALSE(info_getter::time_utils::parse_utc_seconds("2026-02-29T00:00:00Z").has_value());
  EXPECT_FALSE(info_getter::time_utils::parse_utc_seconds("2026-03-21T10:00:00").has_value());
}

TEST(TimeUtilsTests, FormatRoundTrip) {
  const long long epoch = 1774087200;
  const auto formatted = info_getter::time_utils::format_utc_seconds(epoch);
  const auto parsed = info_getter::time_utils::parse_utc_seconds(formatted);
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(*parsed, epoch);
}

TEST(TimeUtilsTests, IsValidRejectsNoZ) {
  EXPECT_FALSE(info_getter::time_utils::is_valid_utc_timestamp("2026-03-21T10:00:00+00:00"));
}

TEST(TimeUtilsTests, SubtractTwentyFourHours) {
  const auto epoch = info_getter::time_utils::parse_utc_seconds("2026-03-21T14:00:00Z");
  ASSERT_TRUE(epoch.has_value());
  const auto earlier = info_getter::time_utils::format_utc_seconds(*epoch - 24LL * 60LL * 60LL);
  EXPECT_EQ(earlier, "2026-03-20T14:00:00Z");
}

}  // namespace
