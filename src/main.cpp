#include <cstdlib>
#include <iostream>
#include <string>

#include "httplib.h"
#include "http_api.hpp"
#include "service.hpp"
#include "sqlite_repository.hpp"

int main() {
  const char* env_db = std::getenv("INFO_GETTER_DB_PATH");
  const std::string db_path = env_db == nullptr ? "info_getter.db" : env_db;

  info_getter::SqliteWeatherRepository repository(db_path);
  std::string error;
  if (!repository.init_schema(&error)) {
    std::cerr << "failed to initialize schema: " << error << "\n";
    return 1;
  }

  info_getter::WeatherService service(repository);
  httplib::Server server;
  info_getter::HttpApi api(server, service);
  api.register_routes();

  const char* env_host = std::getenv("INFO_GETTER_HOST");
  const std::string host = env_host == nullptr ? "0.0.0.0" : env_host;
  const char* env_port = std::getenv("INFO_GETTER_PORT");
  const int port = env_port == nullptr ? 8080 : std::stoi(env_port);

  std::cout << "info_getter listening on " << host << ":" << port << " using db " << db_path << "\n";
  if (!server.listen(host, port)) {
    std::cerr << "failed to listen on " << host << ":" << port << "\n";
    return 1;
  }

  return 0;
}
