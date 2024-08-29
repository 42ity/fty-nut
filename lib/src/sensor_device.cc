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
#include <fty_common_nut.h>
#include <fty_log.h>
#include <fty_proto.h>
#include <fty_shm.h>
#include <string>
#include <vector>

static std::string collapse_commas(const std::vector<std::string>& values)
{
    std::string result;
    for (auto& value : values) {
        result += (result.empty() ? "" : ", ") + value;
    }
    return result;
}

void Sensor::update(nut::TcpClient& conn, const std::map<std::string, std::string>& mapping)
{
    log_debug("sa: updating sensor(s) temperature and humidity from NUT device %s", _nutMaster.c_str());
    auto nutDevice = conn.getDevice(_nutMaster);
    if (!nutDevice.isOk()) {
        log_debug("sa: NUT device %s is not ready", _nutMaster.c_str());
        return;
    }

    try {
        std::string prefix   = nutPrefix();
        const int   prefixId = nutIndex();
        log_debug("sa: prefix='%s' prefixId='%d'", prefix.c_str(), prefixId);

        // Translate NUT keys into 42ity keys.
        {
            std::map<std::string, std::vector<std::string>> deviceVars = nutDevice.getVariableValues();
            fty::nut::KeyValues                             scalarVars;
            for (auto var : deviceVars) {
                scalarVars.emplace(var.first, collapse_commas(var.second));
            }
            _inventory = fty::nut::performMapping(mapping, scalarVars, prefixId);
        }

        // Handle asset precedence
        {
            // maintain friendlyName from asset
            auto it = _inventory.find("name");
            if (it != _inventory.cend() && _asset && !_asset->friendlyName().empty()) {
                it->second = _asset->friendlyName();
            }
            // maintain model from asset (assume sensor model can't be changed on device)
            it = _inventory.find("model");
            if (it != _inventory.cend() && _asset && !_asset->model().empty()) {
                it->second = _asset->model();
            }
        }

        try {
            // Check for actual sensor presence, if ambient.present is available!
            auto sensorPresent = nutDevice.getVariableValue(prefix + "present");
            log_debug("sa: sensor '%s' present: '%s'", prefix.c_str(), sensorPresent[0].c_str());
            if ((!sensorPresent.empty()) && (sensorPresent[0] != "yes")) {
                log_debug("sa: sensor '%s' is not present or disconnected on NUT device %s", prefix.c_str(),
                    _nutMaster.c_str());
                return;
            }
        } catch (...) {
        }

        log_debug("sa: getting %stemperature from %s", prefix.c_str(), _nutMaster.c_str());
        auto temperature = nutDevice.getVariableValue(prefix + "temperature");
        if (temperature.empty()) {
            log_debug("sa: %stemperature on %s is not present", prefix.c_str(), location().c_str());
        } else {
            _temperature = temperature[0];
            log_debug("sa: %stemperature on %s is %s", prefix.c_str(), location().c_str(), _temperature.c_str());
        }

        log_debug("sa: getting %shumidity from %s", prefix.c_str(), _nutMaster.c_str());
        auto humidity = nutDevice.getVariableValue(prefix + "humidity");
        if (humidity.empty()) {
            log_debug("sa: %shumidity on %s is not present", prefix.c_str(), location().c_str());
        } else {
            _humidity = humidity[0];
            log_debug("sa: %shumidity on %s is %s", prefix.c_str(), location().c_str(), _humidity.c_str());
        }

        _contacts.clear();

        for (int i = 1; i <= 2; i++) {
            std::string baseVar = prefix + "contacts." + std::to_string(i);
            std::string state   = nutDevice.getVariableValue(baseVar + ".status")[0];
            if (state != "unknown" && state != "bad") {
                // process new status style (active / inactive), found on EMP002
                // WRT the polarity configured
                if (state == "active" || state == "inactive") {
                    std::string contactConfig = nutDevice.getVariableValue(baseVar + ".config")[0];
                    if (!contactConfig.empty()) {
                        if (contactConfig == "normal-opened") {
                            if (state == "active")
                                state = "closed";
                            else
                                state = "opened";
                        } else {
                            if (state == "active")
                                state = "opened";
                            else
                                state = "closed";
                        }
                    } else {
                        // FIXME: what to do here? break or?
                        log_debug("sa: new style dry-contact status, but missing config");
                    }
                }
                // workaround for EMP01: state is "open" or "closed"
                else if (state == "open") {
                    state = "opened";
                }
                _contacts.push_back(state);
                log_debug(
                    "sa: %scontact.%i.status state %s (%s)", prefix.c_str(), i, state.c_str(), assetName().c_str());
            } else {
                log_debug(
                    "sa: %scontact.%i.status state '%s' not supported and discarded", prefix.c_str(), i, state.c_str());
            }
        }
    } catch (...) {
    }
}

