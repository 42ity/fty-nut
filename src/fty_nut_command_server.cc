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

/*
@header
    fty_nut_command_server - fty nut commands actor
@discuss
@end
*/

#include "fty_nut_command_server.h"

#include <functional>
#include <regex>

namespace ftynut {

/**
 * Function helpers.
 *
 * These isolate the side-effects of power command requests so that the
 * power command compute mechanism can be tested without a fully-blown
 * 42ity setup. In short, these interfaces are mockable.
 */
using DeviceCommandRequester = std::function<std::set<std::string>(const std::string&)>;
using DaisyChainRequester = std::function<std::map<int, std::string>(const std::string&)>;
using TopologyRequester = std::function<std::vector<std::pair<std::string, int>>(const std::string&)>;

/**
 * 42ity function helpers.
 *
 * These are the canonical function helpers for everyday usage.
 */
static std::set<std::string> deviceCommandRequesterNut(nut::TcpClient& client, const std::string& asset) {
    std::set<std::string> rawNutCommands;

    try {
        rawNutCommands = client.getDeviceCommandNames(asset);
    }
    catch (...) {
        // Treat errors as if the asset has no commands.
    }

    return rawNutCommands;
}

static std::map<int, std::string> daisyChainRequesterDatabase(tntdb::Connection &conn, const std::string& asset) {
    auto daisyChain = DBAssets::select_daisy_chain(conn, asset);
    if (daisyChain.status && daisyChain.item.size() != 0) {
        return daisyChain.item;
    }

    // Treat errors as if the asset has no power chain.
    return { };
}

static std::vector<std::pair<std::string, int>> topologyRequesterFty(const std::string& asset) {
    std::vector<std::pair<std::string, int>> result;

    /// XXX: make this suck less.
    const auto clientId = messagebus::getClientId("_-fty-nut-command-powerchain-requester");
    MlmClientGuard mClient(mlm_client_new());
    mlm_client_connect(mClient.get(), MLM_ENDPOINT, 1000, clientId.c_str());

    // Request direct power topology of asset.
    zmsg_t *request = zmsg_new();
    zmsg_addstr(request, "REQUEST");
    zmsg_addstr(request, "xxx");
    zmsg_addstr(request, "POWER_TO");
    zmsg_addstr(request, asset.c_str());
    mlm_client_sendto(mClient.get(), "asset-agent", "TOPOLOGY", nullptr, 1000, &request);
    ZpollerGuard poller(zpoller_new(mlm_client_msgpipe(mClient.get()), nullptr));

    if (zpoller_wait(poller.get(), 1000)) {
        ZmsgGuard reply(mlm_client_recv(mClient.get()));
        ZstrGuard replyCorrId(zmsg_popstr(reply.get()));
        ZstrGuard replyType(zmsg_popstr(reply.get()));
        ZstrGuard replySubtype(zmsg_popstr(reply.get()));
        ZstrGuard replyAsset(zmsg_popstr(reply.get()));
        ZstrGuard replyResult(zmsg_popstr(reply.get()));
        ZstrGuard replyData(zmsg_popstr(reply.get()));

        if (streq(replyResult.get(), "OK")) {
            cxxtools::SerializationInfo si;
            std::istringstream s(replyData.get());
            cxxtools::JsonDeserializer json(s);
            json.deserialize(si);

            for (const auto& chain : si.getMember("powerchains")) {
                std::string realAsset, realOutlet;
                chain.getMember("src-id").getValue(realAsset);
                chain.getMember("src-socket").getValue(realOutlet);

                result.emplace_back(realAsset, std::stoi(realOutlet));
            }
        }
    }

    return result;
}

constexpr auto NUT_USER_ENV = "NUT_USER";
constexpr auto NUT_PASS_ENV = "NUT_PASSWD";

// Like std::transform, but callable may return 0, 1 or n objects.
template<class InputIt, class OutputIt, class naryOperation>
OutputIt expand(InputIt first1, InputIt last1, OutputIt d_first, naryOperation nary_op) {
    while (first1 != last1) {
        auto results = nary_op(*first1++);
        for (const auto& result : results) {
            *d_first++ = result;
        }
    }
    return d_first;
}

static void connectToNutServer(nut::TcpClient& client, const std::string& nutHost, const std::string& nutUsername, const std::string& nutPassword) {
    try {
        client.connect(nutHost);
        client.authenticate(nutUsername, nutPassword);
        client.setFeature(nut::TcpClient::TRACKING, true);
    }
    catch (std::exception &e) {
        log_error("Error while connecting to NUT server: %s.", e.what());
        throw;
    }
}

/**
 * \brief Returns NUT device and daisychain index from FTY asset.
 */
static std::pair<std::string, int> getNutDeviceFromFtyDaisyChain(DaisyChainRequester daisyChainRequester, const std::string& asset) {
    std::pair<std::string, int> result { asset, -1 };

    // Check if asset is daisy-chained.
    auto daisyChain = daisyChainRequester(asset);
    if (!daisyChain.empty() && daisyChain.begin()->first == 1) {
        for (const auto &i : daisyChain) {
            if (i.second == asset) {
                result.second = i.first;
                break;
            }
        }
        result.first = daisyChain.begin()->second; // NUT driver is based on the host of the daisy-chain.
    }

    return result;
}

/**
 * \brief Map from 42ity daisy-chained command to NUT command.
 */
static dto::commands::Command ftyDaisyChainToNutCommand(DaisyChainRequester daisyChainRequester, const dto::commands::Command &job) {
    dto::commands::Command command = job;

    auto daisyChain = daisyChainRequester(job.asset);
    if (!daisyChain.empty() && daisyChain.begin()->first == 1) {
        // Daisy-chained, walk the chain until we find asset we're interested in.
        for (const auto &i : daisyChain) {
            if (i.second == job.asset) {
                command.asset = daisyChain.begin()->second; // NUT driver is based on the host of the daisy-chain.
                if (job.target.empty()) {
                    command.target = "device." + std::to_string(i.first);
                }
                else {
                    command.target = "device." + std::to_string(i.first) + "." + job.target;
                }
                break;
            }
        }
    }

    return command;
}

/**
 * \brief Downfilter daisy-chained NUT commands to a single device and remove device prefix.
 */
static std::vector<std::string> nutDaisyChainedToSingleDevice(const std::string& rawNutCommand, int daisyChainIndex) {
    // Isolate device commands from daisy-chained root NUT device.
    const std::string prefix = "device." + std::to_string(daisyChainIndex) + ".";

    std::vector<std::string> results;

    if (rawNutCommand.find(prefix) == 0) {
        results.emplace_back(rawNutCommand.substr(prefix.length()));
    }

    return results;
}

/**
 * \brief Translate high-level 42ity power source command to low-level 42ity command(s).
 */
static dto::commands::Commands ftyTranslatePowerSourceCommand(TopologyRequester topologyRequester, const std::string& asset, const std::string& commandType, const std::string& argument) {
    dto::commands::Commands result;

    const auto powerSources = topologyRequester(asset);
    for (const auto& powerSource : powerSources) {
        dto::commands::Command command = {
            powerSource.first,
            commandType,
            "outlet." + std::to_string(powerSource.second),
            argument
        } ;

        result.emplace_back(command);
    }

    // Beyond plain old errors, do not allow empty powerchains.
    if (result.empty()) {
        throw std::runtime_error("Failed to query power chain of asset " + asset);
    }

    return result;
}

/**
 * \brief Translate high-level 42ity commands to low-level 42ity commands.
 */
static dto::commands::Commands ftyTranslateHighLevelCommand(TopologyRequester topologyRequester, const dto::commands::Command &command) {
    dto::commands::Commands result;

    // Map to convert power chain power commands to simple power commands.
    const static std::map<std::string, std::string> powerSourceCommandMapping = {
        { "powersource.cycle", "load.cycle" },
        { "powersource.cycle.delay", "load.cycle.delay" },
        { "powersource.off", "load.off" },
        { "powersource.off.delay", "load.off.delay" },
        { "powersource.off.stagger", "load.off.delay" },
        { "powersource.on", "load.on" },
        { "powersource.on.delay", "load.on.delay" },
        { "powersource.on.stagger", "load.on.delay" }
    } ;

    const static std::set<std::string> powerSourceStaggerCommands = {
        { "powersource.off.stagger" },
        { "powersource.on.stagger" }
    } ;

    auto commandMapping = powerSourceCommandMapping.find(command.command);

    if (commandMapping != powerSourceCommandMapping.end()) {
        result = ftyTranslatePowerSourceCommand(topologyRequester, command.asset, commandMapping->second, command.argument);

        if (powerSourceStaggerCommands.count(command.command)) {
            // Patch up delay for staggered commands.
            const int delay = std::stoi(command.argument);
            int delayAccumulated = delay;

            for (auto& staggerCommand : result) {
                staggerCommand.argument = std::to_string(delayAccumulated);
                delayAccumulated += delay;
            }
        }
    }
    else {
        // Pass-through the command.
        result.push_back(command);
    }

    return result;
}

/**
 * \brief Convert from NUT commands to 42ity high-level commands.
 */
static dto::commands::CommandDescriptions nutCommandsToFtyCommands(const std::string& asset, const std::vector<std::string>& rawNutCommands) {
    dto::commands::CommandDescriptions result;

    // Map to convert NUT outlet commands to 42ity commands.
    const static std::map<std::string, std::string> outletDescriptions {
        { "load.cycle", "Power cycle outlet" },
        { "load.cycle.delay", "Power cycle outlet with delay (seconds)" },
        { "load.off", "Shut off outlet" },
        { "load.off.delay", "Shut off outlet with delay (seconds)" },
        { "load.on", "Switch on outlet" },
        { "load.on.delay", "Switch on outlet with delay (seconds)" }
    } ;
    const static std::regex outletRegex("(outlet(?:\\.group)?)\\.([[:digit:]]+)\\.([a-z.]+)", std::regex_constants::optimize);

    std::map<std::string, dto::commands::CommandDescription> ftyCommands;
    std::vector<dto::commands::CommandDescription> unrecognizedCommands;

    for (const auto &rawNutCommand : rawNutCommands) {
        std::smatch outletMatches;

        if (std::regex_match(rawNutCommand, outletMatches, outletRegex)) {
            // NUT command is about an outlet or outlet group, convert it to 42ity command.
            const std::string& type = outletMatches[1].str();
            const std::string& outlet = outletMatches[2].str();
            const std::string& command = outletMatches[3].str();

            if (ftyCommands.count(command) == 0) {
                // Create initial FTY command.
                dto::commands::CommandDescription commandDescription;
                commandDescription.asset = asset;
                commandDescription.command = command;
                commandDescription.description = outletDescriptions.at(command);
                ftyCommands[command] = commandDescription;
            }

            // Add target to FTY command.
            ftyCommands[command].targets.push_back(type + "." + outlet);
        }
        else {
            // NUT command is not recognized, expose it raw.
            dto::commands::CommandDescription commandDescription;
            commandDescription.asset = asset;
            commandDescription.command = rawNutCommand;
            commandDescription.description = "Description unavailable";
            unrecognizedCommands.push_back(commandDescription);
        }
    }

    // Collect all commands.
    for (const auto &ftyCommand : ftyCommands) {
        result.push_back(ftyCommand.second);
    }
    for (const auto &unrecognizedCommand : unrecognizedCommands) {
        result.push_back(unrecognizedCommand);
    }

    return result;
}

/**
 * These are the "high-level" functions for power commands. There're the ones
 * unit-tested below and use the utilities defined above.
 */
static dto::commands::CommandDescriptions queryNativePowerCommands(DeviceCommandRequester deviceCommandRequester, DaisyChainRequester daisyChainRequester, const std::string& asset) {
    // Grab NUT device.
    std::string nutDevice;
    int nutIndex;
    std::tie(nutDevice, nutIndex) = getNutDeviceFromFtyDaisyChain(daisyChainRequester, asset);
    std::set<std::string> rawNutCommands = deviceCommandRequester(nutDevice);

    // Deal with NUT to 42ity daisy-chain convertion.
    std::vector<std::string> nutCommands;
    if (nutIndex != -1) {
        expand(rawNutCommands.begin(), rawNutCommands.end(), std::back_inserter(nutCommands), std::bind(nutDaisyChainedToSingleDevice, std::placeholders::_1, nutIndex));
    }
    else {
        std::copy(rawNutCommands.begin(), rawNutCommands.end(), std::back_inserter(nutCommands));
    }

    return nutCommandsToFtyCommands(asset, nutCommands);
}

static dto::commands::CommandDescriptions queryPowerChainPowerCommands(const std::string& asset) {
    dto::commands::CommandDescriptions result;

    /**
     * FIXME: always indicate power chain power commands, not checking for any
     * semblance of validity, i.e. we bluff.
     */
    const static std::vector<std::pair<std::string, std::string>> generatedCommands = {
        { "powersource.on", "Switch on power source(s) of asset" },
        { "powersource.on.delay", "Switch on power source(s) of asset with delay (seconds)" },
        { "powersource.on.stagger", "Switch on power source(s) of asset with stagger (seconds)" },
        { "powersource.off", "Shut off on power source(s) of asset" },
        { "powersource.off.delay", "Shut off on power source(s) of asset with delay (seconds)" },
        { "powersource.off.stagger", "Shut off on power source(s) of asset with stagger (seconds)" },
        { "powersource.cycle", "Cycle power source(s) of asset" },
        { "powersource.cycle.delay", "Cycle power source(s) of asset with delay (seconds)" },
    } ;

    auto fct = [&asset](const std::string& command, const std::string& description) -> dto::commands::CommandDescription {
        dto::commands::CommandDescription commandDescription;
        commandDescription.asset = asset;
        commandDescription.command = command;
        commandDescription.description = description;
        return commandDescription;
    } ;

    for (const auto& generatedCommand : generatedCommands) {
        result.push_back(fct(generatedCommand.first, generatedCommand.second));
    }

    return result;
}

static dto::commands::Commands computePowerCommands(DaisyChainRequester daisyChainRequester, TopologyRequester topologyRequester, const dto::commands::Commands& jobs) {
    // Translate 42ity high-level commands to 42ity real commands.
    dto::commands::Commands translatedJobs;
    expand(jobs.begin(), jobs.end(), std::back_inserter(translatedJobs), std::bind(ftyTranslateHighLevelCommand, std::ref(topologyRequester), std::placeholders::_1));

    // Convert 42ity commands to NUT commands.
    dto::commands::Commands nutJobs;
    std::transform(translatedJobs.begin(), translatedJobs.end(), std::back_inserter(nutJobs), std::bind(ftyDaisyChainToNutCommand, std::ref(daisyChainRequester), std::placeholders::_1));

    return nutJobs;
}

// NutCommandManager

static std::string buildCommandMessage(const dto::commands::Command &job) {
    std::stringstream msg;
    msg << "Command '" << job.command << "' target '" << job.target << "' argument '" << job.argument << "' on asset '" << job.asset << "'";
    return msg.str();
}

static std::string buildCommandResultErrorMessage(const dto::commands::Command &job, nut::TrackingResult result) {
    std::stringstream err;
    err << buildCommandMessage(job);
    switch (result) {
        case nut::UNKNOWN:
            err << " result is missing." << std::endl;
            break;
        case nut::FAILURE:
            err << " failed." << std::endl;
            break;
        case nut::INVALID_ARGUMENT:
            err << " has an invalid argument." << std::endl;
            break;
        default:
            err << " encountered an unknown error." << std::endl;
            break;
    }
    return err.str();
}

NutCommandManager::NutCommandManager(const std::string& nutHost, const std::string& nutUsername, const std::string& nutPassword, const std::string& dbConn) :
    m_nutHost(nutHost),
    m_nutUsername(nutUsername),
    m_nutPassword(nutPassword),
    m_dbConn(dbConn)
{
}

dto::commands::GetAssetsByCommandReplyDto NutCommandManager::getAssetsByCommand(const std::string &command) {
    dto::commands::GetAssetsByCommandReplyDto reply;

    // Connect to stuff.
    auto conn = tntdb::connectCached(m_dbConn);
    nut::TcpClient client;
    connectToNutServer(client, m_nutHost, m_nutUsername, m_nutPassword);

    auto assets = DBAssets::list_devices_with_status(conn, "active");

    for (const auto& asset : assets) {
        dto::commands::CommandDescriptions commandDescriptions;

        // Prepare our data query function helpers.
        DeviceCommandRequester deviceCommandRequester = std::bind(deviceCommandRequesterNut, std::ref(client), std::placeholders::_1);
        DaisyChainRequester daisyChainRequester = std::bind(daisyChainRequesterDatabase, std::ref(conn), std::placeholders::_1);

        // Query native power commands.
        auto nativeCommands = queryNativePowerCommands(deviceCommandRequester, daisyChainRequester, asset);
        commandDescriptions.insert(commandDescriptions.end(), std::make_move_iterator(nativeCommands.begin()), std::make_move_iterator(nativeCommands.end()));

        // Query power chain power commands.
        auto powerchainCommands = queryPowerChainPowerCommands(asset);
        commandDescriptions.insert(commandDescriptions.end(), std::make_move_iterator(powerchainCommands.begin()), std::make_move_iterator(powerchainCommands.end()));

        for (const auto& commandDescription : commandDescriptions) {
            if (commandDescription.command == command) {
                reply.emplace_back(asset);
                break;
            }
        }
    }

    return reply;
}

dto::commands::CommandDescriptions NutCommandManager::getCommands(const std::string &asset) {
    dto::commands::CommandDescriptions reply;

    // Connect to stuff.
    auto conn = tntdb::connectCached(m_dbConn);
    nut::TcpClient client;
    connectToNutServer(client, m_nutHost, m_nutUsername, m_nutPassword);

    // Prepare our data query function helpers.
    DeviceCommandRequester deviceCommandRequester = std::bind(deviceCommandRequesterNut, std::ref(client), std::placeholders::_1);
    DaisyChainRequester daisyChainRequester = std::bind(daisyChainRequesterDatabase, std::ref(conn), std::placeholders::_1);

    // Query native power commands.
    auto nativeCommands = queryNativePowerCommands(deviceCommandRequester, daisyChainRequester, asset);
    reply.insert(reply.end(), std::make_move_iterator(nativeCommands.begin()), std::make_move_iterator(nativeCommands.end()));

    // Query power chain power commands.
    auto powerchainCommands = queryPowerChainPowerCommands(asset);
    reply.insert(reply.end(), std::make_move_iterator(powerchainCommands.begin()), std::make_move_iterator(powerchainCommands.end()));

    return reply;
}

dto::commands::Commands NutCommandManager::computeCommands(const dto::commands::Commands &jobs) {
    // Connect to stuff.
    nut::TcpClient client;
    connectToNutServer(client, m_nutHost, m_nutUsername, m_nutPassword);
    auto conn = tntdb::connectCached(m_dbConn);

    // Prepare our data query function helpers.
    DaisyChainRequester daisyChainRequester = std::bind(daisyChainRequesterDatabase, std::ref(conn), std::placeholders::_1);
    TopologyRequester topologyRequester = &topologyRequesterFty;

    return computePowerCommands(daisyChainRequester, topologyRequester, jobs);
}

void NutCommandManager::performCommands(const dto::commands::Commands &jobs) {
    std::stringstream errorMessageStream;

    // Connect to NUT.
    nut::TcpClient client;
    connectToNutServer(client, m_nutHost, m_nutUsername, m_nutPassword);

    // Submit jobs to NUT.
    std::map<nut::TrackingID, const dto::commands::Command&> ids;

    for (const auto &job : jobs) {
        const std::string nutCommand = job.target.empty() ?
            job.command :
            job.target + "." + job.command;

        try {
            auto id = client.executeDeviceCommand(job.asset, nutCommand, job.argument);
            ids.emplace(id, job);
        }
        catch (...) {
            errorMessageStream << buildCommandMessage(job) << " couldn't be submitted.";
        }
    }

    // Collect results of NUT jobs.
    while (!ids.empty()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        for (auto it = ids.begin(); it != ids.end(); ) {
            auto result = client.getTrackingResult(it->first);

            switch (result) {
                case nut::PENDING:
                    break;
                case nut::SUCCESS:
                    it = ids.erase(it);
                    break;
                default:
                    errorMessageStream << buildCommandResultErrorMessage(it->second, result);
                    it = ids.erase(it);
                    break;
            }
        }
    }

    // Report errors, if any.
    const std::string errorMessage = errorMessageStream.str();
    if (!errorMessage.empty()) {
        throw std::runtime_error(errorMessage);
    }
}

// NutCommandConnector

NutCommandConnector::Parameters::Parameters() :
    endpoint(MLM_ENDPOINT),
    agentName("fty-nut-command"),
    nutHost("localhost"),
    nutUsername(getenv(NUT_USER_ENV) ? getenv(NUT_USER_ENV) : ""),
    nutPassword(getenv(NUT_PASS_ENV) ? getenv(NUT_PASS_ENV) : ""),
    dbUrl(DBConn::url)
{
}

NutCommandConnector::NutCommandConnector(NutCommandConnector::Parameters params) :
    m_parameters(params),
    m_manager(params.nutHost, params.nutUsername, params.nutPassword, params.dbUrl),
    m_dispatcher({
        { "GetAssetsByCommand", std::bind(&NutCommandConnector::requestGetAssetsByCommand, this, std::placeholders::_1) },
        { "GetCommands", std::bind(&NutCommandConnector::requestGetCommands, this, std::placeholders::_1) },
        { "PerformCommands", std::bind(&NutCommandConnector::requestPerformCommands, this, std::placeholders::_1) }
    }),
    m_msgBus(messagebus::MlmMessageBus(params.endpoint, params.agentName))
{
    m_msgBus->connect();
    m_msgBus->receive("ETN.Q.IPMCORE.POWERACTION", std::bind(&NutCommandConnector::handleRequest, this, std::placeholders::_1));
}

void NutCommandConnector::handleRequest(messagebus::Message msg) {
    if ((msg.metaData().count(messagebus::Message::SUBJECT) == 0) ||
        (msg.metaData().count(messagebus::Message::CORRELATION_ID) == 0) ||
        (msg.metaData().count(messagebus::Message::REPLY_TO) == 0)) {
        log_error("Missing subject/correlationID/replyTo in request.");
        return;
    }

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
}

void NutCommandConnector::sendReply(const messagebus::MetaData& metadataRequest, bool status, const messagebus::UserData& dataReply) {
    messagebus::Message reply;

    reply.metaData() = {
        { messagebus::Message::CORRELATION_ID, metadataRequest.at(messagebus::Message::CORRELATION_ID) },
        { messagebus::Message::SUBJECT, metadataRequest.at(messagebus::Message::SUBJECT) },
        { messagebus::Message::STATUS, status ? "ok" : "ko" },
        { messagebus::Message::TO, metadataRequest.at(messagebus::Message::REPLY_TO) }
    } ;
    reply.userData() = dataReply;

    m_msgBus->sendReply("ETN.R.IPMCORE.POWERACTION", reply);
}

messagebus::UserData NutCommandConnector::requestGetCommands(messagebus::UserData data) {
    dto::commands::GetCommandsQueryDto query;
    data >> query;

    messagebus::UserData reply;
    auto commands = m_manager.getCommands(query.asset);

    {
        std::stringstream logMessage;
        logMessage << "Asset '" << query.asset << "' has the following commands:" << std::endl;
        for (const auto& i : commands) {
            logMessage << "\t" << i.command << " - " << i.description << std::endl;
        }
        log_trace("%s", logMessage.str().c_str());
    }

    reply << commands;
    return reply;
}

messagebus::UserData NutCommandConnector::requestGetAssetsByCommand(messagebus::UserData data) {
    dto::commands::GetAssetsByCommandQueryDto query;
    data >> query;

    messagebus::UserData reply;
    auto commands = m_manager.getAssetsByCommand(query.command);

    {
        std::stringstream logMessage;
        logMessage << "Command '" << query.command << "' is supported by the following assets:" << std::endl;
        for (const auto& i : commands) {
            logMessage << "\t" << i << std::endl;
        }
        log_trace("%s", logMessage.str().c_str());
    }

    // FIXME: template substitution fails.
    for (const auto& i : commands) {
        reply.push_back(i);
    }
    return reply;
}

messagebus::UserData NutCommandConnector::requestPerformCommands(messagebus::UserData data) {
    dto::commands::PerformCommandsQueryDto query;
    data >> query;

    {
        std::stringstream logMessage;
        logMessage << "Commands requested:" << std::endl;
        for (const auto& i : query.commands) {
            logMessage << "\t" << buildCommandMessage(i) << std::endl;
        }
        log_debug("%s", logMessage.str().c_str());
    }

    auto computedCommands = m_manager.computeCommands(query.commands);

    {
        std::stringstream logMessage;
        logMessage << "Effective commands computed:" << std::endl;
        for (const auto& i : computedCommands) {
            logMessage << "\t" << buildCommandMessage(i) << std::endl;
        }
        log_trace("%s", logMessage.str().c_str());
    }

    m_manager.performCommands(computedCommands);
    return {};
}

}

