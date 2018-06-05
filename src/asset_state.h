/*  =========================================================================
    asset_state - list of known assets

    Copyright (C) 2014 - 2017 Eaton

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

#ifndef ASSET_STATE_H_INCLUDED
#define ASSET_STATE_H_INCLUDED

#include <unordered_map>
#include <ftyproto.h>
#include <memory>
#include <map>

class AssetState {
public:
    class Asset {
    public:
        // The Asset class is created from a proto message and
        // never modified, since different instances of AssetState may
        // share a pointer to it
        explicit Asset(fty_proto_t *message);
        const std::string& name() const
        {
            return name_;
        }
        const std::string& IP() const
        {
            return IP_;
        }
        const std::string& port() const
        {
            return port_;
        }
        const std::string& subtype() const
        {
            return subtype_;
        }
        const std::string& location() const
        {
            return location_;
        }
        const std::string& upsconf_block() const
        {
            return upsconf_block_;
        }
        const bool have_upsconf_block() const
        {
            return have_upsconf_block_;
        }
        const bool upsconf_enable_dmf() const
        {
            return upsconf_enable_dmf_;
        }
        double maxCurrent() const
        {
            return max_current_;
        }
        double maxPower() const
        {
            return max_power_;
        }
        int daisychain() const
        {
            return daisychain_;
        }
    private:
        std::string name_;
        std::string IP_;
        std::string port_;
        std::string subtype_;
        std::string location_;
        std::string upsconf_block_;
        double max_current_;
        double max_power_;
        bool have_upsconf_block_;
        bool upsconf_enable_dmf_;
        int daisychain_;
    };
    // Update the state from a received fty_proto message. Return true if an
    // update has actually been performed, false if the message was skipped
    bool updateFromProto(fty_proto_t* message);
    // Same for encoded proto messages or licensing messages which are not
    // proto. Note that this overload destroys the passed zmsg
    bool updateFromProto(zmsg_t* message);
    // Build the ip2master map
    void recompute();
    // Use a std::map to process the assets in a defined order each time
    // Additions and removals do not happen _that_ often to worry about
    typedef std::map<std::string, std::shared_ptr<Asset> > AssetMap;
    const AssetMap& getPowerDevices() const
    {
        return powerdevices_;
    }
    const AssetMap& getSensors() const
    {
        return sensors_;
    }
    // Return the name of the asset with given IP address
    const std::string& ip2master(const std::string& ip) const;
private:
    AssetMap powerdevices_;
    AssetMap sensors_;
    std::unordered_map<std::string, std::string> ip2master_;
};

#endif
