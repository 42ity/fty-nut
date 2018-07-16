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
#include <fty_log.h>

#include <ftyproto.h>
#include <fstream>

// autoconfig agent public methods

Autoconfig::Autoconfig(StateManager::Reader* reader)
    : _traversal_color(0)
    , _state_reader(reader)

{
}

void Autoconfig::onUpdate()
{
    if (!_state_reader->refresh())
        return;
    const AssetState& deviceState = _state_reader->getState();
    auto& devices = deviceState.getAllPowerDevices();
    _traversal_color = !_traversal_color;
    // Add new devices and mark existing ones as visited
    for (auto i : devices) {
        const std::string& name = i.first;
        auto it = _configDevices.find(name);
        if (it == _configDevices.end()) {
            AutoConfigurationInfo device;
            device.state = AutoConfigurationInfo::STATE_NEW;
            device.asset = i.second.get();
            auto res = _configDevices.insert(std::make_pair(name, device));
            it = res.first;
        } else if (it->second.asset != i.second.get()) {
            // This is an updated asset, mark it for reconfiguration
            // (STATE_NEW is a misnomer, but the semantics of a potential
            // STATE_UPDATED would be identical)
            it->second.state = AutoConfigurationInfo::STATE_NEW;
            it->second.asset = i.second.get();
        }
        it->second.traversal_color = _traversal_color;
    }
    // Mark no longer existing devices for deletion
    for (auto &i : _configDevices) {
        if (i.second.traversal_color != _traversal_color) {
            i.second.state = AutoConfigurationInfo::STATE_DELETING;
            // Not needed, but null pointer derefs are easier to chase down
            // than use after free bugs
            i.second.asset = nullptr;
        }
    }
    // Mark stale snippets for deletion (this can happen after startup)
    std::vector<std::string> snippets;
    if (NUTConfigurator::known_assets(snippets)) {
        for (auto i : snippets) {
            if (_configDevices.count(i))
                continue;
            AutoConfigurationInfo device;
            device.asset = nullptr;
            device.state = AutoConfigurationInfo::STATE_DELETING;
            _configDevices.insert(std::make_pair(i, device));
        }
    }
    setPollingInterval();
}

void Autoconfig::onPoll()
{
    NUTConfigurator configurator;
    for(auto it = _configDevices.begin(); it != _configDevices.end(); ) {
        switch (it->second.state) {
        case AutoConfigurationInfo::STATE_NEW:
        case AutoConfigurationInfo::STATE_CONFIGURING:
            // check not configured devices
            if (configurator.configure(it->first, it->second))
                it->second.state = AutoConfigurationInfo::STATE_CONFIGURED;
            else
                it->second.state = AutoConfigurationInfo::STATE_CONFIGURING;
            break;
        case AutoConfigurationInfo::STATE_CONFIGURED:
            // Nothing to do
            break;
        case AutoConfigurationInfo::STATE_DELETING:
            configurator.erase(it->first);
            it = _configDevices.erase(it);
            continue;
        }
        ++it;
    }
    setPollingInterval();
}

// autoconfig agent private methods

void Autoconfig::setPollingInterval( )
{
    bool have_quick = false, have_discovery = false, have_failed = false;

    for( auto &it : _configDevices) {
        switch (it.second.state) {
        case AutoConfigurationInfo::STATE_NEW:
            if (it.second.asset->have_upsconf_block()) {
                // For devices in verbatim mode, proceed to configuration even
                // faster
                have_quick = true;
            } else {
                // Schedule autodiscovery after 5 seconds
                have_discovery = true;
            }
            break;
        case AutoConfigurationInfo::STATE_CONFIGURING:
            // we failed to configure some device let's try after one minute
            // again
            have_failed = true;
            break;
        case AutoConfigurationInfo::STATE_CONFIGURED:
            // Nothing to do
            break;
        case AutoConfigurationInfo::STATE_DELETING:
            // Deletion is also quick to deal with
            have_quick = true;
        }
    }
    // This is not entirely correct, we should record the timestamp of the
    // last configuration attempt for each asset and just select the timeout
    // of the first asset to expire.
    if (have_quick)
        _timeout = 100;
    else if (have_discovery)
        _timeout = 5000;
    else if (have_failed)
        _timeout = 60000;
    else
        _timeout = -1;
}

void
fty_nut_configurator_server (zsock_t *pipe, void *args)
{
    StateManager state_manager;
    StateManager::Writer& state_writer = state_manager.getWriter();
    Autoconfig agent(state_manager.getReader());
    const char *endpoint = static_cast<const char *>(args);

    MlmClientGuard client(mlm_client_new());
    if (!client) {
        log_error("mlm_client_new() failed");
        return;
    }
    if (mlm_client_connect(client, endpoint, 5000, ACTOR_CONFIGURATOR_NAME) < 0) {
        log_error("client %s failed to connect", ACTOR_CONFIGURATOR_NAME);
        return;
    }
    if (mlm_client_set_consumer(client, FTY_PROTO_STREAM_ASSETS, ".*") < 0) {
        log_error("mlm_client_set_consumer (stream = '%s', pattern = '.*') failed",
                FTY_PROTO_STREAM_ASSETS);
        return;
    }
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
        agent.onUpdate();
    }

    ZpollerGuard poller(zpoller_new(pipe, mlm_client_msgpipe(client), NULL));

    zsock_signal (pipe, 0);
    while (!zsys_interrupted)
    {
        void *which = zpoller_wait (poller, agent.timeout());
        if (which == pipe || zsys_interrupted)
            break;
        if (!which) {
            zsys_debug("Periodic polling");
            agent.onPoll ();
            continue;
        }
        zmsg_t *msg = mlm_client_recv(client);
        if (is_fty_proto(msg)) {
            if (state_writer.getState().updateFromProto(msg))
                state_writer.commit();
            agent.onUpdate();
            continue;
        }
        log_error ("Unhandled message (%s/%s)",
                mlm_client_command(client),
                mlm_client_subject(client));
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
