/*  =========================================================================
    alert_device - structure for device producing alerts

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

#include "alert_device.h"
#include "fty_nut_library.h"
#include "logger.h"

void
Device::fixAlertLimits (DeviceAlert& alert) {
    // lower limit
    if (alert.lowWarning.empty() && ! alert.lowCritical.empty()) {
        alert.lowWarning = alert.lowCritical;
    }
    if (!alert.lowWarning.empty() && alert.lowCritical.empty()) {
        alert.lowCritical = alert.lowWarning;
    }
    // upper limit
    if (alert.highWarning.empty() && ! alert.highCritical.empty()) {
        alert.highWarning = alert.highCritical;
    }
    if (!alert.highWarning.empty() && alert.highCritical.empty()) {
        alert.highCritical = alert.highWarning;
    }
}

void
Device::addAlert(const std::string& quantity, const std::map<std::string,std::vector<std::string> >& variables)
{
    log_debug ("aa: device %s provides %s alert", _assetName.c_str(), quantity.c_str());
    std::string prefix = daisychainPrefix() + quantity;
    DeviceAlert alert;
    alert.name = quantity;

    if (_alerts.find (quantity) != _alerts.end()) {
        log_debug ("aa: device %s, alert %s already known", _assetName.c_str(), quantity.c_str());
        return;
    }

    // does the device evaluation?
    {
        const auto& it = variables.find(prefix + ".status");
        if (it == variables.cend ()) {
            log_debug ("aa: device %s doesn't support %s.status", _assetName.c_str(), quantity.c_str());
            return;
        }
    }
    // some devices provides ambient.temperature.(high|low)
    {
        const auto& it = variables.find(prefix + ".high");
        if (it != variables.cend ()) {
            alert.highWarning = it->second[0];
            alert.highCritical = it->second[0];
        }
    }
    {
        const auto& it = variables.find (prefix + ".low");
        if (it != variables.cend()) {
            alert.lowWarning = it->second[0];
            alert.lowCritical = it->second[0];
        }
    }
    // some devices provides ambient.temperature.(high|low).(warning|critical)
    {
        const auto& it = variables.find(prefix + ".high.warning");
        if (it != variables.cend()) alert.highWarning = it->second[0];
    }
    {
        const auto& it = variables.find(prefix + ".high.critical");
        if (it != variables.cend()) alert.highCritical = it->second[0];
    }
    {
        const auto& it = variables.find(prefix + ".low.warning");
        if (it != variables.cend()) alert.lowWarning = it->second[0];
    }
    {
        const auto& it = variables.find(prefix + ".low.critical");
        if (it != variables.cend()) alert.lowCritical = it->second[0];
    }
    // if some limits are missing, use those present
    fixAlertLimits (alert);
    if (
        alert.lowWarning.empty() ||
        alert.lowCritical.empty() ||
        alert.highWarning.empty() ||
        alert.highCritical.empty()
    ) {
        log_error("aa: thresholds for %s are not present in %s", quantity.c_str (), _assetName.c_str ());
    } else {
        _alerts[quantity] = alert;
    }
}

int
Device::scanCapabilities (nut::TcpClient& conn)
{
    log_debug ("aa: scanning capabilities for %s", _assetName.c_str());
    if (!conn.isConnected ()) return 0;
    std::string prefix = daisychainPrefix();

    _alerts.clear();
    try {
        auto nutDevice = conn.getDevice(_nutName);
        if (! nutDevice.isOk()) { throw std::runtime_error("device " + _assetName + " is not configured in NUT yet"); }
        auto vars = nutDevice.getVariableValues();
        if (vars.empty ()) return 0;
        if (vars.find (prefix + "ambient.temperature.status") != vars.cend()) {
            addAlert ("ambient.temperature", vars);
        }
        if (vars.find (prefix + "ambient.humidity.status") != vars.cend()) {
            addAlert ("ambient.humidity", vars);
        }
        for (int a=1; a<=3; a++) {
            std::string q = "input.L" + std::to_string(a) + ".current";
            if (vars.find (prefix + q + ".status") != vars.cend()) {
                addAlert (q, vars);
            }
            q = "input.L" + std::to_string(a) + ".voltage";
            if (vars.find (prefix + q + ".status") != vars.cend()) {
                addAlert (q, vars);
            }
        }
        for (int a=1; a<=1000; a++) {
            int found = 0;
            std::string q = "outlet.group." + std::to_string(a) + ".current";
            if (vars.find (prefix + q + ".status") != vars.cend()) {
                addAlert (q, vars);
                ++found;
            }
            q = "outlet.group." + std::to_string(a) + ".voltage";
            if (vars.find (prefix + q + ".status") != vars.cend()) {
                addAlert (q, vars);
                ++found;
            }
            if (!found) break;
        }
    } catch ( std::exception &e ) {
        log_error("aa: Communication problem with %s (%s)", _assetName.c_str(), e.what() );
        return 0;
    }
    _scanned = true;
    return 1;
}

void
Device::publishAlerts (mlm_client_t *client, uint64_t ttl) {
    if (!client) return;
    log_debug("aa: publishing %zu alerts on %s", _alerts.size (), _assetName.c_str());
    for (auto& it: _alerts) {
        publishAlert (client, it.second, ttl);
    }
}

void
Device::publishAlert (mlm_client_t *client, DeviceAlert& alert, uint64_t ttl)
{
    if (!client) return;
    if (alert.status.empty()) return;

    const char *state = "ACTIVE", *severity = NULL;
    std::string description = alert.name;

    log_debug ("aa: alert status '%s'", alert.status.c_str ());
    if (alert.status == "good") {
        state = "RESOLVED";
        severity = "ok";
        description += " is resolved";
    }
    else if (alert.status == "warning-low") {
        severity = "WARNING";
        description += " is low";
    }
    else if (alert.status == "critical-low") {
        severity = "CRITICAL";
        description += " is critically low";
    }
    else if (alert.status == "warning-high") {
        severity = "WARNING";
        description += " is high";
    }
    else if (alert.status == "critical-high") {
        severity = "CRITICAL";
        description += " is critically high";
    }
    std::string rule = alert.name + "@" + _assetName;

    if (!severity) {
        log_error ("aa: alert %s has unknown severity value %s. Set to WARNING.", rule.c_str (), alert.status.c_str ());
        severity = "WARNING";
    }

    log_debug("aa: publishing alert %s", rule.c_str ());
    zmsg_t *message = fty_proto_encode_alert(
        NULL,               // aux
        alert.timestamp,    // timestamp
        ttl,
        rule.c_str (),      // rule
        _assetName.c_str (),// element
        state,              // state
        severity,           // severity
        description.c_str (), // description
        ""                  // action ?email
    );
    std::string topic = rule + "/" + severity + "@" + _assetName;
    if (message) {
        mlm_client_send (client, topic.c_str (), &message);
    };
    zmsg_destroy (&message);
}

void
Device::publishRules (mlm_client_t *client) {
    if (!client) return;

    for (auto& it: _alerts) {
        publishRule (client, it.second);
    }
}

static std::string
s_values_unit (const std::string& alert_name)
{
    if (alert_name.find ("power") != std::string::npos)
        return "W";
    else
    if (alert_name.find ("voltage") != std::string::npos)
        return "V";
    else
    if (alert_name.find ("current") != std::string::npos)
        return "A";
    else
        return "";
}

static std::string
s_rule_desc (const std::string& alert_name)
{
    if (alert_name.find ("power") != std::string::npos)
        return "Power";
    else
    if (alert_name.find ("voltage") != std::string::npos)
        return "Voltage";
    else
    if (alert_name.find ("current") != std::string::npos)
        return "Current";
    else
        return "";
}

void
Device::publishRule (mlm_client_t *client, DeviceAlert& alert)
{
    if (!client || alert.rulePublished) return;

    zmsg_t *message = zmsg_new();
    assert (message);

    std::string ruleName = alert.name + "@" + _assetName;
    std::string rule =
        "{ \"threshold\" : {"
        "  \"rule_name\"     : \"" + ruleName + "\","
        "  \"rule_source\"   : \"NUT\","
        "  \"rule_class\"    : \"Device internal\","
        "  \"rule_hierarchy\": \"internal.device\","
        "  \"rule_desc\"     : \"" + s_rule_desc (alert.name) + "\","
        "  \"target\"        : \"" + ruleName + "\","
        "  \"element\"       : \"" + _assetName + "\","
        "  \"values_unit\"   : \"" + s_values_unit (alert.name) + "\","
        "  \"values\"        : ["
        "    { \"low_warning\"  : \"" + alert.lowWarning + "\"},"
        "    { \"low_critical\" : \"" + alert.lowCritical + "\"},"
        "    { \"high_warning\"  : \"" + alert.highWarning + "\"},"
        "    { \"high_critical\" : \"" + alert.highCritical + "\"}"
        "    ],"
        "  \"results\"       : ["
        "    { \"low_critical\"  : { \"action\" : [\"EMAIL\", \"SMS\"], \"severity\":\"CRITICAL\", \"description\" : \"" + alert.name + " is critically low\" }},"
        "    { \"low_warning\"   : { \"action\" : [\"EMAIL\", \"SMS\"], \"severity\":\"WARNING\" , \"description\" : \"" + alert.name + " is low\"}},"
        "    { \"high_warning\"  : { \"action\" : [\"EMAIL\", \"SMS\"], \"severity\":\"WARNING\" , \"description\" : \"" + alert.name + " is critically high\" }},"
        "    { \"high_critical\" : { \"action\" : [\"EMAIL\", \"SMS\"], \"severity\":\"CRITICAL\", \"description\" : \"" + alert.name + " is high\" } }"
        "  ] } }";
    log_debug("aa: publishing rule %s", ruleName.c_str ());
    zmsg_addstr (message, "ADD");
    zmsg_addstr (message, rule.c_str ());
    if (mlm_client_sendto (client, "fty-alert-engine", "rfc-evaluator-rules", NULL, 1000, &message) == 0) {
        alert.rulePublished = true;
    };
    zmsg_destroy (&message);
}

void
Device::update (nut::TcpClient& conn)
{
    auto nutDevice = conn.getDevice(_nutName);
    if (! nutDevice.isOk()) return;
    for (auto &it: _alerts) {
        try {
            std::string prefix = daisychainPrefix();
            auto value = nutDevice.getVariableValue (prefix + it.first + ".status");
            if (value.empty ()) {
                log_debug ("aa: %s on %s is not present", it.first.c_str (), _assetName.c_str ());
            } else {
                std::string newStatus =  value[0];
                log_debug ("aa: %s on %s is %s", it.first.c_str (), _assetName.c_str (), newStatus.c_str());
                if (it.second.status != newStatus) {
                    it.second.timestamp = ::time(NULL);
                    it.second.status = newStatus;
                }
            }
        } catch (...) {}
    }
}

std::string Device::daisychainPrefix() const
{
    if (_chain == 0) return "";
    return "device." + std::to_string(_chain) + ".";
}


//  --------------------------------------------------------------------------
//  Self test of this class

void
alert_device_test (bool verbose)
{
    printf (" * alert device: ");
    //  @selftest
    Device dev;
    std::map<std::string,std::vector<std::string> > nothing = {
        { "nothing", {"h1", "h2"} }
    };
    dev.addAlert("ambient.temperature", nothing);
    printf(".");
    assert(dev._alerts.empty());

    std::map<std::string,std::vector<std::string> > alerts = {
        { "ambient.temperature.status", {"good", "", ""} },
        { "ambient.temperature.high.warning", {"80", "", ""} },
        { "ambient.temperature.high.critical", {"100", "", ""} },
        { "ambient.temperature.low.warning", {"10", "", ""} },
        { "ambient.temperature.low.critical", {"5", "", ""} },

        { "ambient.humidity.status", {"good", "", ""} },
        { "ambient.humidity.high", {"100", "", ""} },
        { "ambient.humidity.low", {"10", "", ""} },
    };
    dev.addAlert("ambient.temperature", alerts);
    dev.addAlert("ambient.humidity", alerts);
    assert(dev._alerts.size() == 2);
    assert(dev._alerts["ambient.humidity"].lowWarning == "10");
    assert(dev._alerts["ambient.humidity"].lowCritical == "10");
    assert(dev._alerts["ambient.temperature"].lowWarning == "10");
    assert(dev._alerts["ambient.temperature"].lowCritical == "5");
    assert(dev._alerts["ambient.temperature"].highWarning == "80");
    assert(dev._alerts["ambient.temperature"].highCritical == "100");
    //  @end
    printf (" OK\n");
}
