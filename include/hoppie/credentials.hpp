#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace hoppie {

std::optional<std::string> loadLogonCredential();
bool saveLogonCredential(std::string_view value);
bool deleteLogonCredential();

}  // namespace hoppie
