/*  =========================================================================
    fty_nut_configuration_connector - fty nut configuration connector

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

#ifndef FTY_NUT_CONFIGURATION_CONNECTOR_H_INCLUDED
#define FTY_NUT_CONFIGURATION_CONNECTOR_H_INCLUDED

#include "fty_nut_library.h"
#include "fty_nut_configuration_manager.h"
#include <fty_security_wallet.h>

namespace fty
{
namespace nut
{

class ConfigurationConnector
{
    public:
        struct Parameters {
            Parameters();

            std::string endpoint;
            std::string agentName;
            std::string publisherName;
            std::string dbUrl;
        };

        ConfigurationConnector(Parameters params);
        ~ConfigurationConnector() = default;

    private:
        void handleRequest(messagebus::Message msg);
        void handleNotificationAssets(messagebus::Message msg);
        void handleNotificationSecurityWalletUpdate(const std::string& portfolio, secw::DocumentPtr oldDoc, secw::DocumentPtr newDoc);
        void handleNotificationSecurityWalletDelete(const std::string& portfolio, secw::DocumentPtr doc);
        void handleNotificationSecurityWalletCreate(const std::string& portfolio, secw::DocumentPtr doc);
        void sendReply(const messagebus::MetaData& metadataRequest, bool status, const messagebus::UserData& dataReply);
        void publish(std::string asset_name, std::string subject);

        Parameters m_parameters;
        ConfigurationManager m_manager;
        messagebus::Dispatcher<std::string, std::function<messagebus::UserData(messagebus::UserData)>,
            std::function<messagebus::UserData(const std::string&, messagebus::UserData)>> m_dispatcher;
        messagebus::PoolWorker m_worker;
        std::unique_ptr<messagebus::MessageBus> m_msgBus;
        std::unique_ptr<messagebus::MessageBus> m_msgBusPublisher;

        fty::SocketSyncClient m_sync_client;
        std::unique_ptr<mlm::MlmStreamClient> m_stream_client;
        std::unique_ptr<secw::ConsumerAccessor> m_consumer_accessor;
        t_asset_mutex_map m_asset_mutex_map;
};

}
}

#ifdef __cplusplus
extern "C" {
#endif

//  Self test of this class
FTY_NUT_EXPORT void fty_nut_configuration_connector_test (bool verbose);

#ifdef __cplusplus
}
#endif

#endif
