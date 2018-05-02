/*  =========================================================================
    actor_commands - actor commands

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

#ifndef ACTOR_COMMANDS_H_INCLUDED
#define ACTOR_COMMANDS_H_INCLUDED

#include "nut.h"

#include <string>

class NUTAgent;


// Supported actor commands:
//  $TERM
//      terminate
//
//  VERBOSE
//      set verbosity of bios_nut_server, malamute in tests
//
//  CONNECT/enpoint/name
//      connect to malamute broker on 'endpoint' registering as 'name'
//
//  PRODUCER/stream
//      publish to specified 'stream'
//
//  CONSUMER/stream/pattern
//      consume messages from 'stream' with subjects matching 'pattern'
//
//  CONFIGURE/mapping/state_file
//      configure actor, where
//      mapping_file - full path to mapping file
//      state_file - full pathname of state file
//
//  POLLING/value
//      change polling interval, where
//      value - new polling interval in seconds
//



// Performs the actor commands logic
// Destroys the message
// Returns 1 for $TERM (means exit), 0 otherwise
int actor_commands (
            mlm_client_t *client,
            zmsg_t **message_p,
            bool& verbose,
            uint64_t& timeout,
            NUTAgent& nut_agent,
            nut_t *data,
            std::string& state_fullpath);

//  Self test of this class
void actor_commands_test (bool verbose);
//  @end

#endif
