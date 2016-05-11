#ifndef __ALERT_DEVICE
#define __ALERT_DEVICE

#include <map>
#include <string>

#include "alert_device_alert.h"

class Device {
 public:
    void updateDeviceList () { };
 private:
    std::map <std::string, DeviceAlert> _alerts;
};

#endif // __ALERT_DEVICE
