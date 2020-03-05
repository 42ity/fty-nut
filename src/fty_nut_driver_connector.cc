/*  =========================================================================
    fty_nut_drivers_connector - fty nut driver connector

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
    fty_nut_drivers_connector - fty nut driver connector
@discuss
@end
*/

#include "fty_nut_library.h"
#include "fty_nut_classes.h"


namespace fty
{
namespace nut
{

DriverConnector::Parameters::Parameters() :
    endpoint(MLM_ENDPOINT),
    agentName("fty-nut-driver")
{
}

DriverConnector::DriverConnector(DriverConnector::Parameters params, DriverManager& manager) :
    m_parameters(params),
    m_manager(manager),
    m_worker(1),
    m_dispatcher({
        { "refresh", std::bind(&DriverConnector::refreshConfig, this, std::placeholders::_1) },
    }),
    m_msgBus(messagebus::MlmMessageBus(params.endpoint, params.agentName))
{
    m_msgBus->connect();
    m_msgBus->subscribe("ETN.Q.IPMCORE.NUTDRIVERCONFIGURATION", std::bind(&DriverConnector::handleMessage, this, std::placeholders::_1));
}

void DriverConnector::refreshConfig(messagebus::UserData data)
{
    std::vector<std::string> drivers(data.begin(), data.end());
    m_manager.refreshDrivers(drivers);
}

void DriverConnector::handleMessage(messagebus::Message msg)
{
    log_trace("DriverConnector::handleMessage: received message with %d elements.", msg.userData().size());

    if ((msg.metaData().count(messagebus::Message::SUBJECT) == 0)) {
        log_error("Missing subject in message.");
    }
    else {
        m_worker.offload([this](messagebus::Message msg) {
            auto subject = msg.metaData()[messagebus::Message::SUBJECT];

            try {
                m_dispatcher(subject, msg.userData());
            }
            catch (std::exception& e) {
                log_error("Exception while processing %s: %s", subject.c_str(), e.what());
            }
        }, std::move(msg));
    }
}

}
}
