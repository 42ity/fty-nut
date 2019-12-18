/*  =========================================================================
    fty_nut_configuration_server - fty nut configuration actor

    Copyright (C) 2014 - 2018 Eaton

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

#ifndef FTY_NUT_CONFIGURATION_SERVER_H_INCLUDED
#define FTY_NUT_CONFIGURATION_SERVER_H_INCLUDED

#include "fty_nut_library.h"

namespace fty
{
namespace nut
{

class ConfigurationManager
{
    public:
        ConfigurationManager(const std::string& dbConn);
        ~ConfigurationManager() = default;

        void automaticAssetConfigurationPrioritySort(const std::string& asset);
        void scanAssetConfigurations(const std::string& asset);
    
    private:
        messagebus::PoolWorker m_poolScanners;
        std::string m_dbConn;
};

class ConfigurationConnector
{
    public:
        struct Parameters {
            Parameters();

            std::string endpoint;
            std::string agentName;

            std::string dbUrl;
        };

        ConfigurationConnector(Parameters params);
        ~ConfigurationConnector() = default;

    private:
        void handleRequest(messagebus::Message msg);
        void sendReply(const messagebus::MetaData& metadataRequest, bool status, const messagebus::UserData& dataReply);

        Parameters m_parameters;
        ConfigurationManager m_manager;
        messagebus::Dispatcher<std::string, std::function<messagebus::UserData(messagebus::UserData)>, std::function<messagebus::UserData(const std::string&, messagebus::UserData)>> m_dispatcher;
        messagebus::PoolWorker m_worker;
        std::unique_ptr<messagebus::MessageBus> m_msgBus;
};

}
}

#ifdef __cplusplus
extern "C" {
#endif

//  Self test of this class
FTY_NUT_EXPORT void fty_nut_configuration_server_test (bool verbose);

#ifdef __cplusplus
}
#endif

#endif
