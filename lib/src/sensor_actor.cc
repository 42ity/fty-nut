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

//returns 1 if $TERM, else 0
static int sensor_actor_commands(zmsg_t** message_p, uint64_t& timeout_ms, Sensors& sensors)
{
    assert(message_p && *message_p);
    zmsg_t* message = *message_p;

    char* cmd = zmsg_popstr(message);
    log_debug("sa: sensor actor command = '%s'", cmd);

    int ret = 0;

    if (!cmd) {
        log_error(
            "sa: Given `which == pipe` function `zmsg_popstr (msg)` returned NULL. "
            "Message received is most probably empty (has no frames).");
    }
    else if (streq(cmd, "$TERM")) {
        log_info("sa: Got $TERM");
        ret = 1;
    }
    else if (streq(cmd, ACTION_POLLING)) {
        char* polling = zmsg_popstr(message);
        if (!polling) {
            log_error(
                "sa: Expected multipart string format: POLLING/value. "
                "Received POLLING/nullptr");
        }
        else {
            char* end;
            timeout_ms = std::strtoul(polling, &end, 10) * 1000;
            if (timeout_ms == 0) {
                log_error("sa: invalid POLLING value '%s', using default instead", polling);
                timeout_ms = 30000;
            }
        }
        log_debug("sa: timeout: %zu ms", timeout_ms);
        zstr_free(&polling);
    }
    else if (streq(cmd, ACTION_CONFIGURE)) {
        char* mapping = zmsg_popstr(message);
        if (!mapping) {
            log_error(
                "sa: Expected multipart string format: CONFIGURE/mapping_file. "
                "Received CONFIGURE/nullptr");
        }
        else {
            sensors.loadSensorMapping(mapping);
        }
        zstr_free(&mapping);
    }
    else {
        log_warning("sa: Command '%s' is unknown or not implemented", cmd);
    }

    zstr_free(&cmd);
    zmsg_destroy(message_p);
    return ret;
}

void sensor_actor(zsock_t* pipe, void* args)
{
    const char* endpoint = static_cast<const char*>(args);

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

    log_info("sensor actor started");

    uint64_t last = uint64_t(zclock_mono());
    uint64_t timeout = 30000; //ms

    Sensors sensors(NutStateManager.getReader());

    while (!zsys_interrupted)
    {
        uint64_t now = uint64_t(zclock_mono());
        if ((now - last) >= timeout) {
            log_debug("sa: sensor update");
            try {
                nut::TcpClient nutClient;
                nutClient.connect("localhost", 3493);

                sensors.updateSensorList(nutClient, client);
                sensors.updateFromNUT(nutClient);
                sensors.advertiseInventory(client);
                // hotfix IPMVAL-2713 (data stale on device which host sensors cause communication failure alarms on
                // sensors) increase ttl from 60 to 240 sec (polling period is equal to 30 sec).
                sensors.publish(client, int((timeout * 8) / 1000));

                nutClient.disconnect();
            }
            catch (...) {
            }

            last = uint64_t(zclock_mono());
            log_debug("sa: sensor update lap time: %zu ms", (last - now));
        }

        void* which = zpoller_wait(poller, int(timeout));

        if (which == NULL) {
            if (zpoller_terminated(poller) || zsys_interrupted) {
                log_debug("sa: zpoller_terminated () or zsys_interrupted");
                break;
            }
        }
        else if (which == pipe) {
            zmsg_t* msg = zmsg_recv(pipe);
            if (msg) {
                int quit = sensor_actor_commands(&msg, timeout, sensors);
                zmsg_destroy(&msg);
                if (quit) {
                    break; //$TERM
                }
            }
        }
        else if (which == mlm_client_msgpipe(client)) {
            zmsg_t* msg = mlm_client_recv(client);
            zmsg_destroy(&msg);
            log_debug("sa: Message not handled (%s/%s)",
                mlm_client_sender(client), mlm_client_subject(client));
        }
    }

    log_info("sensor actor ended");
}
