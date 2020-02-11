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
    m_dispatcher({
    }),
    m_worker(10),
    m_msgBus(messagebus::MlmMessageBus(params.endpoint, params.agentName)),
    m_msgBusPublisher(messagebus::MlmMessageBus(params.endpoint, params.publisherName)),
    m_sync_client("fty-nut-configuration.socket")
{
    m_msgBus->connect();
    m_msgBus->receive("ETN.Q.IPMCORE.NUTCONFIGURATION", std::bind(&ConfigurationConnector::handleRequest, this, std::placeholders::_1));
    m_msgBus->subscribe(FTY_PROTO_STREAM_ASSETS, std::bind(&ConfigurationConnector::handleNotificationAssets, this, std::placeholders::_1));

    m_msgBusPublisher->connect();

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
 * \brief Handle request message.
 * \param msg Message receive.
 */
void ConfigurationConnector::handleRequest(messagebus::Message msg) {
    if ((msg.metaData().count(messagebus::Message::SUBJECT) == 0) ||
        (msg.metaData().count(messagebus::Message::CORRELATION_ID) == 0) ||
        (msg.metaData().count(messagebus::Message::REPLY_TO) == 0)) {
        log_error("Missing subject/correlationID/replyTo in request.");
    }
    else {
        m_worker.offload([this](messagebus::Message msg) {
            auto subject = msg.metaData()[messagebus::Message::SUBJECT];
            auto corrId = msg.metaData()[messagebus::Message::CORRELATION_ID];
            log_info("Received %s (%s) request.", subject.c_str(), corrId.c_str());

            try {
                auto result = m_dispatcher(subject, msg.userData());

                log_info("Request %s (%s) performed successfully.", subject.c_str(), corrId.c_str());
                sendReply(msg.metaData(), true, result);
            }
            catch (std::exception& e) {
                log_error("Exception while processing %s (%s): %s", subject.c_str(), corrId.c_str(), e.what());
                sendReply(msg.metaData(), false, { e.what() });
            }
        }, std::move(msg));
    }
}

/**
 * \brief Handle request message of type asset notification.
 * \param msg Message receive.
 */
void ConfigurationConnector::handleNotificationAssets(messagebus::Message msg) {

    m_worker.offload([this](messagebus::Message msg) {

        for (const auto& pair : msg.metaData()) {
            std::cout << pair.first << "=" << pair.second << std::endl;
        }

        for(const auto& data : msg.userData()) {
            zmsg_t* zmsg = zmsg_new();
            zmsg_addmem(zmsg, data.c_str(), data.length());
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
                    if (m_manager.applyAssetConfiguration(proto.get())) {
                        publish(name, "addConfig");
                    }
                    protect_asset_unlock(m_asset_mutex_map, name);
                }
                else if (operation == FTY_PROTO_ASSET_OP_UPDATE) {
fty_proto_print(proto.get());
                    protect_asset_lock(m_asset_mutex_map, name);
                    if (m_manager.updateAssetConfiguration(proto.get())) {
                        if (status == "active") {
                            publish(name, "addConfig");
                        }
                        else if (status == "nonactive") {
                            publish(name, "removeConfig");
                        }
                    }
                    protect_asset_unlock(m_asset_mutex_map, name);
                }
                else if (operation == FTY_PROTO_ASSET_OP_DELETE) {
fty_proto_print(proto.get());
                    protect_asset_lock(m_asset_mutex_map, name);
                    if (m_manager.removeAssetConfiguration(proto.get())) {
                        publish(name, "removeConfig");
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
void ConfigurationConnector::handleNotificationSecurityWalletUpdate(const std::string& portfolio, secw::DocumentPtr oldDoc, secw::DocumentPtr newDoc) {
    log_info("handleNotificationSecurityWalletUpdate: receive message (portfolio=%s name=%s id=%s)",
                portfolio.c_str(), newDoc->getName().c_str(), newDoc->getId().c_str());
    m_manager.manageCredentialsConfiguration(newDoc->getId());
}

/**
 * \brief Handle request message of type security document deleted.
 * \param portfolio Portfolio name.
 * \param doc Security document removed.
 */
void ConfigurationConnector::handleNotificationSecurityWalletDelete(const std::string& portfolio, secw::DocumentPtr doc) {
    log_info("handleNotificationSecurityWalletDelete: receive message (portfolio=%s name=%s id=%s)",
                portfolio.c_str(), doc->getName().c_str(), doc->getId().c_str());
    m_manager.manageCredentialsConfiguration(doc->getId());
}

/**
 * \brief Handle request message of type security document added.
 * \param portfolio Portfolio name.
 * \param doc Security document added.
 */
void ConfigurationConnector::handleNotificationSecurityWalletCreate(const std::string& portfolio, secw::DocumentPtr doc) {
    log_info("handleNotificationSecurityWalletCreate: receive message (portfolio=%s name=%s id=%s)",
                portfolio.c_str(), doc->getName().c_str(), doc->getId().c_str());
    m_manager.manageCredentialsConfiguration(doc->getId());
}

/**
 * \brief Send reply to message bus.
 * \param metadataRequest Meta data request.
 * \param dataReply Data reply.
 */
void ConfigurationConnector::sendReply(const messagebus::MetaData& metadataRequest, bool status, const messagebus::UserData& dataReply) {
    messagebus::Message reply;

    reply.metaData() = {
        { messagebus::Message::CORRELATION_ID, metadataRequest.at(messagebus::Message::CORRELATION_ID) },
        { messagebus::Message::SUBJECT, metadataRequest.at(messagebus::Message::SUBJECT) },
        { messagebus::Message::STATUS, status ? "ok" : "ko" },
        { messagebus::Message::TO, metadataRequest.at(messagebus::Message::REPLY_TO) }
    } ;
    reply.userData() = dataReply;

    m_msgBus->sendReply("ETN.R.IPMCORE.NUTCONFIGURATION", reply);
}

/**
 * \brief Publish message.
 * \param asset_name asset name.
 * \param subject Subject of message.
 */
void ConfigurationConnector::publish(std::string asset_name, std::string subject) {
    messagebus::Message message;
    message.userData().push_back(asset_name);
    message.metaData().clear();
    message.metaData().emplace(messagebus::Message::FROM, m_parameters.publisherName);
    message.metaData().emplace(messagebus::Message::SUBJECT, subject);
    m_msgBusPublisher->publish("ETN.Q.IPMCORE.NUTDRIVERSCONFIGURATION", message);
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