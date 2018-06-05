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

#ifndef FTY_NUT_CONFIGURATOR_SERVER_H_INCLUDED
#define FTY_NUT_CONFIGURATOR_SERVER_H_INCLUDED

#include "nut_configurator.h"
#include "state_manager.h"

#include <string>
#include <vector>

class Autoconfig {
 public:
    explicit Autoconfig(StateManager::Reader* reader);

    void onPoll();
    void onUpdate();
    int timeout () const {return _timeout;}
    void handleLimitations (zmsg_t **message );
 private:
    void setPollingInterval();
    void addDeviceIfNeeded(const std::string& name, AssetState::Asset *asset);
    void cleanupState();
    int _traversal_color;
    std::map<std::string,AutoConfigurationInfo> _configDevices;
    std::unique_ptr<StateManager::Reader> _state_reader;

 protected:
    int _timeout = 2000;
};


#ifdef __cplusplus
extern "C" {
#endif

//  @interface
//  Create a fty_nut_configurator_server
void fty_nut_configurator_server (zsock_t *pipe, void *args);

//  Self test of this class
void fty_nut_configurator_server_test (bool verbose);

//  @end

#ifdef __cplusplus
}
#endif

#endif
