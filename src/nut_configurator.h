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

#include <map>
#include <vector>
#include <string>

// core.git/src/shared/asset_types.h
enum asset_type {
    TUNKNOWN     = 0,
    GROUP       = 1,
    DATACENTER  = 2,
    ROOM        = 3,
    ROW         = 4,
    RACK        = 5,
    DEVICE      = 6
};

enum asset_subtype {
    SUNKNOWN = 0,
    UPS = 1,
    GENSET,
    EPDU,
    PDU,
    SERVER,
    FEED,
    STS,
    SWITCH,
    STORAGE,
    VIRTUAL,
    N_A = 11
    /* ATTENTION: don't change N_A id. It is used as default value in init.sql for types, that don't have N_A */
};

enum asset_operation {
    INSERT = 1,
    DELETE,
    UPDATE,
    GET,
    RETIRE
};


struct AutoConfigurationInfo
{
    uint32_t type = 0;
    uint32_t subtype = 0;
    int8_t operation = 0;
    bool configured = false;
    time_t date = 0;
    std::map<std::string,std::string> attributes;
};

class NUTConfigurator {
 public:
    bool configure( const std::string &name, const AutoConfigurationInfo &info );
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
