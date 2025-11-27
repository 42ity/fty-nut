/*  =========================================================================
    alert_device - structure for device producing alerts

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

#include "alert_device.h"
#include <fty_common_macros.h>
#include <fty_log.h>
#include <fty_proto.h>
#include <stdexcept>
#include <fty_shm.h>
#include <fty_common_mlm_guards.h>
#include <fty_common_json.h>

void Device::fixAlertLimits(DeviceAlert& alert)
{
    // lower limit
    if (alert.lowWarning.empty() && !alert.lowCritical.empty()) {
        alert.lowWarning = alert.lowCritical;
    }
    if (!alert.lowWarning.empty() && alert.lowCritical.empty()) {
        alert.lowCritical = alert.lowWarning;
    }
    // upper limit
    if (alert.highWarning.empty() && !alert.highCritical.empty()) {
        alert.highWarning = alert.highCritical;
    }
    if (!alert.highWarning.empty() && alert.highCritical.empty()) {
        alert.highCritical = alert.highWarning;
    }
}

void Device::addAlert(const std::string& nutQuantity, const std::string& ftyQuantity, const std::map<std::string, std::vector<std::string>>& variables)
{
    log_debug("aa: device %s provides %s alert", assetName().c_str(), ftyQuantity.c_str());
    std::string prefix = daisychainPrefix() + nutQuantity;

    DeviceAlert alert;
    alert.name = ftyQuantity;

    // Is there an existing alert which we can change?
    const auto& _existingalert = _alerts.find(ftyQuantity);
    bool updatingalert  = false;
    DeviceAlert existingalert;
    if (_existingalert != _alerts.end()) {
        existingalert = _existingalert->second; // Dereference operator in iterator
        if (existingalert.ruleRescanned) {
            log_debug("aa: device %s, alert %s already known", assetName().c_str(), ftyQuantity.c_str());
            return;
        } else {
            // This entry is in the list, but was not refreshed in this
            // run of scanAlerts(). Initialize "alert" from this value
            // and below we will put it back into the list, overwriting
            // the old existing value.
            updatingalert = true;
            alert         = existingalert;
        }
    } // else go on using the freshly made "alert" instance
    else {
        log_debug("aa: device %s, alert %s is new", assetName().c_str(), ftyQuantity.c_str());
    }

    // does the device evaluation?
    {
        const auto& it = variables.find(prefix + ".status");
        if (it == variables.cend()) {
            log_debug("aa: device %s doesn't support %s.status", assetName().c_str(), nutQuantity.c_str());
            return;
        }
    }

    // some devices provides ambient.temperature.(high|low)
    {
        const auto& it = variables.find(prefix + ".high");
        if (it != variables.cend()) {
            alert.highWarning  = it->second[0];
            alert.highCritical = it->second[0];
        }
    }
    {
        const auto& it = variables.find(prefix + ".low");
        if (it != variables.cend()) {
            alert.lowWarning  = it->second[0];
            alert.lowCritical = it->second[0];
        }
    }
    // some devices provides ambient.temperature.(high|low).(warning|critical)
    {
        const auto& it = variables.find(prefix + ".high.warning");
        if (it != variables.cend())
            alert.highWarning = it->second[0];
    }
    {
        const auto& it = variables.find(prefix + ".high.critical");
        if (it != variables.cend())
            alert.highCritical = it->second[0];
    }
    {
        const auto& it = variables.find(prefix + ".low.warning");
        if (it != variables.cend())
            alert.lowWarning = it->second[0];
    }
    {
        const auto& it = variables.find(prefix + ".low.critical");
        if (it != variables.cend())
            alert.lowCritical = it->second[0];
    }
    // if some limits are missing, use those present
    fixAlertLimits(alert);
    if (alert.lowWarning.empty() || alert.lowCritical.empty() || alert.highWarning.empty() ||
        alert.highCritical.empty()) {
        log_error("aa: thresholds for %s are not present in %s", nutQuantity.c_str(), assetName().c_str());
    } else {
        alert.ruleRescanned = true;
        if (updatingalert && alert.rulePublished) {
            // If anything changed, reset the flag to make the info known
            if (alert.lowWarning != existingalert.lowWarning) {
                alert.rulePublished = false;
            }
            if (alert.highWarning != existingalert.highWarning) {
                alert.rulePublished = false;
            }
            if (alert.lowCritical != existingalert.lowCritical) {
                alert.rulePublished = false;
            }
            if (alert.highCritical != existingalert.highCritical) {
                alert.rulePublished = false;
            }
        }

        // If entry exists we must update at least the alert.ruleRescanned
        // otherwise we must add it to the list.
        log_debug("aa: adding alert %s to %s", ftyQuantity.c_str(), assetName().c_str());
        _alerts[ftyQuantity] = alert;
    }
}

const std::map<std::string, DeviceAlert>& Device::alerts() const
{
    return _alerts;
}

std::map<std::string, DeviceAlert>& Device::alerts()
{
    return _alerts;
}

int Device::scanCapabilities(nut::ConnectionClient& conn)
{
    log_debug("aa: scanning capabilities for %s", assetName().c_str());

    if (!conn.isConnected()) {
        log_debug("aa: Connection to NUT is not established");
        return 0;
    }

    std::string prefix = daisychainPrefix();
    int         retval = -1;

    for (auto& it : _alerts) {
        it.second.ruleRescanned = false;
    }

    try {
        auto nutDevice = conn.getDevice(_nutName);
        if (!nutDevice.isOk()) {
            throw std::runtime_error("device " + assetName() + " is not configured in NUT yet");
        }
        auto vars = nutDevice.getVariableValues();
        if (vars.empty()) {
            log_debug("aa: no variables found for %s", assetName().c_str());
            return 0;
        }

        // Sensors handling
        if (vars.find(prefix + "ambient.count") != vars.cend()) {
            // New style sensor(s) (EMP002: ambient collection, with index)
            auto sensor_count_var = vars.find(prefix + "ambient.count");
            int  sensors_count    = std::stoi(sensor_count_var->second[0]);
            log_debug("aa: found %d sensor(s)", sensors_count);

            for (int a = 1; a <= sensors_count; a++) {
                std::string q = "ambient." + std::to_string(a) + ".temperature";
                if (vars.find(prefix + q + ".status") != vars.cend()) {
                    addAlert(q, q, vars);
                    _scanned = true;
                }
                q = "ambient." + std::to_string(a) + ".humidity";
                if (vars.find(prefix + q + ".status") != vars.cend()) {
                    addAlert(q, q, vars);
                    _scanned = true;
                }
            }
        } else {
            // Legacy sensor (EMP001: ambient collection, without index)
            std::string q = "ambient.temperature";
            if (vars.find(prefix + q + ".status") != vars.cend()) {
                addAlert(q, q, vars);
                _scanned = true;
            }
            q = "ambient.humidity";
            if (vars.find(prefix + q + ".status") != vars.cend()) {
                addAlert(q, q, vars);
                _scanned = true;
            }
        }

        // Input handling
        for (int a = 1; a <= 3; a++) {
            std::string nutName = "input.L" + std::to_string(a) + ".current";
            std::string ftyName = "current.input.L" + std::to_string(a);
            if (vars.find(prefix + nutName + ".status") != vars.cend()) {
                addAlert(nutName, ftyName, vars);
                _scanned = true;
            }
            nutName = "input.L" + std::to_string(a) + ".voltage";
            ftyName = "voltage.input.L" + std::to_string(a) + "-N";
            if (vars.find(prefix + nutName + ".status") != vars.cend()) {
                addAlert(nutName, ftyName, vars);
                _scanned = true;
            }
        }

        // Outlets groups handling
        for (int a = 1; a <= 1000; a++) {
            bool found = false;
            std::string nutName = "outlet.group." + std::to_string(a) + ".current";
            std::string ftyName = "current.outlet.group." + std::to_string(a);
            if (vars.find(prefix + nutName + ".status") != vars.cend()) {
                addAlert(nutName, ftyName, vars);
                found = true;
                _scanned = true;
            }
            nutName = "outlet.group." + std::to_string(a) + ".voltage";
            ftyName = "voltage.outlet.group." + std::to_string(a);
            if (vars.find(prefix + nutName + ".status") != vars.cend()) {
                addAlert(nutName, ftyName, vars);
                found = true;
                _scanned = true;
            }
            if (!found)
                break;
        }
    } catch (const std::exception& e) {
        log_error("aa: Communication problem with %s (%s)", assetName().c_str(), e.what());
        retval = 0;
        goto cleanup;
    }
    retval = 1;

cleanup:
    for (auto it = _alerts.begin(); it != _alerts.end();) {
        if (!it->second.ruleRescanned) {
            // Remove the obsolete entry not touched by current scan
            // or where addAlert errored out and returned early
            _alerts.erase(it++);
        } else {
            ++it;
        }
    }
    return retval;
}

// HOTFIX arrange as we can the alert name displayed (en_US)
// TODO use translation string instead
// NOTE: alertname modified on return
// ex.: "input.L3.voltage" -> "Input L3 voltage"

static void makeAlertNameMoreHumanReadable(const char* alertname)
{
    if (!alertname) return;

    bool capitalize = true;
    for (char* p = const_cast<char*>(alertname); (*p) != 0; p++) {
        if (capitalize) { // capitalize 1st char
            capitalize = false;
            *p = char(toupper(*p));
        }
        if ((*p)== '.') { // subs '.' with ' '
            *p = ' ';
        }
    }
}

void Device::publishRules(mlm_client_t* client)
{
    if (!client) {
        log_error("publishRules: no client defined");
        return;
    }

    for (auto& it : _alerts) {
        publishRule(client, it.second);
    }
}

static std::string s_values_unit(const std::string& alert_name)
{
    if (alert_name.find("power") != std::string::npos)
        return "W";
    else if (alert_name.find("voltage") != std::string::npos)
        return "V";
    else if (alert_name.find("current") != std::string::npos)
        return "A";
    else if (alert_name.find("temperature") != std::string::npos)
        return "C";
    else if (alert_name.find("humidity") != std::string::npos)
        return "%";
    else
        return "";
}

static std::string s_rule_desc(const std::string& alert_name)
{
    if (alert_name.find("power") != std::string::npos)
        return "TRANSLATE_LUA(Power)";
    else if (alert_name.find("voltage") != std::string::npos)
        return "TRANSLATE_LUA(Voltage)";
    else if (alert_name.find("current") != std::string::npos)
        return "TRANSLATE_LUA(Current)";
    else if (alert_name.find("temperature") != std::string::npos)
        return "TRANSLATE_LUA(Internal temperature)";
    else if (alert_name.find("humidity") != std::string::npos)
        return "TRANSLATE_LUA(Internal humidity)";
    else
        return "";
}

fty::Expected<cxxtools::SerializationInfo> Device::getRule(mlm_client_t* client, const DeviceAlert& alert)
{
    if (!client) {
        return fty::unexpected("No client defined");
    }

    zmsg_t* message = zmsg_new();
    if (!message) {
        return fty::unexpected("Failed to create message");
    }

    std::string errorStr;
    std::string alertNameStr = alert.name;
    std::string assetNameStr = assetName(); //iname

    char *ruleName = nullptr;
    asprintf(&ruleName, "%s@%s", alertNameStr.c_str(), assetNameStr.c_str());
    ZstrGuard ruleNameGuard(ruleName);

    log_debug("getRule %s", ruleNameGuard.get());

    zmsg_addstr(message, "GET");
    zmsg_addstr(message, ruleNameGuard.get());
    int r = mlm_client_sendto(client, "fty-alert-engine", "rfc-evaluator-rules", NULL, 1000, &message);
    zmsg_destroy(&message);
    if (r != 0) {
        return fty::unexpected("Failed to send message to fty-alert-engine");
    }

    ZpollerGuard poller(zpoller_new(mlm_client_msgpipe(client), NULL));
    ZmsgGuard resp(poller && zpoller_wait(poller, 5000) ? mlm_client_recv(client) : NULL);
    if (!resp) {
        return fty::unexpected("No response from fty-alert-engine");
    }

    ZstrGuard result(zmsg_popstr(resp));
    if (result && streq(result, "OK")) {
        try {
            ZstrGuard alertJson(zmsg_popstr(resp));
            cxxtools::SerializationInfo alertSi;
            JSON::readFromString(alertJson.get(), alertSi);
            return alertSi;
        }
        catch(const std::exception& e) {
            errorStr = "Error in the json: " + std::string(e.what());
        }
    }
    else {
        ZstrGuard reason(zmsg_popstr(resp));
        log_error("Request fty-alert-engine GET rule %s failed (%s, %s)",
                ruleNameGuard.get(), result.get(), reason.get());
        errorStr = "Request fty-alert-engine GET rule failed";
    }
    return fty::unexpected(errorStr);
}

void Device::publishRule(mlm_client_t* client, DeviceAlert& alert)
{
    if (!client) {
        log_error("publishRule: no client defined");
        return;
    }

    if (alert.rulePublished) {
        log_debug("publishRule: rule %s already published", alert.name.c_str());
        return;
    }

    std::map<std::string, std::list<std::string>> actions;

    // Saved value before changing value
    bool ruleNewSaved = alert.ruleNew;
    if (alert.ruleNew) {
        log_debug("aa: rule %s is new", alert.name.c_str());
        alert.ruleNew = false;
    }
    else {
        log_debug("aa: rule %s found", alert.name.c_str());

        // Get the rule from alert engine
        auto siRule = getRule(client, alert);
        if (!siRule) {
            log_error("aa: rule %s not found: %s", alert.name.c_str(), siRule.error().c_str());
            return;
        }

        // Get actions list from response
        for(const auto& result : siRule->getMember(0).getMember("results")) {
            if (!result.getMember(0).isNull() &&
                result.getMember(0).getMember("action").category() == cxxtools::SerializationInfo::Array) {
                for (const auto& actionMember : result.getMember(0).getMember("action")) {
                    if (actionMember.getMember("action").category() == cxxtools::SerializationInfo::Value) {
                        std::string action;
                        actionMember.getMember("action").getValue(action);
                        actions[result.getMember(0).name()].push_back(action);
                    }
                }
            }
        }
    }

    zmsg_t *message = zmsg_new();
    if (!message) {
        log_error("Failed to create message");
        return;
    }

    std::string alertNameStr = alert.name;
    const char *alert_name   = alertNameStr.c_str();

    std::string alertNameLabelStr = alert.name; // cpy
    const char *alert_name_label  = alertNameLabelStr.c_str();
    makeAlertNameMoreHumanReadable(alert_name_label);

    std::string assetNameStr = assetName(); //iname
    const char *asset_name   = assetNameStr.c_str();

    std::string assetFriendlyNameStr = assetFriendlyName();
    const char *asset_friendly_name  = assetFriendlyNameStr.c_str();

    char* ruleName = nullptr;
    asprintf(&ruleName, "%s@%s", alert_name, asset_name);
    ZstrGuard ruleNameGuard(ruleName);

    // ruleClass: en_US display (best as we can)
    std::string ruleClass = "TRANSLATE_LUA(" + std::string(alert_name_label) + ")";
/** ruleClass traduction strings definition (runtime, best effort) - see fty-common-nut/resources/mapping.conf
TRANSLATE_LUA(Ambient 1 temperature) TRANSLATE_LUA(Ambient 2 temperature) TRANSLATE_LUA(Ambient 3 temperature) TRANSLATE_LUA(Ambient 4 temperature)
TRANSLATE_LUA(Ambient 1 humidity) TRANSLATE_LUA(Ambient 2 humidity) TRANSLATE_LUA(Ambient 3 humidity) TRANSLATE_LUA(Ambient 4 humidity)
TRANSLATE_LUA(Current input L1) TRANSLATE_LUA(Current input L2) TRANSLATE_LUA(Current input L3)
TRANSLATE_LUA(Load input L1) TRANSLATE_LUA(Load input L2) TRANSLATE_LUA(Load input L3)
TRANSLATE_LUA(Voltage input L1-N) TRANSLATE_LUA(Voltage input L2-N) TRANSLATE_LUA(Voltage input L3-N)
*/

    const char* TR_LUA_LC = "TRANSLATE_LUA({{alert_name}} is critically low for {{ename}}.)";
    const char* TR_LUA_LW = "TRANSLATE_LUA({{alert_name}} is low for {{ename}}.)";
    const char* TR_LUA_HW = "TRANSLATE_LUA({{alert_name}} is high for {{ename}}.)";
    const char* TR_LUA_HC = "TRANSLATE_LUA({{alert_name}} is critically high for {{ename}}.)";

    // Build actions json (e.g "[{ "action": "EMAIL" }, { "action": "SMS" }]")
    auto buildActionJson = [&](const std::string category) {
        std::string res {"["};
        bool first = true;
        if (actions.find(category) != actions.end()) {
            for (auto action : actions[category]) {
                if (!first) {
                    res += ",";
                }
                first = false;
                res += "{ \"action\": \"" + action + "\" }";
            }
        }
        res += "]";
        return res;
    };

    // clang-format off
    char *rule = nullptr;
    asprintf (&rule,
        "{"
            "\"threshold\" : {"
            "  \"rule_name\"     : \"%s\"," //@1
            "  \"rule_source\"   : \"NUT\","
            "  \"rule_class\"    : \"%s\"," //@1b
            "  \"rule_hierarchy\": \"internal.device\","
            "  \"rule_desc\"     : \"%s\"," //@2
            "  \"target\"        : \"%s\"," //@3
            "  \"element\"       : \"%s\"," //@4
            "  \"values_unit\"   : \"%s\"," //@5
            "  \"values\" : ["
            "    { \"low_warning\"  : \"%s\"  }," //@6
            "    { \"low_critical\" : \"%s\"  }," //@7
            "    { \"high_warning\"  : \"%s\" }," //@8
            "    { \"high_critical\" : \"%s\" }" //@9
            "  ],"
            "  \"results\" : ["
            "    { \"low_critical\"  : { \"action\" : %s, \"severity\":\"CRITICAL\", \"description\" : \"  {\\\"key\\\" : \\\"%s\\\", \\\"variables\\\" : {\\\"alert_name\\\" : \\\"%s\\\", \\\"ename\\\" : { \\\"value\\\" : \\\"%s\\\", \\\"assetLink\\\" : \\\"%s\\\" } } }\" } },"
            "    { \"low_warning\"   : { \"action\" : %s, \"severity\":\"WARNING\" , \"description\" : \"  {\\\"key\\\" : \\\"%s\\\", \\\"variables\\\" : {\\\"alert_name\\\" : \\\"%s\\\", \\\"ename\\\" : { \\\"value\\\" : \\\"%s\\\", \\\"assetLink\\\" : \\\"%s\\\" } } }\" } },"
            "    { \"high_warning\"  : { \"action\" : %s, \"severity\":\"WARNING\" , \"description\" : \"  {\\\"key\\\" : \\\"%s\\\", \\\"variables\\\" : {\\\"alert_name\\\" : \\\"%s\\\", \\\"ename\\\" : { \\\"value\\\" : \\\"%s\\\", \\\"assetLink\\\" : \\\"%s\\\" } } }\" } },"
            "    { \"high_critical\" : { \"action\" : %s, \"severity\":\"CRITICAL\", \"description\" : \"  {\\\"key\\\" : \\\"%s\\\", \\\"variables\\\" : {\\\"alert_name\\\" : \\\"%s\\\", \\\"ename\\\" : { \\\"value\\\" : \\\"%s\\\", \\\"assetLink\\\" : \\\"%s\\\" } } }\" } }"
            "  ]"
            "}"
        "}",

        ruleNameGuard.get(), //@1
        ruleClass.c_str(), //@1b
        s_rule_desc (alert.name).c_str (), //@2
        ruleNameGuard.get(), //@3
        asset_name, //@4
        s_values_unit (alert.name).c_str (), //@5

        alert.lowWarning.c_str (),  //@6
        alert.lowCritical.c_str (), //@7
        alert.highWarning.c_str (), //@8
        alert.highCritical.c_str (), //@9

        //low_critical
        buildActionJson("low_critical").c_str (),
        TR_LUA_LC,
        alert_name_label,
        asset_friendly_name,
        asset_name,

        //low_warning
        buildActionJson("low_warning").c_str (),
        TR_LUA_LW,
        alert_name_label,
        asset_friendly_name,
        asset_name,

        //high_warning
        buildActionJson("high_warning").c_str (),
        TR_LUA_HW,
        alert_name_label,
        asset_friendly_name,
        asset_name,

        //high_critical
        buildActionJson("high_critical").c_str (),
        TR_LUA_HC,
        alert_name_label,
        asset_friendly_name,
        asset_name
    );
    // clang-format on
    ZstrGuard ruleGuard(rule);

    log_debug("aa: publishing rule %s", ruleNameGuard.get());
    log_trace("%s", ruleGuard.get());

    zmsg_addstr(message, "ADD");
    zmsg_addstr(message, ruleGuard.get());

    // Test if the rule is new or need to be updated
    if (!ruleNewSaved) {
        // Add rule name to the message for update
        zmsg_addstr(message, ruleNameGuard.get());
    }

    int r = mlm_client_sendto(client, "fty-alert-engine", "rfc-evaluator-rules", NULL, 1000, &message);
    zmsg_destroy(&message);
    if (r == 0) {
        ZpollerGuard poller(zpoller_new(mlm_client_msgpipe(client), NULL));
        ZmsgGuard resp(poller && zpoller_wait(poller, 5000) ? mlm_client_recv(client) : NULL);
        if (resp) {
            ZstrGuard result(zmsg_popstr(resp));
            ZstrGuard reason(zmsg_popstr(resp));
            if ((result && streq(result, "OK")) || (reason && streq(reason, "ALREADY_EXISTS"))) {
                alert.rulePublished = true;
            }
            else {
                log_error("Request fty-alert-engine ADD rule %s failed (%s, %s)",
                    ruleNameGuard.get(), result.get(), reason.get());
            }
        }
    }
}

