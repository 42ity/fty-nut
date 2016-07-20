/*  =========================================================================
    sensor_list - list of sensor attached to UPSes

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
    sensor_list - list of sensor attached to UPSes
@discuss
@end
*/

#include "agent_nut_classes.h"

//  Structure of our class

void Sensors::updateFromNUT ()
{
    try {
        nut::TcpClient nutClient;
        nutClient.connect ("localhost", 3493);
        for (auto& it : _sensors) {
            it.second.update (nutClient);
        }
        nutClient.disconnect();
    } catch (std::exception& e) {
        log_error ("reading data from NUT: %s", e.what ());
    }
}

void Sensors::updateSensorList (nut_t *config)
{
}


void Sensors::publish (mlm_client_t *client)
{
    for (auto& it : _sensors) {
        it.second.publish (client);
    }
}

void Sensors::addIfNotPresent (Sensor sensor)
{
}


//  --------------------------------------------------------------------------
//  Self test of this class

void
sensor_list_test (bool verbose)
{
    printf (" * sensor_list: ");

    //  @selftest
    //  @end
    printf ("OK\n");
}
