#include "src/ups_alarm.h"
#include <catch2/catch.hpp>

TEST_CASE("ups alarm test")
{
    CHECK(upsalarm_to_int("") == 0);
    CHECK(upsalarm_to_int("hello world") == 0);

    // "internal failure' bit used in 'internal-failure' alert rule
    CHECK(upsalarm_to_int("Internal UPS fault!") == (1 << 8L));
    CHECK(upsalarm_to_int("Internal failure!") == (1 << 8L));
}
