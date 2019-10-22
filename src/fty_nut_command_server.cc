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
 * \brief Algorithm for emitting a number of objects from an object.
 */
template <class InputIt, class OutputIt, class UnaryOperation> OutputIt emit(InputIt i_first, OutputIt o_first, UnaryOperation unary_op) {
    while (i_first != o_first) {
        auto emitted = unary_op(*i_first++);
        for (auto it = emitted.begin(); it != emitted.end(); it++) {
            *o_first++ = *it;
        }
    }
    return o_first;
}

/**
 * \brief Map from 42ity daisy-chained to NUT command.
 */
static dto::commands::Command daisyChainedToNutCommand(tntdb::Connection &conn, const dto::commands::Command &job) {
    dto::commands::Command command = job;

    auto daisy_chain = DBAssets::select_daisy_chain(conn, job.asset);
    if (daisy_chain.status && daisy_chain.item.size() != 0) {
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

        log_debug("Executing NUT command '%s' argument '%s' on asset '%s'.", nutCommand.c_str(), job.argument.c_str(), job.asset.c_str());
        auto id = client.executeDeviceCommand(job.asset, job.command, job.argument);
        ids.insert(id);
        log_trace("NUT job ID: %s.", id.c_str());
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
                log_trace("NUT job ID '%s' finished with %s.", jobCompleted.first.c_str(), jobCompleted.second ? "success" : "failure");
                m_callback(jobCompleted.first, jobCompleted.second);
            }
        }
    }
}

NutCommandManager::NutCommandManager(const std::string& nutHost, const std::string& nutUsername, const std::string& nutPassword, const std::string& dbConn, CompletionCallback callback) :
    m_worker(nutHost, nutUsername, nutPassword, std::bind(&NutCommandManager::completionCallback, this, std::placeholders::_1, std::placeholders::_2)),
    m_dbConn(dbConn),
    m_callback(callback)
{
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

    // Convert 42ity jobs to NUT commands.
    dto::commands::Commands nutJobs;
    std::transform(jobs.begin(), jobs.end(), std::back_inserter(nutJobs), std::bind(daisyChainedToNutCommand, std::ref(conn), std::placeholders::_1));

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
                    job.success &= job.success;
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
    m_msgBus->receive("ETN.Q.IPMCORE.NUTCOMMAND", fct);
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

    log_trace("PerformCommands correlation ID=%s finished with %s.", correlationId.c_str(), result ? "success" : "failure");
    buildAndSendReply(job, result);
}

void NutCommandConnector::buildAndSendReply(const messagebus::MetaData &sender, bool result) {
    messagebus::Message message;
    message.metaData()[messagebus::Message::COORELATION_ID] = sender.at(messagebus::Message::COORELATION_ID);
    message.metaData()[messagebus::Message::SUBJECT] = sender.at(messagebus::Message::SUBJECT);
    message.metaData()[messagebus::Message::STATUS] = result ? "ok" : "ko";
    message.metaData()[messagebus::Message::TO] = sender.at(messagebus::Message::REPLY_TO);
    m_msgBus->sendReply(sender.at(messagebus::Message::REPLY_TO), message);
}

}

#if 0


const char *COMMAND_SUBJECT = "power-actions";
const char *ACTOR_COMMAND_NAME = "fty-nut-command";
const int TIMEOUT = 5;


struct PendingCommand
{
    PendingCommand(const std::list<nut::TrackingID>& ids, const std::string& addr, const std::string& msgId) : trackingIDs(ids), address(addr), uuid(msgId), numCommands(ids.size()) {}

    std::list<nut::TrackingID> trackingIDs;
    std::string address;
    std::string uuid;
    std::string errMsg;
    int numCommands;
};

typedef std::list<PendingCommand> PendingCommands;

//
// Helpers
//

static void
send_reply(mlm_client_t *client, const std::string &address, const std::string &command, zmsg_t **msg)
{
    std::string status = zmsg_popstr(*msg);
    zmsg_pushstr(*msg, status.c_str());

    if (mlm_client_sendto(client, address.c_str(), COMMAND_SUBJECT, nullptr, TIMEOUT, msg) == 0) {
        if (status == "OK") {
            log_info("Processed request '%s' from '%s', status='%s'.", command.c_str(), address.c_str(), status.c_str());
        }
        else {
            log_error("Processed request '%s' from '%s', status='%s'.", command.c_str(), address.c_str(), status.c_str());
        }
    }
    else {
        log_error("Failed to send reply of request '%s' to '%s', status='%s'.", command.c_str(), address.c_str(), status.c_str());
    }
}