// Unit tests.

void
fty_nut_command_server_test(bool verbose)
{
    /**
     * Test setup:
     *  - epdu-1 : standalone EPDU with two outlets.
     *  - epdu-2 : daisy-chain host EPDU with three outlets.
     *  - epdu-3 : daisy-chain device 1 EPDU with three outlets.
     *  - server-4: server with one power source connected to epdu-1.
     *  - server-5: server with two power sources connected to epdu-2 and epdu-3.
     *
     * Tests consist of calling the functions with a set of input data and
     * checking that the results are as expected.
     */

    /**
     * Callables defining our mock data-center without having to instanciate
     * a full 42ity environment.
     */
    auto generateEpduCommands = [](int outlets, int devices) -> std::set<std::string> {
        const static std::vector<std::string> commandList = {
            ".load.cycle",
            ".load.cycle.delay",
            ".load.off",
            ".load.off.delay",
            ".load.on",
            ".load.on.delay"
        } ;

        std::set<std::string> commands;
        for (int i = 1; i <= devices; i++) {
            const std::string prefix = (devices == 1) ? "" : "device." + std::to_string(i) + ".";

            for (int j = 1; j <= outlets; j++) {
                for (const auto& command : commandList) {
                    commands.insert(prefix + "outlet." + std::to_string(j) + command);
                }
            }
        }
        return commands;
    } ;

    ftynut::DeviceCommandRequester deviceCommandRequester = [&generateEpduCommands](const std::string& asset) -> std::set<std::string> {
        const std::map<std::string, std::set<std::string>> assetCommands = {
            { "epdu-1", generateEpduCommands(2, 1) },
            { "epdu-2", generateEpduCommands(3, 2) },
            { "epdu-3", {} },
            { "server-4", {} },
            { "server-5", {} }
        } ;

        return assetCommands.at(asset);
    } ;

    ftynut::DaisyChainRequester daisyChainRequester = [](const std::string& asset) -> std::map<int, std::string> {
        const static std::map<int, std::string> doubleDaisyChain = {
            { 1, "epdu-2" },
            { 2, "epdu-3" }
        } ;

        const static std::map<std::string, std::map<int, std::string>> daisyChains = {
            { "epdu-1", { } },
            { "epdu-2", doubleDaisyChain },
            { "epdu-3", doubleDaisyChain },
            { "server-4", { } },
            { "server-5", { } }
        } ;

        return daisyChains.at(asset);
    } ;

    ftynut::TopologyRequester topologyRequester = [](const std::string& asset) -> std::vector<std::pair<std::string, int>> {
        const static std::map<std::string, std::vector<std::pair<std::string, int>>> topologies = {
            { "epdu-1", {} },
            { "epdu-2", {} },
            { "epdu-3", {} },
            { "server-4", { { "epdu-1", 2 } } },
            { "server-5", { { "epdu-2", 3 }, { "epdu-3", 1 } } }
        } ;

        return topologies.at(asset);
    } ;

    // Actual unit tests.

    std::cerr << " * fty_nut_command_server: " << std::endl;

    {
        std::cerr << "  - queryNativePowerCommands: ";

        const static std::map<std::string, dto::commands::CommandDescriptions> expectedAssetCommands = {
            { "epdu-1", {
                dto::commands::CommandDescription({ "epdu-1", "load.cycle",       "", { "outlet.1", "outlet.2" } }),
                dto::commands::CommandDescription({ "epdu-1", "load.cycle.delay", "", { "outlet.1", "outlet.2" } }),
                dto::commands::CommandDescription({ "epdu-1", "load.off",         "", { "outlet.1", "outlet.2" } }),
                dto::commands::CommandDescription({ "epdu-1", "load.off.delay",   "", { "outlet.1", "outlet.2" } }),
                dto::commands::CommandDescription({ "epdu-1", "load.on",          "", { "outlet.1", "outlet.2" } }),
                dto::commands::CommandDescription({ "epdu-1", "load.on.delay",    "", { "outlet.1", "outlet.2" } }) } },
            { "epdu-2", {
                dto::commands::CommandDescription({ "epdu-2", "load.cycle",       "", { "outlet.1", "outlet.2", "outlet.3" } }),
                dto::commands::CommandDescription({ "epdu-2", "load.cycle.delay", "", { "outlet.1", "outlet.2", "outlet.3" } }),
                dto::commands::CommandDescription({ "epdu-2", "load.off",         "", { "outlet.1", "outlet.2", "outlet.3" } }),
                dto::commands::CommandDescription({ "epdu-2", "load.off.delay",   "", { "outlet.1", "outlet.2", "outlet.3" } }),
                dto::commands::CommandDescription({ "epdu-2", "load.on",          "", { "outlet.1", "outlet.2", "outlet.3" } }),
                dto::commands::CommandDescription({ "epdu-2", "load.on.delay",    "", { "outlet.1", "outlet.2", "outlet.3" } }) } },
            { "epdu-3", {
                dto::commands::CommandDescription({ "epdu-3", "load.cycle",       "", { "outlet.1", "outlet.2", "outlet.3" } }),
                dto::commands::CommandDescription({ "epdu-3", "load.cycle.delay", "", { "outlet.1", "outlet.2", "outlet.3" } }),
                dto::commands::CommandDescription({ "epdu-3", "load.off",         "", { "outlet.1", "outlet.2", "outlet.3" } }),
                dto::commands::CommandDescription({ "epdu-3", "load.off.delay",   "", { "outlet.1", "outlet.2", "outlet.3" } }),
                dto::commands::CommandDescription({ "epdu-3", "load.on",          "", { "outlet.1", "outlet.2", "outlet.3" } }),
                dto::commands::CommandDescription({ "epdu-3", "load.on.delay",    "", { "outlet.1", "outlet.2", "outlet.3" } }) } }
        } ;

        for (const auto& expectedAssetCommand : expectedAssetCommands) {
            const auto& assetName = expectedAssetCommand.first;
            const auto& assetCommands = expectedAssetCommand.second;

            auto commandDescriptions = ftynut::queryNativePowerCommands(deviceCommandRequester, daisyChainRequester, assetName);

            assert(assetCommands.size() == commandDescriptions.size());
            for (size_t i = 0; i < assetCommands.size(); i++) {
                assert(assetCommands[i].asset == commandDescriptions[i].asset);
                assert(assetCommands[i].command == commandDescriptions[i].command);

                assert(assetCommands[i].targets.size() == commandDescriptions[i].targets.size());
                for (size_t j = 0; j < assetCommands[i].targets.size(); j++) {
                    assert(assetCommands[i].targets[j] == commandDescriptions[i].targets[j]);
                }
            }
        }

        std::cerr << "OK" << std::endl;
    }

    {
        std::cerr << "  - queryPowerChainPowerCommands: ";
        /// FIXME: Actually test this function (no need to as long as it's stubbed).
        std::cerr << "OK" << std::endl;
    }

    {
        std::cerr << "  - computePowerCommands: ";

        const static dto::commands::Commands commands = {
            dto::commands::Command({ "epdu-1",      "load.off",                 "outlet.1", ""  }),
            dto::commands::Command({ "epdu-1",      "load.on.delay",            "outlet.2", "3" }),
            dto::commands::Command({ "epdu-2",      "load.cycle",               "outlet.1", ""  }),
            dto::commands::Command({ "epdu-3",      "load.cycle",               "outlet.3", ""  }),
            dto::commands::Command({ "server-4",    "powersource.off",          "",         ""  }),
            dto::commands::Command({ "server-5",    "powersource.cycle",        "",         ""  }),
            dto::commands::Command({ "server-5",    "powersource.off.delay",    "",         "3" }),
            dto::commands::Command({ "server-5",    "powersource.on.stagger",   "",         "3" })
        } ;

        const static std::vector<dto::commands::Commands> expectedResults = {
            {
                dto::commands::Command({ "epdu-1", "load.off", "outlet.1", "" })
            },
            {
                dto::commands::Command({ "epdu-1", "load.on.delay", "outlet.2", "3" })
            },
            {
                dto::commands::Command({ "epdu-2", "load.cycle", "device.1.outlet.1", "" })
            },
            {
                dto::commands::Command({ "epdu-2", "load.cycle", "device.2.outlet.3", "" })
            },
            {
                dto::commands::Command({ "epdu-1", "load.off", "outlet.2", "" })
            },
            {
                dto::commands::Command({ "epdu-2", "load.cycle", "device.1.outlet.3", "" }),
                dto::commands::Command({ "epdu-2", "load.cycle", "device.2.outlet.1", "" })
            },
            {
                dto::commands::Command({ "epdu-2", "load.off.delay", "device.1.outlet.3", "3" }),
                dto::commands::Command({ "epdu-2", "load.off.delay", "device.2.outlet.1", "3" })
            },
            {
                dto::commands::Command({ "epdu-2", "load.on.delay", "device.1.outlet.3", "3" }),
                dto::commands::Command({ "epdu-2", "load.on.delay", "device.2.outlet.1", "6" })
            }
        } ;

        assert(commands.size() == expectedResults.size());

        for (size_t i = 0; i < commands.size(); i++) {
            auto result = ftynut::computePowerCommands(daisyChainRequester, topologyRequester, { commands[i] });

            assert(result.size() == expectedResults[i].size());
            for (size_t j = 0; j < expectedResults[i].size(); j++) {
                assert(result[j].asset      == expectedResults[i][j].asset);
                assert(result[j].command    == expectedResults[i][j].command);
                assert(result[j].target     == expectedResults[i][j].target);
                assert(result[j].argument   == expectedResults[i][j].argument);
            }
        }

        std::cerr << "OK" << std::endl;
    }
}
