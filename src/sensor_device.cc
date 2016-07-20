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
#include "agent_nut_library.h"
#include "sensor_device.h"
#include "logger.h"

void Sensor::update (nut::TcpClient &conn)
{
    auto nutDevice = conn.getDevice(_nutName);
    if (! nutDevice.isOk()) return;
    try {
        std::string prefix = sensorPrefix();
        auto temperature = nutDevice.getVariableValue (prefix + ".temperature");
        if (temperature.empty ()) {
            log_debug ("sa: %s.temperature on %s is not present", prefix.c_str(), _nutName.c_str ());
        } else {
            std::string _temperature =  temperature[0];
            log_debug ("sa: %s.temperature on %s is %s", prefix.c_str (), _nutName.c_str (), _temperature.c_str());
        }
        auto humidity = nutDevice.getVariableValue (prefix + ".humidity");
        if (humidity.empty ()) {
            log_debug ("sa: %s.humidity on %s is not present", prefix.c_str(), _nutName.c_str ());
        } else {
            std::string _humidity =  humidity[0];
            log_debug ("sa: %s.humidity on %s is %s", prefix.c_str (), _nutName.c_str (), _humidity.c_str());
        }
    } catch (...) {}
}

void Sensor::publish (mlm_client_t *client)
{
    zmsg_t *msg = bios_proto_encode_metric (
        NULL,
        "temperature",
        _nutName.c_str (),
        _temperature.c_str (),
        "C",
        _ttl);
    if (msg) {
        std::string subject = "temperature@" + _nutName; // TODO: correct subject
        log_debug ("sending new temperature for element_src = '%s', value = '%s'",
                   _nutName.c_str (), _temperature.c_str ());
        int r = mlm_client_send (client, subject.c_str (), &msg);
        if( r != 0 ) log_error("failed to send measurement %s result %" PRIi32, subject.c_str(), r);
        zmsg_destroy (&msg);
    }

    msg = bios_proto_encode_metric (
        NULL,
        "humidity",
        _nutName.c_str (),
        _humidity.c_str (),
        "C",
        _ttl);
    if (msg) {
        std::string subject = "humidity@" + _nutName; // TODO: correct subject
        log_debug ("sending new humidity for element_src = '%s', value = '%s'",
                   _nutName.c_str (), _humidity.c_str ());
        int r = mlm_client_send (client, subject.c_str (), &msg);
        if( r != 0 ) log_error("failed to send measurement %s result %" PRIi32, subject.c_str(), r);
        zmsg_destroy (&msg);
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

void
sensor_device_test(bool verbose)
{
    printf (" * sensor device: ");
    //  @selftest
    //  @end
    printf (" OK\n");
}
