/*  =========================================================================
    alert_device_list - list of devices, producing alerts

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
#ifndef __ALERT_DEVICE_LIST
#define __ALERT_DEVICE_LIST

#include "agent_nut_library.h"
#include "alert_device.h"
#include "nut.h"

class Devices {
 public:
    void updateFromNUT ();
    void updateDeviceList (nut_t *config);
    void publishAlerts (mlm_client_t *client);
    void publishRules (mlm_client_t *client);

    // friend function for unit-testing
    friend void alert_actor_test (bool verbose);
 private:
    std::map <std::string, Device>  _devices;

    void updateDeviceCapabilities (nut::TcpClient& nutClient);
    void updateDevices (nut::TcpClient& nutClient);
    void addIfNotPresent (Device dev);
};

#endif // __ALERT_DEVICE_LIST
