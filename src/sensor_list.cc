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

#include "fty_nut_classes.h"

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
    log_debug("sa: updating device list");

    if (!config) return;
    zlist_t *devices = nut_get_powerdevices (config);
    if (!devices) return;
    zlist_t *sensors = nut_get_sensors (config);
    if (!sensors) { zlist_destroy (&devices); return; }
    log_debug ("sa: %zd sensors in assets", zlist_size (sensors));
    _sensors.clear ();
    std::map<std::string, std::string> ip2master;
    {
        // make ip->master map
        const char *name = (char *)zlist_first(devices);
        while (name) {
            const char* ip = nut_asset_ip (config, name);
            const char* chain = nut_asset_daisychain (config, name);
            if (ip == NULL || chain == NULL || streq (ip, "") ) {
                // this is strange. No IP?
                name = (char *)zlist_next(devices);
                continue;
            }
            if (streq (chain,"") || streq (chain,"1")) {
                // this is master
                ip2master[ip] = name;
            }
            name = (char *)zlist_next(devices);
        }
    }

    char * name = (char *)zlist_first(sensors);
    while (name) {
        const char *connected_to = nut_asset_location (config, name);
        // do we know where is sensor connected?
        if (streq (connected_to, "")) {
            log_debug ("sa: sensor %s ignored (no location)", name);
            name = (char *) zlist_next (sensors);
            continue;
        }

        // is it connected to UPS/epdu?
        if ( ! zlist_exists (devices, (void *) connected_to)) {
            log_debug ("sa: sensor %s ignored (connected to unknown location or not a power device '%s')", name, connected_to);
            name = (char *) zlist_next (sensors);
            continue;
        }

        const char* ip = nut_asset_ip (config, connected_to);
        const char* chain_str = nut_asset_daisychain (config, connected_to);
        int chain = 0;
        if (chain_str) try { chain = std::stoi (chain_str); } catch(...) { };
        if (chain <= 1) {
            // connected to standalone ups or chain master
            _sensors[name] = Sensor(
                connected_to,
                chain,
                connected_to,
                nut_asset_port (config, name)
            );
        } else {
            // ugh, sensor connected to daisy chain slave
            const auto master_it = ip2master.find (ip);
            if (master_it == ip2master.cend()) {
                log_error ("sa: daisychain master for %s not found", connected_to);
            } else {
                _sensors[name] = Sensor(
                    master_it->second,
                    chain,
                    connected_to,
                    nut_asset_port (config, name)
                );
            }
        }
        name = (char *) zlist_next (sensors);
    }
    zlist_destroy (&sensors);
    zlist_destroy (&devices);
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
    //  @selftest
    nut_t *config = nut_new ();

    fty_proto_t *asset = fty_proto_new (FTY_PROTO_ASSET);
    fty_proto_set_name (asset, "%s", "ups-1");
    fty_proto_set_operation (asset, "%s", FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert (asset, "type", "%s", "device");
    fty_proto_aux_insert (asset, "subtype", "%s", "ups");
    fty_proto_ext_insert (asset, "ip.1", "%s", "1.1.1.1");
    nut_put (config, &asset);

    asset = fty_proto_new (FTY_PROTO_ASSET);
    fty_proto_set_name (asset, "%s", "sensor-1");
    fty_proto_set_operation (asset, "%s", FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert (asset, "type", "%s", "device");
    fty_proto_aux_insert (asset, "subtype", "%s", "sensor");
    fty_proto_aux_insert (asset, "parent_name.1", "%s", "ups-1");
    nut_put (config, &asset);

    asset = fty_proto_new (FTY_PROTO_ASSET);
    fty_proto_set_name (asset, "%s", "epdu-1");
    fty_proto_set_operation (asset, "%s", FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert (asset, "type", "%s", "device");
    fty_proto_aux_insert (asset, "subtype", "%s", "epdu");
    fty_proto_ext_insert (asset, "ip.1", "%s", "1.1.1.2");
    fty_proto_ext_insert (asset, "daisy_chain", "%s", "1");
    nut_put (config, &asset);

    asset = fty_proto_new (FTY_PROTO_ASSET);
    fty_proto_set_name (asset, "%s", "epdu-2");
    fty_proto_set_operation (asset, "%s", FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert (asset, "type", "%s", "device");
    fty_proto_aux_insert (asset, "subtype", "%s", "epdu");
    fty_proto_ext_insert (asset, "ip.1", "%s", "1.1.1.2");
    fty_proto_ext_insert (asset, "daisy_chain", "%s", "2");
    nut_put (config, &asset);

    asset = fty_proto_new (FTY_PROTO_ASSET);
    fty_proto_set_name (asset, "%s", "sensor-2");
    fty_proto_set_operation (asset, "%s", FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert (asset, "type", "%s", "device");
    fty_proto_aux_insert (asset, "subtype", "%s", "sensor");
    fty_proto_aux_insert (asset, "parent_name.1", "%s", "epdu-2");
    fty_proto_ext_insert (asset, "port", "%s", "21");
    nut_put (config, &asset);

    Sensors list;
    list.updateSensorList (config);
    assert (list._sensors.size() == 2);
    assert (list._sensors["sensor-1"].sensorPrefix() == "ambient.");
    assert (list._sensors["sensor-1"].topicSuffix() == ".0@ups-1");
    assert (list._sensors["sensor-2"].sensorPrefix() == "device.2.ambient.21.");
    assert (list._sensors["sensor-2"].topicSuffix() == ".21@epdu-2");

    nut_destroy (&config);
    //  @end
    printf ("OK\n");
}
