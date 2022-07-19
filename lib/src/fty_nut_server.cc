/*  =========================================================================
    fty_nut_server - fty nut actor

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

#include "fty_nut_server.h"
#include "actor_commands.h"
#include "nut_agent.h"
#include "nut_mlm.h"
#include "state_manager.h"
#include <fty_common_agents.h>
#include <fty_common_mlm.h>
#include <fty_log.h>

StateManager NutStateManager;

static bool get_initial_licensing(StateManager::Writer& state_writer, mlm_client_t* client)
{
    log_debug("Get initial licensing");

    const int recvTimeout = 5000; //ms
    zmsg_t* reply = NULL;

    bool ret = false;

    do { // for break facilities
        if (!client)
            { log_error("client is NULL"); break; }

        ZpollerGuard poller(zpoller_new(mlm_client_msgpipe(client), NULL));
        if (!poller)
            { log_error("Poller creation failed"); break; }

        ZuuidGuard uuid(zuuid_new());
        if (!uuid)
            { log_error("Creating UUID failed"); break; }

        // send request
        int r = mlm_client_sendtox(client, "etn-licensing", "LIMITATIONS", "LIMITATION_QUERY",
            zuuid_str_canonical(uuid), "*", "*", NULL);
        if (r < 0)
            { log_error("Sending LIMITATION_QUERY message to etn-licensing failed"); break; }

        // recv response
        if (!zpoller_wait(poller, recvTimeout))
            { log_error("Getting response to LIMITATION_QUERY timed out (%d ms)", recvTimeout); break; }
        reply = mlm_client_recv(client);
        if (!reply)
            { log_error("Getting empty response to LIMITATION_QUERY"); break; }

        ZstrGuard str(zmsg_popstr(reply));
        if (!str || !streq(str, zuuid_str_canonical(uuid)))
            { log_error("Mismatching response to a LIMITATION_QUERY request"); break; }

        str = zmsg_popstr(reply);
        if (!str || !streq(str, "REPLY"))
            { log_error("Got malformed message from etn-licensing"); break; }

        // The rest is a series of value/item/category triplets that
        // updateFromMsg() can grok
        ret = state_writer.getState().updateFromMsg(&reply);
        break;
    } while(0);

    zmsg_destroy(&reply);
    return ret;
}

// Query fty-asset about existing devices. This has to be done after
// subscribing ourselves to the ASSETS stream, to make sure that we do not
// miss assets created between the mailbox request and the subscription to
// the stream.
void get_initial_assets(StateManager::Writer& state_writer, mlm_client_t* client, bool query_licensing)
{
    log_debug("Get initial assets");

    if (!client)
        { log_error("client is NULL"); return; }

    const int sendTimeout = 5000; //ms
    const int recvTimeout = 5000; //ms

    ZpollerGuard poller(zpoller_new(mlm_client_msgpipe(client), NULL));
    if (!poller) {
        log_error("Poller creation failed");
        return;
    }

    log_debug("send request ASSETS");
    ZmsgGuard reply;
    {
        ZuuidGuard uuid(zuuid_new());
        if (!uuid) {
            log_error("Creating UUID for the ASSETS message failed");
            return;
        }

        // send request
        zmsg_t* msg = zmsg_new();
        if (!msg) {
            log_error("Creating ASSETS message failed");
            return;
        }
        zmsg_addstr(msg, "GET");
        zmsg_addstr(msg, zuuid_str_canonical(uuid));
        zmsg_addstr(msg, "ups");
        zmsg_addstr(msg, "epdu");
        zmsg_addstr(msg, "sts");
        zmsg_addstr(msg, "sensor");
        zmsg_addstr(msg, "sensorgpio");
        int r = mlm_client_sendto(client, AGENT_FTY_ASSET, "ASSETS", NULL, sendTimeout, &msg);
        zmsg_destroy(&msg);
        if (r < 0) {
            log_error("Sending ASSETS message failed");
            return;
        }

        if (zsys_interrupted) {
            return; //$TERM
        }

        // recv response
        if (!zpoller_wait(poller, recvTimeout)) {
            log_error("Getting response from ASSETS timed out (%d ms)", recvTimeout);
            return;
        }
        reply = mlm_client_recv(client);
        if (!reply) {
            log_error("Empty response received from ASSETS message");
            return;
        }

        ZstrGuard uuid_reply(zmsg_popstr(reply));
        if (!streq(uuid_reply, zuuid_str_canonical(uuid))) {
            log_error("Mismatching response to an ASSETS request");
            return;
        }
        ZstrGuard status(zmsg_popstr(reply));
        if (!streq(status, "OK")) {
            log_error("Got %s response to an ASSETS request", status.get());
            zmsg_print(reply);
            return;
        }
    }

    log_debug("send %zu ASSET_DETAIL requests", zmsg_size(reply.get()));
    std::set<std::string> uuids; // Remember which UUIDs we sent
    {
        ZstrGuard asset(zmsg_popstr(reply));
        while (asset) {
            if (zsys_interrupted) {
                return; //$TERM
            }

            ZuuidGuard uuid(zuuid_new());
            if (!uuid) {
                log_error("Creating UUID for the ASSET_DETAIL message failed");
                return;
            }
            const char* uuid_str = zuuid_str_canonical(uuid);

            //log_debug("send request asset detail '%s'", asset.get());
            zmsg_t* msg = zmsg_new();
            if (!msg) {
                log_error("Creating ASSET_DETAIL message failed");
                return;
            }
            zmsg_addstr(msg, "GET");
            zmsg_addstr(msg, uuid_str);
            zmsg_addstr(msg, asset);
            int r = mlm_client_sendto(client, AGENT_FTY_ASSET, "ASSET_DETAIL", NULL, sendTimeout, &msg);
            zmsg_destroy(&msg);
            if (r < 0) {
                log_warning("Sending ASSET_DETAIL message for %s failed", asset.get());
            }
            else {
                uuids.emplace(uuid_str); // ok, remember uuid
            }

            asset = zmsg_popstr(reply); // next
        }
        //log_debug("%zu ASSET_DETAIL requests sent", uuids.size());
    }

    log_debug("recv %zu ASSET_DETAIL responses", uuids.size());
    bool changed = false;
    {
        size_t noResponseCnt = 0;
        while (uuids.size() > noResponseCnt) {
            if (zsys_interrupted) {
                return; //$TERM
            }

            // recv a response
            zmsg_t* msg = NULL;
            if (!zpoller_wait(poller, recvTimeout)) {
                log_warning("Getting ASSET_DETAIL response timed out (%d ms)", recvTimeout);
            }
            else {
                msg = mlm_client_recv(client);
            }
            if (!msg) {
                noResponseCnt++;
                continue;
            }

            char* uuid = zmsg_popstr(msg);

            if (!uuid || uuids.erase(uuid) == 0) {
                log_warning("Mismatching response to an ASSET_DETAIL request");
            }
            else if (!fty_proto_is(msg)) {
                log_warning("Response to an ASSET_DETAIL message is not fty_proto");
            }
            else if (state_writer.getState().updateFromMsg(&msg)) {
                changed = true;
            }

            zstr_free(&uuid);
            zmsg_destroy(&msg);
        }

        if (uuids.size() != 0) {
            log_warning("Missed %zu ASSET_DETAIL responses", uuids.size());
        }
    }

    if (query_licensing) {
        if (get_initial_licensing(state_writer, client)) {
            changed = true;
        }
    }

    if (changed) {
        state_writer.commit();
    }

    log_info("Initial ASSETS request complete (%zu/%zu powerdevices, %zu/%zu sensors)",
        state_writer.getState().getPowerDevices().size(),
        state_writer.getState().getAllPowerDevices().size(),
        state_writer.getState().getSensors().size(),
        state_writer.getState().getAllSensors().size());
}

void fty_nut_server(zsock_t* pipe, void* args)
{
    const char* endpoint = static_cast<const char*>(args);

    MlmClientGuard client(mlm_client_new());
    if (!client) {
        log_fatal("mlm_client_new () failed");
        return;
    }
    if (mlm_client_connect(client, endpoint, 5000, ACTOR_NUT_NAME) < 0) {
        log_error("client %s failed to connect", ACTOR_NUT_NAME);
        return;
    }
    if (mlm_client_set_consumer(client, FTY_PROTO_STREAM_ASSETS, ".*") < 0) {
        log_error("mlm_client_set_consumer (stream = '%s', pattern = '.*') failed", FTY_PROTO_STREAM_ASSETS);
        return;
    }
    if (mlm_client_set_consumer(client, "LICENSING-ANNOUNCEMENTS", ".*") < 0) {
        log_error("mlm_client_set_consumer (stream = '%s', pattern = '.*') failed", "LICENSING-ANNOUNCEMENTS");
        return;
    }

    // inventory client
    MlmClientGuard iclient(mlm_client_new());
    if (!iclient) {
        log_fatal("mlm_client_new () failed");
        return;
    }
    int r = mlm_client_connect(iclient, endpoint, 5000, "bios-agent-nut-inventory");
    if (r == -1) {
        log_error("connect of iclient failed");
        return;
    }
    r = mlm_client_set_producer(iclient, FTY_PROTO_STREAM_ASSETS);
    if (r == -1) {
        log_error("iclient set_producer failed");
        return;
    }

    ZpollerGuard poller(zpoller_new(pipe, mlm_client_msgpipe(client), NULL));
    if (!poller) {
        log_fatal("zpoller_new () failed");
        return;
    }

    zsock_signal(pipe, 0);

    log_info("fty-nut starting...");

    NUTAgent nut_agent(NutStateManager.getReader());
    nut_agent.setClient(client);
    nut_agent.setiClient(iclient);

    StateManager::Writer& state_writer = NutStateManager.getWriter();
    // (Ab)use the iclient for the initial assets mailbox request, because it
    // will not receive any interfering stream messages
    get_initial_assets(state_writer, iclient, true);

    log_info("fty-nut started");

    uint64_t timeout = 30000; //ms
    uint64_t last = uint64_t(zclock_mono());

    while (!zsys_interrupted) {
        uint64_t now = uint64_t(zclock_mono());
        if ((now - last) >= timeout) {
            log_debug("Periodic polling");
            nut_agent.updateDeviceList();
            nut_agent.onPoll();

            last = uint64_t(zclock_mono());
            log_debug("Periodic polling lap time: %zu ms", (last - now));
        }

        void* which = zpoller_wait(poller, int(timeout));

        if (which == NULL) {
            if (zpoller_terminated(poller) || zsys_interrupted) {
                log_debug("zpoller_terminated () or zsys_interrupted");
                break;
            }
        }
        else if (which == pipe) {
            zmsg_t* msg = zmsg_recv(pipe);
            if (msg) {
                int quit = actor_commands(&msg, timeout, nut_agent);
                zmsg_destroy(&msg);
                if (quit) {
                    break; //$TERM
                }
            }
        }
        else if (which == mlm_client_msgpipe(client)) {
            zmsg_t* msg = mlm_client_recv(client);
            if (msg && fty_proto_is(msg)) {
                bool changed = state_writer.getState().updateFromMsg(&msg);
                if (changed) {
                    state_writer.commit();
                }
            }
            zmsg_destroy(&msg);
        }
    }

    log_info("fty-nut ended");
}
