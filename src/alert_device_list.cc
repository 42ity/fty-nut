#include <malamute.h>
#include <nutclient.h>
#include "alert_device_list.h"
#include "agent_nut_library.h"
#include "logger.h"

void Devices::update()
{
    nut::TcpClient nutClient;
    nutClient.connect ("localhost", 3493);

    updateDeviceList(nutClient);
    updateDevices(nutClient);
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
        for (const auto &it : devs) {
            if (_devices.find (it) == _devices.end ()) {
                auto device = Device (it);
                if (device.scanCapabilities (nutClient)) {
                    log_debug ("aa: adding device %s", it.c_str ());
                    _devices[it] = device;
                } else {
                    log_debug ("aa: not adding device %s", it.c_str ());
                }
                // TODO: create rules
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

}

void Devices::publishRules (mlm_client_t *client)
{

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
