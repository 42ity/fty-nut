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

#include <forward_list>
#include <regex>
#include <future>
#include <fty_common_nut_credentials.h>

namespace fty
{
namespace nut
{

ConfigurationConnector::Parameters::Parameters() :
    endpoint(MLM_ENDPOINT),
    agentName("fty-nut-configuration"),
    dbUrl(DBConn::url)
{
}

ConfigurationConnector::ConfigurationConnector(ConfigurationConnector::Parameters params) :
    m_parameters(params),
    m_manager(params.dbUrl),
    m_dispatcher({
    }),
    m_worker(0),
    m_msgBus(messagebus::MlmMessageBus(params.endpoint, params.agentName))
{
    m_msgBus->connect();
    m_msgBus->receive("ETN.Q.IPMCORE.NUTCONFIGURATION", std::bind(&ConfigurationConnector::handleRequest, this, std::placeholders::_1));
    m_msgBus->subscribe(FTY_PROTO_STREAM_ASSETS, std::bind(&ConfigurationConnector::handleNotificationAssets, this, std::placeholders::_1));    
}

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

using FtyProto = std::unique_ptr<fty_proto_t, std::function<void (fty_proto_t*)>>;

//std::mutex m_mtx;

void ConfigurationConnector::handleNotificationAssets(messagebus::Message msg) {
    
    m_worker.offload([this](messagebus::Message msg) {
                  
        //std::unique_lock<std::mutex> lock(m_mtx);

        zmsg_t *zmsg = zmsg_new();    
        for (const auto& pair : msg.metaData()) {
            std::cout << pair.first << "=" << pair.second << std::endl;
        }
        /*for(const auto& item : msg.userData()) {
            std::cout << "add size=" << item.size() << " data=" << item.c_str() << std::endl;
            zmsg_addmem(zmsg, item.c_str(), item.size());        
        }*/

        for(const auto& data : msg.userData()) {           
            FtyProto proto(messagebus::decodeFtyProto(data), [](fty_proto_t *p) -> void { fty_proto_destroy(&p); });

            if (!proto) {
                log_error("Failed to decode fty_proto_t on stream " FTY_PROTO_STREAM_ASSETS);
                return;
            }                    
            std::string name = fty_proto_name(proto.get());                
            std::string operation = fty_proto_operation(proto.get());

            std::string type = fty_proto_aux_string(proto.get(), "type", "");
            std::string status = fty_proto_aux_string(proto.get(), "status", "");
            std::string subtype = fty_proto_aux_string(proto.get(), "subtype", "");
                                    
            fty_proto_print(proto.get()); 
            //std::stringstream buffer;
            //messagebus::dumpFtyProto(proto, buffer);

            std::cout << "operation=" << operation << " status=" << status << std::endl;
            std::cout << "type=" << type << " subtype=" << subtype << std::endl;

            if ((type == "device") && ((subtype == "ups") || (subtype == "pdu") || (subtype == "sts"))) {                            
                if (operation == FTY_PROTO_ASSET_OP_CREATE) {                                        
                    m_manager.scanAssetConfigurations(proto.get());
                    m_manager.automaticAssetConfigurationPrioritySort(proto.get());
                }
                else if (operation == FTY_PROTO_ASSET_OP_UPDATE) {
                    // FIXME: Test if properties has changed (e.g. ip address)    
                    //const auto addresses = getNetworkAddressesFromAsset(asset);
                }    
                else if (operation == FTY_PROTO_ASSET_OP_DELETE) {
                    // FIXME: Remove the  configuration
                }               
            }
        }    
    }, std::move(msg));    
}

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
