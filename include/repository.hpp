#pragma once

#include <optional>
#include <string>
#include <vector>

#include "domain.hpp"

namespace info_getter {

struct AggregateRequest {
  std::vector<std::string> sensor_ids;
  std::vector<Metric> metrics;
  std::vector<Aggregation> aggregations;
  std::string from;
  std::string to;
};

class IWeatherRepository {
 public:
  virtual ~IWeatherRepository() = default;

  virtual bool init_schema(std::string* error) = 0;
  virtual bool upsert_sensor(const std::string& sensor_id, std::string* error) = 0;
  virtual bool insert_reading(const Reading& reading, std::string* error) = 0;
  virtual bool sensors_exist(
      const std::vector<std::string>& sensor_ids,
      std::string* error) = 0;
  virtual std::optional<std::string> latest_observed_at(
      const std::vector<std::string>& sensor_ids,
      std::string* error) = 0;
  virtual std::vector<AggregateResultRow> aggregate(
      const AggregateRequest& request,
      std::string* error) = 0;
};

}  // namespace info_getter
