#include "src/alert_device_list.h"
#include <catch2/catch.hpp>

TEST_CASE("alert device test")
{
    //  @selftest
    Device                                          dev;
    std::map<std::string, std::vector<std::string>> nothing = {{"nothing", {"h1", "h2"}}};
    dev.addAlert("ambient.temperature", nothing);
    assert(dev.alerts().empty());

    std::map<std::string, std::vector<std::string>> alerts = {
        {"ambient.temperature.status", {"good", "", ""}},
        {"ambient.temperature.high.warning", {"80", "", ""}},
        {"ambient.temperature.high.critical", {"100", "", ""}},
        {"ambient.temperature.low.warning", {"10", "", ""}},
        {"ambient.temperature.low.critical", {"5", "", ""}},

        {"ambient.humidity.status", {"good", "", ""}},
        {"ambient.humidity.high", {"100", "", ""}},
        {"ambient.humidity.low", {"10", "", ""}},
    };

    dev.addAlert("ambient.temperature", alerts);
    dev.addAlert("ambient.humidity", alerts);
    REQUIRE(dev.alerts().size() == 2);
    CHECK(dev.alerts()["ambient.humidity"].lowWarning == "10");
    CHECK(dev.alerts()["ambient.humidity"].lowCritical == "10");
    CHECK(dev.alerts()["ambient.temperature"].lowWarning == "10");
    CHECK(dev.alerts()["ambient.temperature"].lowCritical == "5");
    CHECK(dev.alerts()["ambient.temperature"].highWarning == "80");
    CHECK(dev.alerts()["ambient.temperature"].highCritical == "100");
}
