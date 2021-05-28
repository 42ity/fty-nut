#include "src/ups_status.h"
#include <catch2/catch.hpp>

TEST_CASE("ups status test")
{
    struct
    {
        const char* status;
        uint16_t    result;
    } test_vector[] = {
        {"", 0}, {"foo", 0}, {"CAL", STATUS_CAL}, {"TRIM", STATUS_TRIM}, {"BOOST", STATUS_BOOST}, {"OL", STATUS_OL},
        {"OB", STATUS_OB}, {"OVER", STATUS_OVER}, {"LB", STATUS_LB}, {"RB", STATUS_RB}, {"BYPASS", STATUS_BYPASS},
        {"OFF", STATUS_OFF},
        //  { "CHRG",    STATUS_CHRG}, // see WA IPMVAL-1889
        //  { "DISCHRG", STATUS_DISCHRG},
        {"HB", STATUS_HB}, {"FSD", STATUS_FSD}, {"ALARM", STATUS_ALARM},

        // WA IPMVAL-1889
        {"OL", STATUS_OL}, {"OL DISCHRG", STATUS_OL | STATUS_DISCHRG}, {"OL CHRG", STATUS_OL | STATUS_CHRG},
        {"OB", STATUS_OB}, {"OB DISCHRG", STATUS_OB | STATUS_DISCHRG}, {"OB CHRG", STATUS_OB | STATUS_CHRG},
        {"CHRG DISCHRG", STATUS_CHRG | STATUS_DISCHRG}, {"CHRG", STATUS_OL | STATUS_CHRG}, // fix active (set OL)
        {"DISCHRG", STATUS_OB | STATUS_DISCHRG},                                           // fix active (set OB)

        {NULL, 0} // term
    };

    for (int i = 0; test_vector[i].status; i++) {
        uint16_t result = upsstatus_to_int(test_vector[i].status, "");
        REQUIRE(result == test_vector[i].result);
    }

    // power_status()
    {
        struct
        {
            uint16_t          ups_status;
            const std::string expected;
        } test_vector1[] = {
            {STATUS_OL, POWERSTATUS_ONLINE},
            {STATUS_OB, POWERSTATUS_ONBATTERY},
            {STATUS_OL | STATUS_OB, POWERSTATUS_UNDEFINED},
            {0, POWERSTATUS_UNDEFINED},
            {0xffff, POWERSTATUS_UNDEFINED},
        };

        for (auto& test : test_vector1) {
            auto ups_status  = test.ups_status;
            auto powerstatus = power_status(ups_status);
            auto expected    = test.expected;
            CHECK(powerstatus == expected);
        }
    }
}
