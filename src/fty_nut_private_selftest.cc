/*  =========================================================================
    fty_nut_private_selftest.c - run private classes selftests

    Runs all private classes selftests.

    -------------------------------------------------------------------------
    Copyright (C) 2014 - 2018 Eaton

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

################################################################################
#  THIS FILE IS 100% GENERATED BY ZPROJECT; DO NOT EDIT EXCEPT EXPERIMENTALLY  #
#  Read the zproject/README.md for information about making permanent changes. #
################################################################################
    =========================================================================
*/

#include "fty_nut_classes.h"


//  -------------------------------------------------------------------------
//  Run all private classes tests.
//

void
fty_nut_private_selftest (bool verbose, const char *subtest)
{
// Tests for stable private classes:
    if (streq (subtest, "$ALL") || streq (subtest, "assets_test"))
        assets_test (verbose);
    if (streq (subtest, "$ALL") || streq (subtest, "nutdumpdata_test"))
        nutdumpdata_test (verbose);
    if (streq (subtest, "$ALL") || streq (subtest, "device_scan_test"))
        device_scan_test (verbose);
    if (streq (subtest, "$ALL") || streq (subtest, "range_scan_test"))
        range_scan_test (verbose);
    if (streq (subtest, "$ALL") || streq (subtest, "scan_dns_test"))
        scan_dns_test (verbose);
    if (streq (subtest, "$ALL") || streq (subtest, "scan_nut_test"))
        scan_nut_test (verbose);
    if (streq (subtest, "$ALL") || streq (subtest, "actor_commands_test"))
        actor_commands_test (verbose);
    if (streq (subtest, "$ALL") || streq (subtest, "ups_status_test"))
        ups_status_test (verbose);
    if (streq (subtest, "$ALL") || streq (subtest, "nut_device_test"))
        nut_device_test (verbose);
    if (streq (subtest, "$ALL") || streq (subtest, "nut_agent_test"))
        nut_agent_test (verbose);
    if (streq (subtest, "$ALL") || streq (subtest, "nut_configurator_test"))
        nut_configurator_test (verbose);
    if (streq (subtest, "$ALL") || streq (subtest, "alert_device_test"))
        alert_device_test (verbose);
    if (streq (subtest, "$ALL") || streq (subtest, "alert_device_list_test"))
        alert_device_list_test (verbose);
    if (streq (subtest, "$ALL") || streq (subtest, "sensor_device_test"))
        sensor_device_test (verbose);
    if (streq (subtest, "$ALL") || streq (subtest, "sensor_list_test"))
        sensor_list_test (verbose);
    if (streq (subtest, "$ALL") || streq (subtest, "state_manager_test"))
        state_manager_test (verbose);
}
/*
################################################################################
#  THIS FILE IS 100% GENERATED BY ZPROJECT; DO NOT EDIT EXCEPT EXPERIMENTALLY  #
#  Read the zproject/README.md for information about making permanent changes. #
################################################################################
*/
