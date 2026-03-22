#pragma once

#include <string>

#include "domain.hpp"
#include <expected>

namespace info_getter {
namespace json_codec {

using ReadingDecodeResult = std::expected<Reading, ApiError>;
using QueryDecodeResult = std::expected<AggregateQuery, ApiError>;

ReadingDecodeResult decode_reading_request(const std::string& body);
QueryDecodeResult decode_aggregate_request(const std::string& body);

std::string encode_reading_response(const Reading& reading, const std::string& id);
std::string encode_aggregate_response(const AggregateResponse& response);
std::string encode_error_response(const ApiError& error);

std::string metric_to_string(Metric metric);
std::expected<Metric, ApiError> metric_from_string(const std::string& value);
std::string aggregation_to_string(Aggregation aggregation);
std::expected<Aggregation, ApiError> aggregation_from_string(const std::string& value);
std::string query_window_mode_to_string(QueryWindowMode mode);

}  // namespace json_codec
}  // namespace info_getter
