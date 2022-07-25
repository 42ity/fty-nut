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

#include "sensor_list.h"
#include "nut_agent.h"
#include <fty_asset_accessor.h>
#include <fty_common_agents.h>
#include <fty_common_mlm.h>
#include <fty_common_nut.h>
#include <fty_log.h>
#include <nutclientmem.h>

Sensors::Sensors(StateManager::Reader* reader)
    : _state_reader(reader)
{
}

void Sensors::updateFromNUT(nut::TcpClient& conn)
{
    try {
        for (auto& it : _sensors) {
            it.second.update(conn, _sensorInventoryMapping);
        }
    } catch (const std::exception& e) {
        log_error("reading data from NUT: %s", e.what());
    }
}

bool Sensors::updateAssetConfig(AssetState::Asset* asset, mlm_client_t* client)
{
    if (!client || !asset)
        return false;

    const int sendTimeout = 5000; //ms
    const int recvTimeout = 5000; //ms

    ZpollerGuard poller(zpoller_new(mlm_client_msgpipe(client), NULL));
    if (!poller) {
        log_error("updateAssetConfig: Poller creation failed");
        return false;
    }

    // request asset-agent for ASSET_DETAIL on asset
    fty_proto_t* proto = NULL;
    {
        ZuuidGuard uuid(zuuid_new());
        if (!uuid) {
            log_error("updateAssetConfig: UUID creation failed");
            return false;
        }
        const char* uuid_sent = zuuid_str_canonical(uuid);

        zmsg_t* msg = zmsg_new();
        if (!msg) {
            log_error("zmsg_new() failed");
            return false;
        }
        zmsg_addstr(msg, "GET");
        zmsg_addstr(msg, uuid_sent);
        zmsg_addstr(msg, asset->name().c_str());
        int r = mlm_client_sendto(client, AGENT_FTY_ASSET, "ASSET_DETAIL", NULL, sendTimeout, &msg);
        zmsg_destroy(&msg);
        if (r < 0) {
            log_error("updateAssetConfig: %s ASSET_DETAIL query failed (timeout: %d ms)",
                asset->name().c_str(), sendTimeout);
            return false;
        }
        //log_trace("client sent query for asset %s", asset->name().c_str());

        // get response
        if (!zpoller_wait(poller, recvTimeout)) {
            log_error("updateAssetConfig: %s ASSET_DETAIL timed out (%d ms)",
                asset->name().c_str(), recvTimeout);
            return false;
        }
        msg = mlm_client_recv(client);
        if (!msg) {
            log_error("updateAssetConfig: %s ASSET_DETAIL no response", asset->name().c_str());
            return false;
        }
        char* uuid_recv = zmsg_popstr(msg);
        bool uuidIsOk = (uuid_recv && streq(uuid_recv, uuid_sent));
        zstr_free(&uuid_recv);
        if (uuidIsOk)
            { proto = fty_proto_decode(&msg); }
        zmsg_destroy(&msg);

        if (!uuidIsOk) {
            log_error("updateAssetConfig: %s ASSET_DETAIL uuid mismatch", asset->name().c_str());
            return false;
        }
        if (!proto) {
            log_error("updateAssetConfig: %s ASSET_DETAIL decode failed", asset->name().c_str());
            return false;
        }

        log_debug("updateAssetConfig: %s ASSET_DETAIL succeed", asset->name().c_str());
    }

    const char* parentName = fty_proto_aux_string(proto, "parent_name.1", "");

    // If need update (modbus address not empty or parent has changed)
    if (!asset->subAddress().empty()
        || !streq(parentName, asset->location().c_str())
    ){
        fty_proto_set_operation(proto, FTY_PROTO_ASSET_OP_UPDATE);

        // Update modbus address
        fty_proto_ext_insert(proto, "endpoint.1.sub_address", asset->subAddress().c_str());
        // Update parent name
        fty_proto_aux_insert(proto, "parent_name.1", "%s", asset->location().c_str());

        // Update parent id (get parent id from database)
        auto parentId = fty::AssetAccessor::assetInameToID(asset->location().c_str());
        if (!parentId) {
            log_error("updateAssetConfig for %s: get parent id failed", asset->name().c_str());
            fty_proto_destroy(&proto);
            return false;
        }
        log_debug("updateAssetConfig for %s: get parent id=%d", asset->name().c_str(), parentId.value());
        fty_proto_aux_insert(proto, "parent", "%d", parentId.value());

        // send update request
        zmsg_t* msg = fty_proto_encode(&proto);
        fty_proto_destroy(&proto);
        zmsg_pushstrf(msg, "%s", "READWRITE");
        int r = mlm_client_sendto(client, AGENT_FTY_ASSET, "ASSET_MANIPULATION", NULL, sendTimeout, &msg);
        zmsg_destroy(&msg);
        if (r < 0) {
            log_error("updateAssetConfig for %s: client failed to send update (timeout: %d ms)",
                asset->name().c_str(), sendTimeout);
            return false;
        }
        log_debug("updateAssetConfig: client sent update request for asset %s", asset->name().c_str());

        // recv response
        if (!zpoller_wait(poller, recvTimeout)) {
            log_error("updateAssetConfig for %s: timed out (%d ms)", asset->name().c_str(), recvTimeout);
            return false;
        }
        msg = mlm_client_recv(client);
        if (!msg) {
            log_error("updateAssetConfig for %s: client empty response", asset->name().c_str());
            return false;
        }

        char* status = zmsg_popstr(msg);
        zmsg_destroy(&msg);
        log_debug("updateAssetConfig: client got response %s for asset %s", status, asset->name().c_str());
        bool success = (status && streq(status, "OK"));
        zstr_free(&status);

        if (!success) {
            log_error("updateAssetConfig for %s: client failed update request", asset->name().c_str());
            return false;
        }
    }

    fty_proto_destroy(&proto);
    return true;
}

