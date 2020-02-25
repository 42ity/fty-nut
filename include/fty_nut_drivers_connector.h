/*  =========================================================================
    fty_nut_drivers_connector - fty nut drivers connector

    Copyright (C) 2014 - 2020 Eaton

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

#ifndef FTY_NUT_DRIVERS_CONNECTOR_H_INCLUDED
#define FTY_NUT_DRIVERS_CONNECTOR_H_INCLUDED

#include "fty_nut_library.h"
#include "fty_nut_drivers_manager.h"

namespace fty
{
namespace nut
{

class ConfigurationDriversConnector
{
    public:
        struct Parameters {
            Parameters();

            std::string endpoint;
            std::string agentName;
        };

        ConfigurationDriversConnector(Parameters params);
        ~ConfigurationDriversConnector() = default;

    private:
        void handleMessage(messagebus::Message msg);
        void addConfig(messagebus::UserData data);
        void removeConfig(messagebus::UserData data);

        Parameters m_parameters;
        ConfigurationDriversManager m_driversManager;
        messagebus::Dispatcher<std::string, std::function<void(messagebus::UserData)>, std::function<void(const std::string&, messagebus::UserData)>> m_dispatcher;
        messagebus::PoolWorker m_worker;
        std::unique_ptr<messagebus::MessageBus> m_msgBus;
};

}
}

#ifdef __cplusplus
extern "C" {
#endif

//  Self test of this class
FTY_NUT_EXPORT void fty_nut_drivers_connector_test (bool verbose);

#ifdef __cplusplus
}
#endif

#endif
