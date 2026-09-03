#pragma once

#include "hoppie/core.hpp"

#include <optional>
#include <string_view>

namespace hoppie {

struct VatsimFlightPlans {
    std::optional<FlightPlanSnapshot> online;
    std::optional<FlightPlanSnapshot> prefile;
};

VatsimFlightPlans parseVatsimData(std::string_view jsonText, std::string_view cid);
std::optional<FlightPlanSnapshot> parseSimBriefOfp(std::string_view jsonText);

}  // namespace hoppie
