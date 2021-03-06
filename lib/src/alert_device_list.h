/*  =========================================================================
    alert_device_list - list of devices, producing alerts

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
#pragma once

#include "alert_device.h"
#include "state_manager.h"

class Devices
{
public:
    explicit Devices(StateManager::Reader* reader);
    void updateFromNUT();
    void updateDeviceList();
    void publishAlerts(mlm_client_t* client);
    void publishRules(mlm_client_t* client);
    void setPollingMs(uint64_t polling_ms)
    {
        _polling_ms = polling_ms;
    }

    std::map<std::string, Device>& devices();

private:
    uint64_t                              _polling_ms = 30000;
    std::map<std::string, Device>         _devices;
    std::unique_ptr<StateManager::Reader> _state_reader;

    void updateDeviceCapabilities(nut::TcpClient& nutClient);
    void updateDevices(nut::TcpClient& nutClient);
    void addIfNotPresent(const Device& dev);
};
