/*  =========================================================================
    fty_nut_server - fty nut actor

    Copyright (C) 2014 - 2017 Eaton

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
    fty_nut_server - fty nut actor
@discuss
@end
*/

#include "actor_commands.h"
#include "fty_nut_server.h"
#include "state_manager.h"
#include "nut_agent.h"
#include "nut_mlm.h"
#include <fty_log.h>

StateManager NutStateManager;

static bool
get_initial_licensing(StateManager::Writer& state_writer, mlm_client_t *client)
{
    ZuuidGuard uuid(zuuid_new());
    int err = mlm_client_sendtox(client, "etn-licensing", "LIMITATIONS",
                "LIMITATION_QUERY", zuuid_str_canonical(uuid), "*", "*", NULL);
    if (err < 0) {
        log_error("Sending LIMITATION_QUERY message to etn-licensing failed");
        return false;
    }
    zmsg_t* reply = mlm_client_recv(client);
    if (!reply) {
        zmsg_destroy(&reply);
        log_error("Getting response to LIMITATION_QUERY failed");
        return false;
    }
    ZstrGuard reply_str(zmsg_popstr(reply));
    if (!reply_str || strcmp(reply_str, zuuid_str_canonical(uuid)) != 0) {
        zmsg_destroy(&reply);
        log_warning("Mismatching response to a LIMITATION_QUERY request");
        return false;
    }
    reply_str = zmsg_popstr(reply);
    if (!reply_str || strcmp(reply_str, "REPLY") != 0) {
        zmsg_destroy(&reply);
        log_error("Got malformed message from etn-licensing");
        return false;
    }
    // The rest is a series of value/item/category triplets that
    // updateFromProto() can grok
    return state_writer.getState().updateFromProto(reply);
}

// Query fty-asset about existing devices. This has to be done after
// subscribing ourselves to the ASSETS stream, to make sure that we do not
// miss assets created between the mailbox request and the subscription to
// the stream.
void
get_initial_assets(StateManager::Writer& state_writer, mlm_client_t *client,
        bool query_licensing)
{
    zmsg_t *msg = zmsg_new();
    if (!msg) {
        log_error("Creating ASSETS message failed");
        return;
    }
    ZuuidGuard uuid(zuuid_new());
    if (!uuid) {
        zmsg_destroy(&msg);
        log_error("Creating UUID for the ASSETS message failed");
        return;
    }
    zmsg_addstr(msg, "GET");
    zmsg_addstr(msg, zuuid_str_canonical(uuid));
    zmsg_addstr(msg, "ups");
    zmsg_addstr(msg, "epdu");
    zmsg_addstr(msg, "sts");
    zmsg_addstr(msg, "sensor");
    zmsg_addstr(msg, "sensorgpio");
    if (mlm_client_sendto(client, "asset-agent", "ASSETS", NULL, 5000, &msg) < 0) {
        log_error("Sending ASSETS message failed");
        return;
    }
    ZmsgGuard reply;
    while (reply = mlm_client_recv(client)) {
        ZstrGuard uuid_reply(zmsg_popstr(reply));
        if (strcmp(uuid_reply, zuuid_str_canonical(uuid)) != 0) {
            log_warning("Mismatching response to an ASSETS request");
            continue;
        }
        ZstrGuard status(zmsg_popstr(reply));
        if (strcmp(status, "OK") != 0) {
            log_warning("Got %s response to an ASSETS request", status.get());
            zmsg_print(reply);
            return;
        }
        break;
    }

    ZstrGuard asset(zmsg_popstr(reply));
    // Remember which UUIDs we sent
    std::set<std::string> uuids;
    while (asset) {
        ZuuidGuard uuid(zuuid_new());
        auto i = uuids.emplace(zuuid_str_canonical(uuid));
        zmsg_t *req = zmsg_new();
        zmsg_addstr(req, "GET");
        zmsg_addstr(req, i.first->c_str());
        zmsg_addstr(req, asset);
        if (mlm_client_sendto(client, "asset-agent", "ASSET_DETAIL", NULL, 5000, &req) < 0) {
            log_error("Sending ASSET_DETAIL message for %s failed", asset.get());
        }
        asset = zmsg_popstr(reply);
    }
    bool changed = false;
    while (!uuids.empty()) {
        zmsg_t *reply = mlm_client_recv(client);
        if (uuids.erase(ZstrGuard(zmsg_popstr(reply)).get()) == 0) {
            log_warning("Mismatching response to an ASSET_DETAIL request");
            zmsg_destroy(&reply);
            continue;
        }
        if (!is_fty_proto(reply)) {
            log_warning("Response to an ASSET_DETAIL message is not fty_proto");
            zmsg_destroy(&reply);
            continue;
        }
        if (state_writer.getState().updateFromProto(reply))
            changed = true;
    }
    if (query_licensing) {
        if (get_initial_licensing(state_writer, client))
            changed = true;
    }
    if (changed)
        state_writer.commit();
    log_info("Initial ASSETS request complete (%zd/%zd powerdevices, %zd/%zd sensors)",
		    state_writer.getState().getPowerDevices().size(),
		    state_writer.getState().getAllPowerDevices().size(),
		    state_writer.getState().getSensors().size(),
		    state_writer.getState().getAllSensors().size());
}

