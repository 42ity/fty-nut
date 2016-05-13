#include "agent_nut_library.h"
#include "alert_actor.h"
#include "alert_device_list.h"
#include "malamute.h"
#include "logger.h"

void
alert_actor (zsock_t *pipe, void *args)
{

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
        void *which = zpoller_wait (poller, 30000);
        if (which == NULL) {
            log_debug ("alert update");
            devices.update();
        }
        else if (which == pipe) {
            zmsg_t *msg = zmsg_recv (pipe);
            if (msg) {
                char *cmd = zmsg_popstr (msg);
                log_debug ("aa: got message %s", cmd ? cmd : "NULL");
                if (streq (cmd, "$TERM")) {
                    zstr_free (&cmd);
                    zmsg_destroy (&msg);
                    break;
                }
                zstr_free (&cmd);
            }
            zmsg_destroy (&msg);
        }
    }
    zpoller_destroy (&poller);
    mlm_client_destroy (&client);
}

//  --------------------------------------------------------------------------
//  Self test of this class

void
alert_actor_test (bool verbose)
{
    printf (" * alert_actor: ");

    //  @selftest
    //  @end
    printf ("Empty test - OK\n");
}
