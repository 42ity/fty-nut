/*  =========================================================================
    alert_device_alert - structure for one alert comming from device

    Copyright (C) 2014 - 2016 Eaton

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
#ifndef __ALERT_DEVICE_ALERT
#define __ALERT_DEVICE_ALERT

#include <string>

struct DeviceAlert {
    std::string name;
    std::string lowWarning;
    std::string highWarning;
    std::string lowCritical;
    std::string highCritical;
    std::string status;
    int64_t timestamp = 0;
    bool rulePublished = false;
};

#endif // __ALERT_DEVICE_ALERT
