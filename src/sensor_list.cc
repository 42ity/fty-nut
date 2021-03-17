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

/*
@header
    sensor_list - list of sensor attached to UPSes
@discuss
@end
*/

#include "sensor_list.h"
#include "nut_agent.h"
#include <fty_log.h>
#include <fty_common_nut.h>
#include <fty_asset_accessor.h>

Sensors::Sensors (StateManager::Reader *reader)
    : _state_reader(reader)
{
}


void Sensors::updateFromNUT (nut::TcpClient &conn)
{
    try {
        for (auto& it : _sensors) {
            it.second.update (conn, _sensorInventoryMapping);
        }
    } catch (std::exception& e) {
        log_error ("reading data from NUT: %s", e.what ());
    }
}

bool Sensors::updateAssetConfig (AssetState::Asset *asset, mlm_client_t *client)
{
    if (!client || !asset) return false;

    zmsg_t *msg = zmsg_new();
    zmsg_addstr (msg, "GET");
    zmsg_addstr (msg, "");
    zmsg_addstr (msg, asset->name().c_str());
    if (mlm_client_sendto(client, "asset-agent", "ASSET_DETAIL", NULL, 10, &msg) < 0) {
        log_error("updateAssetConfig for %s: client failed to send query", asset->name().c_str());
        zmsg_destroy (&msg);
        return false;
    }
    zmsg_destroy (&msg);
    log_debug("client sent query for asset %s", asset->name().c_str());
    zmsg_t *response = mlm_client_recv(client);
    if(!response) {
        log_error("updateAssetConfig for %s: client empty response", asset->name().c_str());
        return false;
    }
    char* uuid = zmsg_popstr(response);
    zstr_free(&uuid);
    fty_proto_t* proto = fty_proto_decode(&response);
    zmsg_destroy(&response);
    log_debug("updateAssetConfig: client got response for asset %s", asset->name().c_str());
    if(!proto) {
        log_error("updateAssetConfig for %s: client failed query request", asset->name().c_str());
        return false;
    }

    const char *parentName = fty_proto_aux_string(proto, "parent_name.1", "");
    // If need update (modbus address not empty or parent has changed)
    if (!asset->subAddress().empty() || strcmp(parentName, asset->location().c_str()) != 0) {
        fty_proto_set_operation(proto, FTY_PROTO_ASSET_OP_UPDATE);
        // Update modbus address
        fty_proto_ext_insert(proto, "endpoint.1.sub_address", asset->subAddress().c_str());
        // Update parent
        fty_proto_aux_insert(proto, "parent_name.1", "%s", asset->location().c_str());
        // Get parent id from database
        auto parentId = fty::AssetAccessor::assetInameToID(asset->location().c_str());
        if (!parentId) {
            log_error("updateAssetConfig for %s: get parent id failed", asset->name().c_str());
            zmsg_destroy (&msg);
            fty_proto_destroy (&proto);
            return false;
        }
        log_info("updateAssetConfig for %s: get parent id=%d", asset->name().c_str(), parentId.value());
        // Update parent id
        fty_proto_aux_insert(proto, "parent", "%d", parentId.value());

        msg = fty_proto_encode(&proto);
        fty_proto_destroy (&proto);
        zmsg_pushstrf (msg, "%s", "READWRITE");
        if (mlm_client_sendto(client, "asset-agent", "ASSET_MANIPULATION", NULL, 10, &msg) < 0) {
            log_error("updateAssetConfig for %s: client failed to send update", asset->name().c_str());
            zmsg_destroy (&msg);
            return false;
        }
        zmsg_destroy (&msg);
        log_debug("updateAssetConfig: client sent update request for asset %s", asset->name().c_str());
        response = mlm_client_recv(client);
        if(!response) {
            log_error("updateAssetConfig for %s: client empty response", asset->name().c_str());
            return false;
        }
        char *str_resp = zmsg_popstr(response);
        log_debug("updateAssetConfig: client got response %s for asset %s", str_resp, asset->name().c_str());
        zmsg_destroy(&response);
        if(!str_resp || !streq(str_resp, "OK")) {
            zstr_free(&str_resp);
            log_error("updateAssetConfig for %s: client failed update request", asset->name().c_str());
            return false;
        }
        zstr_free(&str_resp);
    }
    return true;
}

