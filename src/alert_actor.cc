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
#include "alert_device_list.h"
#include "alert_actor.h"
#include "asset_state.h"
#include "nut_mlm.h"
#include <fty_log.h>

#include <ftyproto.h>
#include <malamute.h>

int
alert_actor_commands (
    mlm_client_t *client,
    mlm_client_t *mb_client,
    zmsg_t **message_p,
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
    if (streq (cmd, ACTION_POLLING)) {
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

void
alert_actor (zsock_t *pipe, void *args)
{

    uint64_t polling = 30000;
    const char *endpoint = static_cast<const char *>(args);

    MlmClientGuard client(mlm_client_new());
    if (!client) {
        log_fatal ("mlm_client_new () failed");
        return;
    }
    if (mlm_client_connect(client, endpoint, 5000, ACTOR_ALERT_NAME) < 0) {
        log_error("client %s failed to connect", ACTOR_ALERT_NAME);
        return;
    }
    if (mlm_client_set_producer(client, FTY_PROTO_STREAM_ALERTS_SYS) < 0) {
        log_error("mlm_client_set_producer (stream = '%s') failed",
                FTY_PROTO_STREAM_ALERTS_SYS);
        return;
    }

    MlmClientGuard mb_client(mlm_client_new());
    if (!mb_client) {
       log_fatal ("mlm_client_new () failed");
       return;
    }
    if (mlm_client_connect(mb_client, endpoint, 5000, ACTOR_ALERT_MB_NAME) < 0) {
        log_error("client %s failed to connect", ACTOR_ALERT_MB_NAME);
        return;
    }

    Devices devices(NutStateManager.getReader());
    devices.setPollingMs (polling);

    ZpollerGuard poller(zpoller_new(pipe, mlm_client_msgpipe(client), NULL));
    if (!poller) {
        log_fatal ("zpoller_new () failed");
        return;
    }
    zsock_signal (pipe, 0);
    log_debug ("alert actor started");

    uint64_t last = zclock_mono ();
    while (!zsys_interrupted) {
        void *which = zpoller_wait (poller, polling);
        uint64_t now = zclock_mono ();
        if (now - last >= polling) {
            last = now;
            zsys_debug ("Polling data now");
            devices.updateDeviceList ();
            devices.updateFromNUT ();
            devices.publishRules (mb_client);
            devices.publishAlerts (client);
        }
        if (which == NULL) {
            log_debug ("aa: alert update");
        }
        else if (which == pipe) {
            zmsg_t *msg = zmsg_recv (pipe);
            if (msg) {
                int quit = alert_actor_commands (client, mb_client, &msg, polling);
                devices.setPollingMs (polling);
                zmsg_destroy (&msg);
                if (quit) break;
            }
        }
        else {
            zmsg_t *msg = zmsg_recv (which);
            zmsg_destroy (&msg);
        }
    }
}

//  --------------------------------------------------------------------------
//  Self test of this
void
alert_actor_test (bool verbose)
{
    printf (" * alert_actor: ");
    //  @selftest
    static const char* endpoint = "ipc://fty-alert-actor";

    // malamute broker
    zactor_t *malamute = zactor_new (mlm_server, (void*) "Malamute");
    assert (malamute);
    zstr_sendx (malamute, "BIND", endpoint, NULL);

    fty_proto_t *msg = fty_proto_new(FTY_PROTO_ASSET);
    assert(msg);
    fty_proto_set_name(msg, "mydevice");
    fty_proto_set_operation(msg, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert(msg, "type", "device");
    fty_proto_aux_insert(msg, "subtype", "ups");
    fty_proto_ext_insert(msg, "ip.1", "192.0.2.1");
    std::shared_ptr<AssetState::Asset> asset(new AssetState::Asset(msg));
    fty_proto_destroy(&msg);

    Device dev(asset);
    std::map<std::string,std::vector<std::string> > alerts = {
        { "ambient.temperature.status", {"critical-high", "", ""} },
        { "ambient.temperature.high.warning", {"80", "", ""} },
        { "ambient.temperature.high.critical", {"100", "", ""} },
        { "ambient.temperature.low.warning", {"10", "", ""} },
        { "ambient.temperature.low.critical", {"5", "", ""} },
    };
    dev.addAlert("ambient.temperature", alerts);
    dev._alerts["ambient.temperature"].status = "critical-high";
    StateManager manager;
    Devices devs(manager.getReader());
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

    mlm_client_sendtox (rfc_evaluator, "agent-nut-alert", "rfc-evaluator-rules", "OK", NULL);
    devs.publishRules (client);

    // check rule message
    {
        printf ("\n    recvrule\n");
        void *which = zpoller_wait (poller, 1000);
        assert (which);
        zmsg_t *msg = mlm_client_recv (rfc_evaluator);
        assert (msg);
        assert (streq (mlm_client_subject (rfc_evaluator), "rfc-evaluator-rules"));

        printf ("    rule command\n");
        char *item = zmsg_popstr (msg);
        assert (item);
        assert (streq (item, "ADD"));
        zstr_free (&item);

        printf ("    rule json\n");
        item = zmsg_popstr (msg);
        assert (item);
        assert (item[0] == '{');
        zstr_free (&item);

        zmsg_destroy (&msg);
    }
    // check alert message
    devs.publishAlerts (client);
    {
        printf ("    receive alert\n");
        void *which = zpoller_wait (poller, 1000);
        assert (which);
        zmsg_t *msg = mlm_client_recv (alert_list);
        assert (msg);
        assert (is_fty_proto(msg));
        fty_proto_t *bp = fty_proto_decode (&msg);
        assert (bp);

        printf ("    is alert\n");
        assert (streq (fty_proto_command (bp), "ALERT"));

        printf ("    is active\n");
        assert (streq (fty_proto_state (bp), "ACTIVE"));

        printf ("    severity\n");
        assert (streq (fty_proto_severity (bp), "CRITICAL"));

        printf ("    element\n");
        assert (streq (fty_proto_name (bp), "mydevice"));

        fty_proto_destroy (&bp);
        zmsg_destroy (&msg);
    }
    devs._devices["mydevice"]._alerts["ambient.temperature"].status = "good";
    devs.publishAlerts (client);
    // check alert message
    {
        printf ("    receive resolved\n");
        void *which = zpoller_wait (poller, 1000);
        assert (which);
        zmsg_t *msg = mlm_client_recv (alert_list);
        assert (msg);
        assert (is_fty_proto(msg));
        fty_proto_t *bp = fty_proto_decode (&msg);
        assert (bp);
        assert (streq (fty_proto_command (bp), "ALERT"));

        printf ("    is resolved\n");
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
