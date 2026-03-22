#pragma once

#include <optional>
#include <string>
#include <vector>

namespace info_getter {

struct Reading {
  std::string sensor_id;
  std::string observed_at;
  double temperature{};
  double humidity{};
  double wind_speed{};
};

struct DateRange {
  std::string from;
  std::string to;
};

enum class Metric {
  kTemperature,
  kHumidity,
  kWindSpeed,
};

enum class Aggregation {
  kMin,
  kMax,
  kSum,
  kAvg,
};

struct AggregateQuery {
  std::vector<std::string> sensor_ids;
  std::vector<Metric> metrics;
  std::vector<Aggregation> aggregations;
  std::optional<DateRange> date_range;
};

struct AggregateResultRow {
  std::string sensor_id;
  std::string metric;
  Aggregation aggregation;
  double value{};
};

enum class QueryWindowMode {
  kExplicit,
  kLatest,
};

struct QueryWindow {
  std::string from;
  std::string to;
  QueryWindowMode mode{QueryWindowMode::kExplicit};
};

struct AggregateResponse {
  QueryWindow window;
  std::vector<AggregateResultRow> rows;
};

struct ApiError {
  std::string code;
  std::string message;
  int http_status{400};
};

}  // namespace info_getter
