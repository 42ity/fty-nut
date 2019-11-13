/*  =========================================================================
    fty_nut_command_server - fty nut command actor

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
    fty_nut_command_server - fty nut commands actor
@discuss
@end
*/

#include "fty_nut_command_server.h"

#include <functional>
#include <regex>

namespace ftynut {

constexpr auto NUT_USER_ENV = "NUT_USER";
constexpr auto NUT_PASS_ENV = "NUT_PASSWD";

#if 0
constexpr auto AGENT_NAME_KEY =                   "agentName";
constexpr auto AGENT_NAME =                       "fty-srr";
constexpr auto ENDPOINT_KEY =                     "endPoint";
//constexpr auto DEFAULT_ENDPOINT =                 "ipc://@/malamute";
//constexpr auto DEFAULT_LOG_CONFIG =               "/etc/fty/ftylog.cfg";
constexpr auto SRR_QUEUE_NAME_KEY =               "queueName";
constexpr auto SRR_MSG_QUEUE_NAME =               "ETN.Q.IPMCORE.SRR";
#endif

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
        log_trace("Connected to NUT server '%s'.", nutHost.c_str());
    }
    catch (std::exception &e) {
        log_error("Error while connecting to NUT server: %s.", e.what());
        throw;
    }
}

/**
 * \brief Returns NUT device and daisychain index from FTY asset.
 */
static std::pair<std::string, int> getNutDeviceFromFtyDaisyChain(tntdb::Connection &conn, const std::string& asset) {
    std::pair<std::string, int> result { asset, -1 };

    // Check if asset is daisy-chained.
    auto daisyChain = DBAssets::select_daisy_chain(conn, asset);
    if (daisyChain.status && daisyChain.item.size() != 0) {
        for (const auto &i : daisyChain.item) {
            if (i.second == asset) {
                result.second = i.first;
                break;
            }
        }
        result.first = daisyChain.item.begin()->second;
    }

    return result;
}

/**
 * \brief Map from 42ity daisy-chained command to NUT command.
 */
