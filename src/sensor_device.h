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

#include "alert_device_alert.h"

class Sensor {
 public:
    Sensor (const std::string& nutMaster, int chain, const std::string& location, const std::string& port, std::map <std::string, std::string>& children, const std::string& sname) :
        _nutMaster(nutMaster),
        _chain(chain),
        _location(location),
        _port(port),
        _children (children),
        _sname (sname)
        { };
    Sensor () { };
    void update (nut::TcpClient &conn);
    void publish (mlm_client_t *client, int ttl);
    void addChild (const char* port, const char *child_name);
    std::map <std::string, std::string> getChildren ();

    // friend functions for unit-testing
    friend void sensor_device_test (bool verbose);
    friend void sensor_list_test (bool verbose);
    friend void sensor_actor_test (bool verbose);
 protected:
    std::string _nutMaster;
    int _chain = 0;
    std::string _location;
    std::string _port;
    std::map <std::string, std::string> _children; // port | child_name
    std::string _sname;

    std::string _temperature;
    std::string _humidity;
    std::vector <std::string> _contacts;  // contact status

    std::string topicSuffixExternal (const std::string &port) const;
    std::string sensorPrefix() const;
    std::string nutPrefix() const;
    std::string topicSuffix() const;
    std::string port() const;
};

void sensor_device_test(bool verbose);


#endif // __ALERT_DEVICE
