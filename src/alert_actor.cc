/*  =========================================================================
    alert_actor - actor for handling device alerts

    Copyright (C) 2014 - 2016 Eaton

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
#include <malamute.h>
#include "fty_nut_classes.h"
#include "fty_nut_library.h"
#include "alert_actor.h"
#include "alert_device_list.h"
#include "logger.h"

/* No consumers for PATH at this time:
static const char* PATH = "/var/lib/fty/fty-nut";
 */
static const char* STATE = "/var/lib/fty/fty-nut/state_file";

int
alert_actor_commands (
    mlm_client_t *client,
    zmsg_t **message_p,
    bool& verbose,
    uint64_t& timeout
)
{
    assert (message_p && *message_p);
    zmsg_t *message = *message_p;

    char *cmd = zmsg_popstr (message);
    if (!cmd) {
        log_error (
                "aa: Given `which == pipe` function `zmsg_popstr (msg)` returned NULL. "
                "Message received is most probably empty (has no frames).");
        zmsg_destroy (message_p);
        return 0;
    }

    int ret = 0;
    log_debug ("aa: actor command = '%s'", cmd);
    if (streq (cmd, "$TERM")) {
        log_info ("Got $TERM");
        ret = 1;
    }
    else
    if (streq (cmd, "VERBOSE")) {
        verbose = true;
    }
    else
    if (streq (cmd, "CONNECT")) {
        char *endpoint = zmsg_popstr (message);
        if (!endpoint) {
            log_error (
                    "aa: Expected multipart string format: CONNECT/endpoint/name. "
                    "Received CONNECT/nullptr");
            zstr_free (&cmd);
            zmsg_destroy (message_p);
            return 0;
        }
        char *name = zmsg_popstr (message);
        if (!name) {
            log_error (
                    "aa: Expected multipart string format: CONNECT/endpoint/name. "
                    "Received CONNECT/endpoint/nullptr");
            zstr_free (&endpoint);
            zstr_free (&cmd);
            zmsg_destroy (message_p);
            return 0;
        }
        int rv = mlm_client_connect (client, endpoint, 1000, name);
        if (rv == -1) {
            log_error (
                    "aa: mlm_client_connect (endpoint = '%s', timeout = '%d', address = '%s') failed",
                    endpoint, 1000, name);
        }
        zstr_free (&endpoint);
        zstr_free (&name);
    }
    else
    if (streq (cmd, "PRODUCER")) {
        char *stream = zmsg_popstr (message);
        if (!stream) {
            log_error (
                    "aa: Expected multipart string format: PRODUCER/stream. "
                    "Received PRODUCER/nullptr");
            zstr_free (&stream);
            zstr_free (&cmd);
            zmsg_destroy (message_p);
            return 0;
        }
        int rv = mlm_client_set_producer (client, stream);
        if (rv == -1) {
            log_error ("mlm_client_set_producer (stream = '%s') failed", stream);
        }
        zstr_free (&stream);
    }
    else
    if (streq (cmd, "CONSUMER")) {
        char *stream = zmsg_popstr (message);
        char *pattern = zmsg_popstr (message);
        if (!stream || !pattern) {
            log_error (
                    "aa: Expected multipart string format: CONSUMER/stream/pattern. "
                    "Received PRODUCER/%s/%s", stream ? stream : "nullptr", pattern ? pattern : "nullptr");
            zstr_free (&stream);
            zstr_free (&pattern);
            zstr_free (&cmd);
            zmsg_destroy (message_p);
            return 0;
        }
        int rv = mlm_client_set_consumer (client, stream, pattern);
        if (rv == -1) {
            log_error ("mlm_client_set_consumer (stream = '%s', pattern = '%s') failed", stream, pattern);
        }
        zstr_free (&stream);
        zstr_free (&pattern);
    }
    else
    if (streq (cmd, "POLLING")) {
        char *polling = zmsg_popstr (message);
        if (!polling) {
            log_error (
                "aa: Expected multipart string format: POLLING/value. "
                "Received POLLING/nullptr");
            zstr_free (&cmd);
            zmsg_destroy (message_p);
            return 0;
        }
        timeout = atoi(polling) * 1000;
        if (timeout == 0) {
            log_error ("aa: invalid POLLING value '%s', using default instead", polling);
            timeout = 30000;
        }
        zstr_free (&polling);
    }
    else {
        log_warning ("aa: Command '%s' is unknown or not implemented", cmd);
    }

    zstr_free (&cmd);
    zmsg_destroy (message_p);
    return ret;
}

