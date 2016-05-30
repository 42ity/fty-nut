/*  =========================================================================
    stream - stream deliver command

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
    stream - stream deliver command
@discuss
@end
*/

#include "agent_nut_classes.h"


//  --------------------------------------------------------------------------
//  Handle stream deliver command

void
stream_deliver_handle (
        mlm_client_t *client,
        NUTAgent& nut_agent,
        nut_t *data,
        zmsg_t **message_p)
{
    assert (client);
    assert (data);
    assert (message_p && *message_p);
    
    if (!is_bios_proto (*message_p)) {
        log_warning (
                "Message received is not bios_proto; sender = '%s', subject = '%s'",
                mlm_client_sender (client), mlm_client_subject (client));
        zmsg_destroy (message_p);
        return;
    }
    bios_proto_t *proto = bios_proto_decode (message_p);
    if (!proto) {
        log_critical ("bios_proto_decode () failed.");
        zmsg_destroy (message_p);
        return;
    }
    nut_put (data, &proto);
    nut_agent.updateDeviceList (data);
}

//  --------------------------------------------------------------------------
//  Self test of this class

void
stream_test (bool verbose)
{
    printf (" * stream: ");

    //  @selftest
    
    //  @end
    printf ("Empty test - OK\n");
}
