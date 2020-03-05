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

#include "fty_nut_configuration_manager.h"
#include "fty_nut_configuration_server.h"
#include "fty_nut_library.h"

namespace fty
{
namespace nut
{

class ConfigurationConnector
{
    public:
        struct Parameters
        {
            Parameters();

            std::string endpoint;
            std::string agentName;
            std::string requesterName;

            unsigned threadPoolSize;
            bool automaticPrioritySort;

            bool rescanOnSecurityWalletCreate;
            bool rescanOnSecurityWalletUpdate;
            bool rescanOnSecurityWalletDelete;
        };

        ConfigurationConnector(Parameters parameters, ConfigurationManager& manager);
        ~ConfigurationConnector() = default;

        void triggerRescan();

    private:
        /**
         * \brief Handle request assets message.
         * \param msg Message receive.
         */
        void handleRequestAssets(messagebus::Message msg);

        /**
         * \brief Handle request asset detail message.
         * \param msg Message received.
         */
        void handleRequestAssetDetail(messagebus::Message msg);

        /**
         * \brief Handle request message of type asset notification.
         * \param msg Message received.
         */
        void handleNotificationAssets(messagebus::Message msg);

        /**
         * \brief Handle request message of type security document added.
         * \param portfolio Portfolio name.
         * \param doc Security document added.
         */
        void handleNotificationSecurityWalletCreate(const std::string& portfolio, secw::DocumentPtr doc);

        /**
         * \brief Handle request message of type security document updated.
         * \param portfolio Portfolio name.
         * \param oldDoc Previous security document.
         * \param newDoc New security document.
         */
        void handleNotificationSecurityWalletUpdate(const std::string& portfolio, secw::DocumentPtr oldDoc, secw::DocumentPtr newDoc);

        /**
         * \brief Handle request message of type security document deleted.
         * \param portfolio Portfolio name.
         * \param doc Security document removed.
         */
        void handleNotificationSecurityWalletDelete(const std::string& portfolio, secw::DocumentPtr doc);

        /**
         * \brief Request asset driver configuration refresh.
         * \param assetNames Assets to refresh.
         */
        void publishToDriverConnector(const std::vector<std::string>& assetNames);

        /**
         * \brief Handle asset update.
         * \param data Asset data in fty_proto form.
         * \param forceScan Force scan of asset.
         */
        void handleAsset(const std::string& data, bool forceScan = false);

        Parameters m_parameters;
        ConfigurationManager& m_manager;
        messagebus::PoolWorker m_worker;
        std::unique_ptr<messagebus::MessageBus> m_msgBusReceiver;
        std::unique_ptr<messagebus::MessageBus> m_msgBusRequester;

        fty::SocketSyncClient m_syncClient;
        mlm::MlmStreamClient m_streamClient;
        secw::ConsumerAccessor m_consumerAccessor;
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
