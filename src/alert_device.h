#ifndef __ALERT_DEVICE
#define __ALERT_DEVICE

#include <map>
#include <string>
#include <nutclient.h>
#include "alert_device_alert.h"

class Device {
 public:
    Device () { };
    Device (const std::string& name) { _name = name; };

    void name (const std::string& aName) { _name = aName; };
    std::string name () { return _name; }
    
    void update (nut::TcpClient &conn);
    int scanCapabilities (nut::TcpClient &conn);
 private:
    std::string _name;
    std::map <std::string, DeviceAlert> _alerts;

    void addAlert (
        const std::string& quantity,
        const std::map<std::string,std::vector<std::string> >& variables
    );
};

#endif // __ALERT_DEVICE
