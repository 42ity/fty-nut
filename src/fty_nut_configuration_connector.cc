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
    m_msg_bus(messagebus::MlmMessageBus(params.endpoint, params.agentName)),
    m_msg_bus_publisher(messagebus::MlmMessageBus(params.endpoint, params.publisherName)),
    m_sync_client("fty-nut-configuration.socket")
{
    m_msg_bus->connect();
    m_msg_bus->subscribe(FTY_PROTO_STREAM_ASSETS, std::bind(&ConfigurationConnector::handleNotificationAssets, this, std::placeholders::_1));

    m_msg_bus_publisher->connect();
    m_msg_bus_publisher->receive("ASSETS", std::bind(&ConfigurationConnector::handleRequestAssets, this, std::placeholders::_1));
    m_msg_bus_publisher->receive(params.publisherName.c_str(), std::bind(&ConfigurationConnector::handleRequestAssetDetail, this, std::placeholders::_1));

    m_stream_client = std::unique_ptr<mlm::MlmStreamClient>(new mlm::MlmStreamClient(params.agentName, SECW_NOTIFICATIONS, 1000, params.endpoint));
    m_consumer_accessor = std::unique_ptr<secw::ConsumerAccessor>(new secw::ConsumerAccessor(m_sync_client, *m_stream_client.get()));
    m_consumer_accessor->setCallbackOnUpdate(std::bind(&ConfigurationConnector::handleNotificationSecurityWalletUpdate, this,
            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    m_consumer_accessor->setCallbackOnDelete(std::bind(&ConfigurationConnector::handleNotificationSecurityWalletDelete, this,
            std::placeholders::_1, std::placeholders::_2));
    m_consumer_accessor->setCallbackOnCreate(std::bind(&ConfigurationConnector::handleNotificationSecurityWalletCreate, this,
            std::placeholders::_1, std::placeholders::_2));
}

/**
 * \brief Get initial assets.
 *
 */
void ConfigurationConnector::get_initial_assets()
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
    m_msg_bus_publisher->sendRequest("asset-agent", message);
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
            m_msg_bus_publisher->unreceive("ASSETS", std::bind(&ConfigurationConnector::handleRequestAssets, this, std::placeholders::_1));
            // Extract uuid in message
            // FIXME: uuid not checked
            if (!msg.userData().empty()) {
                std::string uuid_read = msg.userData().front();
                if (uuid_read.empty()) {
                    log_error("handleRequestAssets: uuid empty");
                    return;
                }
                msg.userData().pop_front();
            }
            else {
                log_error("handleRequestAssets: no uuid defined");
                return;
            }
            // Extract result in message
            if (!msg.userData().empty()) {
                std::string result = msg.userData().front();
                if (result.empty() || result != "OK") {
                    log_error("handleRequestAssets: bad result: '%s'", result.c_str());
                    return;
                }
                msg.userData().pop_front();
            }
            else {
                log_error("handleRequestAssets: no result defined");
                return;
            }
            std::vector<std::string> devices_list;
            // For each asset name in message
            while (!msg.userData().empty()) {
                // Extract asset name in message
                std::string asset_name = msg.userData().front();
                msg.userData().pop_front();
                // Get detail of asset received
                messagebus::Message message;
                std::string uuid = messagebus::generateUuid();
                message.userData().push_back("GET");
                message.userData().push_back(uuid.c_str());
                message.userData().push_back(asset_name.c_str());

                message.metaData().clear();
                message.metaData().emplace(messagebus::Message::RAW, "");
                message.metaData().emplace(messagebus::Message::CORRELATION_ID, uuid);
                message.metaData().emplace(messagebus::Message::SUBJECT, "ASSET_DETAIL");
                message.metaData().emplace(messagebus::Message::FROM, m_parameters.publisherName);
                message.metaData().emplace(messagebus::Message::TO, "asset-agent");
                message.metaData().emplace(messagebus::Message::REPLY_TO, m_parameters.publisherName);
                log_info("handleRequestAssets: Get asset details for %s", asset_name.c_str());
                devices_list.push_back(asset_name);
                m_msg_bus_publisher->sendRequest("asset-agent", message);
            }

            // Get the list of driver present on local disk
            std::vector<std::string> devices_list_in_directory = shared::files_in_directory(NUT_PART_STORE);
            // For each driver, remove it if not present in current device list
            for (auto asset_name : devices_list_in_directory) {
                auto it = std::find(devices_list.begin(), devices_list.end(), asset_name);
                if (it == devices_list.end()) {
                    // Remove driver
                    log_info("handleRequestAssets: Remove asset %s", asset_name.c_str());
                    m_manager.removeDeviceConfigurationFile(asset_name);
                    publishToDriversConnector(asset_name, "removeConfig");
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
                    log_error("handleRequestAssetDetail: uuid empty");
                    return;
                }
                msg.userData().pop_front();
            }
            else {
                log_error("handleRequestAssetDetail: no uuid defined");
                return;
            }
            // Extract data in message
            std::string data;
            if (!msg.userData().empty()) {
                data = msg.userData().front();
            }
            else {
                log_error("handleRequestAssetDetail: no data defined");
                return;
            }
            zmsg_t* zmsg = zmsg_new();
            zmsg_addmem(zmsg, data.c_str(), data.length());
            if (!is_fty_proto(zmsg)) {
                log_warning("Response to an ASSET_DETAIL message is not fty_proto");
                return;
            }
            FtyProto proto(fty_proto_decode(&zmsg), [](fty_proto_t *p) -> void { fty_proto_destroy(&p); });
            // FIXME: To restore when lib messagebus updated
            //FtyProto proto(messagebus::decodeFtyProto(data), [](fty_proto_t *p) -> void { fty_proto_destroy(&p); });
            if (!proto) {
                log_error("Failed to decode fty_proto_t on stream " FTY_PROTO_STREAM_ASSETS);
                return;
            }
            std::string name = fty_proto_name(proto.get());
            std::string operation = fty_proto_operation(proto.get());
            std::string type = fty_proto_aux_string(proto.get(), "type", "");
            std::string subtype = fty_proto_aux_string(proto.get(), "subtype", "");
            std::string status = fty_proto_aux_string(proto.get(), "status", "");
            log_info("handleNotificationAssets: receive message (operation=%s type=%s subtype=%s status=%s)",
                operation.c_str(), type.c_str(), subtype.c_str(), status.c_str());
            // Get config in config file
            bool need_update = false;
            nutcommon::DeviceConfiguration config;
            m_manager.readDeviceConfigurationFile(name, config);
            if (!config.empty()) {
                log_trace("handleRequestAssetDetail: Config read from file=\n%s", ConfigurationManager::serialize_config("", config).c_str());
                std::set<secw::Id> secw_document_id_list;
                nutcommon::DeviceConfigurations configs_to_test;
                nutcommon::DeviceConfigurations configs_to_save;
                configs_to_test.push_back(config);
                m_manager.getAssetConfigurationsWithSecwDocuments(proto.get(), configs_to_save, secw_document_id_list);
                if(m_manager.isConfigurationsChange(configs_to_test, configs_to_save, true)) {
                    log_trace("handleRequestAssetDetail: config change for %s", name.c_str());
                    need_update = true;
                }
                else {
                    log_trace("handleRequestAssetDetail: init config for %s", name.c_str());
                    m_manager.saveAssetConfigurations(name, configs_to_save, secw_document_id_list);
                }
            } else {
                need_update = true;
                log_trace("handleRequestAssetDetail: no config file read for %s", name.c_str());
            }
            if (need_update) {
                log_trace("handleRequestAssetDetail: %s need rescan", name.c_str());
                protect_asset_lock(m_asset_mutex_map, name);
                m_manager.scanAssetConfigurations(proto.get());
                m_manager.automaticAssetConfigurationPrioritySort(proto.get());
                nutcommon::DeviceConfigurations new_configs;
                std::set<secw::Id> new_secw_document_id_list;
                m_manager.getAssetConfigurationsWithSecwDocuments(proto.get(), new_configs, new_secw_document_id_list);
                m_manager.saveAssetConfigurations(name, new_configs, new_secw_document_id_list);
                if (new_configs.size() > 0) {
                    // Save the first configuration into config file
                    log_trace("\nSave config:\n%s", ConfigurationManager::serialize_config("", new_configs.at(0)).c_str());
                    m_manager.updateDeviceConfigurationFile(name, new_configs.at(0));
                    publishToDriversConnector(name, "addConfig");
                }
                protect_asset_unlock(m_asset_mutex_map, name);
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

        for (const auto& pair : msg.metaData()) {
            std::cout << pair.first << "=" << pair.second << std::endl;
        }

        for(const auto& data : msg.userData()) {
            zmsg_t* zmsg = zmsg_new();
            zmsg_addmem(zmsg, data.c_str(), data.length());
            if (!is_fty_proto(zmsg)) {
                log_warning("Notification asset message is not fty_proto");
                return;
            }
            FtyProto proto(fty_proto_decode(&zmsg), [](fty_proto_t *p) -> void { fty_proto_destroy(&p); });
            // FIXME: To restore when lib messagebus updated
            //FtyProto proto(messagebus::decodeFtyProto(data), [](fty_proto_t *p) -> void { fty_proto_destroy(&p); });
            if (!proto) {
                log_error("Failed to decode fty_proto_t on stream " FTY_PROTO_STREAM_ASSETS);
                return;
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
                    protect_asset_lock(m_asset_mutex_map, name);
                    m_manager.scanAssetConfigurations(proto.get());
                    m_manager.automaticAssetConfigurationPrioritySort(proto.get());
                    nutcommon::DeviceConfigurations configs;
                    std::set<secw::Id> secw_document_id_list;
                    m_manager.getAssetConfigurationsWithSecwDocuments(proto.get(), configs, secw_document_id_list);
                    m_manager.saveAssetConfigurations(name, configs, secw_document_id_list);
                    if (configs.size() > 0) {
                        // Save the first configuration into config file
                        log_trace("\nSave config:\n%s", ConfigurationManager::serialize_config("", configs.at(0)).c_str());
                        m_manager.updateDeviceConfigurationFile(name, configs.at(0));
                        publishToDriversConnector(name, "addConfig");
                    }
                    protect_asset_unlock(m_asset_mutex_map, name);
                }
                else if (operation == FTY_PROTO_ASSET_OP_UPDATE) {
fty_proto_print(proto.get());
                    protect_asset_lock(m_asset_mutex_map, name);
                    if (m_manager.updateAssetConfiguration(proto.get())) {
                        if (status == "active") {
                            publishToDriversConnector(name, "addConfig");
                        }
                        else if (status == "nonactive") {
                            publishToDriversConnector(name, "removeConfig");
                        }
                    }
                    protect_asset_unlock(m_asset_mutex_map, name);
                }
                else if (operation == FTY_PROTO_ASSET_OP_DELETE) {
fty_proto_print(proto.get());
                    protect_asset_lock(m_asset_mutex_map, name);
                    if (m_manager.removeAssetConfiguration(proto.get())) {
                        publishToDriversConnector(name, "removeConfig");
                    }
                    protect_asset_unlock(m_asset_mutex_map, name);
                    protect_asset_remove(m_asset_mutex_map, name);
                }
            }
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
    std::set<std::string> asset_list_change;
    m_manager.manageCredentialsConfiguration(newDoc->getId(), asset_list_change);
    // for each asset impacted with the security document changed
    for (std::string asset_name : asset_list_change) {
        publishToDriversConnector(asset_name, "addConfig");
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
    std::set<std::string> asset_list_change;
    m_manager.manageCredentialsConfiguration(doc->getId(), asset_list_change);
    // for each asset impacted with the security document changed
    for (std::string asset_name : asset_list_change) {
        publishToDriversConnector(asset_name, "addConfig");
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
    std::set<std::string> asset_list_change;
    m_manager.manageCredentialsConfiguration(doc->getId(), asset_list_change);
    // for each asset impacted with the security document changed
    for (std::string asset_name : asset_list_change) {
        publishToDriversConnector(asset_name, "addConfig");
    }
}

/**
 * \brief Publish message to drivers connector.
 * \param asset_name asset name.
 * \param subject Subject of message.
 */
void ConfigurationConnector::publishToDriversConnector(std::string asset_name, std::string subject)
{
    messagebus::Message message;
    message.userData().push_back(asset_name);
    message.metaData().clear();
    message.metaData().emplace(messagebus::Message::FROM, m_parameters.publisherName);
    message.metaData().emplace(messagebus::Message::SUBJECT, subject);
    m_msg_bus_publisher->publish("ETN.Q.IPMCORE.NUTDRIVERSCONFIGURATION", message);
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