void Sensors::updateSensorList (nut::TcpClient &conn, mlm_client_t *client)
{
    // Note: force refresh sensors list if an error has been detected
    if (!_sensorListError && !_state_reader->refresh())
        return;

    bool sensorListError = false;
    const AssetState& deviceState = _state_reader->getState();
    auto& devices = deviceState.getPowerDevices();
    auto& sensors = deviceState.getSensors();

    log_debug("sa: updating sensors list");

    log_debug ("sa: %zd sensors in assets", sensors.size());
    _sensors.clear ();
    for (auto i : sensors) {
        const std::string& name = i.first;
        const std::string& parent_name = i.second->location();
        // do we know where is sensor connected?
        if (parent_name.empty()) {
            log_debug ("sa: sensor %s ignored (no location)", name.c_str());
            removeInventory(name);
            continue;
        }
        log_debug ("sa: checking sensor %s (location: %s, port: %s)", name.c_str(), parent_name.c_str(), i.second->port().c_str());

        // is it connected to UPS/epdu/ATS?
        const auto parent_it = devices.find(parent_name);
        if (parent_it == devices.cend()) {
            log_debug ("sa: sensor parent '%s' not found", parent_name.c_str());
            // Connected to a sensor?
            if (sensors.count(parent_name)) {
                // give parent his child
                const std::string& port = i.second->port();
                if (!port.empty()) {
                    _sensors[parent_name].addChild (port, name);
                    // FIXME: support multiple children
                    log_debug ("sa: sensor %s has port '%s')", name.c_str(), port.c_str());
                }
                else
                    log_debug ("sa: sensor %s has no port)", name.c_str());
            }
            else
                log_debug ("sa: sensor '%s' ignored (location is unknown/not a power device/not a sensor '%s')", name.c_str(), parent_name.c_str());

            removeInventory(name);
            continue;
        }
        else
            log_debug ("sa: sensor parent found: '%s' (chain: %d)", parent_name.c_str(), parent_it->second->daisychain());

        const AssetState::Asset *parent = parent_it->second.get();
        const std::string& ip = parent->IP();
        int chain = parent->daisychain();
        std::string master;

        Sensor::ChildrenMap children = _sensors [name].getChildren ();
        // for emp01 sensor
        if (i.second->port() == "0") {
            if (chain == 0) {
                _sensors[name] = Sensor(i.second.get(), parent, children);
                log_debug ("sa: adding sensor, with parent (not daisy): '%s'", parent_name.c_str());
            }
            else {
                master = deviceState.ip2master(ip);
                _sensors[name] = Sensor(i.second.get(), parent, children, master, 0);
                log_debug ("sa: adding sensor, with parent (daisy) and index %d: '%s'", 0, parent_name.c_str());
            }
        }
        // for emp02 sensor
        else {
            std::string prefix;
            int index = 0;
            int offset = 0;

            if (chain == 0) {
                // connected to standalone ups
                master = parent->name();
            } else {
                // ugh, sensor connected to daisy chain device
                master = deviceState.ip2master(ip);
                prefix = "device.1.";
                offset = (chain -1) * 3;
            }

            std::string subAddress = i.second->subAddress();
            log_debug ("sa: sensor with sub address %s", subAddress.c_str());
            // Normal treatment with modbus address
            if (!subAddress.empty()) {
                // search index corresponding to sub address
                for (int iSensor = offset + 1; iSensor <= offset + 3; iSensor ++) {
                    std::string addressDeviceName = prefix + std::string("ambient.") + std::to_string(iSensor) + std::string(".address");
                    try {
                        std::vector<std::string> values = conn.getDeviceVariableValue(master, addressDeviceName);
                        if (values.size() > 0) {
                            std::string subAddressDevice = values.at(0);
                            log_debug ("sa: get device sub address: %s", subAddressDevice.c_str());
                            if (subAddressDevice == subAddress) {
                                index = iSensor;
                                log_debug ("sa: found index %d for sub address %s", index, subAddressDevice.c_str());
                                break;
                            }
                        }
                    } catch (std::exception &e) {
                        log_error("Nut object %s not found for (%s): %s", addressDeviceName.c_str(), master.c_str(), e.what());
                        // Error of communication detected with nut driver, need to refresh sensors list later
                        sensorListError = true;
                        continue;
                    }
                }
            }
            // Backward compatibility with port (no modbus address)
            else {
                log_debug ("sa: backward compatibility with port (no modubus address)");
                std::string port = i.second->port();
                index = std::atoi(port.c_str());
                if (index > 0) {
                    // Update parent
                    AssetState::Asset *newParent = nullptr;
                    int newChain = (index / 3) + 1;
                    // Here we have the master for location, need to find the good parent and update location
                    if (newChain > 1) {
                        for (auto device : devices) {
                            const std::string& ipDevice = device.second->IP();
                            int chainDevice = device.second->daisychain();
                            if (ipDevice == ip && chainDevice == newChain) {
                                newParent = device.second.get();
                                break;
                            }
                        }
                        if (newParent) {
                            log_debug ("sa: set new parent %s", newParent->name().c_str());
                            parent = newParent;
                            i.second->setLocation(newParent->name());
                        }
                    }
                    // update modbus address
                    std::string addressDeviceName = prefix + std::string("ambient.") + port + std::string(".address");
                    log_debug ("sa: index=%d addressDeviceName='%s'", index, addressDeviceName.c_str());
                    try {
                        std::vector<std::string> values = conn.getDeviceVariableValue(master, addressDeviceName);
                        if (values.size() > 0) {
                            std::string addressDevice = values.at(0);
                            log_debug ("sa: set device sub address: %s", addressDevice.c_str());
                            // Save sub_address attribute
                            i.second->setSubAddress(addressDevice);
                        }
                    } catch (std::exception &e) {
                        log_warning("sa: nut object %s not found for (%s): %s", addressDeviceName.c_str(), master.c_str(), e.what());
                    }
                    // Update asset config values
                    updateAssetConfig(i.second.get(), client);
                }
            }
            // If found correct index
            if (index > 0) {
                // If no daisychain
                if (chain == 0) {
                    _sensors[name] = Sensor(i.second.get(), parent, children, index);
                    log_debug ("sa: adding sensor, with parent (not daisy) and index %d: '%s'", index, parent_name.c_str());
                }
                // else daisychain
                else {
                    if (master.empty()) {
                        log_error ("sa: daisychain host for %s not found", parent_name.c_str());
                        removeInventory(name);
                    } else {
                        _sensors[name] = Sensor(i.second.get(), parent, children, master, index);
                        log_debug ("sa: adding sensor, with parent (daisy) and index %d: '%s'", index, parent_name.c_str());
                    }
                }
            }
        }
    }
    _sensorListError = sensorListError;
    if (_sensorListError)
        log_debug ("sa: loaded %zd nut sensors with error(s): retry in a moment", _sensors.size());
    else
        log_debug ("sa: loaded %zd nut sensors", _sensors.size());

}

