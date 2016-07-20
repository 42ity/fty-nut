/*  =========================================================================
    sensor_sensor - structure for device producing alerts

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

#ifndef __SENSOR_DEVICE_H
#define __SENSOR_DEVICE_H

#include <map>
#include <string>
#include <nutclient.h>
#include <malamute.h>

#include "agent_nut_library.h"
#include "alert_device_alert.h"

class Sensor {
 public:
    Sensor (const std::string& asset, const std::string& nut, int chain, std::string port, std::string logical_asset) :
        _nutName(nut),
        _chain(chain),
        _location(asset),
        _port(port),
        _logical_asset(logical_asset)
        { };
    Sensor () { };
    void update (nut::TcpClient &conn);
    void publish (mlm_client_t *client);
    // friend functions for unit-testing
    // friend void alert_device_test (bool verbose);
    // friend void alert_actor_test (bool verbose);
 private:
    std::string _nutName;
    int _chain;
    std::string _location;
    std::string _port;
    std::string _logical_asset;
    std::string _temperature;
    std::string _humidity;
    int _ttl = 300;

    std::string sensorPrefix() const;
};

AGENT_NUT_EXPORT void
sensor_device_test(bool verbose);


#endif // __ALERT_DEVICE
