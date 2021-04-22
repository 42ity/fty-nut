/*  ========================================================================
    Copyright (C) 2020 Eaton
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
        MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
    ========================================================================
*/

#define CATCH_CONFIG_MAIN // This tells Catch to provide a main() - only do this in one cpp file
#include <catch2/catch.hpp>

#include <filesystem>
#include "include/fty_nut.h"
#include "src/fty_nut_classes.h"

typedef struct {
    const char *testname;           // test name, can be called from command line this way
    void (*test) (bool);            // function to run the test (or NULL for private tests)
} test_item_t;

#define SENSOR_LIST_TESTS_DISABLED  //FIXME known issue
//#undef SENSOR_LIST_TESTS_DISABLED
#ifdef SENSOR_LIST_TESTS_DISABLED
#   pragma message "=== SENSOR_LIST_TESTS_DISABLED ==="
#endif

static test_item_t
all_tests [] = {
    { "cidr", cidr_test },
    { "actor_commands", actor_commands_test },
    { "ups_status", ups_status_test },
    { "nut_device", nut_device_test },
    { "nut_agent", nut_agent_test },
    { "nut_configurator", nut_configurator_test },
    { "alert_device", alert_device_test },
    { "alert_device_list", alert_device_list_test },
    { "sensor_device", sensor_device_test },
#ifndef SENSOR_LIST_TESTS_DISABLED
    { "sensor_list", sensor_list_test },
#endif
    { "state_manager", state_manager_test },

    { "alert_actor", alert_actor_test },
    { "sensor_actor", sensor_actor_test },
    { "fty_nut_server", fty_nut_server_test },
    { "fty_nut_command_server", fty_nut_command_server_test },
    { "fty_nut_configurator_server", fty_nut_configurator_server_test },

    {NULL, NULL}          //  Sentinel
};

//  -------------------------------------------------------------------------
//  Run all tests.
//

static void
test_runall (bool verbose)
{
    printf ("Running fty-nut selftests...\n");
    test_item_t *item;
    for (item = all_tests; item->testname; item++) {
        if (item->test)
            item->test (verbose);
    }
    printf ("Tests passed OK\n");
}

TEST_CASE("All the stuff of before")
{
    std::cout << "Current path is " << std::filesystem::current_path() << std::endl;
    test_runall(true);

#ifdef SENSOR_LIST_TESTS_DISABLED
    std::cout << "\n=== SENSOR_LIST_TESTS_DISABLED\n" << std::endl;
#endif
}
