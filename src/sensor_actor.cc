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

#include "sensor_actor.h"
#include "alert_actor.h"
#include "sensor_list.h"
#include "nut_mlm.h"
#include <fty_log.h>

#include <malamute.h>

void
sensor_actor (zsock_t *pipe, void *args)
{

    uint64_t polling = 30000;
    const char *endpoint = static_cast<const char *>(args);
    Sensors sensors(NutStateManager.getReader());

    MlmClientGuard client(mlm_client_new());
    if (!client) {
        log_fatal ("mlm_client_new () failed");
        return;
    }
    if (mlm_client_connect(client, endpoint, 5000, ACTOR_SENSOR_NAME) < 0) {
        log_error("client %s failed to connect", ACTOR_SENSOR_NAME);
        return;
    }
    if (mlm_client_set_producer(client, FTY_PROTO_STREAM_METRICS_SENSOR) < 0) {
        log_error("mlm_client_set_producer (stream = '%s') failed",
                FTY_PROTO_STREAM_METRICS_SENSOR);
        return;
    }

    ZpollerGuard poller(zpoller_new(pipe, mlm_client_msgpipe(client), NULL));
    if (!poller) {
        log_fatal ("zpoller_new () failed");
        return;
    }
    zsock_signal (pipe, 0);
    log_debug ("sa: sensor actor started");

    int64_t publishtime = zclock_mono();
    while (!zsys_interrupted) {
        void *which = zpoller_wait (poller, polling);
        if (which == NULL || zclock_mono() - publishtime > (int64_t)polling) {
            log_debug ("sa: sensor update");
            sensors.updateSensorList ();
            sensors.updateFromNUT ();
            sensors.publish (client, polling*2/1000);
            publishtime = zclock_mono();
        }
        else if (which == pipe) {
            zmsg_t *msg = zmsg_recv (pipe);
            if (msg) {
                int quit = alert_actor_commands (client, NULL, &msg, polling);
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
//  Self test of this class

void
sensor_actor_test (bool verbose)
{
    printf (" * sensor_actor: ");
    //  @selftest
    static const char* endpoint = "ipc://fty-sensor-actor";

    // malamute broker
    zactor_t *malamute = zactor_new (mlm_server, (void*) "Malamute");
    assert (malamute);
    zstr_sendx (malamute, "BIND", endpoint, NULL);

    mlm_client_t *consumer = mlm_client_new ();
    assert (consumer);
    mlm_client_connect (consumer, endpoint, 1000, "sensor-client");
    mlm_client_set_consumer (consumer, FTY_PROTO_STREAM_METRICS_SENSOR, ".*");

    mlm_client_t *producer = mlm_client_new ();
    assert (producer);
    mlm_client_connect (producer, endpoint, 1000, "sensor-producer");
    mlm_client_set_producer (producer, FTY_PROTO_STREAM_METRICS_SENSOR);

    StateManager manager;
    Sensors sensors(manager.getReader());
    std::map <std::string, std::string> children;
    fty_proto_t *proto = fty_proto_new(FTY_PROTO_ASSET);
    assert(proto);
    fty_proto_set_name(proto, "sensor-1");
    fty_proto_set_operation(proto, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert(proto, "type", "device");
    fty_proto_aux_insert(proto, "subtype", "sensor");
    fty_proto_aux_insert(proto, "parent_name.1", "PRG");
    fty_proto_ext_insert(proto, "port", "1");
    AssetState::Asset asset1(proto);
    fty_proto_destroy(&proto);
    sensors._sensors["sensor1"] = Sensor (&asset1, nullptr, children, "nut");
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

    // gpio on EMP001
    std::vector <std::string> contacts;
    children.emplace ("1", "sensorgpio-1");
    children.emplace ("2", "sensorgpio-2");
    contacts.push_back ("open");
    contacts.push_back ("close");

    proto = fty_proto_new(FTY_PROTO_ASSET);
    assert(proto);
    fty_proto_set_name(proto, "sensor-2");
    fty_proto_set_operation(proto, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert(proto, "type", "device");
    fty_proto_aux_insert(proto, "subtype", "sensor");
    fty_proto_aux_insert(proto, "parent_name.1", "PRG");
    fty_proto_ext_insert(proto, "port", "4");
    AssetState::Asset asset2(proto);
    fty_proto_destroy(&proto);
    sensors._sensors["sensor1"] = Sensor (&asset2, nullptr, children, "nut");
    sensors._sensors["sensor1"]._contacts = contacts;

    sensors.publish (producer, 300);

    msg = mlm_client_recv (consumer);
    assert (msg);
    bmsg = fty_proto_decode (&msg);
    assert (bmsg);
    fty_proto_print (bmsg);
    assert (streq (fty_proto_type (bmsg), "status.GPI1.4"));
    fty_proto_destroy (&bmsg);

    msg = mlm_client_recv (consumer);
    assert (msg);
    bmsg = fty_proto_decode (&msg);
    assert (bmsg);
    fty_proto_print (bmsg);
    assert (streq (fty_proto_type (bmsg), "status.GPI2.4"));
    fty_proto_destroy (&bmsg);

    mlm_client_destroy (&producer);
    mlm_client_destroy (&consumer);
    zactor_destroy (&malamute);
    //  @end
    printf (" OK\n");
}
