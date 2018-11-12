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

const char *NUT_HOSTNAME = "localhost";
const char *NUT_USERNAME = "admin";
const char *NUT_PASSWORD = "mypass";

struct mapped_device
{
    mapped_device(const std::string &n, int d) : name(n), daisy_chain(d) {}

    std::string name;
    int daisy_chain;
};

//
// Helpers
//

static mapped_device
asset_to_mapped_device(tntdb::Connection &conn, const std::string &asset)
{
    int32_t assetId = DBAssets::name_to_asset_id(asset);
    if (assetId < 0) {
        throw std::runtime_error("Unknown asset");
    }

    db_reply <std::map <int, uint32_t> > daisy_chain = DBAssets::select_daisy_chain(conn, assetId);
    if (!daisy_chain.status) {
        throw std::runtime_error(daisy_chain.msg);
    }
    if (daisy_chain.item.size() == 0) {
        return mapped_device(asset, 0);
    }
    else {
        int daisy_number = 0;
        for (const auto &i : daisy_chain.item) {
            if (i.second == assetId) {
                daisy_number = i.first;
                break;
            }
        }
        return mapped_device(DBAssets::id_to_name_ext_name(daisy_chain.item.begin()->second).first, daisy_number);
    }
}

static void
send_reply(mlm_client_t *client, const std::string &address, const std::string &command, zmsg_t **msg)
{
    if (mlm_client_sendto(client, address.c_str(), COMMAND_SUBJECT, nullptr, TIMEOUT, msg) == 0) {
        log_info("Processed request '%s' by '%s'", command.c_str(), address.c_str());
    }
    else {
        log_error("Failed to send reply of request '%s' to '%s'", command.c_str(), address.c_str());
    }
}

static std::set<std::string>
get_commands_nut(nut::Client &nut, const mapped_device &asset)
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
send_error(mlm_client_t *client, const std::string &address, const std::string &reason, const char *uuid)
{
    zmsg_t *error = zmsg_new();
    zmsg_addstr(error, "ERROR");
    zmsg_addstr(error, uuid);
    zmsg_addstr(error, reason.c_str());

    if (mlm_client_sendto(client, address.c_str(), COMMAND_SUBJECT, nullptr, TIMEOUT, &error) == 0) {
        log_error("Sent error '%s' to '%s'", reason.c_str(), address.c_str());
    }
    else {
        log_error("Failed to send error '%s' to '%s'", reason.c_str(), address.c_str());
    }
}

//
// Commands
//

static void
get_commands(nut::Client &nut, tntdb::Connection &conn, mlm_client_t *client, const std::string &address, zmsg_t *msg, const char *uuid)
{
    std::vector<std::pair<std::string, std::set<std::string>>> replyData;

    while (zmsg_size(msg)) {
        ZstrGuard asset(zmsg_popstr(msg));
        replyData.emplace_back(asset.get(), get_commands_nut(nut, asset_to_mapped_device(conn, std::string(asset))));
    }

    // Build reply message
    zmsg_t *reply = zmsg_new();
    zmsg_addstr(reply, "OK");
    zmsg_addstr(reply, uuid);
    for (const auto &asset : replyData) {
        zmsg_addstr(reply, "ASSET");
        zmsg_addstr(reply, asset.first.c_str());
        for (const auto &commands : asset.second) {
            zmsg_addstr(reply, commands.c_str());
        }
    }

    send_reply(client, address, "GET_COMMANDS", &reply);
}