static dto::commands::Command ftyDaisyChainToNutCommand(tntdb::Connection &conn, const dto::commands::Command &job) {
    dto::commands::Command command = job;

    auto daisy_chain = DBAssets::select_daisy_chain(conn, job.asset);
    if (daisy_chain.status && daisy_chain.item.size() != 0) {
        // Daisy-chained, walk the chain until we find asset we're interested in.
        for (const auto &i : daisy_chain.item) {
            if (i.second == job.asset) {
                command.asset = daisy_chain.item.begin()->second;
                if (!job.target.empty()) {
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
static dto::commands::Commands ftyTranslatePowerSourceCommand(const std::string& asset, const std::string& commandType) {
    dto::commands::Commands result;

    /**
     * XXX: this is quite quick'n dirty, but we don't have a good way to
     * interface with old-style mailboxes yet from modern code.
     */
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

            /**
             * For every component of the power chain, generate the
             * corresponding 42ity low-level power command. Don't check for
             * validity as NUT will eventually complain if commands don't
             * actually exist.
             */
            for (const auto& chain : si.getMember("powerchains")) {
                std::string realAsset, realOutlet;
                chain.getMember("src-id").getValue(realAsset);
                chain.getMember("src-socket").getValue(realOutlet);

                dto::commands::Command command;
                command.command = commandType;
                command.asset = realAsset;
                command.target = "outlet." + realOutlet;

                result.push_back(command);
            }
        }
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
static dto::commands::Commands ftyTranslateHighLevelCommand(const dto::commands::Command &command) {
    dto::commands::Commands result;

    const std::map<std::string, std::string> powerSourceCommandMapping = {
        { "powersource.cycle", "load.cycle" },
        { "powersource.off", "load.off" },
        { "powersource.on", "load.on" }
    } ;

    auto commandMapping = powerSourceCommandMapping.find(command.command);
    if (commandMapping != powerSourceCommandMapping.end()) {
        result = ftyTranslatePowerSourceCommand(command.asset, commandMapping->second);
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
void nutCommandsToFtyCommands(const std::string& asset, const std::vector<std::string>& rawNutCommands, dto::commands::CommandDescriptions& commandDescriptions) {
    // Stuff to convert NUT outlet commands to 42ity commands.
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
            // NUT command is about an outlet, convert it to 42ity command.
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
        commandDescriptions.push_back(ftyCommand.second);
    }
    for (const auto &unrecognizedCommand : unrecognizedCommands) {
        commandDescriptions.push_back(unrecognizedCommand);
    }
}

void queryNativePowerCommands(nut::TcpClient& client, tntdb::Connection& conn, const std::string& asset, dto::commands::CommandDescriptions& commandDescriptions) {
    // Grab NUT device.
    std::string nutDevice;
    int nutIndex;
    std::tie(nutDevice, nutIndex) = getNutDeviceFromFtyDaisyChain(conn, asset);

    // Grab NUT commands - don't care if it errors out.
    std::set<std::string> rawNutCommands;
    try {
        rawNutCommands = client.getDeviceCommandNames(nutDevice);
    }
    catch (...) {
    }

    // Deal with NUT to 42ity daisy-chain convertion.
    std::vector<std::string> nutCommands;
    if (nutIndex != -1) {
        expand(rawNutCommands.begin(), rawNutCommands.end(), std::back_inserter(nutCommands), std::bind(nutDaisyChainedToSingleDevice, std::placeholders::_1, nutIndex));
    }
    else {
        std::copy(rawNutCommands.begin(), rawNutCommands.end(), std::back_inserter(nutCommands));
    }

    nutCommandsToFtyCommands(asset, nutCommands, commandDescriptions);
}

void queryPowerChainPowerCommands(nut::TcpClient& client, tntdb::Connection& conn, const std::string& asset, dto::commands::CommandDescriptions& commandDescriptions) {
    /**
     * FIXME: always indicate power chain power commands, not checking for any
     * semblance of validity, i.e. we bluff.
     */
    dto::commands::CommandDescription powerSourceOn;
    powerSourceOn.asset = asset;
    powerSourceOn.command = "powersource.on";
    powerSourceOn.description = "Switch on power source(s) of asset";
    commandDescriptions.push_back(powerSourceOn);

    dto::commands::CommandDescription powerSourceOff;
    powerSourceOff.asset = asset;
    powerSourceOff.command = "powersource.off";
    powerSourceOff.description = "Shut off power source(s) of asset";
    commandDescriptions.push_back(powerSourceOff);

    dto::commands::CommandDescription powerSourceCycle;
    powerSourceCycle.asset = asset;
    powerSourceCycle.command = "powersource.cycle";
    powerSourceCycle.description = "Cycle power source(s) of asset";
    commandDescriptions.push_back(powerSourceCycle);
}

NutCommandManager::NutCommandWorker::NutCommandWorker(const std::string& nutHost, const std::string& nutUsername, const std::string& nutPassword, CompletionCallback callback) :
    m_nutHost(nutHost),
    m_nutUsername(nutUsername),
    m_nutPassword(nutPassword),
    m_callback(callback),
    m_worker(std::bind(&NutCommandManager::NutCommandWorker::mainloop, this)),
    m_stopped(false)
{
}

NutCommandManager::NutCommandWorker::~NutCommandWorker() {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_stopped = true;
    m_cv.notify_one();
    m_worker.join();
}

NutCommandManager::NutCommandWorker::NutTrackingIds NutCommandManager::NutCommandWorker::submitWork(const dto::commands::Commands &jobs) {
    NutTrackingIds ids;

    nut::TcpClient client;
    connectToNutServer(client, m_nutHost, m_nutUsername, m_nutPassword);

    for (const auto &job : jobs) {
        std::string nutCommand = job.target.empty() ?
            job.command :
            job.target + "." + job.command;

        log_info("Executing NUT command '%s' argument '%s' on asset '%s'.", nutCommand.c_str(), job.argument.c_str(), job.asset.c_str());
        auto id = client.executeDeviceCommand(job.asset, nutCommand, job.argument);
        ids.insert(id);
        log_debug("NUT job ID: %s.", id.c_str());
    }

    {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (const auto& id : ids) {
            m_pendingCommands.emplace_back(id);
            m_cv.notify_one();
        }
    }

    return ids;
}

void NutCommandManager::NutCommandWorker::mainloop() {
    while (!m_stopped) {
        std::unique_lock<std::mutex> lk(m_mutex);
        m_cv.wait_for(lk, std::chrono::seconds(2));

        if (!m_pendingCommands.empty()) {
            nut::TcpClient client;
            connectToNutServer(client, m_nutHost, m_nutUsername, m_nutPassword);

            // Store completed commands here to not hold the mutex while calling callbacks.
            std::vector<std::pair<nut::TrackingID, bool>> jobsCompleted;

            // Mark and sweep completed NUT commands.
            auto treated = std::remove_if(m_pendingCommands.begin(), m_pendingCommands.end(),
                [&client, &jobsCompleted, this](nut::TrackingID &id) -> bool {
                    auto result = client.getTrackingResult(id);
                    switch (result) {
                        case nut::FAILURE:
                        case nut::SUCCESS:
                            jobsCompleted.emplace_back(id, result == nut::SUCCESS);
                            return true;
                        default:
                            return false;
                    }
                }
            );

            m_pendingCommands.erase(treated, m_pendingCommands.end());

            // Call callbacks without holding the mutex.
            lk.unlock();
            for (auto jobCompleted : jobsCompleted) {
                log_debug("NUT job ID '%s' finished with %s.", jobCompleted.first.c_str(), jobCompleted.second ? "success" : "failure");
                m_callback(jobCompleted.first, jobCompleted.second);
            }
        }
    }
}

NutCommandManager::NutCommandManager(const std::string& nutHost, const std::string& nutUsername, const std::string& nutPassword, const std::string& dbConn, CompletionCallback callback) :
    m_worker(nutHost, nutUsername, nutPassword, std::bind(&NutCommandManager::completionCallback, this, std::placeholders::_1, std::placeholders::_2)),
    m_callback(callback),
    m_nutHost(nutHost),
    m_nutUsername(nutUsername),
    m_nutPassword(nutPassword),
    m_dbConn(dbConn)
{
}

dto::commands::CommandDescriptions NutCommandManager::getCommands(const std::string &asset) {
    dto::commands::CommandDescriptions reply;

    auto conn = tntdb::connectCached(m_dbConn);
    nut::TcpClient client;
    connectToNutServer(client, m_nutHost, m_nutUsername, m_nutPassword);

    queryNativePowerCommands(client, conn, asset, reply);
    queryPowerChainPowerCommands(client, conn, asset, reply);

    return reply;
}

void NutCommandManager::submitWork(const std::string &correlationId, const dto::commands::Commands &jobs) {
    for (const auto& job : jobs) {
        log_info("Performing command '%s' target '%s' argument '%s' on asset '%s'.",
            job.command.c_str(),
            job.target.c_str(),
            job.argument.c_str(),
            job.asset.c_str()
        );
    }

    auto conn = tntdb::connectCached(m_dbConn);

    // Translate 42ity high-level commands to 42ity real commands.
    dto::commands::Commands translatedJobs;
    expand(jobs.begin(), jobs.end(), std::back_inserter(translatedJobs), ftyTranslateHighLevelCommand);

    // Convert 42ity commands to NUT commands.
    dto::commands::Commands nutJobs;
    std::transform(translatedJobs.begin(), translatedJobs.end(), std::back_inserter(nutJobs), std::bind(ftyDaisyChainToNutCommand, std::ref(conn), std::placeholders::_1));

    std::lock_guard<std::mutex> lk(m_mutex);
    m_jobs.emplace_back(correlationId, m_worker.submitWork(nutJobs));
}

void NutCommandManager::completionCallback(nut::TrackingID id, bool result) {
    // Store completed jobs here to not hold the mutex while calling callbacks.
    std::vector<std::pair<std::string, bool>> jobsCompleted;

    {
        std::lock_guard<std::mutex> lk(m_mutex);

        // Mark jobs with no outstanding NUT tracking IDs as terminated.
        auto jobsTreated = std::remove_if(m_jobs.begin(), m_jobs.end(),
            [&id, &result, &jobsCompleted](Job &job) -> bool {
                // Clear NUT tracking IDs of completed commands.
                auto it = job.ids.find(id);
                if (it != job.ids.end()) {
                    job.ids.erase(it);
                    job.success &= result;
                }
                bool done = job.ids.empty();
                if (done) {
                    jobsCompleted.emplace_back(job.correlationId, job.success);
                }
                return done;
            }
        );

        m_jobs.erase(jobsTreated, m_jobs.end());
    }

    // Signal completed jobs without holding the mutex.
    for (auto jobCompleted : jobsCompleted) {
        m_callback(jobCompleted.first, jobCompleted.second);
    }
}

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
    m_manager(params.nutHost, params.nutUsername, params.nutPassword, params.dbUrl, std::bind(&NutCommandConnector::completionCallback, this, std::placeholders::_1, std::placeholders::_2))
{
    m_msgBus = messagebus::MlmMessageBus(m_parameters.endpoint, m_parameters.agentName);
    m_msgBus->connect();

    auto fct = std::bind(&NutCommandConnector::handleRequest, this, std::placeholders::_1);
    m_msgBus->receive("ETN.Q.IPMCORE.POWERACTION", fct);
}

NutCommandConnector::~NutCommandConnector() {
    std::lock_guard<std::mutex> lk(m_mutex);
    delete m_msgBus;
}

void NutCommandConnector::handleRequest(messagebus::Message msg) {
    if ((msg.metaData().count(messagebus::Message::SUBJECT) == 0) ||
        (msg.metaData().count(messagebus::Message::COORELATION_ID) == 0)) {
        log_error("Missing subject/correlationID in request.");
        return;
    }

    const auto& subject = msg.metaData()[messagebus::Message::SUBJECT];
    const auto& correlationId = msg.metaData()[messagebus::Message::COORELATION_ID];
    if (subject == "PerformCommands") {
        dto::commands::PerformCommandsQueryDto query;
        msg.userData() >> query;

        std::lock_guard<std::mutex> lk(m_mutex);

        try {
            log_trace("Handling PerformCommands correlation ID=%s.", correlationId.c_str());
            m_pendingJobs.emplace(std::make_pair(correlationId, msg.metaData()));
            m_manager.submitWork(correlationId, query.commands);
        }
        catch (std::exception &e) {
            log_error("Exception while processing command PerformCommands: %s.", e.what());
            m_pendingJobs.erase(correlationId);
            buildAndSendReply(msg.metaData(), false);
        }
    }
    else if (subject == "GetCommands") {
        dto::commands::GetCommandsQueryDto query;
        msg.userData() >> query;

        dto::commands::GetCommandsReplyDto reply = m_manager.getCommands(query.asset);
        messagebus::UserData replyData;
        replyData << reply;
        buildAndSendReply(msg.metaData(), true, replyData);
    }
    else {
        log_error("Unknown subject '%s' in request.", subject.c_str());
    }
}

void NutCommandConnector::completionCallback(std::string correlationId, bool result) {
    messagebus::MetaData job;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        job = m_pendingJobs.at(correlationId);
        m_pendingJobs.erase(correlationId);
    }

    if (result) {
        log_info("PerformCommands correlation ID=%s finished with %s.", correlationId.c_str(), "success");
    }
    else {
        log_error("PerformCommands correlation ID=%s finished with %s.", correlationId.c_str(), "failure");
    }
    buildAndSendReply(job, result);
}

void NutCommandConnector::buildAndSendReply(const messagebus::MetaData &sender, bool result, const messagebus::UserData &data) {
    messagebus::Message message;
    message.metaData()[messagebus::Message::COORELATION_ID] = sender.at(messagebus::Message::COORELATION_ID);
    message.metaData()[messagebus::Message::SUBJECT] = sender.at(messagebus::Message::SUBJECT);
    message.metaData()[messagebus::Message::STATUS] = result ? "ok" : "ko";
    message.metaData()[messagebus::Message::TO] = sender.at(messagebus::Message::REPLY_TO);
    message.userData() = data;
    m_msgBus->sendReply(sender.at(messagebus::Message::REPLY_TO), message);
}

}

void
fty_nut_command_server_test(bool verbose)
{
    printf(" * fty_nut_command_server: ");
    printf ("OK\n");
}
