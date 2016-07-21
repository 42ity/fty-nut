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

#include "agent_nut_classes.h"
#include "agent_nut_library.h"
#include "malamute.h"
#include "logger.h"

// ugly, declared in actor_commands, TODO: move it to some common
int
handle_asset_message (mlm_client_t *client, nut_t *data, zmsg_t **message_p);

void
sensor_actor (zsock_t *pipe, void *args)
{

    uint64_t polling = 30000;
    bool verbose = false;
    Sensors sensors;
    
    mlm_client_t *client = mlm_client_new ();
    if (!client) {
        log_critical ("mlm_client_new () failed");
        return;
    }

    zpoller_t *poller = zpoller_new (pipe, mlm_client_msgpipe (client), NULL);
    if (!poller) {
        log_critical ("zpoller_new () failed");
        mlm_client_destroy (&client);
        return;
    }
    zsock_signal (pipe, 0);
    log_debug ("sa: sensor actor started");

    nut_t *stateData = nut_new ();
    int rv = nut_load (stateData, "/var/lib/bios/nut/state_file");
    if (rv != 0) {
        log_warning ("Could not load state file '%s'.", "/var/lib/bios/nut/state_file");
    }
    sensors.updateSensorList (stateData);
    while (!zsys_interrupted) {
        void *which = zpoller_wait (poller, polling);
        if (which == NULL) {
            log_debug ("sa: alert update");
            sensors.updateFromNUT ();
            sensors.publish (client, polling*2);
        }
        else if (which == pipe) {
            zmsg_t *msg = zmsg_recv (pipe);
            if (msg) {
                int quit = alert_actor_commands (client, &msg, verbose, polling);
                zmsg_destroy (&msg);
                if (quit) break;
            }
        }
        else if (which == mlm_client_msgpipe (client)) {
            // should be asset message
            zmsg_t *msg = mlm_client_recv (client);
            if (handle_asset_message (client, stateData, &msg)) {
                sensors.updateSensorList (stateData);
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
sensor_actor_test (bool verbose)
{
    printf (" * sensor_actor: ");
    //  @selftest
    static const char* endpoint = "ipc://bios-sensor-actor";

    // malamute broker
    zactor_t *malamute = zactor_new (mlm_server, (void*) "Malamute");
    assert (malamute);
    if (verbose)
        zstr_send (malamute, "VERBOSE");
    zstr_sendx (malamute, "BIND", endpoint, NULL);

    zactor_destroy (&malamute);
    //  @end
    printf (" OK\n");
}
