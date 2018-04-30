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

void Devices::updateDeviceList(nut_t *config)
{
    if (!config) return;
    zlist_t *devices = nut_get_powerdevices (config);
    if (!devices) return;

    log_debug("aa: updating device list");
    std::map<std::string, std::string> ip2master;
    {
        // make ip->master map
        const char *name = (char *)zlist_first(devices);
        while (name) {
            const char* ip = nut_asset_ip (config, name);
            const char* chain = nut_asset_daisychain (config, name);
            if (ip == NULL || chain == NULL || streq (ip, "") ) {
                // this is strange. No IP?
                name = (char *)zlist_next(devices);
                continue;
            }
            if (streq (chain,"") || streq (chain,"1")) {
                // this is master
                ip2master[ip] = name;
            }
            name = (char *)zlist_next(devices);
        }
    }
    {
        // add new/changed devices
        const char *name = (char *)zlist_first(devices);
        while (name) {
            const char* ip = nut_asset_ip (config, name);
            if (!ip || streq (ip, "")) {
                // this is strange. No IP?
                name = (char *)zlist_next(devices);
                continue;
            }
            const char* chain_str = nut_asset_daisychain (config, name);
            int chain = 0;
            if (chain_str) try { chain = std::stoi (chain_str); } catch(...) { };
            switch(chain) {
            case 0:
                addIfNotPresent (Device (name));
                break;
            case 1:
                addIfNotPresent (Device (name, name, 1));
                break;
            default:
                const auto master_it = ip2master.find (ip);
                if (master_it == ip2master.cend()) {
                    log_error ("Daisychain host for %s not found", name);
                } else {
                    addIfNotPresent (Device (name, master_it->second, chain));
                }
                break;
            }
            name = (char *)zlist_next(devices);
        }
    }
    {
        // remove devices
        auto it = _devices.begin();
        while ( it != _devices.end ()) {
            if (nut_asset_ip(config, it->first.c_str ()) == NULL) {
                auto tbd = it;
                ++it;
                _devices.erase (tbd);
            } else {
                ++it;
            }
        }
    }
    zlist_destroy (&devices);
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
