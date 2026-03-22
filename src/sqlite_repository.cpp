#include "sqlite_repository.hpp"

#include <algorithm>
#include <memory>
#include <set>
#include <sstream>
#include <tuple>

#include "sqlite3.h"
#include "json_codec.hpp"

namespace info_getter {

namespace {

struct StmtDeleter {
  void operator()(sqlite3_stmt* stmt) const {
    if (stmt != nullptr) {
      sqlite3_finalize(stmt);
    }
  }
};

using StmtPtr = std::unique_ptr<sqlite3_stmt, StmtDeleter>;

StmtPtr prepare(sqlite3* db, const std::string& sql, std::string* error) {
  sqlite3_stmt* raw = nullptr;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &raw, nullptr) != SQLITE_OK) {
    if (error != nullptr) {
      *error = sqlite3_errmsg(db);
    }
    return nullptr;
  }
  return StmtPtr(raw);
}

std::string metric_to_column(const Metric metric) {
  switch (metric) {
    case Metric::kTemperature:
      return "temperature";
    case Metric::kHumidity:
      return "humidity";
    case Metric::kWindSpeed:
      return "wind_speed";
  }
  return "";
}

std::string agg_to_sql(const Aggregation aggregation, const std::string& column) {
  switch (aggregation) {
    case Aggregation::kMin:
      return "MIN(" + column + ")";
    case Aggregation::kMax:
      return "MAX(" + column + ")";
    case Aggregation::kSum:
      return "SUM(" + column + ")";
    case Aggregation::kAvg:
      return "AVG(" + column + ")";
  }
  return "";
}

std::string placeholders(size_t count) {
  std::string result;
  for (size_t i = 0; i < count; ++i) {
    if (i > 0) {
      result += ",";
    }
    result += "?";
  }
  return result;
}

using ColumnMap = std::vector<std::tuple<Metric, Aggregation>>;

struct AggregateSQL {
  std::string sql;
  ColumnMap columns;
};

AggregateSQL build_aggregate_sql(const AggregateRequest& request) {
  ColumnMap columns;
  std::ostringstream sql;
  sql << "SELECT sensor_id";
  for (const auto& metric : request.metrics) {
    const auto column = metric_to_column(metric);
    for (const auto aggregation : request.aggregations) {
      sql << ", " << agg_to_sql(aggregation, column);
      columns.emplace_back(metric, aggregation);
    }
  }
  sql << " FROM readings WHERE observed_at >= ? AND observed_at <= ?";
  if (!request.sensor_ids.empty()) {
    sql << " AND sensor_id IN (" << placeholders(request.sensor_ids.size()) << ")";
  }
  sql << " GROUP BY sensor_id;";
  return {sql.str(), std::move(columns)};
}

void bind_aggregate_params(sqlite3_stmt* stmt, const AggregateRequest& request) {
  int idx = 1;
  sqlite3_bind_text(stmt, idx++, request.from.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, idx++, request.to.c_str(), -1, SQLITE_TRANSIENT);
  for (const auto& sid : request.sensor_ids) {
    sqlite3_bind_text(stmt, idx++, sid.c_str(), -1, SQLITE_TRANSIENT);
  }
}

std::vector<AggregateResultRow> extract_aggregate_rows(sqlite3_stmt* stmt,
                                                       const ColumnMap& columns) {
  std::vector<AggregateResultRow> rows;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const auto* sensor_txt = sqlite3_column_text(stmt, 0);
    const std::string sensor_id(reinterpret_cast<const char*>(sensor_txt));

    for (size_t i = 0; i < columns.size(); ++i) {
      const int col_idx = static_cast<int>(i + 1);
      if (sqlite3_column_type(stmt, col_idx) == SQLITE_NULL) {
        continue;
      }
      rows.push_back(AggregateResultRow{
          .sensor_id = sensor_id,
          .metric = json_codec::metric_to_string(std::get<0>(columns[i])),
          .aggregation = std::get<1>(columns[i]),
          .value = sqlite3_column_double(stmt, col_idx),
      });
    }
  }
  return rows;
}

}  // namespace

SqliteWeatherRepository::SqliteWeatherRepository(std::string db_path) : db_path_(std::move(db_path)) {}

SqliteWeatherRepository::~SqliteWeatherRepository() {
  if (db_) {
    sqlite3_close(db_);
  }
}

bool SqliteWeatherRepository::open_if_needed(std::string* error) {
  if (db_) {
    return true;
  }

  if (sqlite3_open(db_path_.c_str(), &db_) != SQLITE_OK) {
    if (error) {
      *error = sqlite3_errmsg(db_);
    }
    return false;
  }
  return true;
}

bool SqliteWeatherRepository::exec_sql(const std::string& sql, std::string* error) {
  char* raw_err_msg = nullptr;
  const int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &raw_err_msg);
  std::unique_ptr<char, decltype(&sqlite3_free)> err_msg(raw_err_msg, &sqlite3_free);
  if (rc != SQLITE_OK) {
    if (error != nullptr) {
      *error = err_msg == nullptr ? "sqlite exec error" : err_msg.get();
    }
    return false;
  }
  return true;
}

