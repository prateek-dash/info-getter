#include "http_api.hpp"

#include <chrono>
#include <functional>

#include "json_codec.hpp"

namespace info_getter {

namespace {

void set_error_response(httplib::Response& res, const ApiError& err) {
  res.status = err.http_status;
  res.set_content(json_codec::encode_error_response(err), "application/json");
}

}

HttpApi::HttpApi(httplib::Server& server, WeatherService& service) : server_(server), service_(service) {}

void HttpApi::register_routes() {
  const auto handle_readings_post = [this](const httplib::Request& req, httplib::Response& res) {
    const auto payload = json_codec::decode_reading_request(req.body)
                             .and_then([this](const Reading& reading) {
                               return service_.ingest_reading(reading);
                             })
                             .transform([](const Reading& reading) {
                               const auto id = reading.sensor_id + ":" + reading.observed_at;
                               return json_codec::encode_reading_response(reading, id);
                             });

    if (!payload.has_value()) {
      set_error_response(res, payload.error());
      return;
    }

    res.status = 201;
    res.set_content(payload.value(), "application/json");
  };

  const auto handle_aggregate_post = [this](const httplib::Request& req, httplib::Response& res) {
    const auto payload = json_codec::decode_aggregate_request(req.body)
                             .and_then([this](const AggregateQuery& query) {
                               return service_.query_aggregate(query);
                             })
                             .transform([](const AggregateResponse& response) {
                               return json_codec::encode_aggregate_response(response);
                             });

    if (!payload.has_value()) {
      set_error_response(res, payload.error());
      return;
    }

    res.status = 200;
    res.set_content(payload.value(), "application/json");
  };

  server_.Post("/api/v1/readings", handle_readings_post);
  server_.Post("/api/v1/queries/aggregate", handle_aggregate_post);
}

}  // namespace info_getter