static void
send_error(mlm_client_t *client, const std::string &address, const std::string &command, const std::string &reason, const std::string &uuid)
{
    zmsg_t *error = zmsg_new();
    zmsg_addstr(error, "ERROR");
    zmsg_addstr(error, uuid.c_str());
    zmsg_addstr(error, JSONIFY(reason.c_str()).c_str());

    send_reply(client, address, command, &error);
}

//
// Commands
//

static void
get_commands(nut::TcpClient &nut, tntdb::Connection &conn, mlm_client_t *client, const std::string &address, const std::string &uuid, zmsg_t *msg)
{
    std::vector<std::pair<std::string, std::set<std::string>>> replyData;
    std::map<std::string, MappedDevice> mappedDevices;

    while (zmsg_size(msg)) {
        ZstrGuard asset(zmsg_popstr(msg));
        MappedDevice device = asset_to_mapped_device(conn, std::string(asset));
        replyData.emplace_back(asset.get(), get_commands_nut(nut, device));
        mappedDevices.emplace(asset.get(), device);
    }

    // Build reply message
    zmsg_t *reply = zmsg_new();
    zmsg_addstr(reply, "OK");
    zmsg_addstr(reply, uuid.c_str());

    for (const auto &asset : replyData) {
        const auto& mappedDevice = mappedDevices.at(asset.first);
        const std::string pattern = mappedDevice.daisy_chain ? std::string("device.") + std::to_string(mappedDevice.daisy_chain) + "." : "";

        // Push asset name.
        zmsg_addstr(reply, "ASSET");
        zmsg_addstr(reply, asset.first.c_str());

        for (const auto &commands : asset.second) {
            // Push commands of asset.
            zmsg_addstr(reply, commands.c_str());
            zmsg_addstr(reply, nut.getDeviceCommandDescription(mappedDevice.name, pattern+commands).c_str());
        }
    }

    send_reply(client, address, "GET_COMMANDS", &reply);
}

static void
do_native_commands(nut::TcpClient &nut, tntdb::Connection &conn, mlm_client_t *client, const std::string &address, const std::string &uuid, zmsg_t *msg, PendingCommands &pendingCommands)
{
    ZstrGuard asset(zmsg_popstr(msg));

    if (zmsg_size(msg) % 2) {
        throw std::runtime_error("INVALID_REQUEST");
    }

    std::list<nut::TrackingID> trackingIDs;

    while (zmsg_size(msg)) {
        ZstrGuard cmd(zmsg_popstr(msg));
        ZstrGuard data(zmsg_popstr(msg));
        auto device = asset_to_mapped_device(conn, std::string(asset));
        const auto prefix = device.daisy_chain ? std::string("device.") + std::to_string(device.daisy_chain) + "." : "";
        trackingIDs.emplace_back(nut.executeDeviceCommand(device.name, prefix + cmd.get(), data.get()));
    }

    // Store pending command data for further processing.
    pendingCommands.emplace_back(trackingIDs, address, uuid);
    log_debug("Sent %d NUT commands, correlation ID='%s'.", trackingIDs.size(), uuid.c_str());
}

static void
do_commands(nut::TcpClient &nut, tntdb::Connection &conn, mlm_client_t *client, const std::string &address, const std::string &uuid, zmsg_t *msg, PendingCommands &pendingCommands)
{
    std::string s_action;
    std::string s_asset;
    std::string s_delay;

    ZstrGuard action(zmsg_popstr(msg));   // action to execute
    s_action = action.get();

    if( s_action == "SHUTDOWN_ASSET_POWER_SOURCES" ) {
        if (zmsg_size(msg) != 2) {
            throw std::runtime_error("INVALID_REQUEST");
        }

        ZstrGuard asset(zmsg_popstr(msg)); // asset on which compute action
        ZstrGuard delay(zmsg_popstr(msg)); // delay beafore action

        s_asset  = asset.get();
        s_delay  = delay.get();
    }
}

