/*  =========================================================================
    nut_agent - NUT daemon wrapper

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

#include "nut_agent.h"
#include "ups_status.h"
#include "ups_alarm.h"
#include <fty_log.h>
#include <fty_shm.h>
#include <string>

// clang-format off
const std::map<std::string, std::string> NUTAgent::_unitNameToSymbol =
{
    { "temperature", "C" },
    { "realpower",   "W" },
    { "voltage",     "V" },
    { "current",     "A" },
    { "load",        "%" },
    { "charge",      "%" },
    { "frequency",   "Hz"},
    { "power",       "VA" },
    { "runtime",     "s" },
    { "timer",       "s" },
    { "delay",       "s" },
};
// clang-format on

NUTAgent::NUTAgent(StateManager::Reader* reader)
    : _state_reader(reader)
{
}

bool NUTAgent::loadMapping(const char* path_to_file)
{
    if (!path_to_file)
        return false;
    _conf = path_to_file;
    _deviceList.load_mapping(_conf.c_str());
    return _deviceList.mappingLoaded();
}

bool NUTAgent::isMappingLoaded() const
{
    return _deviceList.mappingLoaded();
}

void NUTAgent::setClient(mlm_client_t* client)
{
    if (!_client) {
        _client = client;
    }
}
void NUTAgent::setiClient(mlm_client_t* client)
{
    if (!_iclient) {
        _iclient = client;
    }
}

void NUTAgent::onPoll()
{
    if (_client)
        advertisePhysics();
    if (_iclient)
        advertiseInventory();
}

void NUTAgent::updateDeviceList()
{
    if (_state_reader->refresh())
        _deviceList.updateDeviceList(_state_reader->getState());
}

int NUTAgent::send(const std::string& subject, zmsg_t** message_p)
{
    if (zsys_interrupted) return -100;

    fty_proto_t* m_decoded = fty_proto_decode(message_p);
    zmsg_destroy(message_p);
    *message_p = fty_proto_encode(&m_decoded);
    fty_proto_destroy(&m_decoded); // secure if encode failed

    int rv = mlm_client_send(_client, subject.c_str(), message_p);
    if (rv == -1) {
        log_error("mlm_client_send (subject = '%s') failed", subject.c_str());
    }
    zmsg_destroy(message_p);
    return rv;
}

// MVY: a hack for inventory messages
int NUTAgent::isend(const std::string& subject, zmsg_t** message_p)
{
    if (zsys_interrupted) return -100;

    fty_proto_t* m_decoded = fty_proto_decode(message_p);
    zmsg_destroy(message_p);
    *message_p = fty_proto_encode(&m_decoded);
    fty_proto_destroy(&m_decoded); // secure if encode failed

    int rv = mlm_client_send(_iclient, subject.c_str(), message_p);
    if (rv == -1) {
        log_error("mlm_client_send (subject = '%s') failed", subject.c_str());
    }
    zmsg_destroy(message_p);
    return rv;
}

std::string NUTAgent::physicalQuantityShortName(const std::string& longName) const
{
    size_t i = longName.find('.');
    if (i == std::string::npos) {
        return longName;
    }
    return longName.substr(0, i);
}

std::string NUTAgent::physicalQuantityToUnits(const std::string& quantity) const
{
    auto it = _unitNameToSymbol.find(quantity);
    if (it == _unitNameToSymbol.end()) {
        return "";
    }
    return it->second;
}

void NUTAgent::advertisePhysics()
{
    _deviceList.update(true);
    for (auto& device : _deviceList) {
        if (zsys_interrupted) break;

        const std::string assetName{device.second.assetName()};

        //map<str::quantity,str::value>
        auto measurements = device.second.physics(false); // take NOT only changed

#if 0 // DBG, display <quantity, value> pairs owned by the asset
        log_debug("### advertisePhysics, measurements for %s:", assetName.c_str());
        for (const auto& measurement : measurements) {
            log_debug("### \t%s: '%s'", measurement.first.c_str(), measurement.second.c_str());
        }
#endif

        for (const auto& measurement : measurements) {
            const std::string quantity{measurement.first}; // or property
            const std::string value{measurement.second};
            std::string type{physicalQuantityShortName(quantity)};
            std::string units{physicalQuantityToUnits(type)};

            int r = fty::shm::write_metric(assetName, quantity, value, units, _ttl);
            if (r != 0)
                log_error("failed to send measurement %s@%s", quantity.c_str(), assetName.c_str());
            device.second.setChanged(quantity, false);
        }

        // 'load' computing
        // BIOS-1185 start
        // if it is epdu, that doesn't provide load.default,
        // but it is still could be calculated (because input.current is known) then do this
        if (device.second.subtype() == "epdu" && measurements.count("load.default") == 0) {
            if (measurements.count("load.input.L1") != 0) {
                std::string value = measurements.at("load.input.L1");
                int r = fty::shm::write_metric(assetName, "load.default", value, "%", _ttl);
                if (r != 0)
                    log_error("failed to write load.default@%s, result %i", assetName.c_str(), r);
            }
            else if (measurements.count("current.input.L1") != 0) { // it is a mapped value!!!!!!!!!!!
                // try to compute it
                // 1. Determine the MAX value
                double max_value = std::nan("");
                if (measurements.count("current.input.nominal") == 1) {
                    try {
                        max_value = std::stod(measurements.at("current.input.nominal"));
                        log_debug("load.default: max_value %lf from UPS", max_value);
                    }
                    catch (...) {
                    }
                }
                else {
                    max_value = device.second.maxCurrent();
                    log_debug("load.default: max_value %lf from user", max_value);
                }
                // 2. if MAX value is known -> do work, otherwise skip
                if (!std::isnan(max_value)) {
                    double value = 0;
                    try {
                        value = std::stod(measurements.at("current.input.L1"));
                    } catch (...) {
                    };
                    // 3. compute a real value
                    char buffer[50];
                    snprintf(buffer, sizeof(buffer), "%lf", value * 100 / max_value); // because it is %!!!!
                    // 4. form message
                    // 5. send the messsage
                    int r = fty::shm::write_metric(assetName, "load.default", buffer, "%", _ttl);
                    if (r != 0) {
                        log_error("failed to write load.default@%s, result %i", assetName.c_str(), r);
                    }
                }
            }
        }

        // expose alarms as a bitsfield
        bool has_alarms = false;
        if (device.second.hasProperty("ups.alarm")) {
            const std::string alarms = device.second.property("ups.alarm");
            uint32_t bitsfield = upsalarm_to_int(alarms);
            int r = fty::shm::write_metric(assetName, "ups.alarm", std::to_string(bitsfield), "", _ttl);
            if (r != 0) {
                log_error("failed to write ups.alarm@%s, result %i", assetName.c_str(), r);
            }
            device.second.setChanged("ups.alarm", false);

            has_alarms = (bitsfield != 0);
            if (has_alarms) {
                log_debug("ups.alarm@%s (%u, '%s')", assetName.c_str(), bitsfield, alarms.c_str());
            }
        }

        // expose status and "in progress" test result as a bitsfield
        if (device.second.hasProperty("status.ups")) {
            std::string status_s = device.second.property("status.ups");
            if (!status_s.empty() // fix IPMVAL-1889 (empty on data-stale)
                && status_s != "WAIT"  // fix when waiting for the driver response to dumpcmd
                && device.second.subtype() != "epdu") // ups.status doesn't make sense for epdu
            {

                std::string test_s = (device.second.hasProperty("ups.test.result")
                    ? device.second.property("ups.test.result")
                    : "no test initiated");

                uint16_t status_i = upsstatus_to_int(status_s, test_s);
                if (has_alarms) {
                    status_i |= STATUS_ALARM;
                }
                // hotfix IPMVAL-1889 (status.ups and data-stale) > increase ttl from 60 to 90 sec.
                // _ttl is 60
                //    - see cfg file "nut/polling_interval = 30"
                //    - see ttl computation (2*polling_interval) in actor_commands.cc cmd=ACTION_POLLING
                // here we increase _ttl of 50%, to pass metric ttl to 90
                int r = fty::shm::write_metric(assetName, "status.ups", std::to_string(status_i), " ", _ttl * 3 / 2);
                if (r != 0) {
                    log_error("failed to write status.ups@%s, result %i", assetName.c_str(), r);
                }

                // publish power.status (same ttl policy)
                r = fty::shm::write_metric(assetName, "power.status", power_status(status_i), " ", _ttl * 3 / 2);
                if (r != 0) {
                    log_error("failed to write power.status@%s, result %i", assetName.c_str(), r);
                }

                device.second.setChanged("status.ups", false);
            }
        }

        // expose epdu outlet status as a bitsfield
        for (int i = 1; i < 100; i++) {
            std::string property = "status.outlet." + std::to_string(i);
            // assumption, if outlet.10 does not exists, outlet.11 does not as well
            if (!device.second.hasProperty(property))
                break;
            std::string status_s = device.second.property(property);
            uint16_t    status_i = status_s == "on" ? 42 : 0;

            int r = fty::shm::write_metric(assetName, property, std::to_string(status_i), " ", _ttl);
            if (r != 0)
                log_error("failed to write %s@%s, result %i", property.c_str(), assetName.c_str(), r);

            device.second.setChanged(property, false);
        }
    }
}

void NUTAgent::advertiseInventory()
{
    bool advertiseAll = false;
    if (_inventoryTimestamp_ms + NUT_INVENTORY_REPEAT_AFTER_MS < static_cast<uint64_t>(zclock_mono())) {
        advertiseAll           = true;
        _inventoryTimestamp_ms = static_cast<uint64_t>(zclock_mono());
    }

    for (auto& device : _deviceList) {
        if (zsys_interrupted) break;

        const std::string assetName{device.second.assetName()};

        zhash_t* inventory = zhash_new();
        zhash_autofree(inventory);

        // !advertiseAll = advertise_Not_OnlyChanged
        //std::string log; //dbg
        for (auto& item : device.second.inventory(!advertiseAll)) {
            if (item.first == "status.ups") {
                // this value is not advertised as inventory information
                continue;
            }
            zhash_insert(inventory, item.first.c_str(), const_cast<char*>(item.second.c_str()));
            //log += item.first + " = \"" + item.second + "\"; ";
            device.second.setChanged(item.first, false);
        }

        if (zhash_size(inventory) == 0) {
            zhash_destroy(&inventory);
            continue;
        }

        zmsg_t* message = fty_proto_encode_asset(NULL, assetName.c_str(), "inventory", inventory);

        if (message) {
            std::string topic = "inventory@" + assetName;
            //log_debug("new inventory message '%s': %s", topic.c_str(), log.c_str());
            log_debug("new inventory message '%s'", topic.c_str());
            int r = isend(topic, &message);
            if (r != 0)
                log_error("failed to send inventory %s result %i", topic.c_str(), r);
            zmsg_destroy(&message);
        }
        zhash_destroy(&inventory);
    }
}
