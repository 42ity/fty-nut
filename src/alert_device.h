/*  =========================================================================
    alert_device - structure for device producing alerts

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

#ifndef __ALERT_DEVICE
#define __ALERT_DEVICE

#include <map>
#include <string>
#include <nutclient.h>
#include <malamute.h>

#include "fty_nut_library.h"
#include "alert_device_alert.h"

class Device {
 public:
    Device () : _chain(0), _scanned(false) { };
    Device (const std::string& name) :
        _nutName(name),
        _assetName(name),
        _chain(0),
        _scanned(false)
    { };
    Device (const std::string& asset, const std::string& nut, int chain) :
        _nutName(nut),
        _assetName(asset),
        _chain(chain),
        _scanned(false)
    { };

    void nutName (const std::string& aName) { _nutName = aName; };
    std::string nutName () const { return _nutName; }
    void assetName (const std::string& aName) { _assetName = aName; };
    std::string assetName () const { return _assetName; }
    void chain (int index) { _chain = index; };
    int chain () const { return _chain; }
    int scanned () const { return _scanned; }

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
    bool _scanned;

    std::map <std::string, DeviceAlert> _alerts;

    void addAlert (
        const std::string& quantity,
        const std::map<std::string,std::vector<std::string> >& variables
    );
    void publishAlert (mlm_client_t *client, DeviceAlert& alert);
    void publishRule (mlm_client_t *client, DeviceAlert& alert);
    void fixAlertLimits (DeviceAlert& alert);
    std::string daisychainPrefix() const;
};

//  Self test of this class
FTY_NUT_EXPORT void
    alert_device_test (bool verbose);

#endif // __ALERT_DEVICE
