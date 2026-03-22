#include "json_codec.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <ranges>

#include "time_utils.hpp"
#include "nlohmann/json.hpp"

namespace info_getter::json_codec {

namespace {

ApiError make_error(const std::string& code, const std::string& message, const int status = 400) {
  return ApiError{.code = code, .message = message, .http_status = status};
}

bool is_finite_number(const nlohmann::json& value) {
  if (!value.is_number()) {
    return false;
  }
  const double v = value.get<double>();
  return std::isfinite(v);
}

const char* metric_json_key(const Metric metric) {
  switch (metric) {
    case Metric::kTemperature:
      return "temperature";
    case Metric::kHumidity:
      return "humidity";
    case Metric::kWindSpeed:
      return "windSpeed";
  }
  return "";
}

std::optional<double>
decode_optional_metric(const nlohmann::json& metrics, const Metric metric) {
  const auto* metric_name = metric_json_key(metric);
  if (!metrics.contains(metric_name)) {
    return std::nullopt;
  }
  if (!is_finite_number(metrics[metric_name])) {
    return std::nullopt;
  }
  return metrics[metric_name].get<double>();
}

std::expected<std::vector<std::string>, ApiError>
decode_string_array(const nlohmann::json& payload, const char* field,
                    const char* error_code, const bool required) {
  if (!payload.contains(field)) {
    if (required) {
      return std::unexpected(make_error(error_code,
          std::string(field) + " is required and must be a non-empty array"));
    }
    return {};
  }
  if (!payload[field].is_array() || (required && payload[field].empty())) {
    return std::unexpected(make_error(error_code,
        required ? std::string(field) + " is required and must be a non-empty array"
                 : std::string(field) + " must be an array"));
  }
  const auto& arr = payload[field];
  if (!std::ranges::all_of(arr, &nlohmann::json::is_string)) {
    return std::unexpected(make_error(error_code,
        std::string(field) + " values must be strings"));
  }
  return arr
      | std::views::transform([](const nlohmann::json& item) { return item.get<std::string>(); })
      | std::ranges::to<std::vector<std::string>>();
}

template <typename T, typename ParseFn>
std::expected<std::vector<T>, ApiError>
decode_parsed_array(const nlohmann::json& payload, const char* field,
                    const char* error_code, ParseFn parse) {
  auto strings = decode_string_array(payload, field, error_code, true);
  if (!strings) {
    return std::unexpected(strings.error());
  }
  std::vector<T> result;
  for (const auto& s : *strings) {
    auto parsed = parse(s);
    if (!parsed) {
      return std::unexpected(parsed.error());
    }
    result.push_back(*parsed);
  }
  return result;
}

std::expected<std::optional<DateRange>, ApiError>
decode_date_range(const nlohmann::json& payload) {
  if (!payload.contains("dateRange")) {
    return std::nullopt;
  }
  if (!payload["dateRange"].is_object()) {
    return std::unexpected(make_error("INVALID_DATE_RANGE", "dateRange must be an object"));
  }
  const auto& date_range = payload["dateRange"];
  if (!date_range.contains("from") || !date_range.contains("to") || !date_range["from"].is_string() ||
      !date_range["to"].is_string()) {
    return std::unexpected(
        make_error("INVALID_DATE_RANGE", "dateRange.from and dateRange.to are required"));
  }
  return DateRange{
      .from = date_range["from"].get<std::string>(),
      .to = date_range["to"].get<std::string>(),
  };
}

std::expected<std::pair<std::string, nlohmann::json>, ApiError>
validate_reading_payload(const nlohmann::json& payload) {
  if (!payload.contains("sensorId") || !payload["sensorId"].is_string() ||
      payload["sensorId"].get<std::string>().empty()) {
    return std::unexpected(make_error("INVALID_SENSOR_ID", "sensorId is required"));
  }
  if (!payload.contains("observedAt") || !payload["observedAt"].is_string()) {
    return std::unexpected(make_error("INVALID_OBSERVED_AT", "observedAt is required"));
  }
  const auto observed_at = payload["observedAt"].get<std::string>();
  if (!time_utils::is_valid_utc_timestamp(observed_at)) {
    return std::unexpected(
        make_error("INVALID_OBSERVED_AT", "observedAt must be ISO-8601 UTC timestamp"));
  }

  if (!payload.contains("metrics") || !payload["metrics"].is_object()) {
    return std::unexpected(make_error("INVALID_METRICS", "metrics must be an object"));
  }

  return std::make_pair(observed_at, payload["metrics"]);
}

ReadingDecodeResult decode_reading_from_payload(const nlohmann::json& payload) {
  const auto validation_result = validate_reading_payload(payload);
  if (!validation_result.has_value()) {
    return std::unexpected(validation_result.error());
  }

  const auto [observed_at, metrics] = validation_result.value();

  Reading reading;
  reading.sensor_id = payload["sensorId"].get<std::string>();
  reading.observed_at = observed_at;

  auto temperature = decode_optional_metric(metrics, Metric::kTemperature);
    if (!temperature) {
      return std::unexpected(
          make_error("INVALID_METRICS", "metrics.temperature must be a finite number"));
    }
    reading.temperature = *temperature;


  auto humidity = decode_optional_metric(metrics, Metric::kHumidity);
    if (!humidity) {
      return std::unexpected(
          make_error("INVALID_METRICS", "metrics.humidity must be a finite number"));
    }
    reading.humidity = *humidity;

  auto wind_speed = decode_optional_metric(metrics, Metric::kWindSpeed);
    if (!wind_speed) {
      return std::unexpected(
          make_error("INVALID_METRICS", "metrics.windSpeed must be a finite number"));
    }
    reading.wind_speed = *wind_speed;

  return reading;
}

}  // namespace

std::string metric_to_string(const Metric metric) {
  switch (metric) {
    case Metric::kTemperature:
      return "temperature";
    case Metric::kHumidity:
      return "humidity";
    case Metric::kWindSpeed:
      return "windSpeed";
  }
  return "";
}

std::expected<Metric, ApiError> metric_from_string(const std::string& value) {
  if (value == "temperature") {
    return Metric::kTemperature;
  }
  if (value == "humidity") {
    return Metric::kHumidity;
  }
  if (value == "windSpeed") {
    return Metric::kWindSpeed;
  }
  return std::unexpected(
      make_error("INVALID_METRICS", "metrics must be temperature, humidity, or windSpeed"));
}

std::string aggregation_to_string(const Aggregation aggregation) {
  switch (aggregation) {
    case Aggregation::kMin:
      return "min";
    case Aggregation::kMax:
      return "max";
    case Aggregation::kSum:
      return "sum";
    case Aggregation::kAvg:
      return "avg";
  }
  return "";
}

std::expected<Aggregation, ApiError> aggregation_from_string(const std::string& value) {
  if (value == "min") {
    return Aggregation::kMin;
  }
  if (value == "max") {
    return Aggregation::kMax;
  }
  if (value == "sum") {
    return Aggregation::kSum;
  }
  if (value == "avg") {
    return Aggregation::kAvg;
  }
  return std::unexpected(
      make_error("INVALID_AGGREGATION", "aggregations must be min, max, sum, or avg"));
}

std::string query_window_mode_to_string(const QueryWindowMode mode) {
  switch (mode) {
    case QueryWindowMode::kExplicit:
      return "explicit";
    case QueryWindowMode::kLatest:
      return "latest";
  }
  return "";
}

ReadingDecodeResult decode_reading_request(const std::string& body) {
  try {
    const auto payload = nlohmann::json::parse(body);
    return decode_reading_from_payload(payload);
  } catch (const std::exception&) {
    return std::unexpected(make_error("INVALID_JSON", "request body must be valid JSON"));
  }
}

QueryDecodeResult decode_aggregate_request(const std::string& body) {
  try {
    const auto payload = nlohmann::json::parse(body);

    auto sensor_ids = decode_string_array(payload, "sensorIds", "INVALID_SENSOR_IDS", false);
    if (!sensor_ids) {
      return std::unexpected(sensor_ids.error());
    }

    auto metrics = decode_parsed_array<Metric>(payload, "metrics", "INVALID_METRICS", metric_from_string);
    if (!metrics) {
      return std::unexpected(metrics.error());
    }

    auto aggregations = decode_parsed_array<Aggregation>(payload, "aggregations", "INVALID_AGGREGATIONS", aggregation_from_string);
    if (!aggregations) {
      return std::unexpected(aggregations.error());
    }

    auto date_range = decode_date_range(payload);
    if (!date_range) {
      return std::unexpected(date_range.error());
    }

    return AggregateQuery{
        .sensor_ids = std::move(*sensor_ids),
        .metrics = std::move(*metrics),
        .aggregations = std::move(*aggregations),
        .date_range = std::move(*date_range),
    };
  } catch (const std::exception&) {
    return std::unexpected(make_error("INVALID_JSON", "request body must be valid JSON"));
  }
}

std::string encode_reading_response(const Reading& reading, const std::string& id) {
  const nlohmann::json response = {
      {"id", id},
      {"sensorId", reading.sensor_id},
      {"observedAt", reading.observed_at},
      {"stored", true},
  };
  return response.dump();
}

std::string encode_aggregate_response(const AggregateResponse& response) {
  nlohmann::json rows = nlohmann::json::array();
  for (const auto& row : response.rows) {
    rows.push_back({
        {"sensorId", row.sensor_id},
        {"metric", row.metric},
        {"aggregation", aggregation_to_string(row.aggregation)},
        {"value", row.value},
    });
  }

  const nlohmann::json payload = {
      {"window",
       {
           {"from", response.window.from},
           {"to", response.window.to},
           {"mode", query_window_mode_to_string(response.window.mode)},
       }},
      {"results", rows},
  };
  return payload.dump();
}

std::string encode_error_response(const ApiError& error) {
  const nlohmann::json payload = {
      {"error",
       {
           {"code", error.code},
           {"message", error.message},
       }},
  };
  return payload.dump();
}

}  // namespace info_getter::json_codec
