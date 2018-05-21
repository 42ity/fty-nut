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

#include <map>
#include <vector>
#include <string>

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
    bool configure( const std::string &name, const AutoConfigurationInfo &info );
    void erase(const std::string &name);
    static bool known_assets(std::vector<std::string>& assets);
 private:
    static std::vector<std::string>::const_iterator selectBest( const std::vector<std::string> &configs);
    static void updateNUTConfig();
    static std::vector<std::string>::const_iterator stringMatch( const std::vector<std::string> &texts, const char *pattern);
    static bool match( const std::vector<std::string> &texts, const char *pattern);
    static bool isEpdu( const std::vector<std::string> &texts);
    static bool isAts( const std::vector<std::string> &texts);
    static bool isUps( const std::vector<std::string> &texts);
    static bool canSnmp( const std::vector<std::string> &texts);
    static bool canXml( const std::vector<std::string> &texts);
    static std::vector<std::string>::const_iterator getBestSnmpMib( const std::vector<std::string> &configs);
    static void systemctl( const std::string &operation, const std::string &service );
};

//  Self test of this class
void nut_configurator_test (bool verbose);

#endif
