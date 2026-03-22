#pragma once

#include <string>

#include "domain.hpp"
#include "repository.hpp"
#include <expected>

namespace info_getter {

using ReadingResult = std::expected<Reading, ApiError>;
using AggregateResult = std::expected<AggregateResponse, ApiError>;

class WeatherService {
 public:
  explicit WeatherService(IWeatherRepository& repository);

  ReadingResult ingest_reading(const Reading& reading) const;
  AggregateResult query_aggregate(const AggregateQuery& query) const;

 private:
  static ApiError validation_error(std::string code, std::string message);

  IWeatherRepository& repository_;
};

}  // namespace info_getter
