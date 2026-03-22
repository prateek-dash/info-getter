#include "time_utils.hpp"

#include <chrono>
#include <format>
#include <sstream>

namespace info_getter::time_utils {

std::optional<long long> parse_utc_seconds(const std::string& iso_utc) {
  std::chrono::sys_seconds tp;
  std::istringstream iss(iso_utc);
  iss >> std::chrono::parse("%FT%TZ", tp);
  if (iss.fail() || iss.peek() != std::istringstream::traits_type::eof()) {
    return std::nullopt;
  }

  if (std::format("{:%FT%TZ}", tp) != iso_utc) {
    return std::nullopt;
  }

  return static_cast<long long>(tp.time_since_epoch().count());
}

std::string format_utc_seconds(long long epoch_seconds) {
  const std::chrono::sys_seconds tp{std::chrono::seconds{epoch_seconds}};
  return std::format("{:%FT%TZ}", tp);
}

bool is_valid_utc_timestamp(const std::string& iso_utc) {
  return parse_utc_seconds(iso_utc).has_value();
}

}  // namespace info_getter::time_utils