void Sensors::publish (mlm_client_t *client, int ttl)
{
    for (auto& it : _sensors) {
        it.second.publish (client, ttl);
    }
}

void Sensors::removeInventory(std::string name)
{
    const auto& it_hash = _lastInventoryHashs.find(name);
    if (it_hash != _lastInventoryHashs.end()) {
        _lastInventoryHashs.erase(it_hash);
    }
}

bool Sensors::isInventoryChanged(std::string name)
{
    const auto& it_sensor = _sensors.find(name);
    if (it_sensor != _sensors.end()) {
        std::string buffer;
        for ( const auto &item : it_sensor->second.inventory() ) {
            buffer += item.first + item.second;
        }
        if (!buffer.empty()) {
            log_debug ("sa: publish sensor inventory for %s: buffer=%s", it_sensor->second.assetName().c_str(), buffer.c_str());
            std::size_t hash = std::hash<std::string>{}(buffer);
            const auto& it_hash = _lastInventoryHashs.find(name);
            if (it_hash != _lastInventoryHashs.end() && hash == it_hash->second) {
                return false;
            }
            else {
                log_debug ("sa: publish sensor inventory for %s: %lu <> %lu", it_sensor->second.assetName().c_str(), hash, it_hash->second);
                _lastInventoryHashs[name] = hash;
                return true;
            }
        }
    }
    return false;
}