bool SqliteWeatherRepository::init_schema(std::string* error) {
  if (!open_if_needed(error)) {
    return false;
  }

  const std::string sensors_sql =
      "CREATE TABLE IF NOT EXISTS sensors ("
      "sensor_id TEXT PRIMARY KEY"
      ");";
  const std::string readings_sql =
      "CREATE TABLE IF NOT EXISTS readings ("
      "sensor_id TEXT NOT NULL,"
      "observed_at TEXT NOT NULL,"
      "temperature REAL NOT NULL,"
      "humidity REAL NOT NULL,"
      "wind_speed REAL NOT NULL,"
      "PRIMARY KEY(sensor_id, observed_at),"
      "FOREIGN KEY(sensor_id) REFERENCES sensors(sensor_id)"
      ");";
  const std::string idx_sql = "CREATE INDEX IF NOT EXISTS idx_readings_observed_at ON readings(observed_at);";

  return exec_sql(sensors_sql, error) && exec_sql(readings_sql, error) && exec_sql(idx_sql, error);
}

bool SqliteWeatherRepository::upsert_sensor(const std::string& sensor_id, std::string* error) {
  if (!open_if_needed(error)) {
    return false;
  }

  auto stmt = prepare(db_, "INSERT OR IGNORE INTO sensors(sensor_id) VALUES(?);", error);
  if (!stmt) {
    return false;
  }
  sqlite3_bind_text(stmt.get(), 1, sensor_id.c_str(), -1, SQLITE_TRANSIENT);
  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    if (error != nullptr) {
      *error = sqlite3_errmsg(db_);
    }
    return false;
  }
  return true;
}

bool SqliteWeatherRepository::insert_reading(const Reading& reading, std::string* error) {
  if (!open_if_needed(error)) {
    return false;
  }

  auto stmt = prepare(
      db_,
      "INSERT OR REPLACE INTO readings(sensor_id, observed_at, temperature, humidity, wind_speed) "
      "VALUES(?, ?, ?, ?, ?);",
      error);
  if (!stmt) {
    return false;
  }
  sqlite3_bind_text(stmt.get(), 1, reading.sensor_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt.get(), 2, reading.observed_at.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_double(stmt.get(), 3, reading.temperature);
  sqlite3_bind_double(stmt.get(), 4, reading.humidity);
  sqlite3_bind_double(stmt.get(), 5, reading.wind_speed);
  const int rc = sqlite3_step(stmt.get());
  if (rc != SQLITE_DONE) {
    if (error != nullptr) {
      *error = sqlite3_errmsg(db_);
    }
    return false;
  }
  return true;
}

bool SqliteWeatherRepository::sensors_exist(
    const std::vector<std::string>& sensor_ids,
    std::string* error) {
  if (!open_if_needed(error)) {
    return false;
  }
  if (sensor_ids.empty()) {
    return true;
  }

  // Deduplicate to avoid count mismatch when caller passes duplicate IDs.
  const std::set<std::string> unique_ids(sensor_ids.begin(), sensor_ids.end());

  const std::string sql =
      "SELECT COUNT(DISTINCT sensor_id) FROM sensors WHERE sensor_id IN (" +
      placeholders(unique_ids.size()) + ");";

  auto stmt = prepare(db_, sql, error);
  if (!stmt) {
    return false;
  }

  int idx = 1;
  for (const auto& id : unique_ids) {
    sqlite3_bind_text(stmt.get(), idx++, id.c_str(), -1, SQLITE_TRANSIENT);
  }

  int found = 0;
  if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
    found = sqlite3_column_int(stmt.get(), 0);
  }
  return found == static_cast<int>(unique_ids.size());
}

std::optional<std::string> SqliteWeatherRepository::latest_observed_at(
    const std::vector<std::string>& sensor_ids,
    std::string* error) {
  if (!open_if_needed(error)) {
    return std::nullopt;
  }

  std::string sql = "SELECT MAX(observed_at) FROM readings";
  if (!sensor_ids.empty()) {
    sql += " WHERE sensor_id IN (" + placeholders(sensor_ids.size()) + ")";
  }
  sql += ";";

  auto stmt = prepare(db_, sql, error);
  if (!stmt) {
    return std::nullopt;
  }

  for (size_t i = 0; i < sensor_ids.size(); ++i) {
    sqlite3_bind_text(stmt.get(), static_cast<int>(i + 1), sensor_ids[i].c_str(), -1, SQLITE_TRANSIENT);
  }

  std::optional<std::string> out;
  if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
    if (sqlite3_column_type(stmt.get(), 0) != SQLITE_NULL) {
      const unsigned char* txt = sqlite3_column_text(stmt.get(), 0);
      out = std::string(reinterpret_cast<const char*>(txt));
    }
  }
  return out;
}

std::vector<AggregateResultRow> SqliteWeatherRepository::aggregate(
    const AggregateRequest& request,
    std::string* error) {
  if (!open_if_needed(error)) {
    return {};
  }

  const auto [sql, columns] = build_aggregate_sql(request);

  auto stmt = prepare(db_, sql, error);
  if (!stmt) {
    return {};
  }

  bind_aggregate_params(stmt.get(), request);
  return extract_aggregate_rows(stmt.get(), columns);
}

}  // namespace info_getter
