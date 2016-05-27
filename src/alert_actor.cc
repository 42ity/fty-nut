#include "agent_nut_library.h"
#include "alert_actor.h"
#include "alert_device_list.h"
#include "malamute.h"
#include "logger.h"

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
    
    zpoller_t *poller = zpoller_new (pipe, mlm_client_msgpipe (client), NULL);
    if (!poller) {
        log_critical ("zpoller_new () failed");
        mlm_client_destroy (&client);
        return;
    }
    zsock_signal (pipe, 0);
    log_debug ("alert actor started");
    
    while (!zsys_interrupted) {
        void *which = zpoller_wait (poller, polling);
        if (which == NULL) {
            log_debug ("alert update");
            devices.update();
            devices.publishRules (client);
            devices.publishAlerts (client);
        }
        else if (which == pipe) {
            zmsg_t *msg = zmsg_recv (pipe);
            if (msg) {
                int quit = alert_actor_commands (client, &msg, verbose, polling);
                zmsg_destroy (&msg);
                if (quit) break;
            }
        }
        else {
            // we can get some messages, but not interested
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
    static const char* endpoint = "ipc://bios-alert-actor";

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
    mlm_client_set_producer (client, BIOS_PROTO_STREAM_ALERTS_SYS);
    
    mlm_client_t *rfc_evaluator = mlm_client_new ();
    assert (rfc_evaluator);
    mlm_client_connect (rfc_evaluator, endpoint, 1000, "alert-agent");

    mlm_client_t *alert_list = mlm_client_new ();
    assert (alert_list);
    mlm_client_connect (alert_list, endpoint, 1000, "alert-list");
    mlm_client_set_consumer (alert_list, BIOS_PROTO_STREAM_ALERTS_SYS, ".*");

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
        assert (is_bios_proto(msg));
        bios_proto_t *bp = bios_proto_decode (&msg);
        assert (bp);

        verbose_printf ("    is alert\n");
        assert (streq (bios_proto_command (bp), "ALERT"));
        
        verbose_printf ("    is active\n");
        assert (streq (bios_proto_state (bp), "ACTIVE"));

        verbose_printf ("    severity\n");
        assert (streq (bios_proto_severity (bp), "CRITICAL"));

        verbose_printf ("    element\n");
        assert (streq (bios_proto_element_src (bp), "mydevice"));

        bios_proto_destroy (&bp);
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
        assert (is_bios_proto(msg));
        bios_proto_t *bp = bios_proto_decode (&msg);
        assert (bp);
        assert (streq (bios_proto_command (bp), "ALERT"));
        
        verbose_printf ("    is resolved\n");
        assert (streq (bios_proto_state (bp), "RESOLVED"));
        
        bios_proto_destroy (&bp);
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
