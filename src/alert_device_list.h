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
    bool _capabilitiesUpdated = false;
    uint64_t _lastUpdate = 0; // TODO: remove when fixed in nut library
    
    void updateDeviceCapabilities (nut::TcpClient& nutClient);
    void updateDevices (nut::TcpClient& nutClient);
    int addIfNotPresent (Device dev);
};
    
#endif // __ALERT_DEVICE_LIST
