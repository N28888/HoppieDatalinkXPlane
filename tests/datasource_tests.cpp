#include "hoppie/datasources.hpp"

#include <cstdlib>
#include <iostream>

namespace {

int failures = 0;
#define CHECK(expression)                                                                  \
    do {                                                                                   \
        if (!(expression)) {                                                               \
            std::cerr << "line " << __LINE__ << ": check failed: " #expression "\n";     \
            ++failures;                                                                    \
        }                                                                                  \
    } while (false)

void testVatsim() {
    const char* data = R"({
      "pilots": [{"cid": 1234567, "callsign": "CCA123", "flight_plan": {
        "aircraft_short": "B789", "departure": "ZBAA", "arrival": "ZSPD",
        "route": "AGVOS DUMET"}}],
      "prefiles": [{"cid": 7654321, "callsign": "CCA999", "flight_plan": {
        "aircraft_short": "A320", "departure": "ZSSS", "arrival": "ZGGG",
        "route": "PREFILE ROUTE"}}]
    })";
    const auto online = hoppie::parseVatsimData(data, "1234567");
    CHECK(online.online.has_value());
    CHECK(online.online->callsign == "CCA123");
    CHECK(online.online->aircraftType == "B789");
    CHECK(!online.prefile.has_value());
    const auto prefile = hoppie::parseVatsimData(data, "7654321");
    CHECK(!prefile.online.has_value());
    CHECK(prefile.prefile.has_value());
    CHECK(prefile.prefile->route == "PREFILE ROUTE");
    const auto missing = hoppie::parseVatsimData(data, "9999999");
    CHECK(!missing.online && !missing.prefile);
}

void testSimbrief() {
    const char* data = R"({
      "atc": {"callsign": "cca123"},
      "aircraft": {"icao_code": "b789"},
      "origin": {"icao_code": "zbaa"},
      "destination": {"icao_code": "zspd"},
      "general": {"route": "agvos dumet"},
      "navlog": {"fix": [{"ident": "agvos"}, {"ident": "dumet"}]}
    })";
    const auto result = hoppie::parseSimBriefOfp(data);
    CHECK(result.has_value());
    CHECK(result->callsign == "CCA123");
    CHECK(result->route == "AGVOS DUMET");
    CHECK(result->waypoints.size() == 2);
    CHECK(!hoppie::parseSimBriefOfp(R"({"fetch":{"status":"Error"}})"));
}

}  // namespace

int main() {
    testVatsim();
    testSimbrief();
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "All HoppieDatalinkXP data-source tests passed\n";
    return EXIT_SUCCESS;
}
