#include "service.hpp"

#include <cmath>
#include <sstream>

#include "json_codec.hpp"
#include "time_utils.hpp"

namespace info_getter {

namespace {

ApiError validation_error(const std::string& code, const std::string& message) {
  return ApiError{.code = code, .message = message, .http_status = 400};
}

ApiError db_error(const std::string& message) {
  return ApiError{.code = "DB_ERROR", .message = message, .http_status = 500};
}

constexpr long long kOneDaySeconds = 24LL * 60LL * 60LL;
constexpr long long kThirtyOneDaysSeconds = 31LL * kOneDaySeconds;

std::expected<void, ApiError>
validate_query(const AggregateQuery& query) {
  if (query.metrics.empty()) {
    return std::unexpected(validation_error("INVALID_METRICS", "metrics is required"));
  }
  if (query.aggregations.empty()) {
    return std::unexpected(validation_error("INVALID_AGGREGATIONS", "aggregations is required"));
  }
  return {};
}

std::expected<void, ApiError>
validate_sensor_ids(const std::vector<std::string>& sensor_ids, IWeatherRepository& repository) {
  if (sensor_ids.empty()) {
    return {};
  }
  std::string error;
  const bool exists = repository.sensors_exist(sensor_ids, &error);
  if (!error.empty()) {
    return std::unexpected(db_error(error));
  }
  if (!exists) {
    return std::unexpected(
        validation_error("INVALID_SENSOR_IDS", "one or more sensorIds do not exist"));
  }
  return {};
}

std::expected<QueryWindow, ApiError>
resolve_explicit_window(const DateRange& date_range) {
  const auto from_epoch = time_utils::parse_utc_seconds(date_range.from);
  const auto to_epoch = time_utils::parse_utc_seconds(date_range.to);
  if (!from_epoch || !to_epoch || *to_epoch <= *from_epoch) {
    return std::unexpected(validation_error(
        "INVALID_DATE_RANGE", "dateRange.from and dateRange.to must be valid and increasing"));
  }
  const long long seconds = *to_epoch - *from_epoch;
  if (seconds < kOneDaySeconds || seconds > kThirtyOneDaysSeconds) {
    return std::unexpected(
        validation_error("INVALID_DATE_RANGE", "dateRange must be between 1 and 31 days"));
  }
  return QueryWindow{
      .from = date_range.from,
      .to = date_range.to,
      .mode = QueryWindowMode::kExplicit,
  };
}

std::expected<QueryWindow, ApiError>
resolve_latest_window(const std::vector<std::string>& sensor_ids, IWeatherRepository& repository) {
  std::string error;
  const auto latest = repository.latest_observed_at(sensor_ids, &error);
  if (!error.empty()) {
    return std::unexpected(db_error(error));
  }
  if (!latest) {
    return std::unexpected(
        validation_error("NO_DATA", "no data available for requested sensors"));
  }
  const auto to_epoch = time_utils::parse_utc_seconds(*latest);
  if (!to_epoch) {
    return std::unexpected(ApiError{
        .code = "DATA_ERROR",
        .message = "latest data timestamp is invalid",
        .http_status = 500,
    });
  }
  return QueryWindow{
      .from = time_utils::format_utc_seconds(*to_epoch - kOneDaySeconds),
      .to = *latest,
      .mode = QueryWindowMode::kLatest,
  };
}

std::expected<QueryWindow, ApiError>
resolve_window(const AggregateQuery& query, IWeatherRepository& repository) {
  if (query.date_range) {
    return resolve_explicit_window(*query.date_range);
  }
  return resolve_latest_window(query.sensor_ids, repository);
}

std::expected<void, ApiError>
validate_reading(const Reading& reading) {
  if (reading.sensor_id.empty()) {
    return std::unexpected(validation_error("INVALID_SENSOR_ID", "sensorId is required"));
  }
  if (!time_utils::is_valid_utc_timestamp(reading.observed_at)) {
    return std::unexpected(
        validation_error("INVALID_OBSERVED_AT", "observedAt must be ISO-8601 UTC timestamp"));
  }
  if (!std::isfinite(reading.temperature) || !std::isfinite(reading.humidity) ||
      !std::isfinite(reading.wind_speed)) {
    return std::unexpected(
        validation_error("INVALID_METRICS", "temperature, humidity, and windSpeed must be finite"));
  }
  return {};
}

std::expected<void, ApiError>
persist_reading(const Reading& reading, IWeatherRepository& repository) {
  std::string error;
  if (!repository.upsert_sensor(reading.sensor_id, &error)) {
    return std::unexpected(db_error(error));
  }
  if (!repository.insert_reading(reading, &error)) {
    return std::unexpected(db_error(error));
  }
  return {};
}

std::expected<std::vector<AggregateResultRow>, ApiError>
execute_aggregate(const AggregateQuery& query, const QueryWindow& window,
                  IWeatherRepository& repository) {
  AggregateRequest request{
      .sensor_ids = query.sensor_ids,
      .metrics = query.metrics,
      .aggregations = query.aggregations,
      .from = window.from,
      .to = window.to,
  };
  std::string error;
  auto rows = repository.aggregate(request, &error);
  if (!error.empty()) {
    return std::unexpected(db_error(error));
  }
  return rows;
}

}  // namespace

WeatherService::WeatherService(IWeatherRepository& repository) : repository_(repository) {}

ApiError WeatherService::validation_error(std::string code, std::string message) {
  return ApiError{.code = std::move(code), .message = std::move(message), .http_status = 400};
}

ReadingResult WeatherService::ingest_reading(const Reading& reading) const {
  return validate_reading(reading)
      .and_then([&]() { return persist_reading(reading, repository_); })
      .transform([&]() { return reading; });
}

AggregateResult WeatherService::query_aggregate(const AggregateQuery& query) const {
  return validate_query(query)
      .and_then([&]() { return validate_sensor_ids(query.sensor_ids, repository_); })
      .and_then([&]() { return resolve_window(query, repository_); })
      .and_then([&](QueryWindow window) -> AggregateResult {
        return execute_aggregate(query, window, repository_)
            .transform([&window](std::vector<AggregateResultRow> rows) {
              return AggregateResponse{.window = std::move(window), .rows = std::move(rows)};
            });
      });
}

}  // namespace info_getter
