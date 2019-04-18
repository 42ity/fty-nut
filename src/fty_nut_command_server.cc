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

const char *COMMAND_SUBJECT = "power-actions";
const char *ACTOR_COMMAND_NAME = "fty-nut-command";
const int TIMEOUT = 5;

struct MappedDevice
{
    MappedDevice(const std::string &n, int d) : name(n), daisy_chain(d) {}

    std::string name;
    int daisy_chain;
};

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

static MappedDevice
asset_to_mapped_device(tntdb::Connection &conn, const std::string &asset)
{
    auto daisy_chain = DBAssets::select_daisy_chain(conn, asset);
    if (!daisy_chain.status) {
        throw std::runtime_error(daisy_chain.msg);
    }
    if (daisy_chain.item.size() == 0) {
        return MappedDevice(asset, 0);
    }
    else {
        int daisy_number = 0;
        for (const auto &i : daisy_chain.item) {
            if (i.second == asset) {
                daisy_number = i.first;
                break;
            }
        }
        return MappedDevice(daisy_chain.item.begin()->second, daisy_number);
    }
}

static std::set<std::string>
get_commands_nut(nut::Client &nut, const MappedDevice &asset)
{
    const auto cmds = nut.getDeviceCommandNames(asset.name);

    if (asset.daisy_chain != 0) {
        // Device is daisy-chained, filter out and rename commands
        const std::string pattern = std::string("device.") + std::to_string(asset.daisy_chain) + ".";
        std::set<std::string> output;

        for (const auto &i : cmds) {
            if (i.find(pattern) != std::string::npos) {
                std::string fakeName = i;
                output.insert(fakeName.erase(0, pattern.length()));
            }
        }
        return output;
    }
    else {
        return cmds;
    }
}

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
    zmsg_addstr(error, reason.c_str());

    send_reply(client, address, command, &error);
}

//
// Commands
//

static void
get_commands(nut::Client &nut, tntdb::Connection &conn, mlm_client_t *client, const std::string &address, const std::string &uuid, zmsg_t *msg)
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
do_commands(nut::Client &nut, tntdb::Connection &conn, mlm_client_t *client, const std::string &address, const std::string &uuid, zmsg_t *msg, PendingCommands &pendingCommands)
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
treat_pending_commands(nut::Client &nutClient, mlm_client_t *client, PendingCommands &pendingCommands)
{
    // Check if any pending commands are completed.
    for (auto &command : pendingCommands) {
        auto treated = std::remove_if(command.trackingIDs.begin(), command.trackingIDs.end(),
            [&command, &nutClient](nut::TrackingID &id) -> bool {
                try {
                    switch (nutClient.getTrackingResult(id)) {
                        case nut::FAILURE:
                            command.errMsg = "Failure to execute command";
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
            send_reply(client, it->address, "DO_COMMANDS", &reply);
        }
        else {
            send_error(client, it->address, "DO_COMMANDS", it->errMsg, it->uuid.c_str());
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
                    nutClient.setFeature(nut::Client::TRACKING, true);

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
