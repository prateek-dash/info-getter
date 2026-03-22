#pragma once

#include <optional>
#include <string>

namespace info_getter {
namespace time_utils {

std::optional<long long> parse_utc_seconds(const std::string& iso_utc);
std::string format_utc_seconds(long long epoch_seconds);
bool is_valid_utc_timestamp(const std::string& iso_utc);

}  // namespace time_utils
}  // namespace info_getter
