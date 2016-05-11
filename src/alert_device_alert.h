#ifndef __ALERT_DEVICE_ALERT
#define __ALERT_DEVICE_ALERT

#include <string>

typedef struct DeviceAlert {
    std::string name;
    std::string lowWarning;
    std::string highWarning;
    std::string lowCritical;
    std::string highCritical;
    std::string value;
} device_alert_t;

#endif // __ALERT_DEVICE_ALERT
