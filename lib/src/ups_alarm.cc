/*  =========================================================================
    Copyright (C) 2014 - 2022 Eaton

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

#include "ups_alarm.h"
#include <string>
#include <map>

// recognized alarms
// "internal failure': referenced by 'internal-[failure|alarm]' alert rules
#define OTHER_ALARMS_TOKEN "OTHER_ALARMS"
static const std::map<std::string, uint32_t> alarmsMap =
{
    { "Replace battery!",             1 <<  0L },
    { "Shutdown imminent!",           1 <<  1L },
    { "Fan failure!",                 1 <<  2L },
    { "No battery installed!",        1 <<  3L },
    { "Battery voltage too low!",     1 <<  4L },
    { "Battery voltage too high!",    1 <<  5L },
    { "Battery charger fail!",        1 <<  6L },
    { "Temperature too high!",        1 <<  7L },
    { "Internal UPS fault!",          1 <<  8L }, // "internal failure"
    { "Internal failure!",            1 <<  8L }, // "internal failure"
    { "Awaiting power!",              1 <<  9L },
    { "Automatic bypass mode!",       1 << 10L },
    { "Manual bypass mode!",          1 << 11L },
    { "Communication fault!",         1 << 12L },
    { "Fuse fault!",                  1 << 13L },
    { OTHER_ALARMS_TOKEN,             1 << 31L }, // <<- other alarms (default)
};

uint32_t upsalarm_to_int(const std::string& alarms)
{
    uint32_t bitsfield = 0;
    if (!alarms.empty()) {
        for (const auto& a : alarmsMap) {
            if (alarms.find(a.first) != std::string::npos) {
                bitsfield |= a.second;
            }
        }
        if (bitsfield == 0) {
            // default to other alarms
            const auto& it = alarmsMap.find(OTHER_ALARMS_TOKEN);
            if (it != alarmsMap.end()) {
                bitsfield |= it->second;
            }
        }
    }
    return bitsfield;
}
