/*  =========================================================================
    ups_status - ups status converting functions

    Copyright (C) 2014 - 2020 Eaton

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
    =========================================================================
*/

/*
@header
    ups_status - ups status converting functions
@discuss
@end
*/

#include "ups_status.h"
#include <fty_common_base.h>
#include <string.h>
#include <assert.h>

// following definition is taken as it is from network ups tool project (dummy-ups.h):
typedef struct {
    const char  *status_str;    /* ups.status string */
    int          status_value;  /* ups.status flag bit */
} status_lkp_t;

// following definition is taken as it is from network ups tool project (dummy-ups.h):
// Status lookup table
static status_lkp_t status_info[] = {
    { "CAL", STATUS_CAL },
    { "TRIM", STATUS_TRIM },
    { "BOOST", STATUS_BOOST },
    { "OL", STATUS_OL },
    { "OB", STATUS_OB },
    { "OVER", STATUS_OVER },
    { "LB", STATUS_LB },
    { "RB", STATUS_RB },
    { "BYPASS", STATUS_BYPASS },
    { "OFF", STATUS_OFF },
    { "CHRG", STATUS_CHRG },
    { "DISCHRG", STATUS_DISCHRG },
    { "HB", STATUS_HB },
    { "FSD", STATUS_FSD },
    { "ALARM", STATUS_ALARM },
    { "NULL", 0 },
};

static uint16_t
s_upsstatus_single_status_to_int (const char *status) {
    if (!status)
        return 0;

    int i = 0;
    while(true) {
        if( status_info[i].status_value == 0 ) {
            // end of array, not found
            return 0;
        }
        if( strncasecmp(status_info[i].status_str, status, strlen(status_info[i].status_str) ) == 0 ) {
            return status_info[i].status_value;
        }
        i++;
    }
}


uint16_t
upsstatus_to_int (const char *status, const char *test_result)
{
    uint16_t result = 0;
    char *buff = strdup (status);
    char *b = buff;
    char *e;

    if(!buff) {
        return 0;
    }
    while(b) {
        e = strchr(b,' ');
        if(e) {
            *e = 0;
            e++;
        }
        result |= s_upsstatus_single_status_to_int (b);
        b = e;
    }
    if (buff) {
        free (buff);
        buff = NULL;
    }
    //detect if a test is in progress 
    if(streq(test_result,"in progress")){
        //add calibration (CAL) flag to ups status
        result |= s_upsstatus_single_status_to_int ("CAL");
    }

    // IPMVAL-1889: in some rare case, OL *and* OB bits are unset.
    // This implies unexpected up/down trigger of onbattery & onacpoweroutage alarms, based on status.ups metric.
    // In such a case, we try to set OL/OB bits knowing CHRG/DISCHRG bits.
    if (!(result & (STATUS_OL | STATUS_OB))) { // !OL && !OB
        if ((result & STATUS_CHRG) && !(result & STATUS_DISCHRG)) { // CHRG && !DISCHRG
            result |= STATUS_OL; // set OL
        }
        else if (!(result & STATUS_CHRG) && (result & STATUS_DISCHRG)) { // !CHRG && DISCHRG
            result |= STATUS_OB; // set OB
        }
    }

    return result;
}

uint16_t
upsstatus_to_int (const std::string& status, const std::string& test_result)
{
    return upsstatus_to_int (status.c_str(), test_result.c_str());
}

std::string
upsstatus_to_string (uint16_t status)
{
    std::string result = "";
    int bit = 1;
    for (unsigned int i = 0; i < sizeof (status_info) / sizeof (status_lkp_t) - 1 ; ++i) {
        if (status & bit) {
            if (result.length ()) {
                result += " ";
            }
            result += status_info[i].status_str;
        }
        bit <<= 1;
    }
    return result;
}

std::string
upsstatus_to_string (const std::string& status)
{
    return upsstatus_to_string (atoi (status.c_str ()));
}

//  --------------------------------------------------------------------------
//  Self test of this class

void
ups_status_test (bool verbose)
{
    printf (" * ups_status: ");

    //  @selftest

    struct {
        const char* status;
        uint16_t result;
    } test_vector[] = {
        { "",        0 },
        { "foo",     0 },
        { "CAL",     STATUS_CAL },
        { "TRIM",    STATUS_TRIM },
        { "BOOST",   STATUS_BOOST },
        { "OL",      STATUS_OL },
        { "OB",      STATUS_OB },
        { "OVER",    STATUS_OVER },
        { "LB",      STATUS_LB },
        { "RB",      STATUS_RB },
        { "BYPASS",  STATUS_BYPASS },
        { "OFF",     STATUS_OFF },
    //  { "CHRG",    STATUS_CHRG}, // see WA IPMVAL-1889
    //  { "DISCHRG", STATUS_DISCHRG},
        { "HB",      STATUS_HB },
        { "FSD",     STATUS_FSD },
        { "ALARM",   STATUS_ALARM },

        // WA IPMVAL-1889
        { "OL",           STATUS_OL },
        { "OL DISCHRG",   STATUS_OL | STATUS_DISCHRG },
        { "OL CHRG",      STATUS_OL | STATUS_CHRG },
        { "OB",           STATUS_OB },
        { "OB DISCHRG",   STATUS_OB | STATUS_DISCHRG },
        { "OB CHRG",      STATUS_OB | STATUS_CHRG },
        { "CHRG DISCHRG", STATUS_CHRG | STATUS_DISCHRG },
        { "CHRG",         STATUS_OL | STATUS_CHRG}, // fix active (set OL)
        { "DISCHRG",      STATUS_OB | STATUS_DISCHRG}, // fix active (set OB)

        { NULL, 0 } //term
    };

    if (verbose) printf("\n");

    for (int i = 0; test_vector[i].status; i++) {
        uint16_t result = upsstatus_to_int(test_vector[i].status, "");
        if (verbose) {
            printf("status %+12s (expected 0x%04x), result: 0x%04x (%-12s)\n",
                test_vector[i].status, test_vector[i].result, result, upsstatus_to_string(result).c_str());
        }
        assert(result == test_vector[i].result);
    }

    //  @end
    printf ("OK\n");
}
