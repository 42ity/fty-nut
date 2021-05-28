/*  =========================================================================
    fty_nut_command_server - fty nut command actor

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

#pragma once
#include <fty_common_messagebus.h>
#include <functional>
#include <memory>
#include <string>

namespace dto::commands {
class CommandDescription;
class Command;
using Commands = std::vector<Command>;
using CommandDescriptions = std::vector<CommandDescription>;
} // namespace dto::commands


namespace ftynut {

/// @brief NUT command manager for 42ity.
///
/// This class provides 42ity-type power commands with NUT as a backend.
class NutCommandManager
{
public:
    NutCommandManager(const std::string& nutHost, const std::string& nutUsername, const std::string& nutPassword,
        const std::string& dbConn);
    ~NutCommandManager() = default;

    dto::commands::CommandDescriptions getCommands(const std::string& asset);
    dto::commands::Commands            computeCommands(const dto::commands::Commands& jobs);
    void                               performCommands(const dto::commands::Commands& jobs);

private:
    std::string m_nutHost;
    std::string m_nutUsername;
    std::string m_nutPassword;
    std::string m_dbConn;
};

/// @brief Bus connector for NutCommandManager.
///
/// This connects the command manager to the rest of the system. It collects
/// command requests and send responses.
class NutCommandConnector
{
public:
    struct Parameters
    {
        Parameters();

        std::string endpoint;
        std::string agentName;

        std::string nutHost;
        std::string nutUsername;
        std::string nutPassword;

        std::string dbUrl;
    };

    NutCommandConnector(Parameters params);
    ~NutCommandConnector() = default;

private:
    void handleRequest(messagebus::Message msg);
    void sendReply(const messagebus::MetaData& metadataRequest, bool status, const messagebus::UserData& dataReply);

    messagebus::UserData requestGetCommands(messagebus::UserData data);
    messagebus::UserData requestPerformCommands(messagebus::UserData data);
    messagebus::UserData requestPerformGroupCommands(messagebus::UserData data);

    Parameters        m_parameters;
    NutCommandManager m_manager;
    messagebus::Dispatcher<std::string, std::function<messagebus::UserData(messagebus::UserData)>,
        std::function<messagebus::UserData(const std::string&, messagebus::UserData)>>
                                            m_dispatcher;
    std::unique_ptr<messagebus::MessageBus> m_msgBus;
};

} // namespace ftynut
