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

#ifndef BIOS_NUT_CONFIGURATOR_SERVER_H_INCLUDED
#define BIOS_NUT_CONFIGURATOR_SERVER_H_INCLUDED

#include <string>
#include <vector>
#include "nut_configurator.h"

class Autoconfig {
 public:
    explicit Autoconfig( const char *agentName ): _agentName (agentName) {  };
    ~Autoconfig () { mlm_client_destroy (&_client);}

    void onStart( );
    void onEnd( ) { cleanupState(); saveState(); };
    void onSend( zmsg_t **message );
    void onPoll( );
    bool connect(
        const char * endpoint,
        const char *stream,
        const char *pattern);
    mlm_client_t *client () const {return _client;}
    int timeout () const {return _timeout;}
 private:
    void sendNewRules(std::vector<std::string> const &rules);
    void setPollingInterval();
    void addDeviceIfNeeded(const char *name, uint32_t type, uint32_t subtype);
    void requestExtendedAttributes( const char *name );
    void cleanupState();
    void saveState();
    void loadState();
    ConfigFactory _configurator;
    std::map<std::string,AutoConfigurationInfo> _configurableDevices;

 protected:
    mlm_client_t *_client = NULL;
    int _exitStatus = 0;
    int _timeout = 2000;
    std::string _agentName;
};

#ifdef __cplusplus
extern "C" {
#endif
void
    bios_nut_configurator_server (zsock_t *pipe, void *args);

void
    bios_nut_configurator_server_test (bool verbose);
#ifdef __cplusplus
}
#endif

#endif
