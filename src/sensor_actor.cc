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

#include "fty_nut_classes.h"
#include "fty_nut_library.h"
#include "malamute.h"
#include "logger.h"

/* No consumers for PATH at this time:
static const char* PATH = "/var/lib/fty/fty-nut";
 */
static const char* STATE = "/var/lib/fty/fty-nut/state_file";

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
    int rv = nut_load (stateData, STATE);
    if (rv != 0) {
        log_warning ("Could not load state file '%s'.", STATE);
    }
    sensors.updateSensorList (stateData);
    int64_t publishtime = zclock_mono();
    while (!zsys_interrupted) {
        void *which = zpoller_wait (poller, polling);
        if (which == NULL || zclock_mono() - publishtime > (int64_t)polling) {
            log_debug ("sa: sensor update");
            sensors.updateFromNUT ();
            sensors.publish (client, polling*2/1000);
            publishtime = zclock_mono();
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
    static const char* endpoint = "ipc://fty-sensor-actor";

    // malamute broker
    zactor_t *malamute = zactor_new (mlm_server, (void*) "Malamute");
    assert (malamute);
    if (verbose)
        zstr_send (malamute, "VERBOSE");
    zstr_sendx (malamute, "BIND", endpoint, NULL);

    mlm_client_t *consumer = mlm_client_new ();
    assert (consumer);
    mlm_client_connect (consumer, endpoint, 1000, "sensor-client");
    mlm_client_set_consumer (consumer, FTY_PROTO_STREAM_METRICS_SENSOR, ".*");

    mlm_client_t *producer = mlm_client_new ();
    assert (producer);
    mlm_client_connect (producer, endpoint, 1000, "sensor-producer");
    mlm_client_set_producer (producer, FTY_PROTO_STREAM_METRICS_SENSOR);

    Sensors sensors;
    sensors._sensors["sensor1"] = Sensor ("nut", 0, "PRG", "1");
    sensors._sensors["sensor1"]._humidity = "50";

    sensors.publish (producer, 300);

    zmsg_t *msg = mlm_client_recv (consumer);
    assert (msg);
    fty_proto_t *bmsg = fty_proto_decode (&msg);
    assert (bmsg);
    assert (streq (fty_proto_value (bmsg), "50"));
    assert (streq (fty_proto_type (bmsg), "humidity.1"));
    assert (fty_proto_ttl (bmsg) == 300);
    fty_proto_destroy (&bmsg);

    sensors._sensors["sensor1"]._temperature = "28";
    sensors._sensors["sensor1"]._humidity = "51";

    sensors.publish (producer, 300);

    msg = mlm_client_recv (consumer);
    assert (msg);
    bmsg = fty_proto_decode (&msg);
    assert (bmsg);
    fty_proto_print (bmsg);
    assert (streq (fty_proto_value (bmsg), "28"));
    assert (streq (fty_proto_type (bmsg), "temperature.1"));
    fty_proto_destroy (&bmsg);

    msg = mlm_client_recv (consumer);
    assert (msg);
    bmsg = fty_proto_decode (&msg);
    assert (bmsg);
    fty_proto_print (bmsg);
    assert (streq (fty_proto_value (bmsg), "51"));
    assert (streq (fty_proto_type (bmsg), "humidity.1"));
    fty_proto_destroy (&bmsg);

    mlm_client_destroy (&producer);
    mlm_client_destroy (&consumer);
    zactor_destroy (&malamute);
    //  @end
    printf (" OK\n");
}
