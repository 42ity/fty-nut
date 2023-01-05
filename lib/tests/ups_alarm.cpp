#include "src/ups_alarm.h"
#include <catch2/catch.hpp>

TEST_CASE("ups alarm test")
{
    struct {
        std::string alarms; // verbatim
        uint32_t bitsfield; // expected
    } testVector[] = {
        { "", 0 },
        { "Internal UPS fault!", 1 << 8L },
        { "Internal failure!", 1 << 8L },
        { "unknown alarm", uint32_t(1 << 31L) }, // other alarms
    };

    printf("upsalarm_to_int\n");
    for (auto &it : testVector) {
        printf("\t'%s' : 0x%x\n", it.alarms.c_str(), it.bitsfield);
        CHECK(upsalarm_to_int(it.alarms) == it.bitsfield);
    }
}
