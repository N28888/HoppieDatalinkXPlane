#include "hoppie/datasources.hpp"

#include <nlohmann/json.hpp>

#include <stdexcept>

namespace hoppie {
namespace {

using json = nlohmann::json;

std::string valueString(const json& object, const char* key) {
    const auto item = object.find(key);
    if (item == object.end() || item->is_null()) return {};
    if (item->is_string()) return item->get<std::string>();
    if (item->is_number_integer()) return std::to_string(item->get<long long>());
    return {};
}

std::string nestedString(const json& object, const char* parent, const char* child) {
    const auto item = object.find(parent);
    return item == object.end() || !item->is_object() ? std::string{} : valueString(*item, child);
}

FlightPlanSnapshot parseVatsimEntry(const json& entry) {
    FlightPlanSnapshot result;
    result.callsign = uppercaseAscii(valueString(entry, "callsign"));
    const auto flightPlan = entry.find("flight_plan");
    if (flightPlan == entry.end() || !flightPlan->is_object()) return result;
    result.aircraftType = uppercaseAscii(valueString(*flightPlan, "aircraft_short"));
    if (result.aircraftType.empty())
        result.aircraftType = uppercaseAscii(valueString(*flightPlan, "aircraft"));
    result.departure = uppercaseAscii(valueString(*flightPlan, "departure"));
    result.destination = uppercaseAscii(valueString(*flightPlan, "arrival"));
    result.route = uppercaseAscii(valueString(*flightPlan, "route"));
    return result;
}

std::optional<FlightPlanSnapshot> findCid(const json& array, std::string_view cid) {
    if (!array.is_array()) return std::nullopt;
    for (const auto& entry : array) {
        if (!entry.is_object()) continue;
        if (valueString(entry, "cid") == cid) return parseVatsimEntry(entry);
    }
    return std::nullopt;
}

}  // namespace

VatsimFlightPlans parseVatsimData(std::string_view jsonText, std::string_view cid) {
    const auto root = json::parse(jsonText.begin(), jsonText.end());
    if (!root.is_object()) throw std::invalid_argument("VATSIM response is not an object");
    VatsimFlightPlans result;
    const auto pilots = root.find("pilots");
    if (pilots != root.end()) result.online = findCid(*pilots, trim(cid));
    const auto prefiles = root.find("prefiles");
    if (prefiles != root.end()) result.prefile = findCid(*prefiles, trim(cid));
    return result;
}

std::optional<FlightPlanSnapshot> parseSimBriefOfp(std::string_view jsonText) {
    const auto root = json::parse(jsonText.begin(), jsonText.end());
    if (!root.is_object()) throw std::invalid_argument("SimBrief response is not an object");
    if (root.contains("fetch") && root["fetch"].is_object() &&
        valueString(root["fetch"], "status") == "Error") return std::nullopt;

    FlightPlanSnapshot result;
    result.callsign = uppercaseAscii(nestedString(root, "atc", "callsign"));
    result.aircraftType = uppercaseAscii(nestedString(root, "aircraft", "icao_code"));
    result.departure = uppercaseAscii(nestedString(root, "origin", "icao_code"));
    result.destination = uppercaseAscii(nestedString(root, "destination", "icao_code"));
    result.route = uppercaseAscii(nestedString(root, "general", "route"));

    const auto navlog = root.find("navlog");
    if (navlog != root.end() && navlog->is_object()) {
        const auto fixes = navlog->find("fix");
        if (fixes != navlog->end() && fixes->is_array()) {
            for (const auto& fix : *fixes) {
                if (!fix.is_object()) continue;
                auto ident = uppercaseAscii(valueString(fix, "ident"));
                if (!ident.empty()) result.waypoints.push_back(std::move(ident));
            }
        }
    }
    if (result.callsign.empty() && result.route.empty() && result.waypoints.empty())
        return std::nullopt;
    return result;
}

}  // namespace hoppie
