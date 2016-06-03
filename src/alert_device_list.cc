#include <malamute.h>
#include <nutclient.h>
#include <exception>

#include "alert_device_list.h"
#include "agent_nut_library.h"
#include "logger.h"

void Devices::updateFromNUT ()
{
    try {
        nut::TcpClient nutClient;
        nutClient.connect ("localhost", 3493);
        if (!_capabilitiesUpdated) updateDeviceCapabilities (nutClient);
        updateDevices (nutClient);
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
    _capabilitiesUpdated = true;
    for (auto& it : _devices) {
        if (! it.second.scanCapabilities (nutClient)) _capabilitiesUpdated = false;
    }
    log_debug ("aa: capabilities updated %i", _capabilitiesUpdated);
}

int Devices::addIfNotPresent (Device dev) {
    auto it = _devices.find (dev.assetName ());
    if (it == _devices.end ()) {
        _devices[dev.assetName ()] = dev;
        return 1;
    }
    if (dev.nutName () != it->second.nutName() || dev.chain() != it->second.chain()) {
        _devices[dev.assetName ()] = dev;
        return 1;        
    }
    return 0;
}

void Devices::updateDeviceList(nut_t *config)
{
    if (!config) return;
    zlistx_t *devices = nut_get_assets (config);
    if (!devices) return;
    int changes = 0;

    log_debug("aa: updating device list");
    std::map<std::string, std::string> ip2master;
    {
        // make ip->master map
        const char *name = (char *)zlistx_first(devices);
        while (name) {
            const char* ip = nut_asset_ip (config, name);
            const char* chain = nut_asset_daisychain (config, name);
            if (ip == NULL || chain == NULL || streq (ip, "") ) {
                // this is strange. No IP?
                name = (char *)zlistx_next(devices);
                continue;
            }
            if (streq (chain,"") || streq (chain,"1")) {
                // this is master
                ip2master[ip] = name;
            }
            name = (char *)zlistx_next(devices);
        }
    }
    {
        // add new/changed devices
        const char *name = (char *)zlistx_first(devices);
        while (name) {
            const char* ip = nut_asset_ip (config, name);
            if (!ip || streq (ip, "")) {
                // this is strange. No IP?
                name = (char *)zlistx_next(devices);
                continue;
            }
            const char* chain_str = nut_asset_daisychain (config, name);
            int chain = 0;
            if (chain_str) try { chain = std::stoi (chain_str); } catch(...) { };
            switch(chain) {
            case 0:
                changes += addIfNotPresent (Device (name));
                break;
            case 1:
                changes += addIfNotPresent (Device (name, name, 1));
                break;
            default:
                const auto master_it = ip2master.find (ip);
                if (master_it == ip2master.cend()) {
                    log_error ("Daisychain master for %s not found", name);
                } else {
                    changes += addIfNotPresent (Device (name, master_it->second, chain));
                }
                break;
            }
            name = (char *)zlistx_next(devices);
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
    zlistx_destroy (&devices);
    _capabilitiesUpdated = (changes == 0);
}

void Devices::publishAlerts (mlm_client_t *client)
{
    if (!client) return;
    for (auto &device : _devices) {
        device.second.publishAlerts (client);
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
