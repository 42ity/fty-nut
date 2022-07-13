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

#include <fty_common_mlm.h>
#include <fty_log.h>
#include <fty_proto.h>
#include <malamute.h>
#include "nut_mlm.h"
#include "alert_device_list.h"

//returns 1 if $TERM, else 0
int alert_actor_commands(zmsg_t** message_p, uint64_t& timeout)
{
    assert(message_p && *message_p);
    zmsg_t* message = *message_p;

    char* cmd = zmsg_popstr(message);
    log_debug("aa: actor command = '%s'", cmd);

    int ret = 0;

    if (!cmd) {
        log_error(
            "aa: Given `which == pipe` function `zmsg_popstr (msg)` returned NULL. "
            "Message received is most probably empty (has no frames).");
    }
    else if (streq(cmd, "$TERM")) {
        log_info("aa: Got $TERM");
        ret = 1;
    }
    else if (streq(cmd, ACTION_POLLING)) {
        char* polling = zmsg_popstr(message);
        if (!polling) {
            log_error(
                "aa: Expected multipart string format: POLLING/value. "
                "Received POLLING/nullptr");
        }
        else {
            char* end;
            timeout = std::strtoul(polling, &end, 10) * 1000;
            if (timeout == 0) {
                log_error("aa: invalid POLLING value '%s', using default instead", polling);
                timeout = 30000;
            }
        }
        zstr_free(&polling);
    }
    else {
        log_warning("aa: Command '%s' is unknown or not implemented", cmd);
    }

    zstr_free(&cmd);
    zmsg_destroy(message_p);
    return ret;
}

void alert_actor(zsock_t* pipe, void* args)
{
    const char* endpoint = static_cast<const char*>(args);

    MlmClientGuard client(mlm_client_new());
    if (!client) {
        log_fatal("mlm_client_new () failed");
        return;
    }
    if (mlm_client_connect(client, endpoint, 5000, ACTOR_ALERT_NAME) < 0) {
        log_error("client %s failed to connect", ACTOR_ALERT_NAME);
        return;
    }
    if (mlm_client_set_producer(client, FTY_PROTO_STREAM_ALERTS_SYS) < 0) {
        log_error("mlm_client_set_producer (stream = '%s') failed", FTY_PROTO_STREAM_ALERTS_SYS);
        return;
    }

    MlmClientGuard mb_client(mlm_client_new());
    if (!mb_client) {
        log_fatal("mlm_client_new () failed");
        return;
    }
    if (mlm_client_connect(mb_client, endpoint, 5000, ACTOR_ALERT_MB_NAME) < 0) {
        log_error("client %s failed to connect", ACTOR_ALERT_MB_NAME);
        return;
    }

    ZpollerGuard poller(zpoller_new(pipe, mlm_client_msgpipe(client), NULL));
    if (!poller) {
        log_fatal("zpoller_new () failed");
        return;
    }

    zsock_signal(pipe, 0);

    uint64_t last = uint64_t(zclock_mono());
    uint64_t polling  = 30000; //ms

    Devices devices(NutStateManager.getReader());
    devices.setPollingMs(polling);

    log_info("alert actor started");

    while (!zsys_interrupted)
    {
        uint64_t now = uint64_t(zclock_mono());
        if ((now - last) >= polling) {
            last = now;
            log_debug("aa: Polling data now");
            devices.updateDeviceList();
            devices.updateFromNUT();
            devices.publishRules(mb_client);
            devices.publishAlerts(client);
        }

        void* which = zpoller_wait(poller, int(polling));

        if (which == NULL) {
            if (zpoller_terminated(poller) || zsys_interrupted) {
                log_debug("aa: zpoller_terminated () or zsys_interrupted");
                break;
            }
        }
        else if (which == pipe) {
            zmsg_t* msg = zmsg_recv(pipe);
            if (msg) {
                int quit = alert_actor_commands(&msg, polling);
                devices.setPollingMs(polling);
                zmsg_destroy(&msg);
                if (quit)
                    break;
            }
        }
        else if (which == mlm_client_msgpipe(client)) {
            zmsg_t* msg = zmsg_recv(client);
            zmsg_destroy(&msg);
            log_debug("aa: Message not handled (%s/%s)",
                mlm_client_sender(client), mlm_client_subject(client));
        }
    }

    log_info("alert actor ended");
}