void Device::update(nut::ConnectionClient& conn)
{
    auto nutDevice = conn.getDevice(_nutName);
    if (!nutDevice.isOk()) {
        log_debug("aa: device %s is not configured in NUT yet", assetName().c_str());
        return;
    }
    int ttl_sec = 60;
    for (auto& it : _alerts) {
        try {
            auto prefix = daisychainPrefix();
            auto name = prefix + it.first;
            // Publish only ambient metrics (others metrics already made by another actor in fty-nut)
            if (name.find("ambient.") == std::string::npos) {
                continue;
            }
            auto value  = nutDevice.getVariableValue(name);
            if (value.empty()) {
                log_debug("aa: %s on %s is not present", it.first.c_str(), assetName().c_str());
                continue;
            }
            // Write the metric in shm
            fty_proto_t* n_met = fty_proto_new(FTY_PROTO_METRIC);
            if (!n_met) {
                log_error("SHM publish: new METRIC failed (%s)", name.c_str());
                return;
            }
            fty_proto_set_name(n_met, _nutName.c_str());
            fty_proto_set_type(n_met, name.c_str());
            fty_proto_set_value(n_met, "%s", value[0].c_str());
            fty_proto_set_unit(n_met, "%s", s_values_unit(name).c_str());
            fty_proto_set_ttl(n_met, uint32_t(ttl_sec));
            fty_proto_set_time(n_met, uint64_t(std::time(nullptr)));
            char* aux_log = nullptr;
            asprintf(&aux_log, "%s@%s (value: %s%s, ttl: %u)",
                fty_proto_type(n_met), fty_proto_name(n_met),
                fty_proto_value(n_met), fty_proto_unit(n_met),
                fty_proto_ttl(n_met));
            ZstrGuard auxLogGuard(aux_log);
            int rv = fty::shm::write_metric(n_met);
            if (rv != 0) {
                log_error("SHM publish failed (%s)", auxLogGuard.get());
            } else {
                log_debug("SHM publish %s", auxLogGuard.get());
            }
            fty_proto_destroy(&n_met);
        } catch (const std::exception& ex) {
            log_error("aa: Communication problem with %s: %s", assetName().c_str(), ex.what());
        }
    }
}

std::string Device::daisychainPrefix() const
{
    if (chain() == 0) {
        return "";
    }
    return "device." + std::to_string(chain()) + ".";
}
