#ifndef __ALERT_DEVICE
#define __ALERT_DEVICE

#include <map>
#include <string>
#include <nutclient.h>
#include <malamute.h>

#include "agent_nut_library.h"
#include "alert_device_alert.h"

class Device {
 public:
    Device () : _chain(0) { };
    Device (const std::string& name) :
        _nutName(name),
        _assetName(name),
        _chain(0)
    { };
    Device (const std::string& asset, const std::string& nut, int chain) :
        _nutName(nut),
        _assetName(asset),
        _chain(chain)
    { };

    void nutName (const std::string& aName) { _nutName = aName; };
    std::string nutName () const { return _nutName; }
    void assetName (const std::string& aName) { _assetName = aName; };
    std::string assetName () const { return _assetName; }
    void chain (int index) { _chain = index; };
    int chain () const { return _chain; }

    void update (nut::TcpClient &conn);
    int scanCapabilities (nut::TcpClient &conn);
    void publishAlerts (mlm_client_t *client);
    void publishRules (mlm_client_t *client);

    // friend functions for unit-testing
    friend void alert_device_test (bool verbose);
    friend void alert_actor_test (bool verbose);
 private:
    std::string _nutName;
    std::string _assetName;
    int _chain;
    std::map <std::string, DeviceAlert> _alerts;

    void addAlert (
        const std::string& quantity,
        const std::map<std::string,std::vector<std::string> >& variables
    );
    void publishAlert (mlm_client_t *client, DeviceAlert& alert);
    void publishRule (mlm_client_t *client, DeviceAlert& alert);
    std::string daisychainPrefix() const;
};

#endif // __ALERT_DEVICE
