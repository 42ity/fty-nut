/*  =========================================================================
    fty_nut_configuration_server - fty nut configuration connector

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

/*
@header
    fty_nut_configuration_connector - fty nut configuration connector
@discuss
@end
*/

#include "fty_nut_library.h"
#include "fty_nut_classes.h"
#include <fty_common_nut_credentials.h>
#include <fty_security_wallet.h>

#include <forward_list>
#include <regex>
#include <future>

#define NUT_PART_STORE "/var/lib/fty/fty-nut/devices"

namespace fty
{
namespace nut
{
static const std::string SECW_SOCKET_PATH = "/run/fty-security-wallet/secw.socket";

SecwMap getCredentials()
{
    SecwMap result;

    // Grab security documents.
    try {
        fty::SocketSyncClient secwSyncClient(SECW_SOCKET_PATH);

        auto client = secw::ConsumerAccessor(secwSyncClient);
        auto secCreds = client.getListDocumentsWithPrivateData("default", "discovery_monitoring");

        for (const auto &i : secCreds) {
            result.emplace(i->getId(), i);
        }
        log_debug("Fetched %d credentials from security wallet.", result.size());
    }
    catch (std::exception &e) {
        log_warning("Failed to fetch credentials from security wallet: %s", e.what());
    }

    return result;
}

ConfigurationConnector::Parameters::Parameters(const uint nbThreadPoolConnector, const uint nbThreadPoolManager,
    const bool scanDummyUps, const bool automaticPrioritySort, const bool prioritizeDmfDriver) :
    endpoint(MLM_ENDPOINT),
    agentName("fty-nut-configuration"),
    requesterName("fty-nut-configuration-requester"),
    dbUrl(DBConn::url),
    nbThreadPoolConnector(nbThreadPoolConnector),
    nbThreadPoolManager(nbThreadPoolManager),
    scanDummyUps(scanDummyUps),
    automaticPrioritySort(automaticPrioritySort),
    prioritizeDmfDriver(prioritizeDmfDriver)
{
}

/**
 * \brief Default constructor of the class.
 * \param params Parameters of the class.
 */
ConfigurationConnector::ConfigurationConnector(ConfigurationConnector::Parameters params) :
    m_parameters(params),
    m_manager(params.dbUrl, params.nbThreadPoolManager, params.scanDummyUps, params.automaticPrioritySort, params.prioritizeDmfDriver),
    m_worker(params.nbThreadPoolConnector),
    m_msgBusReceiver(messagebus::MlmMessageBus(params.endpoint, params.agentName)),
    m_msgBusRequester(messagebus::MlmMessageBus(params.endpoint, params.requesterName)),
    m_syncClient("fty-nut-configuration.socket"),
    m_streamClient(params.agentName, SECW_NOTIFICATIONS, 1000, params.endpoint),
    m_consumerAccessor(secw::ConsumerAccessor(m_syncClient, m_streamClient))
{
    m_msgBusReceiver->connect();
    m_msgBusReceiver->subscribe(FTY_PROTO_STREAM_ASSETS, std::bind(&ConfigurationConnector::handleNotificationAssets, this, std::placeholders::_1));

    m_msgBusRequester->connect();
    m_msgBusRequester->receive("ASSETS", std::bind(&ConfigurationConnector::handleRequestAssets, this, std::placeholders::_1));
    m_msgBusRequester->receive(params.requesterName.c_str(), std::bind(&ConfigurationConnector::handleRequestAssetDetail, this, std::placeholders::_1));

    m_consumerAccessor.setCallbackOnUpdate(std::bind(&ConfigurationConnector::handleNotificationSecurityWalletUpdate, this,
            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    m_consumerAccessor.setCallbackOnDelete(std::bind(&ConfigurationConnector::handleNotificationSecurityWalletDelete, this,
            std::placeholders::_1, std::placeholders::_2));
    m_consumerAccessor.setCallbackOnCreate(std::bind(&ConfigurationConnector::handleNotificationSecurityWalletCreate, this,
            std::placeholders::_1, std::placeholders::_2));
}

/**
 * \brief Get initial assets.
 *
 */
void ConfigurationConnector::getInitialAssets()
{
    // Get list of assets
    messagebus::Message message;
    std::string uuid = messagebus::generateUuid();
    message.userData().push_back("GET");
    message.userData().push_back(uuid.c_str());
    message.userData().push_back("ups");
    message.userData().push_back("epdu");
    message.userData().push_back("sts");
    //message.userData().push_back("sensor");
    //message.userData().push_back("sensorgpio");

    message.metaData().clear();
    message.metaData().emplace(messagebus::Message::RAW, "");
    message.metaData().emplace(messagebus::Message::CORRELATION_ID, uuid);
    message.metaData().emplace(messagebus::Message::SUBJECT, FTY_PROTO_STREAM_ASSETS);
    message.metaData().emplace(messagebus::Message::FROM, m_parameters.requesterName);
    message.metaData().emplace(messagebus::Message::TO, "asset-agent");
    message.metaData().emplace(messagebus::Message::REPLY_TO, m_parameters.requesterName);
    m_msgBusRequester->sendRequest("asset-agent", message);
}

/**
 * \brief Handle request assets message.
 * \param msg Message receive.
 */
void ConfigurationConnector::handleRequestAssets(messagebus::Message msg)
{
    m_worker.offload([this](messagebus::Message msg) {
        try {
            log_debug("handleRequestAssets: Receive message from ASSETS");
            // First unreceive ASSETS message
            m_msgBusRequester->unreceive("ASSETS", std::bind(&ConfigurationConnector::handleRequestAssets, this, std::placeholders::_1));

            if (msg.userData().size() < 2 || *(std::next(msg.userData().cbegin(), 1)) != "OK") {
                throw std::runtime_error("handleRequestAssets: bad ASSETS message");
            }
            
            msg.userData().pop_front(); // Pop UUID.
            msg.userData().pop_front(); // Pop status.

            std::set<std::string> devicesList;
    
            for (const auto& assetName : msg.userData()) {
                // Get detail of asset received
                const std::string uuid = messagebus::generateUuid();
                messagebus::Message message {
                    {
                        { messagebus::Message::RAW, "" },
                        { messagebus::Message::CORRELATION_ID, uuid },
                        { messagebus::Message::SUBJECT, "ASSET_DETAIL" },
                        { messagebus::Message::FROM, m_parameters.requesterName },
                        { messagebus::Message::TO, "asset-agent" },
                        { messagebus::Message::REPLY_TO, m_parameters.requesterName },
                    },
                    { "GET", uuid, assetName },
                };
                log_trace("handleRequestAssets: Get asset details for %s", assetName.c_str());
                devicesList.emplace(assetName);
                m_msgBusRequester->sendRequest("asset-agent", message);
            }

            // For each driver, remove it if not present in current device list
            for (const auto& assetName : shared::files_in_directory(NUT_PART_STORE)) {
                if (devicesList.find(assetName) == devicesList.end()) {
                    // Remove driver
                    log_info("handleRequestAssets: Remove asset %s", assetName.c_str());
                    m_manager.m_configurationRepositoryNut.setConfigurations(assetName, {});
                    publishToDriversConnector(assetName, DRIVERS_REMOVE_CONFIG);
                }
            }
        }
        catch (std::exception& e) {
            log_error("Exception while processing message: %s", e.what());
        }
    }, std::move(msg));
}

/**
 * \brief Handle request asset detail message.
 * \param msg Message receive.
 */
void ConfigurationConnector::handleRequestAssetDetail(messagebus::Message msg)
{
    m_worker.offload([this](messagebus::Message msg) {
        try {
            auto credentials = getCredentials();
            log_info("handleRequestAssetDetail: Receive message from ASSET_DETAIL");
            // Extract uuid in message

            if (msg.userData().size() < 2) {
                throw std::runtime_error("handleRequestAssets: bad ASSET_DETAIL message");
            }
            msg.userData().pop_front(); // Pop UUID.
            
            // Extract data in message
            const std::string& data = *msg.userData().cbegin();
            zmsg_t* zmsg = zmsg_new();
            zmsg_addmem(zmsg, data.c_str(), data.length());
            if (!is_fty_proto(zmsg)) {
                zmsg_destroy(&zmsg);
                throw std::runtime_error("Response to an ASSET_DETAIL message is not fty_proto");
            }
            FtyProto proto(fty_proto_decode(&zmsg), [](fty_proto_t *p) -> void { fty_proto_destroy(&p); });
            // FIXME: To restore when lib messagebus updated
            //FtyProto proto(messagebus::decodeFtyProto(data), [](fty_proto_t *p) -> void { fty_proto_destroy(&p); });
            if (!proto) {
                throw std::runtime_error("Failed to decode fty_proto_t on stream " FTY_PROTO_STREAM_ASSETS);
            }
            const std::string name =        fty_proto_name(proto.get());
            const std::string operation =   fty_proto_operation(proto.get());
            const std::string type =        fty_proto_aux_string(proto.get(), "type", "");
            const std::string subtype =     fty_proto_aux_string(proto.get(), "subtype", "");
            const std::string status =      fty_proto_aux_string(proto.get(), "status", "");
            log_info("handleNotificationAssets: receive message (operation=%s type=%s subtype=%s status=%s)",
                operation.c_str(), type.c_str(), subtype.c_str(), status.c_str());
            // Get config in config file
            bool needUpdate = false;
            fty::nut::DeviceConfigurations configs = m_manager.m_configurationRepositoryNut.getConfigurations(name);
            if (!configs.empty()) {
                fty::nut::DeviceConfiguration& config = configs[0];
                log_trace("handleRequestAssetDetail: Config read from file=\n%s", ConfigurationManager::serializeConfig("", config).c_str());
                if (status == "active") {
                    auto configsAsset = m_manager.getAssetConfigurationsWithSecwDocuments(proto.get(), credentials);
                    fty::nut::DeviceConfigurations configsToSave = std::get<0>(configsAsset);
                    fty::nut::DeviceConfigurations configsToTest;
                    configsToTest.push_back(config);
                    if(m_manager.haveConfigurationsChanged(configsToTest, configsToSave, true)) {
                        log_trace("handleRequestAssetDetail: config change for %s", name.c_str());
                        needUpdate = true;
                    }
                    else {
                        log_trace("handleRequestAssetDetail: init config for %s", name.c_str());
                        m_manager.saveAssetConfigurations(name, configsAsset);
                    }
                }
                else {
                    log_trace("handleRequestAssetDetail: remove config file for inactive asset %s", name.c_str());
                    // Remove config file
                    m_manager.m_configurationRepositoryNut.setConfigurations(name, {});
                    publishToDriversConnector(name, DRIVERS_REMOVE_CONFIG);
                }
            } else if (status == "active") {
                needUpdate = true;
                log_trace("handleRequestAssetDetail: no config file read for %s", name.c_str());
            }
            if (needUpdate) {
                log_trace("handleRequestAssetDetail: %s need rescan", name.c_str());
                m_protectAsset.lock(name);
                m_manager.scanAssetConfigurations(proto.get(), credentials);
                m_manager.automaticAssetConfigurationPrioritySort(proto.get(), credentials);
                auto newConfigAsset = m_manager.getAssetConfigurationsWithSecwDocuments(proto.get(), credentials);
                m_manager.saveAssetConfigurations(name, newConfigAsset);
                fty::nut::DeviceConfigurations newConfigs = std::get<0>(newConfigAsset);
                if (!newConfigs.empty()) {
                    // Save the first configuration into config file
                    log_trace("Save config: %s", ConfigurationManager::serializeConfig("", newConfigs.at(0)).c_str());
                    m_manager.m_configurationRepositoryNut.setConfigurations(name, { newConfigs.at(0) });
                    publishToDriversConnector(name, DRIVERS_ADD_CONFIG);
                }
                m_protectAsset.unlock(name);
            }
        }
        catch (std::exception& e) {
            log_error("Exception while processing message: %s", e.what());
        }
    }, std::move(msg));
}

/**
 * \brief Handle request message of type asset notification.
 * \param msg Message receive.
 */
void ConfigurationConnector::handleNotificationAssets(messagebus::Message msg)
{
    m_worker.offload([this](messagebus::Message msg) {
        try {
            auto credentials = getCredentials();
            for (const auto& pair : msg.metaData()) {
                log_info("handleNotificationAssets: %s=%s", pair.first.c_str(), pair.second.c_str());
            }

            for(const auto& data : msg.userData()) {
                zmsg_t* zmsg = zmsg_new();
                zmsg_addmem(zmsg, data.c_str(), data.length());
                if (!is_fty_proto(zmsg)) {
                    zmsg_destroy(&zmsg);
                    throw std::runtime_error("Notification asset message is not fty_proto");
                }
                FtyProto proto(fty_proto_decode(&zmsg), [](fty_proto_t *p) -> void { fty_proto_destroy(&p); });
                // FIXME: To restore when lib messagebus updated
                //FtyProto proto(messagebus::decodeFtyProto(data), [](fty_proto_t *p) -> void { fty_proto_destroy(&p); });
                if (!proto) {
                    throw std::runtime_error("Failed to decode fty_proto_t on stream " FTY_PROTO_STREAM_ASSETS);
                }
                std::string name = fty_proto_name(proto.get());
                std::string operation = fty_proto_operation(proto.get());
                std::string type = fty_proto_aux_string(proto.get(), "type", "");
                std::string subtype = fty_proto_aux_string(proto.get(), "subtype", "");
                std::string status = fty_proto_aux_string(proto.get(), "status", "");
                log_info("handleNotificationAssets: receive message (operation=%s type=%s subtype=%s status=%s)",
                    operation.c_str(), type.c_str(), subtype.c_str(), status.c_str());

                if (type == "device" && (subtype == "ups" || subtype == "pdu" || subtype == "epdu" || subtype == "sts")) {
                    if (operation == FTY_PROTO_ASSET_OP_CREATE && status == "active") {
    fty_proto_print(proto.get());
                        m_protectAsset.lock(name);
                        m_manager.scanAssetConfigurations(proto.get(), credentials);
                        m_manager.automaticAssetConfigurationPrioritySort(proto.get(), credentials);
                        auto configs_asset = m_manager.getAssetConfigurationsWithSecwDocuments(proto.get(), credentials);
                        fty::nut::DeviceConfigurations configs = std::get<0>(configs_asset);
                        m_manager.saveAssetConfigurations(name, configs_asset);
                        if (!configs.empty()) {
                            // Save the first configuration into config file
                            log_trace("Save config: %s", ConfigurationManager::serializeConfig("", configs.at(0)).c_str());
                            m_manager.m_configurationRepositoryNut.setConfigurations(name, { configs.at(0) });
                            publishToDriversConnector(name, DRIVERS_ADD_CONFIG);
                        }
                        m_protectAsset.unlock(name);
                    }
                    else if (operation == FTY_PROTO_ASSET_OP_UPDATE) {
    fty_proto_print(proto.get());
                        m_protectAsset.lock(name);
                        if (m_manager.updateAssetConfiguration(proto.get(), credentials)) {
                            if (status == "active") {
                                publishToDriversConnector(name, DRIVERS_ADD_CONFIG);
                            }
                            else if (status == "nonactive") {
                                publishToDriversConnector(name, DRIVERS_REMOVE_CONFIG);
                            }
                        }
                        m_protectAsset.unlock(name);
                    }
                    else if (operation == FTY_PROTO_ASSET_OP_DELETE) {
    fty_proto_print(proto.get());
                        m_protectAsset.lock(name);
                        if (m_manager.removeAssetConfiguration(proto.get())) {
                            publishToDriversConnector(name, DRIVERS_REMOVE_CONFIG);
                        }
                        m_protectAsset.unlock(name);
                        m_protectAsset.remove(name);
                    }
                }
            }
        }
        catch (std::exception& e) {
            log_error("Exception while processing message: %s", e.what());
        }
    }, std::move(msg));
}

/**
 * \brief Handle request message of type security document updated.
 * \param portfolio Portfolio name.
 * \param oldDoc Previous security document.
 * \param newDoc New security document.
 */
void ConfigurationConnector::handleNotificationSecurityWalletUpdate(const std::string& portfolio, secw::DocumentPtr oldDoc, secw::DocumentPtr newDoc)
{
    log_info("handleNotificationSecurityWalletUpdate: receive message (portfolio=%s name=%s id=%s)",
                portfolio.c_str(), newDoc->getName().c_str(), newDoc->getId().c_str());
    std::set<std::string> assetListChange;
    m_manager.manageCredentialsConfiguration(newDoc->getId(), assetListChange, getCredentials());
    // for each asset impacted with the security document changed
    for (std::string assetName : assetListChange) {
        publishToDriversConnector(assetName, DRIVERS_ADD_CONFIG);
    }
}

/**
 * \brief Handle request message of type security document deleted.
 * \param portfolio Portfolio name.
 * \param doc Security document removed.
 */
void ConfigurationConnector::handleNotificationSecurityWalletDelete(const std::string& portfolio, secw::DocumentPtr doc)
{
    log_info("handleNotificationSecurityWalletDelete: receive message (portfolio=%s name=%s id=%s)",
                portfolio.c_str(), doc->getName().c_str(), doc->getId().c_str());
    std::set<std::string> assetListChange;
    m_manager.manageCredentialsConfiguration(doc->getId(), assetListChange, getCredentials());
    // for each asset impacted with the security document changed
    for (std::string assetName : assetListChange) {
        publishToDriversConnector(assetName, DRIVERS_ADD_CONFIG);
    }
}

/**
 * \brief Handle request message of type security document added.
 * \param portfolio Portfolio name.
 * \param doc Security document added.
 */
void ConfigurationConnector::handleNotificationSecurityWalletCreate(const std::string& portfolio, secw::DocumentPtr doc)
{
    log_info("handleNotificationSecurityWalletCreate: receive message (portfolio=%s name=%s id=%s)",
                portfolio.c_str(), doc->getName().c_str(), doc->getId().c_str());
    std::set<std::string> assetListChange;
    m_manager.manageCredentialsConfiguration(doc->getId(), assetListChange, getCredentials());
    // for each asset impacted with the security document changed
    for (std::string assetName : assetListChange) {
        publishToDriversConnector(assetName, DRIVERS_ADD_CONFIG);
    }
}

/**
 * \brief Publish message to drivers connector.
 * \param assetName asset name.
 * \param subject Subject of message.
 */
void ConfigurationConnector::publishToDriversConnector(const std::string& assetName, const std::string& subject)
{
    messagebus::Message message {
        {
            { messagebus::Message::FROM, m_parameters.requesterName },
            { messagebus::Message::SUBJECT, subject },
        },
        {
            assetName
        },
    };

    m_msgBusRequester->publish("ETN.Q.IPMCORE.NUTDRIVERSCONFIGURATION", message);
}

}
}

//  --------------------------------------------------------------------------
//  Self test of this class

// If your selftest reads SCMed fixture data, please keep it in
// src/selftest-ro; if your test creates filesystem objects, please
// do so under src/selftest-rw.
// The following pattern is suggested for C selftest code:
//    char *filename = NULL;
//    filename = zsys_sprintf ("%s/%s", SELFTEST_DIR_RO, "mytemplate.file");
//    assert (filename);
//    ... use the "filename" for I/O ...
//    zstr_free (&filename);
// This way the same "filename" variable can be reused for many subtests.
#define SELFTEST_DIR_RO "src/selftest-ro"
#define SELFTEST_DIR_RW "src/selftest-rw"

void
fty_nut_configuration_connector_test (bool verbose)
{
    std::cerr << " * fty_nut_configuration_connector: no test" << std::endl;
}
