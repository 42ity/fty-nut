/*  =========================================================================
    sensor_list - list of sensor attached to UPSes

    Copyright (C) 2014 - 2020 Eaton

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

#include "sensor_device.h"
#include "state_manager.h"

class Sensors {
 public:
    explicit Sensors (StateManager::Reader *reader);
    void updateFromNUT (nut::TcpClient &conn);
    bool updateAssetConfig (AssetState::Asset *asset, mlm_client_t *client);
    void updateSensorList (nut::TcpClient &conn, mlm_client_t *client);
    void publish (mlm_client_t *client, int ttl);
    void removeInventory(std::string name);
    bool isInventoryChanged(std::string name);
    void advertiseInventory(mlm_client_t *client);
    const std::map <std::string, std::string>& getSensorMapping() const { return _sensorInventoryMapping; };
    void loadSensorMapping(const char *path_to_file);

    // friend function for unit-testing
    friend void sensor_list_test (bool verbose);
    friend void sensor_actor_test (bool verbose);
 protected:
    std::map <std::string, Sensor>  _sensors; // name | Sensor
    std::map <std::string, std::size_t>  _lastInventoryHashs;
    std::unique_ptr<StateManager::Reader> _state_reader;
    uint64_t _inventoryTimestamp_ms = 0; // [ms] it is not an actual timestamp, it is just a reference point in time, when inventory was advertised
    std::map <std::string, std::string> _sensorInventoryMapping; //!< sensor inventory mapping
    bool _sensorMappingLoaded = false;
    bool _sensorListError = false;  // Flag to detect if error during initialisation of sensors list
};

//  Self test of this class
void sensor_list_test (bool verbose);

#endif
