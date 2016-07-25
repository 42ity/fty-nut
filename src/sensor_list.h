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

#ifndef SENSOR_LIST_H_INCLUDED
#define SENSOR_LIST_H_INCLUDED

class Sensors {
 public:
    void updateFromNUT ();
    void updateSensorList (nut_t *config);
    void publish (mlm_client_t *client, int ttl);

    // friend function for unit-testing
    friend void sensor_list_test (bool verbose);
    friend void sensor_actor_test (bool verbose);
 protected:
    std::map <std::string, Sensor>  _sensors;
};

//  Self test of this class
AGENT_NUT_EXPORT void
    sensor_list_test (bool verbose);

#endif
