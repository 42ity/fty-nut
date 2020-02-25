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

namespace fty
{
namespace nut
{

ConfigurationConnector::Parameters::Parameters() :
    endpoint(MLM_ENDPOINT),
    agentName("fty-nut-configuration"),
    publisherName("fty-nut-configuration-publisher"),
    dbUrl(DBConn::url)
{
}

/**
 * \brief Default constructor of the class.
 * \param params Parameters of the class.
 */
ConfigurationConnector::ConfigurationConnector(ConfigurationConnector::Parameters params) :
    m_parameters(params),
    m_manager(params.dbUrl),
    m_worker(10),
    m_msgBusReceiver(messagebus::MlmMessageBus(params.endpoint, params.agentName)),
    m_msgBusRequester(messagebus::MlmMessageBus(params.endpoint, params.publisherName)),
    m_syncClient("fty-nut-configuration.socket")
{
    m_msgBusReceiver->connect();
    m_msgBusReceiver->subscribe(FTY_PROTO_STREAM_ASSETS, std::bind(&ConfigurationConnector::handleNotificationAssets, this, std::placeholders::_1));

    m_msgBusRequester->connect();
    m_msgBusRequester->receive("ASSETS", std::bind(&ConfigurationConnector::handleRequestAssets, this, std::placeholders::_1));
    m_msgBusRequester->receive(params.publisherName.c_str(), std::bind(&ConfigurationConnector::handleRequestAssetDetail, this, std::placeholders::_1));

    m_streamClient = std::unique_ptr<mlm::MlmStreamClient>(new mlm::MlmStreamClient(params.agentName, SECW_NOTIFICATIONS, 1000, params.endpoint));
    m_consumerAccessor = std::unique_ptr<secw::ConsumerAccessor>(new secw::ConsumerAccessor(m_syncClient, *m_streamClient.get()));
    m_consumerAccessor->setCallbackOnUpdate(std::bind(&ConfigurationConnector::handleNotificationSecurityWalletUpdate, this,
            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    m_consumerAccessor->setCallbackOnDelete(std::bind(&ConfigurationConnector::handleNotificationSecurityWalletDelete, this,
            std::placeholders::_1, std::placeholders::_2));
    m_consumerAccessor->setCallbackOnCreate(std::bind(&ConfigurationConnector::handleNotificationSecurityWalletCreate, this,
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
    message.userData().push_back("sensor");
    message.userData().push_back("sensorgpio");

    message.metaData().clear();
    message.metaData().emplace(messagebus::Message::RAW, "");
    message.metaData().emplace(messagebus::Message::CORRELATION_ID, uuid);
    message.metaData().emplace(messagebus::Message::SUBJECT, FTY_PROTO_STREAM_ASSETS);
    message.metaData().emplace(messagebus::Message::FROM, m_parameters.publisherName);
    message.metaData().emplace(messagebus::Message::TO, "asset-agent");
    message.metaData().emplace(messagebus::Message::REPLY_TO, m_parameters.publisherName);
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
            log_info("handleRequestAssets: Receive message from ASSETS");
            // First unreceive ASSETS message
            m_msgBusRequester->unreceive("ASSETS", std::bind(&ConfigurationConnector::handleRequestAssets, this, std::placeholders::_1));
            // Extract uuid in message
            // FIXME: uuid not checked
            if (!msg.userData().empty()) {
                std::string uuidRead = msg.userData().front();
                if (uuidRead.empty()) {
                    throw std::runtime_error("handleRequestAssets: uuid empty");
                }
                msg.userData().pop_front();
            }
            else {
                throw std::runtime_error("handleRequestAssets: no uuid defined");
            }
            // Extract result in message
            if (!msg.userData().empty()) {
                std::string result = msg.userData().front();
                if (result.empty() || result != "OK") {
                    throw std::runtime_error("handleRequestAssets: bad result: " + result);
                }
                msg.userData().pop_front();
            }
            else {
                throw std::runtime_error("handleRequestAssets: no result defined");
            }
            std::vector<std::string> devicesList;
            // For each asset name in message
            while (!msg.userData().empty()) {
                // Extract asset name in message
                std::string assetName = msg.userData().front();
                msg.userData().pop_front();
                // Get detail of asset received
                messagebus::Message message;
                std::string uuid = messagebus::generateUuid();
                message.userData().push_back("GET");
                message.userData().push_back(uuid.c_str());
                message.userData().push_back(assetName.c_str());

                message.metaData().clear();
                message.metaData().emplace(messagebus::Message::RAW, "");
                message.metaData().emplace(messagebus::Message::CORRELATION_ID, uuid);
                message.metaData().emplace(messagebus::Message::SUBJECT, "ASSET_DETAIL");
                message.metaData().emplace(messagebus::Message::FROM, m_parameters.publisherName);
                message.metaData().emplace(messagebus::Message::TO, "asset-agent");
                message.metaData().emplace(messagebus::Message::REPLY_TO, m_parameters.publisherName);
                log_info("handleRequestAssets: Get asset details for %s", assetName.c_str());
                devicesList.push_back(assetName);
                m_msgBusRequester->sendRequest("asset-agent", message);
            }

            // Get the list of driver present on local disk
            std::vector<std::string> devicesListInDirectory = shared::files_in_directory(NUT_PART_STORE);
            // For each driver, remove it if not present in current device list
            for (auto assetName : devicesListInDirectory) {
                auto it = std::find(devicesList.begin(), devicesList.end(), assetName);
                if (it == devicesList.end()) {
                    // Remove driver
                    log_info("handleRequestAssets: Remove asset %s", assetName.c_str());
                    m_manager.removeDeviceConfigurationFile(assetName);
                    publishToDriversConnector(assetName, "removeConfig");
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
            log_info("handleRequestAssetDetail: Receive message from ASSET_DETAIL");
            // Extract uuid in message
            // FIXME: uuid not checked
            if (!msg.userData().empty()) {
                std::string uuid_read = msg.userData().front();
                if (uuid_read.empty()) {
                    throw std::runtime_error("handleRequestAssetDetail: uuid empty");
                }
                msg.userData().pop_front();
            }
            else {
                throw std::runtime_error("handleRequestAssetDetail: no uuid definedy");
            }
            // Extract data in message
            std::string data;
            if (!msg.userData().empty()) {
                data = msg.userData().front();
            }
            else {
                throw std::runtime_error("handleRequestAssetDetail: no data defined");
            }
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
            std::string name = fty_proto_name(proto.get());
            std::string operation = fty_proto_operation(proto.get());
            std::string type = fty_proto_aux_string(proto.get(), "type", "");
            std::string subtype = fty_proto_aux_string(proto.get(), "subtype", "");
            std::string status = fty_proto_aux_string(proto.get(), "status", "");
            log_info("handleNotificationAssets: receive message (operation=%s type=%s subtype=%s status=%s)",
                operation.c_str(), type.c_str(), subtype.c_str(), status.c_str());
            // Get config in config file
            bool needUpdate = false;
            const std::string configFilePath = std::string(NUT_PART_STORE) + shared::path_separator() + name;
            fty::nut::DeviceConfigurations configs = fty::nut::parseConfigurationFile(configFilePath);
            if (!configs.empty()) {
                fty::nut::DeviceConfiguration config = configs.at(0); // Take first configuration (normally just one configuration available)
                config.erase("name");  // Remove name for comparaison
                log_trace("handleRequestAssetDetail: Config read from file=\n%s", ConfigurationManager::serializeConfig("", config).c_str());
                std::tuple<fty::nut::DeviceConfigurations, std::set<secw::Id>> configsAsset = m_manager.getAssetConfigurationsWithSecwDocuments(proto.get());
                fty::nut::DeviceConfigurations configsToSave = std::get<0>(configsAsset);
                fty::nut::DeviceConfigurations configsToTest;
                configsToTest.push_back(config);
                if(m_manager.isConfigurationsChange(configsToTest, configsToSave, true)) {
                    log_trace("handleRequestAssetDetail: config change for %s", name.c_str());
                    needUpdate = true;
                }
                else {
                    log_trace("handleRequestAssetDetail: init config for %s", name.c_str());
                    m_manager.saveAssetConfigurations(name, configsAsset);
                }
            } else {
                needUpdate = true;
                log_trace("handleRequestAssetDetail: no config file read for %s", name.c_str());
            }
            if (needUpdate) {
                log_trace("handleRequestAssetDetail: %s need rescan", name.c_str());
                m_protectAsset.lock(name);
                m_manager.scanAssetConfigurations(proto.get());
                m_manager.automaticAssetConfigurationPrioritySort(proto.get());
                std::tuple<fty::nut::DeviceConfigurations, std::set<secw::Id>> newConfigAsset = m_manager.getAssetConfigurationsWithSecwDocuments(proto.get());
                m_manager.saveAssetConfigurations(name, newConfigAsset);
                fty::nut::DeviceConfigurations newConfigs = std::get<0>(newConfigAsset);
                if (!newConfigs.empty()) {
                    // Save the first configuration into config file
                    log_trace("Save config: %s", ConfigurationManager::serializeConfig("", newConfigs.at(0)).c_str());
                    m_manager.updateDeviceConfigurationFile(name, newConfigs.at(0));
                    publishToDriversConnector(name, "addConfig");
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
                        m_manager.scanAssetConfigurations(proto.get());
                        m_manager.automaticAssetConfigurationPrioritySort(proto.get());
                        std::tuple<fty::nut::DeviceConfigurations, std::set<secw::Id>> configs_asset =
                                m_manager.getAssetConfigurationsWithSecwDocuments(proto.get());
                        fty::nut::DeviceConfigurations configs = std::get<0>(configs_asset);
                        m_manager.saveAssetConfigurations(name, configs_asset);
                        if (!configs.empty()) {
                            // Save the first configuration into config file
                            log_trace("Save config: %s", ConfigurationManager::serializeConfig("", configs.at(0)).c_str());
                            m_manager.updateDeviceConfigurationFile(name, configs.at(0));
                            publishToDriversConnector(name, "addConfig");
                        }
                        m_protectAsset.unlock(name);
                    }
                    else if (operation == FTY_PROTO_ASSET_OP_UPDATE) {
    fty_proto_print(proto.get());
                        m_protectAsset.lock(name);
                        if (m_manager.updateAssetConfiguration(proto.get())) {
                            if (status == "active") {
                                publishToDriversConnector(name, "addConfig");
                            }
                            else if (status == "nonactive") {
                                publishToDriversConnector(name, "removeConfig");
                            }
                        }
                        m_protectAsset.unlock(name);
                    }
                    else if (operation == FTY_PROTO_ASSET_OP_DELETE) {
    fty_proto_print(proto.get());
                        m_protectAsset.lock(name);
                        if (m_manager.removeAssetConfiguration(proto.get())) {
                            publishToDriversConnector(name, "removeConfig");
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
    m_manager.manageCredentialsConfiguration(newDoc->getId(), assetListChange);
    // for each asset impacted with the security document changed
    for (std::string assetName : assetListChange) {
        publishToDriversConnector(assetName, "addConfig");
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
    m_manager.manageCredentialsConfiguration(doc->getId(), assetListChange);
    // for each asset impacted with the security document changed
    for (std::string assetName : assetListChange) {
        publishToDriversConnector(assetName, "addConfig");
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
    m_manager.manageCredentialsConfiguration(doc->getId(), assetListChange);
    // for each asset impacted with the security document changed
    for (std::string assetName : assetListChange) {
        publishToDriversConnector(assetName, "addConfig");
    }
}

/**
 * \brief Publish message to drivers connector.
 * \param assetName asset name.
 * \param subject Subject of message.
 */
void ConfigurationConnector::publishToDriversConnector(const std::string& assetName, const std::string& subject)
{
    messagebus::Message message;
    message.userData().push_back(assetName);
    message.metaData().clear();
    message.metaData().emplace(messagebus::Message::FROM, m_parameters.publisherName);
    message.metaData().emplace(messagebus::Message::SUBJECT, subject);
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