void Sensors::advertiseInventory(mlm_client_t *client)
{
    bool advertiseAll = false;
    if (_inventoryTimestamp_ms + NUT_INVENTORY_REPEAT_AFTER_MS < static_cast<uint64_t> (zclock_mono ())) {
        advertiseAll = true;
        _inventoryTimestamp_ms = static_cast<uint64_t> (zclock_mono ());
    }

    for (auto& sensor : _sensors) {
        // send inventory only if change
        // Note: need to update last inventory before testing advertiseAll
        if (isInventoryChanged(sensor.second.assetName()) || advertiseAll) {
            log_debug ("sa: publish sensor inventory for %s", sensor.second.assetName().c_str());

            std::string log;
            zhash_t *inventory = zhash_new ();
            zhash_autofree(inventory);
            for (auto& item : sensor.second.inventory()) {
                zhash_insert (inventory, item.first.c_str (), (void *) item.second.c_str ()) ;
                log += item.first + " = \"" + item.second + "\"; ";
            }
            if (zhash_size (inventory) != 0) {
                zmsg_t *message = fty_proto_encode_asset (
                        NULL,
                        sensor.second.assetName().c_str(),
                        "inventory",
                        inventory);

                if (message) {
                    std::string topic = "inventory@" + sensor.second.assetName();
                    log_debug ("new sensor inventory message '%s': %s", topic.c_str(), log.c_str());
                    // FIXME: a hack for inventory messages ???
                    //fty_proto_t *m_decoded = fty_proto_decode(&message);
                    //zmsg_destroy(&message);
                    //message = fty_proto_encode(&m_decoded);
                    int r = mlm_client_send (client, topic.c_str (), &message);
                    if( r != 0 ) log_error("failed to send inventory %s result %" PRIi32, topic.c_str(), r);
                    zmsg_destroy (&message);
                }
                zhash_destroy (&inventory);
            }
        }
    }
}

void Sensors::loadSensorMapping(const char *path_to_file)
{
    _sensorMappingLoaded = false;

    try {

        log_debug("Loading sensor inventory mapping...");
        _sensorInventoryMapping = fty::nut::loadMapping(path_to_file, "sensorInventoryMapping");
        log_debug("Number of entries loaded for sensor inventory mapping: %zu", _sensorInventoryMapping.size());

        _sensorMappingLoaded = true;
    }
    catch (std::exception &e) {
        log_error("Couldn't load mapping: %s", e.what());
    }
}


//  --------------------------------------------------------------------------
//  Self test of this class

