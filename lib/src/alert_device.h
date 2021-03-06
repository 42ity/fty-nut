/*  =========================================================================
    alert_device - structure for device producing alerts

    Copyright (C) 2014 - 2020 Eaton

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

#pragma once

#include "asset_state.h"
#include <malamute.h>
#include <map>
#include <memory>
#include <nutclient.h>
#include <string>

struct DeviceAlert
{
    std::string name;
    std::string lowWarning;
    std::string highWarning;
    std::string lowCritical;
    std::string highCritical;
    std::string status;
    int64_t     timestamp     = 0;
    bool        rulePublished = false;
    bool        ruleRescanned = false;
};

class Device
{
public:
    Device()
        : _asset(nullptr)
        , _scanned(false){};
    explicit Device(std::shared_ptr<AssetState::Asset> asset)
        : _asset(asset)
        , _nutName(asset->name())
        , _scanned(false){};
    Device(std::shared_ptr<AssetState::Asset> asset, const std::string& nut)
        : _asset(asset)
        , _nutName(nut)
        , _scanned(false){};

    void assetPtr(std::shared_ptr<AssetState::Asset> asset)
    {
        _asset = asset;
    }
    std::shared_ptr<AssetState::Asset> assetPtr() const
    {
        return _asset;
    }
    void nutName(const std::string& aName)
    {
        _nutName = aName;
    };
    std::string nutName() const
    {
        return _nutName;
    }
    std::string assetName() const
    {
        return _asset ? _asset->name() : std::string();
    }
    int chain() const
    {
        return _asset ? _asset->daisychain() : 0;
    }
    int scanned() const
    {
        return _scanned;
    }

    void update(nut::TcpClient& conn);
    int  scanCapabilities(nut::TcpClient& conn);
    void publishAlerts(mlm_client_t* client, uint64_t ttl);
    void publishRules(mlm_client_t* client);

public:
    void addAlert(const std::string& quantity, const std::map<std::string, std::vector<std::string>>& variables);
    const std::map<std::string, DeviceAlert>& alerts() const;
    std::map<std::string, DeviceAlert>& alerts();

private:
    std::shared_ptr<AssetState::Asset> _asset;
    std::string                        _nutName;
    bool                               _scanned;
    std::map<std::string, DeviceAlert> _alerts;


    void        publishAlert(mlm_client_t* client, DeviceAlert& alert, uint64_t ttl);
    void        publishRule(mlm_client_t* client, DeviceAlert& alert);
    void        fixAlertLimits(DeviceAlert& alert);
    std::string daisychainPrefix() const;
};
