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
static const std::string SECW_SOCKET_PATH = "/run/fty-security-wallet/secw.socket";

ConfigurationConnector::Parameters::Parameters() :
    endpoint(MLM_ENDPOINT),
    agentName("fty-nut-configuration"),
    requesterName("fty-nut-configuration-requester"),
    deviceTypes({ "device" }),
    deviceSubtypes({ "epdu", "pdu", "sts", "ups" }),
    threadPoolSize(1),
    automaticPrioritySort(false),
    rescanOnSecurityWalletCreate(false),
    rescanOnSecurityWalletUpdate(false),
    rescanOnSecurityWalletDelete(false)
{
}

ConfigurationConnector::ConfigurationConnector(Parameters parameters, ConfigurationManager& manager) :
    m_parameters(parameters),
    m_manager(manager),
    m_worker(m_parameters.threadPoolSize),
    m_msgBusReceiver(messagebus::MlmMessageBus(m_parameters.endpoint, m_parameters.agentName)),
    m_msgBusRequester(messagebus::MlmMessageBus(m_parameters.endpoint, m_parameters.requesterName)),
    m_syncClient(SECW_SOCKET_PATH),
    m_streamClient(m_parameters.agentName, SECW_NOTIFICATIONS, 1000, m_parameters.endpoint),
    m_consumerAccessor(secw::ConsumerAccessor(m_syncClient, m_streamClient)),
    m_secwMapCacheValid(false)
{
    m_msgBusReceiver->connect();
    m_msgBusReceiver->subscribe(FTY_PROTO_STREAM_ASSETS, std::bind(&ConfigurationConnector::handleNotificationAssets, this, std::placeholders::_1));

    m_msgBusRequester->connect();
    m_msgBusRequester->receive("ASSETS", std::bind(&ConfigurationConnector::handleRequestAssets, this, std::placeholders::_1));
    m_msgBusRequester->receive(m_parameters.requesterName.c_str(), std::bind(&ConfigurationConnector::handleRequestAssetDetail, this, std::placeholders::_1));

    m_consumerAccessor.setCallbackOnUpdate(std::bind(&ConfigurationConnector::handleNotificationSecurityWalletUpdate, this,
            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    m_consumerAccessor.setCallbackOnDelete(std::bind(&ConfigurationConnector::handleNotificationSecurityWalletDelete, this,
            std::placeholders::_1, std::placeholders::_2));
    m_consumerAccessor.setCallbackOnCreate(std::bind(&ConfigurationConnector::handleNotificationSecurityWalletCreate, this,
            std::placeholders::_1, std::placeholders::_2));

    // Log device filters.
    std::string types, subtypes;
    for (const auto& filter : std::vector<std::pair<std::string&, const std::set<std::string>&>>({
        { types, m_parameters.deviceTypes },
        { subtypes, m_parameters.deviceSubtypes },
    })) {
        std::stringstream ss;
        for (auto it = filter.second.begin(); it != filter.second.end(); it++) {
            if (it != filter.second.begin()) {
                ss << "; ";
            }
            ss << *it;
        }
        filter.first = ss.str();
    }

    log_debug("ConfigurationConnector matches asset types (%s) and subtypes (%s).", types.c_str(), subtypes.c_str());
}

void ConfigurationConnector::triggerRescan()
{
    // Get list of assets.
    const std::string uuid = messagebus::generateUuid();
    messagebus::UserData data { "GET", uuid };
    std::copy(m_parameters.deviceSubtypes.begin(), m_parameters.deviceSubtypes.end(), std::back_inserter(data));

    messagebus::Message message {
        {
            { messagebus::Message::RAW, "" },
            { messagebus::Message::CORRELATION_ID, uuid },
            { messagebus::Message::SUBJECT, FTY_PROTO_STREAM_ASSETS },
            { messagebus::Message::FROM, m_parameters.requesterName },
            { messagebus::Message::TO, "asset-agent" },
            { messagebus::Message::REPLY_TO, m_parameters.requesterName },
        },
        data,
    };

    m_msgBusRequester->sendRequest("asset-agent", message);
}

void ConfigurationConnector::handleRequestAssets(messagebus::Message msg)
{
    log_trace("ConfigurationConnector::handleRequestAssets: received message.");

    m_worker.offload([this](messagebus::Message msg) {
        try {
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

            // For each driver, remove it if not present in current device list.
            publishToDriverConnector(m_manager.purgeNotInList(devicesList));
        }
        catch (std::exception& e) {
            log_error("Exception while processing asset request: %s", e.what());
        }
    }, std::move(msg));
}

void ConfigurationConnector::handleRequestAssetDetail(messagebus::Message msg)
{
    log_trace("ConfigurationConnector::handleRequestAssetDetail: received message.");

    m_worker.offload([this](messagebus::Message msg) {
        try {
            if (msg.userData().size() < 2) {
                throw std::runtime_error("handleRequestAssetDetail: Bad ASSET_DETAIL message");
            }
            msg.userData().pop_front(); // Pop UUID.

            for(const auto& data : msg.userData()) {
                handleAsset(data, true);
            }
        }
        catch (std::exception& e) {
            log_error("Exception while processing asset message: %s", e.what());
        }
    }, std::move(msg));
}

void ConfigurationConnector::handleNotificationAssets(messagebus::Message msg)
{
    log_trace("ConfigurationConnector::handleNotificationAssets: received message.");

    m_worker.offload([this](messagebus::Message msg) {
        try {
            for(const auto& data : msg.userData()) {
                handleAsset(data);
            }
        }
        catch (std::exception& e) {
            log_error("Exception while processing asset notification: %s", e.what());
        }
    }, std::move(msg));
}


void ConfigurationConnector::handleNotificationSecurityWalletCreate(const std::string& portfolio, secw::DocumentPtr doc)
{
    log_trace("ConfigurationConnector::handleNotificationSecurityWalletCreate: received message (portfolio=%s name=%s id=%s)",
                portfolio.c_str(), doc->getName().c_str(), doc->getId().c_str());

    {
        // Invalidate security wallet cache.
        std::lock_guard<std::mutex> lk(m_secwMapMutex);
        m_secwMapCacheValid = false;
    }

    if (m_parameters.rescanOnSecurityWalletCreate) {
        log_debug("Triggering refresh on security wallet create (portfolio=%s; name=%s; id=%s).",
                    portfolio.c_str(), doc->getName().c_str(), doc->getId().c_str());
        triggerRescan();
    }
}

void ConfigurationConnector::handleNotificationSecurityWalletUpdate(const std::string& portfolio, secw::DocumentPtr oldDoc, secw::DocumentPtr newDoc)
{
    log_trace("ConfigurationConnector::handleNotificationSecurityWalletUpdate: received message (portfolio=%s; name=%s; id=%s).",
                portfolio.c_str(), newDoc->getName().c_str(), newDoc->getId().c_str());

    {
        // Invalidate security wallet cache.
        std::lock_guard<std::mutex> lk(m_secwMapMutex);
        m_secwMapCacheValid = false;
    }

    if (m_parameters.rescanOnSecurityWalletUpdate) {
        log_debug("Triggering refresh on security wallet update (portfolio=%s; name=%s; id=%s).",
                    portfolio.c_str(), newDoc->getName().c_str(), newDoc->getId().c_str());
        triggerRescan();
    }
}

void ConfigurationConnector::handleNotificationSecurityWalletDelete(const std::string& portfolio, secw::DocumentPtr doc)
{
    log_trace("ConfigurationConnector::handleNotificationSecurityWalletDelete: received message (portfolio=%s; name=%s; id=%s).",
                portfolio.c_str(), doc->getName().c_str(), doc->getId().c_str());

    {
        // Invalidate security wallet cache.
        std::lock_guard<std::mutex> lk(m_secwMapMutex);
        m_secwMapCacheValid = false;
    }

    if (m_parameters.rescanOnSecurityWalletDelete) {
        log_debug("Triggering refresh on security wallet delete (portfolio=%s; name=%s; id=%s).",
                    portfolio.c_str(), doc->getName().c_str(), doc->getId().c_str());
        triggerRescan();
    }
}


/**
 * \brief Publish message to drivers connector.
 * \param assetName asset name.
 * \param subject Subject of message.
 */
void ConfigurationConnector::publishToDriverConnector(const std::vector<std::string>& assetNames)
{
    messagebus::Message message {
        {
            { messagebus::Message::FROM, m_parameters.requesterName },
            { messagebus::Message::SUBJECT, "refresh" },
        },
        messagebus::UserData(assetNames.begin(), assetNames.end()),
    };

    m_msgBusRequester->publish("ETN.Q.IPMCORE.NUTDRIVERCONFIGURATION", message);
}

void ConfigurationConnector::handleAsset(const std::string& data, bool forceScan)
{
    zmsg_t* zmsg = zmsg_new();
    zmsg_addmem(zmsg, data.c_str(), data.length());
    FtyProto proto(fty_proto_decode(&zmsg), [](fty_proto_t *p) -> void { fty_proto_destroy(&p); });
    if (!proto) {
        throw std::runtime_error("Failed to decode fty_proto_t.");
    }

    const std::string name    = fty_proto_name(proto.get());
    const std::string type    = fty_proto_aux_string(proto.get(), "type", "");
    const std::string subtype = fty_proto_aux_string(proto.get(), "subtype", "");
    const std::string op      = fty_proto_operation(proto.get());

    // Don't care about auxilliary asset messages.
    if (op == FTY_PROTO_ASSET_OP_CREATE || op == FTY_PROTO_ASSET_OP_UPDATE || op == FTY_PROTO_ASSET_OP_DELETE) {
        // Filter out uninteresting assets.
        if (m_parameters.deviceTypes.count(type) && m_parameters.deviceSubtypes.count(subtype)) {
            if (m_manager.processAsset(proto.get(), getCredentials(), forceScan, m_parameters.automaticPrioritySort)) {
                publishToDriverConnector({name});
            }
        }
        else {
            log_trace("Ignoring filtered-out asset %s (type: %s, subtype: %s).", name.c_str(), type.c_str(), subtype.c_str());
        }
    }
}

SecwMap ConfigurationConnector::getCredentials()
{
    std::lock_guard<std::mutex> lk(m_secwMapMutex);

    if (!m_secwMapCacheValid) {
        try {
            // Security wallet cache outdated, grab security documents.
            auto secCreds = m_consumerAccessor.getListDocumentsWithPrivateData("default", "discovery_monitoring");

            m_secwMapCache.clear();
            for (const auto &i : secCreds) {
                m_secwMapCache.emplace(i->getId(), i);
            }
            m_secwMapCacheValid = true;

            log_trace("Fetched %d credentials from security wallet.", m_secwMapCache.size());
        }
        catch (std::exception &e) {
            log_warning("Failed to fetch credentials from security wallet: %s.", e.what());
        }
    }

    return m_secwMapCache;
}

}
}