uint64_t
polling_timeout(uint64_t last_poll, uint64_t polling_timeout)
{
    static uint32_t too_short_poll_count = 0;

    uint64_t now = static_cast<uint64_t> (zclock_mono ());
    if (last_poll + polling_timeout < now) {
        too_short_poll_count++;
        if (too_short_poll_count > 10) {
            log_error("Can't handle so many devices in so short polling interval");
        }
        return 0;
    }
    too_short_poll_count = 0;
    return last_poll + polling_timeout - now;
}

void
fty_nut_server (zsock_t *pipe, void *args)
{
    const char *endpoint = static_cast<const char *>(args);

    MlmClientGuard client(mlm_client_new());
    if (!client) {
        log_fatal ("mlm_client_new () failed");
        return;
    }
    if (mlm_client_connect(client, endpoint, 5000, ACTOR_NUT_NAME) < 0) {
        log_error("client %s failed to connect", ACTOR_NUT_NAME);
        return;
    }
    if (mlm_client_set_producer(client, FTY_PROTO_STREAM_METRICS) < 0) {
        log_error("mlm_client_set_producer (stream = '%s') failed",
                FTY_PROTO_STREAM_METRICS);
        return;
    }
    if (mlm_client_set_consumer(client, FTY_PROTO_STREAM_ASSETS, ".*") < 0) {
        log_error("mlm_client_set_consumer (stream = '%s', pattern = '.*') failed",
                FTY_PROTO_STREAM_ASSETS);
        return;
    }
    if (mlm_client_set_consumer(client, "LICENSING-ANNOUNCEMENTS", ".*") < 0) {
        log_error("mlm_client_set_consumer (stream = '%s', pattern = '.*') failed",
                "LICENSING-ANNOUNCEMENTS");
        return;
    }

    // inventory client
    MlmClientGuard iclient(mlm_client_new());
    if (!iclient) {
        log_fatal ("mlm_client_new () failed");
        return;
    }
    int r = mlm_client_connect (iclient, endpoint, 5000, "bios-agent-nut-inventory");
    if (r == -1) {
        log_error ("connect of iclient failed");
        return;
    }
    r = mlm_client_set_producer (iclient, FTY_PROTO_STREAM_ASSETS);
    if (r == -1) {
        log_error ("iclient set_producer failed");
        return;
    }

    ZpollerGuard poller(zpoller_new (pipe, mlm_client_msgpipe (client), NULL));
    if (!poller) {
        log_fatal ("zpoller_new () failed");
        return;
    }

    NUTAgent nut_agent(NutStateManager.getReader());

    zsock_signal (pipe, 0);

    nut_agent.setClient (client);
    nut_agent.setiClient (iclient);

    StateManager::Writer& state_writer = NutStateManager.getWriter();
    // (Ab)use the iclient for the initial assets mailbox request, because it
    // will not receive any interfering stream messages
    get_initial_assets(state_writer, iclient, true);

    uint64_t timestamp = static_cast<uint64_t> (zclock_mono ());
    uint64_t timeout = 30000;

    uint64_t last = zclock_mono ();
    while (!zsys_interrupted) {
        void *which = zpoller_wait (poller, polling_timeout (timestamp, timeout));
        uint64_t now = zclock_mono();
        if (now - last >= timeout) {
            last = now;
            zsys_debug("Periodic polling");
            nut_agent.updateDeviceList();
            nut_agent.onPoll();
        }
        if (which == NULL) {
            if (zpoller_terminated (poller) || zsys_interrupted) {
                log_warning ("zpoller_terminated () or zsys_interrupted");
                break;
            }
            if (zpoller_expired (poller)) {
                timestamp = static_cast<uint64_t> (zclock_mono ());
            }
            continue;
        }

        if (which == pipe) {
            zmsg_t *message = zmsg_recv (pipe);
            if (!message) {
                log_error ("Given `which == pipe`, function `zmsg_recv (pipe)` returned NULL");
                continue;
            }
            if (actor_commands (client, &message, timeout, nut_agent) == 1) {
                break;
            }
            continue;
        }

        // paranoid non-destructive assertion of a twisted mind
        if (which != mlm_client_msgpipe (client)) {
            log_fatal (
                    "zpoller_wait () returned address that is different from "
                    "`pipe`, `mlm_client_msgpipe (client)`, NULL.");
            continue;
        }

        zmsg_t *message = mlm_client_recv (client);
        if (!message) {
            log_error ("Given `which == mlm_client_msgpipe (client)`, function `mlm_client_recv ()` returned NULL");
            continue;
        }
        if (is_fty_proto(message)) {
            if (state_writer.getState().updateFromProto(message))
                state_writer.commit();
            continue;
        }
        log_error ("Unhandled message (%s/%s)",
                mlm_client_command(client), mlm_client_subject(client));
        zmsg_print (message);
        zmsg_destroy (&message);
    } // while (!zsys_interrupted)
}


//  --------------------------------------------------------------------------
//  Self test of this class

void
fty_nut_server_test (bool verbose)
{
    printf (" * fty_nut_server: ");

    //  @selftest
    //  @end
    printf ("Empty test - OK\n");
}