void
sensor_list_test (bool verbose)
{
    printf (" * sensor_list: ");

    StateManager manager;
    auto& writer = manager.getWriter();

    fty_proto_t *asset = fty_proto_new (FTY_PROTO_ASSET);
    fty_proto_set_name (asset, "ups-1");
    fty_proto_set_operation (asset, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert (asset, "type", "device");
    fty_proto_aux_insert (asset, "subtype", "ups");
    fty_proto_ext_insert (asset, "ip.1", "1.1.1.1");
    writer.getState().updateFromProto(asset);
    fty_proto_destroy(&asset);

    asset = fty_proto_new (FTY_PROTO_ASSET);
    fty_proto_set_name (asset, "sensor-1");
    fty_proto_set_operation (asset, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert (asset, "type", "device");
    fty_proto_aux_insert (asset, "subtype", "sensor");
    fty_proto_aux_insert (asset, "parent_name.1", "ups-1");
    writer.getState().updateFromProto(asset);
    fty_proto_destroy(&asset);


    asset = fty_proto_new (FTY_PROTO_ASSET);
    fty_proto_set_name (asset, "epdu-1");
    fty_proto_set_operation (asset, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert (asset, "type", "device");
    fty_proto_aux_insert (asset, "subtype", "epdu");
    fty_proto_ext_insert (asset, "ip.1", "1.1.1.2");
    fty_proto_ext_insert (asset, "daisy_chain", "1");
    writer.getState().updateFromProto(asset);
    fty_proto_destroy(&asset);

    asset = fty_proto_new (FTY_PROTO_ASSET);
    fty_proto_set_name (asset, "epdu-2");
    fty_proto_set_operation (asset, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert (asset, "type", "device");
    fty_proto_aux_insert (asset, "subtype", "epdu");
    fty_proto_ext_insert (asset, "ip.1", "1.1.1.2");
    fty_proto_ext_insert (asset, "daisy_chain", "2");
    writer.getState().updateFromProto(asset);
    fty_proto_destroy(&asset);

    asset = fty_proto_new (FTY_PROTO_ASSET);
    fty_proto_set_name (asset, "sensor-2");
    fty_proto_set_operation (asset, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert (asset, "type", "device");
    fty_proto_aux_insert (asset, "subtype", "sensor");
    fty_proto_aux_insert (asset, "parent_name.1", "epdu-2");
    fty_proto_ext_insert (asset, "port", "5");
    writer.getState().updateFromProto(asset);
    fty_proto_destroy(&asset);

    asset = fty_proto_new (FTY_PROTO_ASSET);
    fty_proto_set_name (asset, "sensorgpio-1");
    fty_proto_set_operation (asset, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert (asset, "type", "device");
    fty_proto_aux_insert (asset, "subtype", "sensorgpio");
    fty_proto_aux_insert (asset, "parent_name.1", "sensor-2");
    fty_proto_aux_insert (asset, "parent_name.2", "epdu-2");
    fty_proto_ext_insert (asset, "port", "1");
    writer.getState().updateFromProto(asset);
    fty_proto_destroy(&asset);
    writer.commit();

    nut::TcpClient nutClient;
    nutClient.connect ("localhost", 3493);
    Sensors list(manager.getReader());
    list.updateSensorList (nutClient, nullptr);
    nutClient.disconnect();
    assert (list._sensors.size() == 2);

    assert (list._sensors["sensor-1"].sensorPrefix() == "ambient.");
    assert (list._sensors["sensor-1"].topicSuffix() == ".0@ups-1");
    assert (list._sensors["sensor-1"].nutPrefix() == "ambient.");
    assert (list._sensors["sensor-1"].nutIndex() == 0);
    assert (list._sensors["sensor-1"].location() == "ups-1");
    assert (list._sensors["sensor-1"].subAddress() == "");

    assert (list._sensors["sensor-2"].sensorPrefix() == "device.2.ambient.5.");
    assert (list._sensors["sensor-2"].topicSuffix() == ".5@epdu-2");
    assert (list._sensors["sensor-2"].nutPrefix() == "device.1.ambient.5.");
    assert (list._sensors["sensor-2"].nutIndex() == 5);
    assert (list._sensors["sensor-2"].location() == "epdu-2");
    assert (list._sensors["sensor-2"].subAddress() == "");
    assert (list._sensors["sensor-2"].chain() == 2);

    //  @end
    printf ("OK\n");
}
