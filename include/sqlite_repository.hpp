#pragma once

#include <memory>
#include <string>
#include <vector>

#include "repository.hpp"

struct sqlite3;

namespace info_getter {

class SqliteWeatherRepository : public IWeatherRepository {
 public:
  explicit SqliteWeatherRepository(std::string db_path);
  ~SqliteWeatherRepository() override;

  bool init_schema(std::string* error) override;
  bool upsert_sensor(const std::string& sensor_id, std::string* error) override;
  bool insert_reading(const Reading& reading, std::string* error) override;
  bool sensors_exist(
      const std::vector<std::string>& sensor_ids,
      std::string* error) override;
  std::optional<std::string> latest_observed_at(
      const std::vector<std::string>& sensor_ids,
      std::string* error) override;
  std::vector<AggregateResultRow> aggregate(
      const AggregateRequest& request,
      std::string* error) override;

 private:
  bool open_if_needed(std::string* error);
  bool exec_sql(const std::string& sql, std::string* error);

  std::string db_path_;
  sqlite3* db_ = nullptr;
};

}  // namespace info_getter
