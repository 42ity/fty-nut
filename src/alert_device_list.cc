#include <malamute.h>
#include <nutclient.h>
#include <exception>

#include "alert_device_list.h"
#include "agent_nut_library.h"
#include "logger.h"

void Devices::update()
{
    try {
        nut::TcpClient nutClient;
        nutClient.connect ("localhost", 3493);
        updateDeviceList (nutClient);
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

void Devices::updateDeviceList(nut::TcpClient& nutClient)
{
    try {
        if (!nutClient.isConnected ()) return;
        std::set<std::string> devs = nutClient.getDeviceNames ();
        // add newly appeared devices
        for (const auto& name : devs) {
            auto device = _devices.find (name);
            if (device == _devices.end ()) {
                auto newDevice = Device (name);
                if (newDevice.scanCapabilities (nutClient)) {
                    log_debug ("aa: adding device %s", name.c_str ());
                    _devices[name] = newDevice;
                } else {
                    log_debug ("aa: not adding device %s", name.c_str ());
                }
            } else {
                device->second.scanCapabilities (nutClient);
            }
        }
        // remove missing devices
        auto it = _devices.begin ();
        while( it != _devices.end () ) {
            if( devs.count (it->first) == 0 ){
                auto toBeDeleted = it;
                ++it;
                log_debug ("aa: removing device %s", it->first.c_str ());
                // TODO: remove rules?
                _devices.erase (toBeDeleted);
            } else {
                ++it;
            }
        }
    } catch (...) {}
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