int
handle_asset_message (mlm_client_t *client, nut_t *data, zmsg_t **message_p) {
    if (!client || !data || !message_p || !*message_p) return 0;
    if (!is_fty_proto (*message_p)) {
        log_warning (
            "Message received is not fty_proto; sender = '%s', subject = '%s'",
            mlm_client_sender (client), mlm_client_subject (client));
        zmsg_destroy (message_p);
        return 0;
    }
    fty_proto_t *proto = fty_proto_decode (message_p);
    if (!proto) {
        log_critical ("fty_proto_decode () failed.");
        zmsg_destroy (message_p);
        return 0;
    }
    nut_put (data, &proto);
    return 1;
}

void
alert_actor (zsock_t *pipe, void *args)
{

    uint64_t polling = 30000;
    bool verbose = false;

    mlm_client_t *client = mlm_client_new ();
    if (!client) {
        log_critical ("mlm_client_new () failed");
        return;
    }
    Devices devices;
    devices.setPollingMs (polling);

    zpoller_t *poller = zpoller_new (pipe, mlm_client_msgpipe (client), NULL);
    if (!poller) {
        log_critical ("zpoller_new () failed");
        mlm_client_destroy (&client);
        return;
    }
    zsock_signal (pipe, 0);
    log_debug ("alert actor started");

    nut_t *stateData = nut_new ();
    int rv = nut_load (stateData, STATE);
    if (rv != 0) {
        log_warning ("Could not load state file '%s'.", STATE);
    }
    devices.updateDeviceList (stateData);
    while (!zsys_interrupted) {
        void *which = zpoller_wait (poller, polling);
        if (which == NULL) {
            log_debug ("aa: alert update");
            devices.updateFromNUT ();
            devices.publishRules (client);
            devices.publishAlerts (client);
        }
        else if (which == pipe) {
            zmsg_t *msg = zmsg_recv (pipe);
            if (msg) {
                int quit = alert_actor_commands (client, &msg, verbose, polling);
                devices.setPollingMs (polling);
                zmsg_destroy (&msg);
                if (quit) break;
            }
        }
        else if (which == mlm_client_msgpipe (client)) {
            // should be asset message
            zmsg_t *msg = mlm_client_recv (client);
            if (handle_asset_message (client, stateData, &msg)) {
                devices.updateDeviceList (stateData);
            }
            zmsg_destroy (&msg);
        }
        else {
            zmsg_t *msg = zmsg_recv (which);
            zmsg_destroy (&msg);
        }
    }
    zpoller_destroy (&poller);
    mlm_client_destroy (&client);
}

//  --------------------------------------------------------------------------
//  Self test of this class

#define verbose_printf if (verbose) printf

