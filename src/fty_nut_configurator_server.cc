/*  =========================================================================
    fty_nut_configurator_server - fty nut configurator actor

    Copyright (C) 2014 - 2017 Eaton

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

/*
@header
    fty_nut_configurator_server - fty nut configurator actor
@discuss
@end
*/

#include "fty_nut_configurator_server.h"
#include "state_manager.h"
#include "nut_mlm.h"
#include "logger.h"

#include <ftyproto.h>
#include <fstream>

// autoconfig agent public methods

static bool
s_is_ups_epdu_or_sts (fty_proto_t *bmsg)
{
    assert (bmsg);

    if (!streq (fty_proto_aux_string (bmsg, "type", ""), "device"))
        return false;

    const char *subtype = fty_proto_aux_string (bmsg, "subtype", "");
    return streq (subtype, "ups") || streq (subtype, "epdu") || streq (subtype, "sts");
}

static int
s_powerdevice_subtype_id (const char *subtype)
{
    if (streq (subtype, "ups")) return asset_subtype::UPS;
    if (streq (subtype, "epdu")) return asset_subtype::EPDU;
    if (streq (subtype, "sts")) return asset_subtype::STS;
    return -1;
}

static std::map<std::string,std::string>
s_zhash_to_map(zhash_t *hash)
{
    std::map<std::string,std::string> map;
    char *item = (char *)zhash_first(hash);
    while(item) {
        const char * key = zhash_cursor(hash);
        const char * val = (const char *)zhash_lookup(hash,key);
        if( key && val ) map[key] = val;
        item = (char *)zhash_next(hash);
    }
    return map;
}

// see core.git/src/shared/asset_types.h
static int
s_operation2i (fty_proto_t *msg)
{
    if (!msg) return -1;
    const char *operation = fty_proto_operation (msg);
    if (!operation) return -1;
    const char *status = fty_proto_aux_string (msg, "status", "active");
    if (streq (status, "nonactive")) {
        // device is nonactive -> handle it as delete
        return 2;
    }
    if (streq (operation, "create"))
        return 1;
    else
    if (streq (operation, "delete"))
        return 2;
    else
    if (streq (operation, "update"))
        return 3;
    return -1;
}

void Autoconfig::onSend( zmsg_t **message )
{

    if( ! message || ! *message ) return;

    const char *device_name = NULL;
    uint32_t subtype = 0;
    uint64_t count_upsconf_block = 0; // 0 or 1 in practice

    fty_proto_t *bmsg = fty_proto_decode (message);
    if (!bmsg)
    {
        log_warning ("got non fty_proto from %s", mlm_client_sender (_client));
        return;
    }

    // ignore non ups/epdu devices - or those with non interesting operation
    if (!s_is_ups_epdu_or_sts (bmsg) || s_operation2i (bmsg) == -1) {
        fty_proto_destroy (&bmsg);
        return;
    }

    // this is a device that we should configure, we need extended attributes (ip.1 particularly)
    device_name = fty_proto_name (bmsg);
    // MVY: 6 is device, for subtype see core.git/src/shared/asset_types.h
    subtype = s_powerdevice_subtype_id (fty_proto_aux_string (bmsg, "subtype", ""));

    // upsconf_block support - devices with an explicit "upsconf_block"
    // ext-attribute will be always configured ([ab]using nut-scanner logic
    // in nut_configurator.cc). Those without the block may differ...
    count_upsconf_block = fty_proto_ext_number (bmsg, "upsconf_block", 0);
    if (count_upsconf_block == 0) {
        // daisy_chain pdu support - only devices with daisy_chain == 1 or no such ext attribute will be configured via nut-scanner
        if (fty_proto_ext_number (bmsg, "daisy_chain", 0) > 1) {
            fty_proto_destroy (&bmsg);
            return;
        }
    }

    addDeviceIfNeeded( device_name, 6, subtype );
    _configurableDevices[device_name].configured = false;
    _configurableDevices[device_name].attributes.clear();
    _configurableDevices[device_name].operation = s_operation2i (bmsg);
    _configurableDevices[device_name].attributes = s_zhash_to_map(fty_proto_ext (bmsg));
    fty_proto_destroy (&bmsg);

    if (count_upsconf_block == 0) {
        // For devices in non-verbatim mode, schedule discovery to be attempted
        setPollingInterval();
    } else {
        // For devices in verbatim mode, proceed to configuration even faster
        _timeout = 100;
    }
}

