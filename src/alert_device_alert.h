#ifndef __ALERT_DEVICE_ALERT
#define __ALERT_DEVICE_ALERT

#include <string>

struct DeviceAlert {
    std::string name;
    std::string lowWarning;
    std::string highWarning;
    std::string lowCritical;
    std::string highCritical;
    std::string status;
    int64_t timestamp = 0;
    bool rulePublished = false;
};

#endif // __ALERT_DEVICE_ALERT
