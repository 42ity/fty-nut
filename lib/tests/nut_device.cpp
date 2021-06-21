#include <catch2/catch.hpp>
#include "src/nut_device.h"

TEST_CASE("nut device test")
{
    #define SELFTEST_RO "tests/selftest-ro"

    // test case: load the mappig file
    drivers::nut::NUTDeviceList self;
    const char*                 path = SELFTEST_RO "/mapping.conf";

    self.load_mapping(path);
}