void Autoconfig::onPoll( )
{
    bool save = false;

    for( auto &it : _configurableDevices) {
        // check not configured devices
        if( ! it.second.configured ) {
            // we don't need extended attributes for deleting configuration
            // but we need them for update/insert
            if(
                ! it.second.attributes.empty() ||
                it.second.operation == asset_operation::DELETE ||
                it.second.operation == asset_operation::RETIRE
            )
            {
                NUTConfigurator configurator;
                if( configurator.configure (it.first, it.second)) {
                    it.second.configured = true;
                    save = true;
                }
                it.second.date = time(NULL);
            }
        }
    }
    if( save ) { cleanupState(); }
    setPollingInterval();
}

// autoconfig agent private methods

void Autoconfig::setPollingInterval( )
{
    _timeout = -1;
    for( auto &it : _configurableDevices) {
        if( ! it.second.configured ) {
            if( it.second.date == 0 ) {
                // there is device that we didn't try to configure
                // let's try to do it soon
                _timeout = 5000;
                return;
            } else {
                // we failed to configure some device
                // let's try after one minute again
                _timeout = 60000;
            }
        }
    }
}

void Autoconfig::addDeviceIfNeeded(const char *name, uint32_t type, uint32_t subtype) {
    if( _configurableDevices.find(name) == _configurableDevices.end() ) {
        AutoConfigurationInfo device;
        device.type = type;
        device.subtype = subtype;
        _configurableDevices[name] = device;
    }
}

void Autoconfig::cleanupState()
{
    for( auto it = _configurableDevices.cbegin(); it != _configurableDevices.cend() ; ) {
        if( it->second.configured ) {
            _configurableDevices.erase(it++);
        } else {
            ++it;
        }
    }
}

bool Autoconfig::connect(
        const char * endpoint,
        const char *stream = NULL,
        const char *pattern = NULL)
{
    assert (endpoint);

    _client = mlm_client_new ();
    mlm_client_connect (_client, endpoint, 5000, _agentName.c_str ());
    if (stream)
        mlm_client_set_producer (_client, stream);
    if (pattern)
        mlm_client_set_consumer (_client, stream, pattern);

    return true;
}

void
fty_nut_configurator_server (zsock_t *pipe, void *args)
{
    StateManager state_manager;
    StateManager::Writer& state_writer = state_manager.getWriter();
    Autoconfig agent (ACTOR_CONFIGURATOR_NAME);

    const char *endpoint = static_cast<const char *>(args);
    agent.connect (endpoint, "ASSETS", ".*");

    // Ge the initial list of assets. This has to be done after subscribing
    // ourselves to the ASSETS stream. And we do not the infrastructure to do
    // this during unit testing
    if (strcmp(endpoint, MLM_ENDPOINT) == 0) {
        MlmClientGuard mb_client(mlm_client_new());
        if (!mb_client) {
            log_error("mlm_client_new() failed");
            return;
        }
        if (mlm_client_connect(mb_client, endpoint, 5000, ACTOR_CONFIGURATOR_MB_NAME) < 0) {
            log_error("client %s failed to connect", ACTOR_CONFIGURATOR_MB_NAME);
            return;
        }
        get_initial_assets(state_writer, mb_client);
    }

    ZpollerGuard poller(zpoller_new(pipe, mlm_client_msgpipe(agent.client()), NULL));

    zsock_signal (pipe, 0);
    uint64_t last = zclock_mono ();
    while (!zsys_interrupted)
    {
        void *which = zpoller_wait (poller, agent.timeout());

        uint64_t now = zclock_mono();
        if (now - last >= static_cast<uint64_t>(agent.timeout())) {
            last = now;
            zsys_debug("Periodic polling");
            agent.onPoll ();
        }

        if (which == pipe || zsys_interrupted)
            break;

        if (!which) {
            continue;
        }

        zmsg_t *msg = mlm_client_recv (agent.client());
        if (is_fty_proto(msg)) {
            // This is a temporary hack until the agent is fully migrated to
            // the StateManager
            zmsg_t *msg2 = zmsg_dup(msg);
            agent.onSend (&msg2);
            handle_fty_proto(state_writer, msg);
            continue;
        }
        log_error ("Unhandled message (%s/%s)",
                mlm_client_command(agent.client()),
                mlm_client_subject(agent.client()));
        zmsg_print (msg);
        zmsg_destroy (&msg);
    }
}

//  --------------------------------------------------------------------------
//  Self test of this class

void
fty_nut_configurator_server_test (bool verbose)
{
    printf (" * fty_nut_configurator_server: ");


    //  @selftest
    //  Simple create/destroy test
    static const char* endpoint = "inproc://fty_nut_configurator_server-test";
    zactor_t *mlm = zactor_new(mlm_server, (void*) "Malamute");
    assert(mlm);
    zstr_sendx(mlm, "BIND", endpoint, NULL);
    zactor_t *self = zactor_new (fty_nut_configurator_server, (void *)endpoint);
    assert (self);
    zactor_destroy (&self);
    zactor_destroy (&mlm);
    //  @end
    printf ("OK\n");
}
