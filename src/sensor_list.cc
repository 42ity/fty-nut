/*  =========================================================================
    sensor_list - list of sensor attached to UPSes

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

/*
@header
    sensor_list - list of sensor attached to UPSes
@discuss
@end
*/

#include "sensor_list.h"
#include "logger.h"

Sensors::Sensors (StateManager::Reader *reader)
    : _state_reader(reader)
{
}


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

void Sensors::updateSensorList ()
{
    if (!_state_reader->refresh())
        return;
    const AssetState& deviceState = _state_reader->getState();
    auto& devices = deviceState.getPowerDevices();
    auto& sensors = deviceState.getSensors();

    log_debug("sa: updating device list");

    log_debug ("sa: %zd sensors in assets", sensors.size());
    _sensors.clear ();
    for (auto i : sensors) {
        const std::string& name = i.first;
        const std::string& parent_name = i.second->location();
        // do we know where is sensor connected?
        if (parent_name.empty()) {
            log_debug ("sa: sensor %s ignored (no location)", name.c_str());
            continue;
        }

        // is it connected to UPS/epdu?
        const auto parent_it = devices.find(parent_name);
        if (parent_it == devices.cend()) {
            // Connected to a sensor?
            if (sensors.count(parent_name)) {
                // give parent his child
                const std::string& port = i.second->port();
                if (!port.empty()) {
                    _sensors[parent_name].addChild (port, name);
                }
            }
            else
                log_debug ("sa: sensor '%s' ignored (location is unknown/not a power device/not a sensor '%s')", name.c_str(), parent_name.c_str());

            continue;
        }
        const AssetState::Asset *parent = parent_it->second.get();
        const std::string& ip = parent->IP();
        int chain = parent->daisychain();

        Sensor::ChildrenMap children = _sensors [name].getChildren ();

        if (chain <= 1) {
            // connected to standalone ups or chain master
            _sensors[name] = Sensor(i.second.get(), parent, children);
        } else {
            // ugh, sensor connected to daisy chain device
            auto master = deviceState.ip2master(ip);
            if (master.empty()) {
                log_error ("sa: daisychain host for %s not found", parent_name.c_str());
            } else {
                _sensors[name] = Sensor(i.second.get(), parent, children, master);
            }
        }
    }
    log_debug ("sa: loaded %zd nut sensors", _sensors.size());

}

void Sensors::publish (mlm_client_t *client, int ttl)
{
    for (auto& it : _sensors) {
        it.second.publish (client, ttl);
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
    fty_proto_ext_insert (asset, "port", "21");
    writer.getState().updateFromProto(asset);
    fty_proto_destroy(&asset);

    asset = fty_proto_new (FTY_PROTO_ASSET);
    fty_proto_set_name (asset, "sensorgpio-1");
    fty_proto_set_operation (asset, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert (asset, "type", "device");
    fty_proto_aux_insert (asset, "subtype", "sensorgpio");
    fty_proto_aux_insert (asset, "parent_name.1", "sensor-2");
    fty_proto_ext_insert (asset, "port", "1");
    writer.getState().updateFromProto(asset);
    fty_proto_destroy(&asset);
    writer.commit();

    Sensors list(manager.getReader());
    list.updateSensorList ();
    assert (list._sensors.size() == 2);

    assert (list._sensors["sensor-1"].sensorPrefix() == "ambient.");
    assert (list._sensors["sensor-1"].topicSuffix() == ".0@ups-1");
    assert (list._sensors["sensor-2"].sensorPrefix() == "device.2.ambient.21.");
    assert (list._sensors["sensor-2"].topicSuffix() == ".21@epdu-2");

    //  @end
    printf ("OK\n");
}