std::string Sensor::topicSuffix() const
{
    return "." + std::to_string(_index) + "@" + location();
}

// topic for GPI sensors wired to EMP001
std::string Sensor::topicSuffixExternal(const std::string& gpiPort) const
{
    // status.GPI<port>.<empPort>@location
    return ".GPI" + gpiPort + "." + std::to_string(_index) + "@" + location();
}

void Sensor::publish(int ttl)
{
    auto publishOnShm = [ttl](const std::string& name, const std::string& type, const std::string& value, const std::string& unit) {
        int r = 0;
        fty_proto_t* n_met = fty_proto_new(FTY_PROTO_METRIC);
        if (!n_met) {
            log_error("SHM publish: new METRIC failed (%s)", name.c_str());
            return -1;
        }
        fty_proto_set_name(n_met, name.c_str());
        fty_proto_set_type(n_met, type.c_str());
        fty_proto_set_value(n_met, "%s", value.c_str());
        fty_proto_set_unit(n_met, "%s", unit.c_str());
        fty_proto_set_ttl(n_met, uint32_t(ttl));
        fty_proto_set_time(n_met, uint64_t(std::time(nullptr)));

        char* aux_log = NULL;
        asprintf(&aux_log, "%s@%s (value: %s%s, ttl: %u)",
            fty_proto_type(n_met), fty_proto_name(n_met),
            fty_proto_value(n_met), fty_proto_unit(n_met),
            fty_proto_ttl(n_met));

        int rv = fty::shm::write_metric(n_met);
        if (rv != 0) {
            log_error("SHM publish failed (%s)", aux_log);
            r = -1;
        } else {
            log_debug("SHM publish %s", aux_log);
        }
        zstr_free(&aux_log);
        fty_proto_destroy(&n_met);

        return r;
    };

    if (!_temperature.empty()) {
        publishOnShm(assetName(), "temperature.default", _temperature, "C");
    }

    if (!_humidity.empty()) {
        publishOnShm(assetName(), "humidity.default", _humidity, "%");
    }

    if (!_contacts.empty()) {

        int gpiPort = 1;
        for (auto& contact : _contacts) {
            std::string extport = std::to_string(gpiPort);
            auto        search  = _children.find(extport);
            if (search != _children.end()) {
                std::string sname = search->second;

                publishOnShm(sname, "status.GPI" + extport, contact, "");
            } else {
                log_debug("I did not find any child for %s on port %s", assetName().c_str(), extport.c_str());
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
    if (_asset && !_asset->port().empty() && _asset->port() != "0") {
        prefix += _asset->port() + ".";
    }
    return prefix;
}

std::string Sensor::nutPrefix() const
{
    std::string prefix;
    if (chain() != 0) {
        if (_index == 0)
            prefix = "device." + std::to_string(chain()) + ".";
        else
            prefix = "device.1.";
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
    if (_index != 0)
        return _index;
    else if (chain() != 0)
        return chain();
    return 0;
}

void Sensor::addChild(const std::string& child_port, const std::string& child_name)
{
    _children.emplace(child_port, child_name);
}

std::map<std::string, std::string> Sensor::getChildren()
{
    return _children;
}

void Sensor::setContacts(const std::vector<std::string>& contacts)
{
    _contacts = contacts;
}

void Sensor::setHumidity(const std::string& humidity)
{
    _humidity = humidity;
}

void Sensor::setInventory(const fty::nut::KeyValues& values)
{
    _inventory = values;
}

void Sensor::setTemperature(const std::string& temp)
{
    _temperature = temp;
}

