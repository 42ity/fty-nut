/*  =========================================================================
    fty_nut_drivers_connector - fty nut drivers connector

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
    fty_nut_drivers_connector - fty nut drivers connector
@discuss
@end
*/

#include "fty_nut_library.h"
#include "fty_nut_classes.h"


namespace fty
{
namespace nut
{

ConfigurationDriversConnector::Parameters::Parameters() :
    endpoint(MLM_ENDPOINT),
    agentName("fty-nut-configuration-drivers")
{
}

ConfigurationDriversConnector::ConfigurationDriversConnector(ConfigurationDriversConnector::Parameters params) :
    m_parameters(params),
    m_drivers_manager(),
    m_dispatcher({
        { "addConfig", std::bind(&ConfigurationDriversConnector::addConfig, this, std::placeholders::_1) },
        { "removeConfig", std::bind(&ConfigurationDriversConnector::removeConfig, this, std::placeholders::_1) }
    }),
    m_worker(10),
    m_msgBus(messagebus::MlmMessageBus(params.endpoint, params.agentName))
{
    m_msgBus->connect();
    m_msgBus->subscribe("ETN.Q.IPMCORE.NUTDRIVERSCONFIGURATION", std::bind(&ConfigurationDriversConnector::handleMessage, this, std::placeholders::_1));
}

void ConfigurationDriversConnector::addConfig(messagebus::UserData data) {
    m_drivers_manager.addConfigDriver(data.front());
}

void ConfigurationDriversConnector::removeConfig(messagebus::UserData data) {
    m_drivers_manager.removeConfigDriver(data.front());
}


void ConfigurationDriversConnector::handleMessage(messagebus::Message msg) {
    if ((msg.metaData().count(messagebus::Message::SUBJECT) == 0)) {
        log_error("Missing subject in message.");
    }
    else {
        m_worker.offload([this](messagebus::Message msg) {
            auto subject = msg.metaData()[messagebus::Message::SUBJECT];
            log_info("Received %s request.", subject.c_str());

            try {
                m_dispatcher(subject, msg.userData());
                log_info("Request %s performed successfully.", subject.c_str());
            }
            catch (std::exception& e) {
                log_error("Exception while processing %s: %s", subject.c_str(), e.what());
            }
        }, std::move(msg));
    }
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
fty_nut_drivers_connector_test (bool verbose)
{
    std::cerr << " * fty_nut_drivers_connector: no test" << std::endl;
}