static void
treat_pending_commands(nut::TcpClient &nutClient, mlm_client_t *client, PendingCommands &pendingCommands)
{
    // Check if any pending commands are completed.
    for (auto &command : pendingCommands) {
        auto treated = std::remove_if(command.trackingIDs.begin(), command.trackingIDs.end(),
            [&command, &nutClient](nut::TrackingID &id) -> bool {
                try {
                    switch (nutClient.getTrackingResult(id)) {
                        case nut::FAILURE:
                            command.errMsg = TRANSLATE_ME("Failed to execute command on power device.");
                            return true;
                        case nut::SUCCESS:
                            return true;
                        default:
                            return false;
                    }
                }
                catch (nut::NutException &e) {
                    command.errMsg = e.what();
                    return true;
                }
            });

        command.trackingIDs.erase(treated, command.trackingIDs.end());
        log_debug("Reclaimed %d/%d results from NUT command, correlation ID ='%s'.", command.numCommands - command.trackingIDs.size(), command.numCommands, command.uuid.c_str());
    }

    // Check if any pending requests are completed.
    auto treated = std::remove_if(pendingCommands.begin(), pendingCommands.end(),
        [](PendingCommand &c) -> bool {
            return c.trackingIDs.empty();
        }
    );

    // Send results of completed requests.
    for (auto it = treated; it != pendingCommands.end(); it++) {
        if (it->errMsg.empty()) {
            zmsg_t *reply = zmsg_new();
            zmsg_addstr(reply, "OK");
            zmsg_addstr(reply, it->uuid.c_str());
            send_reply(client, it->address, "DO_NATIVE_COMMANDS", &reply);
        }
        else {
            send_error(client, it->address, "DO_NATIVE_COMMANDS", it->errMsg, it->uuid.c_str());
        }
    }

    // Purge completed commands from memory.
    pendingCommands.erase(treated, pendingCommands.end());
}

//
// Mainloop
//

