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
#include "logger.h"

StateManager NutStateManager;

static void
s_handle_fty_proto (
        mlm_client_t *client,
        NUTAgent& nut_agent,
        StateManager::Writer& state_writer,
        zmsg_t *message)
{
    assert (client);
    assert (message);

    if (!is_fty_proto (message)) {
        log_warning (
                "Message received is not fty_proto; sender = '%s', subject = '%s'",
                mlm_client_sender (client), mlm_client_subject (client));
        zmsg_destroy (&message);
        return;
    }
    fty_proto_t *proto = fty_proto_decode (&message);
    if (!proto) {
        log_critical ("fty_proto_decode () failed.");
        zmsg_destroy (&message);
        return;
    }
    if (state_writer.getState().updateFromProto(proto))
        state_writer.commit();
    fty_proto_destroy(&proto);
    nut_agent.updateDeviceList();
}


// Handle response to the initial ASSETS request
static void
s_handle_ASSETS (mlm_client_t *client, zmsg_t *message, zuuid_t **uuid_p)
{
    assert (client);
    assert (message);
    ZmsgGuard msg(message);
    zuuid_t *uuid_request = *uuid_p;

    if (!uuid_request) {
        log_warning("Spurious response to an ASSETS request");
        return;
    }
    char *uuid = zmsg_popstr(msg);
    if (strcmp(uuid, zuuid_str_canonical(uuid_request)) != 0) {
        log_warning ("Mismatching response to an ASSETS request");
        return;
    }
    zuuid_destroy(uuid_p);
    zstr_free(&uuid);
    char *status = zmsg_popstr(msg);
    if (strcmp(status, "OK") != 0) {
        log_warning("Got %s response to an ASSETS request", status);
        zmsg_print(msg);
    }
    zstr_free(&status);

    char *asset = zmsg_popstr(msg);
    while (asset) {
        zuuid_t *uuid = zuuid_new();
        zmsg_t *req = zmsg_new();
        zmsg_addstr(req, "GET");
        zmsg_addstr(req, zuuid_str_canonical(uuid));
        zmsg_addstr(req, asset);
        if (mlm_client_sendto(client, "asset-agent", "ASSET_DETAIL", NULL, 5000, &req) < 0) {
            log_error ("Sending ASSET_DETAIL message for %s failed", asset);
        }
        asset = zmsg_popstr(msg);
        zuuid_destroy(&uuid);
    }
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
    bool verbose = false;
    const char *endpoint = static_cast<const char *>(args);

    MlmClientGuard client(mlm_client_new());
    if (!client) {
        log_critical ("mlm_client_new () failed");
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

    // inventory client
    MlmClientGuard iclient(mlm_client_new());
    if (!iclient) {
        log_critical ("mlm_client_new () failed");
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
        log_critical ("zpoller_new () failed");
        return;
    }

    NUTAgent nut_agent(NutStateManager.getReader());

    zsock_signal (pipe, 0);

    nut_agent.setClient (client);
    nut_agent.setiClient (iclient);

    // Query fty-asset about existing devices. This has to be done after
    // subscribing ourselves to the ASSETS stream, to make sure that we do not
    // miss assets created between the mailbox request and the subscription to
    // the stream.
    zuuid_t *uuid_assets_request;
    {
        zmsg_t *msg = zmsg_new();
        if (!msg) {
            log_error ("Creating ASSETS message failed");
            return;
        }
        uuid_assets_request = zuuid_new();
        if (!uuid_assets_request) {
            zmsg_destroy(&msg);
            log_error("Creating UUID for the ASSETS message failed");
            return;
        }
        zmsg_addstr(msg, "GET");
        zmsg_addstr(msg, zuuid_str_canonical(uuid_assets_request));
        zmsg_addstr(msg, "ups");
        zmsg_addstr(msg, "epdu");
        zmsg_addstr(msg, "sts");
        if (mlm_client_sendto(client, "asset-agent", "ASSETS", NULL, 5000, &msg) < 0) {
            log_error("Sending ASSETS message failed");
            zuuid_destroy(&uuid_assets_request);
            return;
        }
    }

    StateManager::Writer& state_writer = NutStateManager.getWriter();

    uint64_t timestamp = static_cast<uint64_t> (zclock_mono ());
    uint64_t timeout = 30000;

    uint64_t last = zclock_mono ();
    while (!zsys_interrupted) {
        void *which = zpoller_wait (poller, polling_timeout (timestamp, timeout));
        uint64_t now = zclock_mono();
        if (now - last >= timeout) {
            last = now;
            zsys_debug("Periodic polling");
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
            if (actor_commands (client, &message, verbose, timeout, nut_agent) == 1) {
                break;
            }
            continue;
        }

        // paranoid non-destructive assertion of a twisted mind
        if (which != mlm_client_msgpipe (client)) {
            log_critical (
                    "zpoller_wait () returned address that is different from "
                    "`pipe`, `mlm_client_msgpipe (client)`, NULL.");
            continue;
        }

        zmsg_t *message = mlm_client_recv (client);
        if (!message) {
            log_error ("Given `which == mlm_client_msgpipe (client)`, function `mlm_client_recv ()` returned NULL");
            continue;
        }

        const char *command = mlm_client_command (client);
        const char *subject = mlm_client_subject (client);
        if (streq (command, "MAILBOX DELIVER")) {
            if (strcmp(subject, "ASSETS") == 0) {
                s_handle_ASSETS (client, message, &uuid_assets_request);
                continue;
            }
            // Assume that this is a response to an ASSET_DETAIL message,
            // which contains the UUID and a proto message. Just discard the
            // UUID for now
            free(zmsg_popstr(message));
        }
        if (is_fty_proto(message)) {
            // fty_proto messages are received over the ASSETS stream and as
            // responses to the ASSET_DETAIL mailbox request
            s_handle_fty_proto (client, nut_agent, state_writer, message);
            continue;
        }
        log_error ("Unhandled message (%s/%s)", command, subject);
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