void
alert_actor_test (bool verbose)
{
    printf (" * alert_actor: ");
    //  @selftest
    static const char* endpoint = "ipc://fty-alert-actor";

    // malamute broker
    zactor_t *malamute = zactor_new (mlm_server, (void*) "Malamute");
    assert (malamute);
    if (verbose)
        zstr_send (malamute, "VERBOSE");
    zstr_sendx (malamute, "BIND", endpoint, NULL);

    Device dev("mydevice");
    std::map<std::string,std::vector<std::string> > alerts = {
        { "ambient.temperature.status", {"critical-high", "", ""} },
        { "ambient.temperature.high.warning", {"80", "", ""} },
        { "ambient.temperature.high.critical", {"100", "", ""} },
        { "ambient.temperature.low.warning", {"10", "", ""} },
        { "ambient.temperature.low.critical", {"5", "", ""} },
    };
    dev.addAlert("ambient.temperature", alerts);
    dev._alerts["ambient.temperature"].status = "critical-high";
    Devices devs;
    devs._devices["mydevice"] = dev;

    mlm_client_t *client = mlm_client_new ();
    assert (client);
    mlm_client_connect (client, endpoint, 1000, "agent-nut-alert");
    mlm_client_set_producer (client, FTY_PROTO_STREAM_ALERTS_SYS);

    mlm_client_t *rfc_evaluator = mlm_client_new ();
    assert (rfc_evaluator);
    mlm_client_connect (rfc_evaluator, endpoint, 1000, "fty-alert-engine");

    mlm_client_t *alert_list = mlm_client_new ();
    assert (alert_list);
    mlm_client_connect (alert_list, endpoint, 1000, "alert-list");
    mlm_client_set_consumer (alert_list, FTY_PROTO_STREAM_ALERTS_SYS, ".*");

    zpoller_t *poller = zpoller_new (
        mlm_client_msgpipe (client),
        mlm_client_msgpipe (rfc_evaluator),
        mlm_client_msgpipe (alert_list),
        NULL);
    assert (poller);

    devs.publishRules (client);
    devs.publishAlerts (client);

    // check rule message
    {
        verbose_printf ("\n    recvrule\n");
        void *which = zpoller_wait (poller, 1000);
        assert (which);
        zmsg_t *msg = mlm_client_recv (rfc_evaluator);
        assert (msg);
        assert (streq (mlm_client_subject (rfc_evaluator), "rfc-evaluator-rules"));

        verbose_printf ("    rule command\n");
        char *item = zmsg_popstr (msg);
        assert (item);
        assert (streq (item, "ADD"));
        zstr_free (&item);

        verbose_printf ("    rule json\n");
        item = zmsg_popstr (msg);
        assert (item);
        assert (item[0] == '{');
        zstr_free (&item);

        zmsg_destroy (&msg);
    }
    // check alert message
    {
        verbose_printf ("    receive alert\n");
        void *which = zpoller_wait (poller, 1000);
        assert (which);
        zmsg_t *msg = mlm_client_recv (alert_list);
        assert (msg);
        assert (is_fty_proto(msg));
        fty_proto_t *bp = fty_proto_decode (&msg);
        assert (bp);

        verbose_printf ("    is alert\n");
        assert (streq (fty_proto_command (bp), "ALERT"));

        verbose_printf ("    is active\n");
        assert (streq (fty_proto_state (bp), "ACTIVE"));

        verbose_printf ("    severity\n");
        assert (streq (fty_proto_severity (bp), "CRITICAL"));

        verbose_printf ("    element\n");
        assert (streq (fty_proto_name (bp), "mydevice"));

        fty_proto_destroy (&bp);
        zmsg_destroy (&msg);
    }
    devs._devices["mydevice"]._alerts["ambient.temperature"].status = "good";
    devs.publishAlerts (client);
    // check alert message
    {
        verbose_printf ("    receive resolved\n");
        void *which = zpoller_wait (poller, 1000);
        assert (which);
        zmsg_t *msg = mlm_client_recv (alert_list);
        assert (msg);
        assert (is_fty_proto(msg));
        fty_proto_t *bp = fty_proto_decode (&msg);
        assert (bp);
        assert (streq (fty_proto_command (bp), "ALERT"));

        verbose_printf ("    is resolved\n");
        assert (streq (fty_proto_state (bp), "RESOLVED"));

        fty_proto_destroy (&bp);
        zmsg_destroy (&msg);
    }

    zpoller_destroy (&poller);
    mlm_client_destroy(&client);
    mlm_client_destroy(&alert_list);
    mlm_client_destroy(&rfc_evaluator);
    zactor_destroy (&malamute);
    //  @end
    printf (" OK\n");
}
