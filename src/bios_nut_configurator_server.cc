/*  =========================================================================
    bios_nut_configurator_server - bios nut configurator actor

    Copyright (C) 2014 - 2015 Eaton

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
    bios_nut_configurator_server - bios nut configurator actor
@discuss
@end
*/

#include "agent_nut_classes.h"

#include <fstream>
#include <cxxtools/jsonserializer.h>
#include <cxxtools/jsondeserializer.h>

#define MLM_ENDPOINT "ipc://@/malamute"

/* TODO: This state is shared with core::agent-autoconfig due to needed
 * upgradability from Alpha. Should be cleaned up around/after release. */
static const char* PATH = "/var/lib/bios/agent-autoconfig";
static const char* STATE = "/var/lib/bios/agent-autoconfig/state";

static int
load_agent_info(std::string &info)
{
    if (shared::is_file (STATE))
    {
        try {
            std::fstream f{STATE};
            f >> info;
            return 0;
        }
        catch (const std::exception& e)
        {
            log_error("Fail to read '%s', %s", PATH, e.what());
            return -1;
        }
    }
    info = "";
    return 0;
}

static int
save_agent_info(const std::string& json)
{
    if (!shared::is_dir (PATH)) {
        zsys_error ("Can't serialize state, '%s' is not directory", PATH);
        return -1;
    }
    try {
        std::fstream f{STATE};
        f << json;
    }
    catch (const std::exception& e) {
        zsys_error ("Can't serialize state, %s", e.what());
        return -1;
    }
    return 0;
}

inline void operator<<= (cxxtools::SerializationInfo& si, const AutoConfigurationInfo& info)
{
    si.setTypeName("AutoConfigurationInfo");
    // serializing integer doesn't work for unknown reason
    si.addMember("type") <<= std::to_string(info.type);
    si.addMember("subtype") <<= std::to_string(info.subtype);
    si.addMember("operation") <<= std::to_string(info.operation);
    si.addMember("configured") <<= info.configured;
    si.addMember("attributes") <<= info.attributes;
}

inline void operator>>= (const cxxtools::SerializationInfo& si, AutoConfigurationInfo& info)
{
    si.getMember("configured") >>= info.configured;
    {
        // serializing integer doesn't work
        std::string tmp;
        si.getMember("type") >>= tmp;
        info.type = atoi(tmp.c_str());

        si.getMember("subtype") >>= tmp;
        info.subtype = atoi(tmp.c_str());

        si.getMember("operation") >>= tmp;
        info.operation = atoi(tmp.c_str());
    }
    si.getMember("attributes")  >>= info.attributes;
}

// autoconfig agent public methods

void Autoconfig::onStart( )
{
    loadState();
    setPollingInterval();
}

static bool
s_is_ups_or_epdu (bios_proto_t *bmsg)
{
    assert (bmsg);

    if (!streq (bios_proto_aux_string (bmsg, "type", ""), "device"))
        return false;

    if ((!streq (bios_proto_aux_string (bmsg, "subtype", ""), "ups")) &&
        (!streq (bios_proto_aux_string (bmsg, "subtype", ""), "epdu")))
        return false;

    return true;
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
s_operation2i (const char *operation)
{
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

    bios_proto_t *bmsg = bios_proto_decode (message);
    if (!bmsg)
    {
        log_warning ("got non bios_proto from %s", mlm_client_sender (_client));
        return;
    }

    // ignore non ups/epdu devices - or those with non interesting operation
    if (!s_is_ups_or_epdu (bmsg) || s_operation2i (bios_proto_operation (bmsg)) == -1) {
        bios_proto_destroy (&bmsg);
        return;
    }

    // this is a device that we should configure, we need extended attributes (ip.1 particularly)
    device_name = bios_proto_name (bmsg);
    // MVY: 6 is device, for subtype see core.git/src/shared/asset_types.h
    subtype = streq (bios_proto_aux_string (bmsg, "subtype", ""), "ups") ? 1 : 3;

    // upsconf_block support - devices with an explicit "upsconf_block"
    // ext-attribute will be always configured ([ab]using nut-scanner logic
    // in nut_configurator.cc). Those without the block may differ...
    count_upsconf_block = bios_proto_ext_number (bmsg, "upsconf_block", 0);
    if (count_upsconf_block == 0) {
        // daisy_chain pdu support - only devices with daisy_chain == 1 or no such ext attribute will be configured via nut-scanner
        if (bios_proto_ext_number (bmsg, "daisy_chain", 0) > 1) {
            bios_proto_destroy (&bmsg);
            return;
        }
    }

    addDeviceIfNeeded( device_name, 6, subtype );
    _configurableDevices[device_name].configured = false;
    _configurableDevices[device_name].attributes.clear();
    _configurableDevices[device_name].operation = s_operation2i (bios_proto_operation (bmsg));
    _configurableDevices[device_name].attributes = s_zhash_to_map(bios_proto_ext (bmsg));
    bios_proto_destroy (&bmsg);
    saveState();

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
                auto factory = ConfigFactory();
                if( factory.configureAsset (it.first, it.second)) {
                    it.second.configured = true;
                    save = true;
                }
                it.second.date = time(NULL);
            }
        }
    }
    if( save ) { cleanupState(); saveState(); }
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

void Autoconfig::loadState()
{
    std::string json = "";
    int rv = load_agent_info(json);
    if ( rv != 0 || json.empty() )
        return;

    std::istringstream in(json);

    try {
        _configurableDevices.clear();
        cxxtools::JsonDeserializer deserializer(in);
        deserializer.deserialize(_configurableDevices);
    } catch( std::exception &e ) {
        log_error( "can't parse state: %s", e.what() );
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

void Autoconfig::saveState()
{
    std::ostringstream stream;
    cxxtools::JsonSerializer serializer(stream);

    serializer.serialize( _configurableDevices ).finish();
    std::string json = stream.str();
    save_agent_info(json );
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
bios_nut_configurator_server (zsock_t *pipe, void *args)
{
    Autoconfig agent ("agent-autoconfig");
    agent.connect (MLM_ENDPOINT, "ASSETS", ".*");

    zpoller_t *poller = zpoller_new (pipe, mlm_client_msgpipe (agent.client()), NULL);

    zsock_signal (pipe, 0);
    while (!zsys_interrupted)
    {
        void *which = zpoller_wait (poller, agent.timeout());

        if (which == pipe || zsys_interrupted)
            break;

        if (!which) {
            agent.onPoll ();
            continue;
        }

        zmsg_t *msg = mlm_client_recv (agent.client());
        const char* command = mlm_client_command (agent.client());
        if (streq (command, "STREAM DELIVER"))
            agent.onSend (&msg);

        zmsg_destroy (&msg);
    }

    zpoller_destroy (&poller);
}

//  --------------------------------------------------------------------------
//  Self test of this class

void
bios_nut_configurator_server_test (bool verbose)
{
    printf (" * bios_nut_configurator_server: ");


    //  @selftest
    //  Simple create/destroy test
    zactor_t *self = zactor_new (bios_nut_configurator_server, NULL);
    assert (self);
    zactor_destroy (&self);
    //  @end
    printf ("OK\n");
}
