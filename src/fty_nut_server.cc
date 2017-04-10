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

#include "fty_nut_classes.h"

/* Consumers of these vars are currently commented away below
static const char* PATH = "/var/lib/fty/fty-nut";
static const char* STATE = "/var/lib/fty/fty-nut/state_file";
 */

static void
s_handle_poll (NUTAgent& nut_agent, nut_t *data)
{
    assert (data);
    nut_agent.onPoll (data);
}

static void
s_handle_service (mlm_client_t *client, zmsg_t **message_p)
{
    assert (client);
    assert (message_p && *message_p);

    log_error ("Service deliver is not implemented.");

    zmsg_destroy (message_p);
}

static void
s_handle_mailbox (mlm_client_t *client, zmsg_t **message_p)
{
   assert (client);
   assert (message_p && *message_p);

   log_error ("Mailbox command is not implemented.");

   zmsg_destroy (message_p);
}

static void
s_handle_stream (mlm_client_t *client, NUTAgent& nut_agent, nut_t *data, zmsg_t **message_p)
{
    assert (client);
    assert (data);
    assert (message_p && *message_p);

    stream_deliver_handle (client, nut_agent, data, message_p);
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

    mlm_client_t *client = mlm_client_new ();
    if (!client) {
        log_critical ("mlm_client_new () failed");
        return;
    }
    int r = mlm_client_set_consumer (client, FTY_PROTO_STREAM_ASSETS, ".*");
    if (r == -1) {
        log_error ("client set_consumer failed");
    }

    // inventory client
    mlm_client_t *iclient = mlm_client_new ();
    if (!iclient) {
        log_critical ("mlm_client_new () failed");
        return;
    }
    r = mlm_client_connect (iclient, "ipc://@/malamute", 5000, "bios-agent-nut-inventory");
    if (r == -1) {
        log_error ("connect of iclient failed");
    }
    r = mlm_client_set_producer (iclient, FTY_PROTO_STREAM_ASSETS);
    if (r == -1) {
        log_error ("iclient set_producer failed");
    }

    zpoller_t *poller = zpoller_new (pipe, mlm_client_msgpipe (client), NULL);
    if (!poller) {
        log_critical ("zpoller_new () failed");
        mlm_client_destroy (&client);
        mlm_client_destroy (&iclient);
        return;
    }

    nut_t *data = nut_new ();
    if (!data) {
        zpoller_destroy (&poller);
        log_critical ("nut_new () failed");
        mlm_client_destroy (&client);
        mlm_client_destroy (&iclient);
        return;
    }
    NUTAgent nut_agent;
    std::string state_file;

    zsock_signal (pipe, 0);
/*
    r = nut_load (data, STATE);
    if (r != 0) {
        log_warning ("Could not load state file '%s'.", STATE);
    }
    nut_agent.updateDeviceList (data);
*/

    nut_agent.setiClient (iclient);

    uint64_t timestamp = static_cast<uint64_t> (zclock_mono ());
    uint64_t timeout = 30000;

    while (!zsys_interrupted) {
        void *which = zpoller_wait (poller, polling_timeout (timestamp, timeout));
        if (nut_changed (data)) {
            r = nut_save (data, state_file.c_str ());
            if (r != 0) {
                log_warning ("Could not save state file '%s'.", state_file.c_str ());
            }
        }
        if (which == NULL) {
            if (zpoller_terminated (poller) || zsys_interrupted) {
                log_warning ("zpoller_terminated () or zsys_interrupted");
                break;
            }
            if (zpoller_expired (poller)) {
                timestamp = static_cast<uint64_t> (zclock_mono ());
                s_handle_poll (nut_agent, data);
            }
            continue;
        }

        if (which == pipe) {
            zmsg_t *message = zmsg_recv (pipe);
            if (!message) {
                log_error ("Given `which == pipe`, function `zmsg_recv (pipe)` returned NULL");
                continue;
            }
            if (actor_commands (client, &message, verbose, timeout, nut_agent, data, state_file) == 1) {
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
        if (streq (command, "STREAM DELIVER")) {
            s_handle_stream (client, nut_agent, data, &message);
        }
        else
        if (streq (command, "MAILBOX DELIVER")) {
            s_handle_mailbox (client, &message);
        }
        else
        if (streq (command, "SERVICE DELIVER")) {
            s_handle_service (client, &message);
        }
        else {
            log_error ("Unrecognized mlm_client_command () = '%s'", command ? command : "(null)");
        }

        zmsg_destroy (&message);
    } // while (!zsys_interrupted)
    if (nut_changed (data)) {
        r = nut_save (data, state_file.c_str ());
        if (r != 0) {
            log_warning ("Could not save state file '%s'.", state_file.c_str ());
        }
    }
    nut_destroy (&data);
    zpoller_destroy (&poller);
    mlm_client_destroy (&client);
    mlm_client_destroy (&iclient);
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
