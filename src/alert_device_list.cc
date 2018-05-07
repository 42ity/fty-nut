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
#include "alert_device_list.h"
#include "logger.h"

#include <malamute.h>
#include <nutclient.h>
#include <exception>

Devices::Devices (StateManager::Reader *reader)
    : _state_reader(reader)
{
}

void Devices::updateFromNUT ()
{
    try {
        nut::TcpClient nutClient;
        nutClient.connect ("localhost", 3493);
        updateDeviceCapabilities (nutClient);
        updateDevices (nutClient);
        nutClient.disconnect();
    } catch (std::exception& e) {
        log_error ("reading data from NUT: %s", e.what ());
    }
}

void Devices::updateDevices(nut::TcpClient& nutClient)
{
    for (auto& it : _devices) {
        it.second.update (nutClient);
    }
}

void Devices::updateDeviceCapabilities (nut::TcpClient& nutClient)
{
    for (auto& it : _devices) {
        if (! it.second.scanned ()) it.second.scanCapabilities (nutClient);
    }
}

void Devices::addIfNotPresent (Device dev) {
    auto it = _devices.find (dev.assetName ());
    if (it == _devices.end ()) {
        _devices[dev.assetName ()] = dev;
        return;
    }
    if (dev.nutName () != it->second.nutName() || dev.chain() != it->second.chain()) {
        _devices[dev.assetName ()] = dev;
        return;
    }
}

void Devices::updateDeviceList()
{
    if (!_state_reader->refresh())
        return;
    const AssetState& deviceState = _state_reader->getState();
    auto& devices = deviceState.getPowerDevices();

    log_debug("aa: updating device list");
    for (auto i : devices) {
        const std::string& ip = i.second->IP();
        if (ip.empty()) {
            // this is strange. No IP?
            continue;
        }
        const std::string& name = i.first;
        switch(i.second->daisychain()) {
        case 0:
            addIfNotPresent(Device(i.second.get()));
            break;
        default:
            auto master = deviceState.ip2master(ip);
            if (master.empty()) {
                log_error("Daisychain host for %s not found", name.c_str());
            } else {
                addIfNotPresent(Device(i.second.get(), master));
            }
            break;
        }
    }
    // remove devices
    auto it = _devices.begin();
    while ( it != _devices.end ()) {
        // XXX: Instead of doing these lookups, we could color the elements
        // in _devices with alternating colors and simply erase elements with
        // the old color here
        if (devices.count(it->first) == 0) {
            auto tbd = it;
            ++it;
            _devices.erase (tbd);
        } else {
            ++it;
        }
    }
}

void Devices::publishAlerts (mlm_client_t *client)
{
    if (!client) return;
    for (auto &device : _devices) {
        device.second.publishAlerts (client, (_polling_ms / 1000) * 3);
    }
}

void Devices::publishRules (mlm_client_t *client)
{
    if (!client) return;
    for (auto &device : _devices) {
        device.second.publishRules (client);
    }
}


//  --------------------------------------------------------------------------
//  Self test of this class

void
alert_device_list_test (bool verbose)
{
    printf (" * alert device list: ");
    //  @selftest
    //  @end
    printf ("Empty test - OK\n");
}
