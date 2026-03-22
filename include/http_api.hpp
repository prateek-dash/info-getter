#pragma once

#include "httplib.h"
#include "repository.hpp"
#include "service.hpp"

namespace info_getter {

class HttpApi {
 public:
  HttpApi(httplib::Server& server, WeatherService& service);

  void register_routes();

 private:
  httplib::Server& server_;
  WeatherService& service_;
};

}  // namespace info_getter