static void
do_commands(nut::Client &nut, tntdb::Connection &conn, mlm_client_t *client, const std::string &address, zmsg_t *msg, const char *uuid)
{
    ZstrGuard asset(zmsg_popstr(msg));
    if (!asset) {
        throw std::runtime_error("INVALID_REQUEST");
    }
    auto device = asset_to_mapped_device(conn, std::string(asset));
    auto valid_commands = get_commands_nut(nut, device);

    if (zmsg_size(msg) % 2) {
        throw std::runtime_error("INVALID_REQUEST");
    }

    std::vector<std::pair<std::string, std::string>> commands;
    while (zmsg_size(msg)) {
        ZstrGuard cmd(zmsg_popstr(msg));
        ZstrGuard data(zmsg_popstr(msg));

        // Check if command is known in advance (try to prevent failures halfway)
        if (!valid_commands.count(cmd.get())) {
            throw std::runtime_error("CMD-NOT-SUPPORTED");
        }
        // XXX: we don't handle arguments just yet
        if (!streq(data, "")) {
            throw std::runtime_error("CMD-NOT-SUPPORTED");
        }

        commands.emplace_back(cmd.get(), data.get());
    }

    const std::string prefix = device.daisy_chain ? std::string("device.") + std::to_string(device.daisy_chain) + "." : "";
    for (const auto &i : commands) {
        nut.executeDeviceCommand(device.name, prefix + i.first);
    }

    zmsg_t *reply = zmsg_new();
    zmsg_addstr(reply, "OK");
    zmsg_addstr(reply, uuid);
    send_reply(client, address, "DO_COMMANDS", &reply);
}

//
// Mainloop
//

void
fty_nut_command_server(zsock_t *pipe, void *args)
{
    // Connect to Malamute
    MlmClientGuard client(mlm_client_new());
    if (!client) {
        log_error("mlm_client_new() failed");
        return;
    }
    const char *endpoint = static_cast<const char*>(args);
    if (mlm_client_connect(client, endpoint, 5000, ACTOR_COMMAND_NAME) < 0) {
        log_error("client %s failed to connect", ACTOR_COMMAND_NAME);
        return;
    }

    // Grab DB credentials
    if (!DBConn::dbreadcredentials()) {
        log_error("failed to read DB credentials");
        return;
    }
    DBConn::dbpath();

    // Enter mainloop
    ZpollerGuard poller(zpoller_new(pipe, mlm_client_msgpipe(client), nullptr));
    zsock_signal(pipe, 0);
    while (!zsys_interrupted) {
        void *which = zpoller_wait(poller, -1);
        if (which == pipe || zsys_interrupted) {
            break;
        }

        ZmsgGuard msg(mlm_client_recv(client));

        if (!streq(mlm_client_subject(client), COMMAND_SUBJECT)) {
            log_error("Unrecognized subject '%s', ignoring message", mlm_client_subject(client));
            continue;
        }
        if (zmsg_size(msg) < 2) {
            log_error("Message doesn't have UUID and command fields, ignoring message", mlm_client_subject(client));
            continue;
        }

        const char *sender = mlm_client_sender(client);
        ZstrGuard action(zmsg_popstr(msg));
        ZstrGuard uuid(zmsg_popstr(msg));

        try {
            // Connect to NUT server
            nut::TcpClient nutClient;
            nutClient.connect(NUT_HOSTNAME);
            log_info("Connected to NUT server");
            nutClient.authenticate(NUT_USERNAME, NUT_PASSWORD);
            log_info("Authenticated to NUT server");

            // Connect to database
            tntdb::Connection conn = tntdb::connectCached(DBConn::url);

            log_info("Received request '%s' from '%s', UUID='%s'", action.get(), sender, uuid.get());

            if (streq(action.get(), "GET_COMMANDS")) {
                get_commands(nutClient, conn, client, sender, msg, uuid.get());
            }
            else if (streq(action.get(), "DO_COMMANDS")) {
                do_commands(nutClient, conn, client, sender, msg, uuid.get());
            }
            else if (streq(action.get(), "ERROR")) {
                ZstrGuard desc(zmsg_popstr(msg));
                log_error("Received error message with payload '%s' '%s', ignoring", uuid ? uuid.get() : "(null)", desc ? desc.get() : "(null)");
            }
            else {
                log_error("Request '%s' is not valid", action.get());
                throw std::runtime_error("INVALID_REQUEST");
            }
        } catch (std::exception &e) {
            send_error(client, sender, e.what(), uuid.get());
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