void Sensors::updateSensorList (nut::Client &conn, mlm_client_t *client)
{
    // Note: force refresh sensors list if an error has been detected
    if (!_sensorListError && !_state_reader->refresh())
        return;

    bool sensorListError = false;
    const AssetState& deviceState = _state_reader->getState();
    auto& devices = deviceState.getPowerDevices();
    auto& sensors = deviceState.getSensors();

    log_debug("sa: updating sensors list (%zu sensors)", sensors.size());

    _sensors.clear();
    for (auto i : sensors) {
        const std::string& name = i.first;
        const std::string& parent_name = i.second->location();

        // do we know where is sensor connected?
        if (parent_name.empty()) {
            log_debug("sa: sensor %s ignored (no location)", name.c_str());
            removeInventory(name);
            continue;
        }

        log_debug("sa: checking sensor %s (location: %s, port: %s)", name.c_str(), parent_name.c_str(),
            i.second->port().c_str());

        // is it connected to UPS/epdu/ATS?
        const auto parent_it = devices.find(parent_name);
        if (parent_it == devices.cend()) {
            log_debug("sa: sensor parent '%s' not found", parent_name.c_str());
            // Connected to a sensor?
            if (sensors.count(parent_name)) {
                // give parent his child
                const std::string& port = i.second->port();
                if (!port.empty()) {
                    _sensors[parent_name].addChild(port, name);
                    // FIXME: support multiple children
                    log_debug("sa: sensor %s has port '%s')", name.c_str(), port.c_str());
                } else {
                    log_debug("sa: sensor %s has no port)", name.c_str());
                }
            } else {
                log_debug("sa: sensor '%s' ignored (location is unknown/not a power device/not a sensor '%s')",
                    name.c_str(), parent_name.c_str());
            }

            removeInventory(name);
            continue;
        } else {
            log_debug("sa: sensor parent found: '%s' (chain: %d)",
                parent_name.c_str(), parent_it->second->daisychain());
        }

        const AssetState::Asset* parent = parent_it->second.get();
        const std::string& ip = parent->IP();
        int chain = parent->daisychain();
        std::string master;

        Sensor::ChildrenMap children = _sensors[name].getChildren();
        // for emp01 sensor
        if (i.second->port() == "0") {
            if (chain == 0) {
                _sensors[name] = Sensor(i.second.get(), parent, children);
                log_debug("sa: adding sensor, with parent (not daisy): '%s'", parent_name.c_str());
            } else {
                master = deviceState.ip2master(ip);
                _sensors[name] = Sensor(i.second.get(), parent, children, master, 0);
                log_debug("sa: adding sensor, with parent (daisy) and index %d: '%s'", 0, parent_name.c_str());
            }
        }
        // for emp02 sensor
        else {
            std::string prefix;
            int index = 0;
            if (chain == 0) {
                // connected to standalone ups
                master = parent->name();
            } else {
                // ugh, sensor connected to daisy chain device
                master = deviceState.ip2master(ip);
                prefix = "device.1.";
            }

            std::string subAddress = i.second->subAddress();
            log_debug("sa: sensor with sub address %s", subAddress.c_str());
            // Normal treatment with modbus address
            if (!subAddress.empty()) {
                // search index corresponding to sub address
                std::string sensorCountName = prefix + std::string("ambient.count");
                std::vector<std::string> values = {};
                try {
                    values = conn.getDeviceVariableValue(master, sensorCountName);
                } catch (const std::exception& e) {
                    log_error(
                        "Nut object %s not found for (%s): %s", sensorCountName.c_str(), master.c_str(), e.what());
                    // Error of communication detected with nut driver, need to refresh sensors list later
                    sensorListError = true;
                    continue;
                }
                if (values.size() > 0) {
                    int sensorCount = std::atoi(values.at(0).c_str());
                    log_debug("sa: sensor count: %d", sensorCount);
                    for (int iSensor = 1; iSensor <= sensorCount; iSensor++) {
                        std::string addressDeviceName =
                            prefix + std::string("ambient.") + std::to_string(iSensor) + std::string(".address");
                        try {
                            values = conn.getDeviceVariableValue(master, addressDeviceName);
                            if (values.size() > 0) {
                                std::string subAddressDevice = values.at(0);
                                log_debug("sa: get device sub address: %s", subAddressDevice.c_str());
                                if (subAddressDevice == subAddress) {
                                    index = iSensor;
                                    log_debug("sa: found index %d for sub address %s", index, subAddressDevice.c_str());
                                    break;
                                }
                            }
                        } catch (const std::exception& e) {
                            log_error("Nut object %s not found for (%s): %s", addressDeviceName.c_str(), master.c_str(),
                                e.what());
                            // Error of communication detected with nut driver, need to refresh sensors list later
                            sensorListError = true;
                            continue;
                        }
                    }
                }
            }
            // Backward compatibility with port (no modbus address)
            else {
                log_debug("sa: backward compatibility with port (no modubus address)");
                std::string port = i.second->port();
                index = std::atoi(port.c_str());
                if (index > 0) {
                    // update parent if necessary
                    AssetState::Asset* newParent = nullptr;
                    // get serial number of parent
                    std::string parentSerialNumberName =
                        prefix + std::string("ambient.") + port + std::string(".parent.serial");
                    log_debug ("sa: parentSerialNumberName=%s", parentSerialNumberName.c_str());
                    std::vector<std::string> values = {};
                    try {
                        values = conn.getDeviceVariableValue(master, parentSerialNumberName);
                    } catch (const std::exception& e) {
                        log_error("Nut object %s not found for (%s): %s", parentSerialNumberName.c_str(),
                            master.c_str(), e.what());
                        // Error of communication detected with nut driver, need to refresh sensors list later
                        sensorListError = true;
                        continue;
                    }
                    if (values.size() > 0) {
                        std::string parentSerialNumber = values.at(0);
                        log_debug ("sa: parentSerialNumber %s parent=%s", parentSerialNumber.c_str(), parent->serial().c_str());
                        // Here we have the master for location, need to find the good parent and update location if different of master
                        if (!parentSerialNumber.empty() && parentSerialNumber != parent->serial()) {
                            for (auto device : devices) {
                                const std::string& ipDevice = device.second->IP();
                                const std::string& serialDevice = device.second->serial();
                                log_debug ("sa: ipDevice %s serialDevice %s", ipDevice.c_str(), serialDevice.c_str());
                                if (ipDevice == ip && serialDevice == parentSerialNumber) {
                                    newParent = device.second.get();
                                    break;
                                }
                            }
                            if (newParent) {
                                log_debug("sa: set new parent %s", newParent->name().c_str());
                                parent = newParent;
                                i.second->setLocation(newParent->name());
                            }
                        }
                    }
                    // update modbus address
                    std::string addressDeviceName = prefix + std::string("ambient.") + port + std::string(".address");
                    log_debug("sa: index=%d addressDeviceName='%s'", index, addressDeviceName.c_str());
                    try {
                        std::vector<std::string> values1 = conn.getDeviceVariableValue(master, addressDeviceName);
                        if (values1.size() > 0) {
                            std::string addressDevice = values1.at(0);
                            log_debug("sa: set device sub address: %s", addressDevice.c_str());
                            // Save sub_address attribute
                            i.second->setSubAddress(addressDevice);
                        }
                    } catch (const std::exception& e) {
                        log_warning("sa: nut object %s not found for (%s): %s", addressDeviceName.c_str(),
                            master.c_str(), e.what());
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
                    log_debug(
                        "sa: adding sensor, with parent (not daisy) and index %d: '%s'", index, parent_name.c_str());
                }
                // else daisychain
                else {
                    if (master.empty()) {
                        log_error("sa: daisychain host for %s not found", parent_name.c_str());
                        removeInventory(name);
                    } else {
                        _sensors[name] = Sensor(i.second.get(), parent, children, master, index);
                        log_debug(
                            "sa: adding sensor, with parent (daisy) and index %d: '%s'", index, parent_name.c_str());
                    }
                }
            }
        }
    }

    _sensorListError = sensorListError;
    if (_sensorListError) {
        log_debug("sa: loaded %zd nut sensors with error(s): retry in a moment", _sensors.size());
    } else {
        log_debug("sa: loaded %zd nut sensors", _sensors.size());
    }
}

void Sensors::publish(mlm_client_t* client, int ttl)
{
    if (!client) return;

    for (auto& it : _sensors) {
        it.second.publish(client, ttl);
    }
}

void Sensors::removeInventory(const std::string& name)
{
    const auto& it_hash = _lastInventoryHashs.find(name);
    if (it_hash != _lastInventoryHashs.end()) {
        _lastInventoryHashs.erase(it_hash);
    }
}

bool Sensors::isInventoryChanged(const std::string& name)
{
    const auto& it_sensor = _sensors.find(name);
    if (it_sensor == _sensors.end()) {
        return false;
    }

    std::string buffer;
    for (const auto& item : it_sensor->second.inventory()) {
        buffer += item.first + "(" + item.second + ")";
    }
    if (buffer.empty()) {
        return false;
    }

    std::size_t hash = std::hash<std::string>{}(buffer);

    const auto& it_hash = _lastInventoryHashs.find(name);
    if (it_hash != _lastInventoryHashs.end() && hash == it_hash->second) {
        log_debug("sa: publish sensor inventory for %s: no change", name.c_str());

        return false; // exist & unchanged
    }

    _lastInventoryHashs[name] = hash;

    log_debug("sa: publish sensor inventory for %s: %s", name.c_str(), buffer.c_str());

    return true; // new or changed
}

void Sensors::advertiseInventory(mlm_client_t* client)
{
    bool advertiseAll = false;
    uint64_t now = static_cast<uint64_t>(zclock_mono()); //ms
    if ((_inventoryTimestamp_ms + NUT_INVENTORY_REPEAT_AFTER_MS) < now) {
        _inventoryTimestamp_ms = now;
        advertiseAll = true;
    }

    for (auto& sensor : _sensors) {
        auto sensorName = sensor.second.assetName();

        // send inventory only if changed
        // Note: need to update last inventory **before** testing advertiseAll
        if (isInventoryChanged(sensorName) || advertiseAll) {
            log_debug("sa: publish sensor inventory for %s", sensorName.c_str());

            std::string log;
            zhash_t* inventory = zhash_new();
            zhash_autofree(inventory);
            for (auto& item : sensor.second.inventory()) {
                zhash_insert(inventory, item.first.c_str(), const_cast<char*>(item.second.c_str()));
                log += (log.empty() ? "" : ",") + item.first + "(" + item.second + ")";
            }

            if (zhash_size(inventory) != 0) {
                zmsg_t* message = fty_proto_encode_asset(NULL, sensorName.c_str(), FTY_PROTO_ASSET_OP_INVENTORY, inventory);

                if (message) {
                    std::string topic = "inventory@" + sensorName;
                    int r = mlm_client_send(client, topic.c_str(), &message);
                    if (r < 0) {
                        log_error("sa: send %s failed (r: %d)", topic.c_str(), r);
                    }
                    else {
                        log_debug("sa: send %s (%s)", topic.c_str(), log.c_str());
                    }
                }
                else {
                    log_debug("fty_proto_encode_asset() failed (%s)", sensorName.c_str());
                }
                zmsg_destroy(&message);
            }
            zhash_destroy(&inventory);
        }
    }
}

void Sensors::loadSensorMapping(const char* path_to_file)
{
    log_info("Load sensor mapping from %s", path_to_file);
    _sensorMappingLoaded = false;

    try {
        log_debug("Loading sensor inventory mapping...");
        _sensorInventoryMapping = fty::nut::loadMapping(path_to_file, "sensorInventoryMapping");
        log_debug("Number of entries loaded for sensor inventory mapping: %zu", _sensorInventoryMapping.size());

        _sensorMappingLoaded = true;
    } catch (const std::exception& e) {
        log_error("Couldn't load mapping: %s", e.what());
    }
}

std::map<std::string, Sensor>& Sensors::sensors()
{
    return _sensors;
}
