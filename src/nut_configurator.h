/*  =========================================================================
    nut_configurator - NUT configurator class

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

#ifndef NUT_CONFIGURATOR_H_INCLUDED
#define NUT_CONFIGURATOR_H_INCLUDED

#include "asset_state.h"

#include <set>
#include <vector>
#include <string>
#include <fty_common_nut.h>

struct AutoConfigurationInfo
{
    enum {
        STATE_NEW,
        STATE_CONFIGURING,
        STATE_CONFIGURED,
        STATE_DELETING
    } state;
    // Used to mark visited nodes when refreshing the asset list
    int traversal_color;
    const AssetState::Asset *asset;
};

class NUTConfigurator {
 public:
    ~NUTConfigurator() { commit(); }
    bool configure( const std::string &name, const AutoConfigurationInfo &info );
    void erase(const std::string &name);
    void commit();
    static bool known_assets(std::vector<std::string>& assets);
    // Currently NUT manages services based on config file changes
    // so nut_configurator should not, and this flag defaults to false.
    // This may be ripped out completely after testing, so no fuss about
    // accessor methods to manage this setting.
    bool manage_systemctl {false};
 private:
    fty::nut::DeviceConfigurations::const_iterator getBestSnmpMibConfiguration(const fty::nut::DeviceConfigurations &configs);
    fty::nut::DeviceConfigurations::const_iterator getNetXMLConfiguration(const fty::nut::DeviceConfigurations &configs);
    fty::nut::DeviceConfigurations::const_iterator selectBestConfiguration(const fty::nut::DeviceConfigurations &configs);
    void updateNUTConfig();
    fty::nut::DeviceConfigurations getConfigurationFromUpsConfBlock(const std::string &name, const AutoConfigurationInfo &info);
    fty::nut::DeviceConfigurations getConfigurationFromScanningDevice(const std::string &name, const AutoConfigurationInfo &info);
    void updateDeviceConfiguration(const std::string &name, const AutoConfigurationInfo &info, fty::nut::DeviceConfiguration config);
    static void systemctl( const std::string &operation, const std::string &service );
    template<typename It>
    static void systemctl( const std::string &operation, It first, It last );
    std::set<std::string> start_drivers_;
    std::set<std::string> stop_drivers_;
};

//  Self test of this class
void nut_configurator_test (bool verbose);

#endif
