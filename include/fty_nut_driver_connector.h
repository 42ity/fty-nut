/*  =========================================================================
    fty_nut_drivers_connector - fty nut driver connector

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
#include "fty_nut_driver_manager.h"

#define DRIVERS_ADD_CONFIG    "addConfig"
#define DRIVERS_REMOVE_CONFIG "removeConfig"

namespace fty
{
namespace nut
{

class DriverConnector
{
    public:
        struct Parameters {
            Parameters();

            std::string endpoint;
            std::string agentName;
        };

        DriverConnector(Parameters params, DriverManager& manager);
        ~DriverConnector() = default;

    private:
        void handleMessage(messagebus::Message msg);
        void refreshConfig(messagebus::UserData data);

        Parameters m_parameters;
        DriverManager& m_manager;
        messagebus::PoolWorker m_worker;
        messagebus::Dispatcher<std::string, std::function<void(messagebus::UserData)>, std::function<void(const std::string&, messagebus::UserData)>> m_dispatcher;
        std::unique_ptr<messagebus::MessageBus> m_msgBus;
};

}
}

#endif
