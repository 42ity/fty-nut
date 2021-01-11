/*  =========================================================================
    sensor_sensor - structure for device producing alerts

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

#include "sensor_device.h"
#include <fty_log.h>

#include <ftyproto.h>
#include <vector>
#include <string>

#include <fty_common_nut.h>

static std::string collapse_commas(const std::vector<std::string> &values)
{
    std::string inventory = "";
    for(size_t i = 0 ; i < values.size() ; ++i ) {
        inventory += values[i];
        if( i < values.size() -1 ) {
            inventory += ", ";
        }
    }
    return inventory;
}

void Sensor::update (nut::TcpClient &conn, const std::map <std::string, std::string>& mapping)
{
    log_debug ("sa: updating sensor(s) temperature and humidity from NUT device %s", _nutMaster.c_str());
    auto nutDevice = conn.getDevice(_nutMaster);
    if (! nutDevice.isOk()) {
        log_debug ("sa: NUT device %s is not ready", _nutMaster.c_str());
        return;
    }
    try {
        std::string prefix = nutPrefix();
        const int prefixId = nutIndex();
        log_debug ("sa: prefix='%s' prefixId='%d'", prefix.c_str(), prefixId);

        // Translate NUT keys into 42ity keys.
        {
            std::map<std::string,std::vector<std::string>> deviceVars = nutDevice.getVariableValues();
            fty::nut::KeyValues scalarVars;
            for (auto var : deviceVars) {
              scalarVars.emplace(var.first, collapse_commas(var.second));
            }
            _inventory = fty::nut::performMapping(mapping, scalarVars, prefixId);
        }

        try {
            // Check for actual sensor presence, if ambient.present is available!
            auto sensorPresent = nutDevice.getVariableValue (prefix + "present");
            log_debug ("sa: sensor '%s' presence: '%s'", prefix.c_str(), sensorPresent[0].c_str());
            if ((!sensorPresent.empty ()) && (sensorPresent[0] != "yes")) {
                log_debug ("sa: sensor '%s' is not present or disconnected on NUT device %s", prefix.c_str(), _nutMaster.c_str());
                return;
            }
        } catch (...) {}

        log_debug ("sa: getting %stemperature from %s", prefix.c_str(), _nutMaster.c_str());
        auto temperature = nutDevice.getVariableValue (prefix + "temperature");
        if (temperature.empty ()) {
            log_debug ("sa: %stemperature on %s is not present", prefix.c_str(), location().c_str ());
        } else {
            _temperature =  temperature[0];
            log_debug ("sa: %stemperature on %s is %s", prefix.c_str (), location().c_str (), _temperature.c_str());
        }

        log_debug ("sa: getting %shumidity from %s", prefix.c_str(), _nutMaster.c_str());
        auto humidity = nutDevice.getVariableValue (prefix + "humidity");
        if (humidity.empty ()) {
            log_debug ("sa: %shumidity on %s is not present", prefix.c_str(), location().c_str ());
        } else {
            _humidity =  humidity[0];
            log_debug ("sa: %shumidity on %s is %s", prefix.c_str (), location().c_str (), _humidity.c_str());
        }

        _contacts.clear();

        for (int i = 1 ; i <= 2 ; i++) {
            std::string baseVar = prefix + "contacts." + std::to_string(i);
            std::string state = nutDevice.getVariableValue (baseVar + ".status")[0];
            if (state != "unknown" && state != "bad") {
                // process new status style (active / inactive), found on EMP002
                // WRT the polarity configured
                if (state == "active" || state == "inactive") {
                    std::string contactConfig = nutDevice.getVariableValue (baseVar + ".config")[0];
                    if (!contactConfig.empty()) {
                        if (contactConfig == "normal-opened") {
                            if (state == "active")
                                state = "closed";
                            else
                                state = "opened";
                        }
                        else {
                            if (state == "active")
                                state = "opened";
                            else
                                state = "closed";
                        }
                    }
                    else {
                        // FIXME: what to do here? break or?
                        log_debug ("sa: new style dry-contact status, but missing config");
                    }
                }
                _contacts.push_back (state);
                log_debug ("sa: %scontact.%i.status state %s", prefix.c_str (), i, state.c_str ());
            }
            else {
                log_debug ("sa: %scontact.%i.status state '%s' not supported and discarded", prefix.c_str (), i, state.c_str ());
            }
        }
    } catch (...) {}
}

std::string Sensor::topicSuffix () const
{
    return "." + port() + "@" + location();
}

// topic for GPI sensors wired to EMP001
std::string Sensor::topicSuffixExternal (const std::string& gpiPort) const
{
    // status.GPI<port>.<epmPort>@location
    return ".GPI" + gpiPort + "." + port() + "@" + location();
}

void Sensor::publish (mlm_client_t *client, int ttl)
{
    log_debug ("sa: publishing temperature '%s' and humidity '%s' on '%s' from sensor '%s'",
               _temperature.c_str(), _humidity.c_str(), location().c_str(), assetName().c_str());

    if (! _temperature.empty()) {
        zhash_t *aux = zhash_new ();
        zhash_autofree (aux);
        zhash_insert (aux, "port", (void*) port().c_str());
        zhash_insert (aux, "sname", (void *) assetName().c_str ());
        zmsg_t *msg = fty_proto_encode_metric (
            aux,
            time (NULL),
            ttl,
            ("temperature." + port ()).c_str (),
            location().c_str (),
            _temperature.c_str (),
            "C");
        zhash_destroy (&aux);
        if (msg) {
            std::string topic = "temperature" + topicSuffix();
            log_debug ("sending new temperature for element_src = '%s', value = '%s' on topic '%s'",
                       location().c_str (), _temperature.c_str (), topic.c_str());
            int r = mlm_client_send (client, topic.c_str (), &msg);
            if( r != 0 ) log_error("failed to send measurement %s result %" PRIi32, topic.c_str(), r);
            zmsg_destroy (&msg);
        }
    }
    if (!_humidity.empty ()) {
        zhash_t *aux = zhash_new ();
        zhash_autofree (aux);
        zhash_insert (aux, "port", (void*) port().c_str());
        zhash_insert (aux, "sname", (void *) assetName().c_str ());
        zmsg_t *msg = fty_proto_encode_metric (
            aux,
            time (NULL),
            ttl,
            ("humidity." + port ()).c_str (),
            location().c_str (),
            _humidity.c_str (),
            "%");
        zhash_destroy (&aux);
        if (msg) {
            std::string topic = "humidity" + topicSuffix();
            log_debug ("sending new humidity for element_src = '%s', value = '%s' on topic '%s'",
                       location().c_str (), _humidity.c_str (), topic.c_str());
            int r = mlm_client_send (client, topic.c_str (), &msg);
            if( r != 0 ) log_error("failed to send measurement %s result %" PRIi32, topic.c_str(), r);
            zmsg_destroy (&msg);
        }
    }

    if (!_contacts.empty ())
    {
        int gpiPort = 1;
        for (auto &contact : _contacts)
        {
            std::string extport = std::to_string(gpiPort);
            auto search  = _children.find (extport);

            if (search != _children.end())
            {
                std::string sname = search->second;

                zhash_t *aux = zhash_new ();
                zhash_autofree (aux);
                zhash_insert (aux, "port", (void*) port().c_str ());
                zhash_insert (aux, "ext-port", (void *) extport.c_str ());
                zhash_insert (aux, "sname", (void *) sname.c_str ()); // sname of the child sensor if any
                zmsg_t *msg = fty_proto_encode_metric (
                    aux,
                    ::time (NULL),
                    ttl,
                    ("status.GPI" + std::to_string (gpiPort) + "." + port ()).c_str (),
                    location().c_str (),
                    contact.c_str (),
                    "");
                zhash_destroy (&aux);

                if (msg) {
                    std::string topic = "status" + topicSuffixExternal (std::to_string (gpiPort));
                    log_debug ("sending new contact status information for element_src = '%s', value = '%s'. GPI '%s' on port '%s'.",
                               location().c_str (), contact.c_str (), sname.c_str (), extport.c_str ());
                    int r = mlm_client_send (client, topic.c_str (), &msg);
                    if( r != 0 )
                        log_error("failed to send measurement %s result %" PRIi32, topic.c_str(), r);
                    zmsg_destroy (&msg);
                }
            }
            else {
                log_debug ("I did not find any child for %s on port %s", assetName().c_str (), extport.c_str ());
            }
            ++gpiPort;
        }
    }
}

std::string Sensor::sensorPrefix() const
{
    std::string prefix;
    if (chain() != 0)
        prefix = "device." + std::to_string(chain()) + ".";
    prefix += "ambient.";
    if (_asset && !_asset->port().empty()) {
        prefix += _asset->port() + ".";
    }
    return prefix;
}

std::string Sensor::nutPrefix() const
{
    std::string prefix;
    if (chain() != 0) {
        if (_index == 0) prefix = "device." + std::to_string(chain()) + ".";
        else prefix = "device.1.";
    }
    prefix += "ambient.";
    // Only add port index when different than 0
    if (_index != 0) {
        prefix += std::to_string(_index) + ".";
    }
    return prefix;
}

int Sensor::nutIndex() const
{
    if (_index != 0) return _index;
    else if (chain() != 0)  return chain();
    return 0;
}

void Sensor::addChild (const std::string& child_port, const std::string& child_name)
{
    _children.emplace (child_port, child_name);
}

std::map <std::string, std::string>
Sensor::getChildren ()
{
    return _children;
}

void
sensor_device_test(bool verbose)
{
    printf (" * sensor_device: ");

    //  @selftest
    // epdu master
    fty_proto_t *proto = fty_proto_new(FTY_PROTO_ASSET);
    assert(proto);
    fty_proto_set_name(proto, "epdu_m");
    fty_proto_set_operation(proto, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert(proto, "type", "device");
    fty_proto_aux_insert(proto, "subtype", "epdu");
    fty_proto_aux_insert(proto, "parent_name.1", "ups");
    fty_proto_ext_insert(proto, "daisy_chain", "1");
    AssetState::Asset epdu_m(proto);
    fty_proto_destroy(&proto);
    // epdu slave #1
    proto = fty_proto_new(FTY_PROTO_ASSET);
    assert(proto);
    fty_proto_set_name(proto, "epdu_1");
    fty_proto_set_operation(proto, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert(proto, "type", "device");
    fty_proto_aux_insert(proto, "subtype", "epdu");
    fty_proto_aux_insert(proto, "parent_name.1", "ups");
    fty_proto_ext_insert(proto, "daisy_chain", "2");
    AssetState::Asset epdu_1(proto);
    fty_proto_destroy(&proto);

    std::map <std::string, std::string> children;

    // sensor emp01 connected to standalone ups
    proto = fty_proto_new(FTY_PROTO_ASSET);
    assert(proto);
    fty_proto_set_name(proto, "a");
    fty_proto_set_operation(proto, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert(proto, "type", "device");
    fty_proto_aux_insert(proto, "subtype", "sensor");
    fty_proto_aux_insert(proto, "parent_name.1", "ups");
    AssetState::Asset asset_a(proto);
    fty_proto_destroy(&proto);
    Sensor a(&asset_a, nullptr, children);
    assert (a.sensorPrefix() == "ambient.");
    assert (a.topicSuffix() == ".0@ups");
    assert (a.nutPrefix() == "ambient.");
    assert (a.nutIndex() == 0);

    // sensor emp02 connected to standalone ups
    proto = fty_proto_new(FTY_PROTO_ASSET);
    assert(proto);
    fty_proto_set_name(proto, "b");
    fty_proto_set_operation(proto, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert(proto, "type", "device");
    fty_proto_aux_insert(proto, "subtype", "sensor");
    fty_proto_aux_insert(proto, "parent_name.1", "ups");
    fty_proto_ext_insert(proto, "port", "2");
    fty_proto_ext_insert(proto, "endpoint.1.sub_address", "1");
    AssetState::Asset asset_b(proto);
    fty_proto_destroy(&proto);
    Sensor b(&asset_b, nullptr, children, "ups", 2);
    assert (b.sensorPrefix() == "ambient.2.");
    assert (b.topicSuffix() == ".2@ups");
    assert (b.nutPrefix() == "ambient.2.");
    assert (b.nutIndex() == 2);
    assert (b.subAddress() == "1");

    // sensor emp01 connected to daisy-chain host
    proto = fty_proto_new(FTY_PROTO_ASSET);
    assert(proto);
    fty_proto_set_name(proto, "c");
    fty_proto_set_operation(proto, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert(proto, "type", "device");
    fty_proto_aux_insert(proto, "subtype", "sensor");
    fty_proto_aux_insert(proto, "parent_name.1", "epdu_m");
    fty_proto_ext_insert(proto, "endpoint.1.sub_address", "2");
    AssetState::Asset asset_c(proto);
    fty_proto_destroy(&proto);
    Sensor c(&asset_c, &epdu_m, children, "epdu_m", 0);
    assert (c.sensorPrefix() == "device.1.ambient.");
    assert (c.topicSuffix() == ".0@epdu_m");
    assert (c.nutPrefix() == "device.1.ambient.");
    assert (c.nutIndex() == 1);
    assert (c.subAddress() == "2");

    // sensor emp01 connected to daisy-chain device 1
    proto = fty_proto_new(FTY_PROTO_ASSET);
    assert(proto);
    fty_proto_set_name(proto, "d");
    fty_proto_set_operation(proto, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert(proto, "type", "device");
    fty_proto_aux_insert(proto, "subtype", "sensor");
    fty_proto_aux_insert(proto, "parent_name.1", "epdu_1");
    fty_proto_ext_insert(proto, "endpoint.1.sub_address", "2");
    AssetState::Asset asset_d(proto);
    fty_proto_destroy(&proto);
    Sensor d(&asset_d, &epdu_1, children, "epdu_m", 0);
    assert (d.sensorPrefix() == "device.2.ambient.");
    assert (d.topicSuffix() == ".0@epdu_1");
    assert (d.nutPrefix() == "device.2.ambient.");
    assert (d.nutIndex() == 2);
    assert (d.subAddress() == "2");

    // sensor emp02 connected to daisy-chain master
    proto = fty_proto_new(FTY_PROTO_ASSET);
    assert(proto);
    fty_proto_set_name(proto, "e");
    fty_proto_set_operation(proto, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert(proto, "type", "device");
    fty_proto_aux_insert(proto, "subtype", "sensor");
    fty_proto_aux_insert(proto, "parent_name.1", "epdu_m");
    fty_proto_ext_insert(proto, "port", "3");
    fty_proto_ext_insert(proto, "endpoint.1.sub_address", "8");
    AssetState::Asset asset_e(proto);
    fty_proto_destroy(&proto);
    Sensor e(&asset_e, &epdu_m, children, "epdu_m", 3);
    assert (e.sensorPrefix() == "device.1.ambient.3.");
    assert (e.topicSuffix() == ".3@epdu_m");
    assert (e.nutPrefix() == "device.1.ambient.3.");
    assert (e.nutIndex() == 3);
    assert (e.subAddress() == "8");

    // sensor emp02 connected to daisy-chain device 1
    proto = fty_proto_new(FTY_PROTO_ASSET);
    assert(proto);
    fty_proto_set_name(proto, "f");
    fty_proto_set_operation(proto, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert(proto, "type", "device");
    fty_proto_aux_insert(proto, "subtype", "sensor");
    fty_proto_aux_insert(proto, "parent_name.1", "epdu_1");
    fty_proto_ext_insert(proto, "port", "5");
    fty_proto_ext_insert(proto, "endpoint.1.sub_address", "12");
    AssetState::Asset asset_f(proto);
    fty_proto_destroy(&proto);
    Sensor f(&asset_f, &epdu_1, children, "epdu_m", 5);
    assert (f.sensorPrefix() == "device.2.ambient.5.");
    assert (f.topicSuffix() == ".5@epdu_1");
    assert (f.nutPrefix() == "device.1.ambient.5.");
    assert (f.nutIndex() == 5);
    assert (f.subAddress() == "12");

    //  @end
    printf (" OK\n");
}
