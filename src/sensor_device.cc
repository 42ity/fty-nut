/*  =========================================================================
    sensor_sensor - structure for device producing alerts

    Copyright (C) 2014 - 2016 Eaton

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
#include "fty_nut_library.h"
#include "sensor_device.h"
#include "logger.h"
#include <vector>
#include <string>

void Sensor::update (nut::TcpClient &conn)
{
    log_debug ("sa: updating temperature and humidity from NUT device %s", _nutMaster.c_str());
    auto nutDevice = conn.getDevice(_nutMaster);
    if (! nutDevice.isOk()) {
        log_debug ("sa: NUT device %s is not ready", _nutMaster.c_str());
        return;
    }
    try {
        std::string prefix = nutPrefix();
        log_debug ("sa: getting %stemperature from %s", prefix.c_str(), _nutMaster.c_str());
        auto temperature = nutDevice.getVariableValue (prefix + "temperature");
        if (temperature.empty ()) {
            log_debug ("sa: %stemperature on %s is not present", prefix.c_str(), _location.c_str ());
        } else {
            _temperature =  temperature[0];
            log_debug ("sa: %stemperature on %s is %s", prefix.c_str (), _location.c_str (), _temperature.c_str());
        }

        log_debug ("sa: getting %shumidity from %s", prefix.c_str(), _nutMaster.c_str());
        auto humidity = nutDevice.getVariableValue (prefix + "humidity");
        if (humidity.empty ()) {
            log_debug ("sa: %shumidity on %s is not present", prefix.c_str(), _location.c_str ());
        } else {
            _humidity =  humidity[0];
            log_debug ("sa: %shumidity on %s is %s", prefix.c_str (), _location.c_str (), _humidity.c_str());

        }

        std::string state = nutDevice.getVariableValue (prefix + "contacts.1.status")[0];
        if (state != "unknown" && state != "bad")
            _contacts.push_back (state);
        else
            zsys_debug ("sa: %scontact.1.status state %s", prefix.c_str (), state.c_str ());

        state = nutDevice.getVariableValue (prefix + "contacts.2.status")[0];
        if (state != "unknown" && state != "bad")
            _contacts.push_back (state);
        else
            zsys_debug ("sa: %scontact.2.status state %s", prefix.c_str (), state.c_str ());

        log_debug ("sa: %scontact.status on %s: contact.1 %s, contact.2 %s",
                   prefix.c_str (),
                   _location.c_str (),
                   nutDevice.getVariableValue (prefix + "contacts.1.status")[0].c_str (),
                   nutDevice.getVariableValue (prefix + "contacts.2.status")[0].c_str ()
        );

    } catch (...) {}
}

std::string Sensor::topicSuffix () const
{
    return "." + port() + "@" + _location;
}

// topic for GPI sensors wired to EMP001
std::string Sensor::topicSuffixExternal (std::string gpiPort) const
{
    // status.GPI<port>.<epmPort>@location
    return ".GPI" + gpiPort + "." + port() + "@" + _location;
}

void Sensor::publish (mlm_client_t *client, int ttl)
{
    log_debug ("sa: publishing temperature '%s' and humidity '%s' on '%s'",
               _temperature.c_str(), _humidity.c_str(),  _location.c_str());

    if (! _temperature.empty()) {
        zhash_t *aux = zhash_new ();
        zhash_autofree (aux);
        zhash_insert (aux, "port", (void*) port().c_str());
        zhash_insert (aux, "sname", (void *) _sname.c_str ());
        zmsg_t *msg = fty_proto_encode_metric (
            aux,
            time (NULL),
            ttl,
            ("temperature." + port ()).c_str (),
            _location.c_str (),
            _temperature.c_str (),
            "C");
        zhash_destroy (&aux);
        if (msg) {
            std::string topic = "temperature" + topicSuffix();
            log_debug ("sending new temperature for element_src = '%s', value = '%s'",
                       _location.c_str (), _temperature.c_str ());
            int r = mlm_client_send (client, topic.c_str (), &msg);
            if( r != 0 ) log_error("failed to send measurement %s result %" PRIi32, topic.c_str(), r);
            zmsg_destroy (&msg);
        }
    }
    if (!_humidity.empty ()) {
        zhash_t *aux = zhash_new ();
        zhash_autofree (aux);
        zhash_insert (aux, "port", (void*) port().c_str());
        zhash_insert (aux, "sname", (void *) _sname.c_str ());
        zmsg_t *msg = fty_proto_encode_metric (
            aux,
            time (NULL),
            ttl,
            ("humidity." + port ()).c_str (),
            _location.c_str (),
            _humidity.c_str (),
            "%");
        zhash_destroy (&aux);
        if (msg) {
            std::string topic = "humidity" + topicSuffix();
            log_debug ("sending new humidity for element_src = '%s', value = '%s'",
                       _location.c_str (), _humidity.c_str ());
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
                    _location.c_str (),
                    contact.c_str (),
                    "");
                zhash_destroy (&aux);

                if (msg) {
                    std::string topic = "status" + topicSuffixExternal (std::to_string (gpiPort));
                    log_debug ("sending new contact status information for element_src = '%s', value = '%s'",
                               _location.c_str (), contact.c_str ());
                    int r = mlm_client_send (client, topic.c_str (), &msg);
                    if( r != 0 )
                        log_error("failed to send measurement %s result %" PRIi32, topic.c_str(), r);
                    zmsg_destroy (&msg);
                }
            }
            else
                zsys_debug ("I did not find any child for %s in port %s", _sname.c_str (), extport.c_str ());
            ++gpiPort;
        }
    }
}

std::string Sensor::sensorPrefix() const
{
    std::string prefix;
    if (_chain != 0)  prefix = "device." + std::to_string(_chain) + ".";
    prefix += "ambient.";
    if (! _port.empty ()) {
        prefix += _port + ".";
    }
    return prefix;
}

std::string Sensor::nutPrefix() const
{
    std::string prefix;
    if (_chain != 0)  prefix = "device." + std::to_string(_chain) + ".";
    prefix += "ambient.";
    if (! _port.empty () && _port != "0") {
        prefix += _port + ".";
    }
    return prefix;
}

std::string Sensor::port() const
{
    if (_port.empty()) return "0";
    return _port;
}

void Sensor::addChild (char *child_port, char *child_name)
{
    if (child_name && child_port)
    {
        _children.emplace (child_port, child_name);
    }
}

void
sensor_device_test(bool verbose)
{
    printf (" * sensor_device: ");
    //  @selftest
    // sensor connected to stanalone ups
    std::map <std::string, std::string> children;
    Sensor a("ups", 0, "ups", "", children, "");
    assert (a.sensorPrefix() == "ambient.");
    assert (a.topicSuffix() == ".0@ups");

    // sensor 2 connected to stanalone ups
    Sensor b("ups", 0, "ups", "2", children,"");
    assert (b.sensorPrefix() == "ambient.2.");
    assert (b.topicSuffix() == ".2@ups");

    // sensor connected to daisy-chain master
    Sensor c("ups", 1, "ups", "", children, "");
    assert (c.sensorPrefix() == "device.1.ambient.");
    assert (c.topicSuffix() == ".0@ups");

    // sensor 3 connected to daisy-chain slave 2
    Sensor d("ups", 2, "ups2", "3", children, "");
    assert (d.sensorPrefix() == "device.2.ambient.3.");
    assert (d.topicSuffix() == ".3@ups2");

    //  @end
    printf (" OK\n");
}
