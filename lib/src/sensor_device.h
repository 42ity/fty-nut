/*  =========================================================================
    sensor_sensor - structure for device producing alerts

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
#include <fty_common_nut.h>
#include <malamute.h>
#include <map>
#include <nutclient.h>
#include <string>

class Sensor
{
public:
    // port | child_name
    typedef std::map<std::string, std::string> ChildrenMap;
    Sensor()
        : _asset(nullptr)
        , _parent(nullptr)
        , _nutMaster("invalidNutMaster")
        , _index(0){};
    Sensor(const AssetState::Asset* asset, const AssetState::Asset* parent, ChildrenMap& children)
        : _asset(asset)
        , _parent(parent)
        , _children(children)
        , _nutMaster(asset->location())
        , _index(0){};
    Sensor(const AssetState::Asset* asset, const AssetState::Asset* parent, ChildrenMap& children, int index)
        : _asset(asset)
        , _parent(parent)
        , _children(children)
        , _nutMaster(asset->location())
        , _index(index){};
    Sensor(const AssetState::Asset* asset, const AssetState::Asset* parent, ChildrenMap& children,
        const std::string& nutMaster, int index)
        : _asset(asset)
        , _parent(parent)
        , _children(children)
        , _nutMaster(nutMaster)
        , _index(index){};

    void        update(nut::TcpClient& conn, const std::map<std::string, std::string>& mapping);
    void        publish(mlm_client_t* client, int ttl);
    void        addChild(const std::string& port, const std::string& child_name);
    ChildrenMap getChildren();
    std::string assetName() const
    {
        return _asset ? _asset->name() : std::string();
    }
    // get the daisychain value of the parent powerdevice, not the sensor
    int chain() const
    {
        return _parent ? _parent->daisychain() : 0;
    }
    std::string location() const
    {
        return _asset ? _asset->location() : std::string();
    }
    std::string port() const
    {
        if (_asset && !_asset->port().empty())
            return _asset->port();
        return "0";
    }
    const fty::nut::KeyValues& inventory() const
    {
        return _inventory;
    }
    // get sub_address in endpoint
    std::string subAddress() const
    {
        if (_asset && !_asset->endpoint().empty()) {
            const auto it = _asset->endpoint().find("sub_address");
            if (it != _asset->endpoint().end()) {
                return it->second;
            }
        }
        return "";
    }

    void setContacts(const std::vector<std::string>& contacts);
    void setHumidity(const std::string& humidity);
    void setInventory(const fty::nut::KeyValues& values);
    void setTemperature(const std::string& temp);

    std::string topicSuffixExternal(const std::string& port) const;
    std::string sensorPrefix() const;
    std::string nutPrefix() const;
    int         nutIndex() const;
    std::string topicSuffix() const;

protected:
    const AssetState::Asset *_asset, *_parent;
    ChildrenMap              _children;
    std::string              _nutMaster;
    int                      _index;

    std::string              _temperature;
    std::string              _humidity;
    std::vector<std::string> _contacts; // contact status
    fty::nut::KeyValues      _inventory;
};
