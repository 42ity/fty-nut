/*  =========================================================================
    alert_actor - actor for handling device alerts

    Copyright (C) 2014 - 2020 Eaton

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
#include "nut_mlm.h"
#include "sensor_list.h"
#include <fty_common_mlm.h>
#include <fty_log.h>

int sensor_actor_commands(
    mlm_client_t* /*client*/, mlm_client_t* /*mb_client*/, zmsg_t** message_p, uint64_t& timeout, Sensors& sensors)
{
    assert(message_p && *message_p);
    zmsg_t* message = *message_p;

    char* cmd = zmsg_popstr(message);
    if (!cmd) {
        log_error(
            "aa: Given `which == pipe` function `zmsg_popstr (msg)` returned NULL. "
            "Message received is most probably empty (has no frames).");
        zmsg_destroy(message_p);
        return 0;
    }

    int ret = 0;
    log_debug("aa: sensor actor command = '%s'", cmd);
    if (streq(cmd, "$TERM")) {
        log_info("Got $TERM");
        ret = 1;
    } else if (streq(cmd, ACTION_POLLING)) {
        char* polling = zmsg_popstr(message);
        if (!polling) {
            log_error(
                "aa: Expected multipart string format: POLLING/value. "
                "Received POLLING/nullptr");
            zstr_free(&cmd);
            zmsg_destroy(message_p);
            return 0;
        }
        char* end;
        timeout = std::strtoul(polling, &end, 10) * 1000;
        if (timeout == 0) {
            log_error("aa: invalid POLLING value '%s', using default instead", polling);
            timeout = 30000;
        }
        zstr_free(&polling);
    } else if (streq(cmd, ACTION_CONFIGURE)) {
        char* mapping = zmsg_popstr(message);
        if (!mapping) {
            log_error(
                "Expected multipart string format: CONFIGURE/mapping_file. "
                "Received CONFIGURE/nullptr");
            zstr_free(&cmd);
            zmsg_destroy(message_p);
            return 0;
        }
        sensors.loadSensorMapping(mapping);
        zstr_free(&mapping);
    } else {
        log_warning("aa: Command '%s' is unknown or not implemented", cmd);
    }

    zstr_free(&cmd);
    zmsg_destroy(message_p);
    return ret;
}

void sensor_actor(zsock_t* pipe, void* args)
{

    uint64_t    polling  = 30000;
    const char* endpoint = static_cast<const char*>(args);
    Sensors     sensors(NutStateManager.getReader());

    MlmClientGuard client(mlm_client_new());
    if (!client) {
        log_fatal("mlm_client_new () failed");
        return;
    }
    if (mlm_client_connect(client, endpoint, 5000, ACTOR_SENSOR_NAME) < 0) {
        log_error("client %s failed to connect", ACTOR_SENSOR_NAME);
        return;
    }
    if (mlm_client_set_producer(client, FTY_PROTO_STREAM_METRICS_SENSOR) < 0) {
        log_error("mlm_client_set_producer (stream = '%s') failed", FTY_PROTO_STREAM_METRICS_SENSOR);
        return;
    }

    ZpollerGuard poller(zpoller_new(pipe, mlm_client_msgpipe(client), NULL));
    if (!poller) {
        log_fatal("zpoller_new () failed");
        return;
    }
    zsock_signal(pipe, 0);
    log_debug("sa: sensor actor started");

    int64_t publishtime = zclock_mono();
    while (!zsys_interrupted) {
        void* which = zpoller_wait(poller, int(polling));
        if (which == NULL || zclock_mono() - publishtime > int64_t(polling)) {
            log_debug("sa: sensor update");
            nut::TcpClient nutClient;
            nutClient.connect("localhost", 3493);
            sensors.updateSensorList(nutClient, client);
            sensors.updateFromNUT(nutClient);
            sensors.advertiseInventory(client);
            // hotfix IPMVAL-2713 (data stale on device which host sensors cause communication failure alarms on
            // sensors) increase ttl from 60 to 240 sec (polling is equal to 30 sec).
            sensors.publish(client, int((polling * 8) / 1000));
            nutClient.disconnect();
            publishtime = zclock_mono();
        } else if (which == pipe) {
            zmsg_t* msg = zmsg_recv(pipe);
            if (msg) {
                int quit = sensor_actor_commands(client, NULL, &msg, polling, sensors);
                zmsg_destroy(&msg);
                if (quit)
                    break;
            }
        } else {
            zmsg_t* msg = zmsg_recv(which);
            zmsg_destroy(&msg);
        }
    }
}