void
fty_nut_command_server(zsock_t *pipe, void *args)
{
    PendingCommands pendingCommands;

    std::string dbURL;
    std::string nutHost = "localhost";
    std::string nutUsername;
    std::string nutPassword;

    // Connect to Malamute
    MlmClientGuard client(mlm_client_new());
    if (!client) {
        log_error("mlm_client_new() failed.");
        return;
    }
    const char *endpoint = static_cast<const char*>(args);
    if (mlm_client_connect(client, endpoint, 5000, ACTOR_COMMAND_NAME) < 0) {
        log_error("Client %s failed to connect.", ACTOR_COMMAND_NAME);
        return;
    }

    int64_t lastTrackingCheck = zclock_mono();
    int64_t lastPingPong = zclock_mono();
    nut::TcpClient nutClient;

    // Enter mainloop
    ZpollerGuard poller(zpoller_new(pipe, mlm_client_msgpipe(client), nullptr));
    zsock_signal(pipe, 0);

    while (!zsys_interrupted) {
        void *which = zpoller_wait(poller, pendingCommands.empty() ? 15000 : 250);
        if (zsys_interrupted) {
            break;
        }
        if (which == pipe) {
            ZmsgGuard msg(zmsg_recv(pipe));
            ZstrGuard actor_command(zmsg_popstr(msg));

            // $TERM actor command implementation is required by zactor_t interface
            if (streq(actor_command, "$TERM")) {
                return;
            }
            else if (streq(actor_command, "CONFIGURATION")) {
                // Configure agent
                try {
                    ZstrGuard host(zmsg_popstr(msg));
                    ZstrGuard username(zmsg_popstr(msg));
                    ZstrGuard password(zmsg_popstr(msg));
                    nutHost = host.get();
                    nutUsername = username.get();
                    nutPassword = password.get();

                    log_info("Connecting to NUT server '%s'...", host.get());
                    nutClient.connect(nutHost);
                    log_info("Connected to NUT server '%s'.", host.get());
                    log_info("Authenticating to NUT server '%s' as '%s'...", host.get(), username.get());
                    nutClient.authenticate(nutUsername, nutPassword);
                    log_info("Authenticated to NUT server '%s' as '%s'.", host.get(), username.get());
                    nutClient.setFeature(nut::TcpClient::TRACKING, true);

                    ZstrGuard databaseURL(zmsg_popstr(msg));
                    dbURL = databaseURL.get();
                    log_info("Database URL configured.");
                }
                catch (nut::NutException &e) {
                    zstr_send(pipe, "NUT_CONNECTION_FAILURE");
                }
            }
            else {
                log_error("Unrecognized pipe request '%s'.", actor_command.get());
                continue;
            }
        }

        else if (which == mlm_client_msgpipe(client)) {
            ZmsgGuard msg(mlm_client_recv(client));

            if (!streq(mlm_client_subject(client), COMMAND_SUBJECT)) {
                log_error("Unrecognized subject '%s', ignoring message.", mlm_client_subject(client));
                continue;
            }
            if (zmsg_size(msg) < 2) {
                log_error("Message doesn't have correlation id and command fields, ignoring message.", mlm_client_subject(client));
                continue;
            }

            const char *sender = mlm_client_sender(client);
            ZstrGuard action(zmsg_popstr(msg));
            ZstrGuard uuid(zmsg_popstr(msg));

            try {
                // Connect to database
                tntdb::Connection conn = tntdb::connectCached(dbURL);

                log_info("Received request '%s' from '%s', correlation id='%s'.", action.get(), sender, uuid.get());

                if (streq(action, "GET_COMMANDS")) {
                    get_commands(nutClient, conn, client, sender, uuid.get(), msg);
                }
                else if (streq(action, "DO_NATIVE_COMMANDS")) {
                    do_native_commands(nutClient, conn, client, sender, uuid.get(), msg, pendingCommands);
                }
                else if (streq(action, "DO_COMMANDS")) {
                    do_commands(nutClient, conn, client, sender, uuid.get(), msg, pendingCommands);
                }
                else if (streq(action, "ERROR")) {
                    ZstrGuard desc(zmsg_popstr(msg));
                    log_error("Received error message with payload '%s' '%s', ignoring.", uuid ? uuid.get() : "(null)", desc ? desc.get() : "(null)");
                }
                else {
                    log_error("Request '%s' is not valid.", action.get());
                    throw std::runtime_error("INVALID_REQUEST");
                }
            } catch (std::exception &e) {
                log_error("Caught exception while processing request (%s).", e.what());
                send_error(client, sender, action.get(), e.what(), uuid.get());
            }
        }

        // Perform NUT house-keeping tasks.
        try {
            if ((zclock_mono() > lastTrackingCheck + 1000) && !pendingCommands.empty()) {
                treat_pending_commands(nutClient, client, pendingCommands);
                lastTrackingCheck = zclock_mono();
            }
            if (zclock_mono() > lastPingPong + 30000) {
                /**
                 * Keep alive the NUT connection. If the NUT server dies or the connection closed,
                 * eventually we're going to find it out here and bail out.
                 */
                // TODO: use something a bit more lightweight once available in libnutclient (like querying version numbers).
                nutClient.getDeviceNames();
                lastPingPong = zclock_mono();
            }
        }
        catch (nut::NutException &e) {
            log_fatal("Caught exception while performing NUT house-keeping tasks (%s), aborting...", e.what());
            zstr_send(pipe, "NUT_CONNECTION_FAILURE");
        }
    }
}

//  --------------------------------------------------------------------------
//  Self test of this class

void
fty_nut_command_server_test(bool verbose)
{
    printf(" * fty_nut_command_server: ");

    //  @selftest
    //  Simple create/destroy test
    static const char* endpoint = "inproc://fty_nut_command_server-test";
    zactor_t *mlm = zactor_new(mlm_server, (void*) "Malamute");
    assert(mlm);
    zstr_sendx(mlm, "BIND", endpoint, NULL);
    zactor_t *self = zactor_new(fty_nut_command_server, (void *)endpoint);
    assert (self);
    zactor_destroy (&self);
    zactor_destroy (&mlm);
    //  @end
    printf ("OK\n");
}

#endif

void
fty_nut_command_server_test(bool verbose)
{
    printf(" * fty_nut_command_server: ");
    printf ("OK\n");
}