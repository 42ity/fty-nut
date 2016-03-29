/*  =========================================================================
    actor_commands - actor commands

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

#ifndef ACTOR_COMMANDS_H_INCLUDED
#define ACTOR_COMMANDS_H_INCLUDED

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
//  CONFIGURE/mapping
//      configure actor, where
//      mapping - full path to mapping file

// Performs the actor commands logic
// Destroys the message
// Returns 1 for $TERM (means exit), 0 otherwise
AGENT_NUT_EXPORT int
    actor_commands (
            mlm_client_t *client,
            zmsg_t **message_p,
            bool& verbose,
            NUTAgent& nut_agent);

//  Self test of this class
AGENT_NUT_EXPORT void
    actor_commands_test (bool verbose);
//  @end

#endif
