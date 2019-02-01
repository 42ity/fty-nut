/*  =========================================================================
    alert_device - structure for device producing alerts

    Copyright (C) 2014 - 2019 Eaton

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
#include <fty_log.h>
#include <fty_common_macros.h>

#include <ftyproto.h>

void
Device::fixAlertLimits (DeviceAlert& alert) {
    // lower limit
    if (alert.lowWarning.empty () && ! alert.lowCritical.empty ()) {
        alert.lowWarning = alert.lowCritical;
    }
    if (!alert.lowWarning.empty () && alert.lowCritical.empty ()) {
        alert.lowCritical = alert.lowWarning;
    }
    // upper limit
    if (alert.highWarning.empty () && ! alert.highCritical.empty ()) {
        alert.highWarning = alert.highCritical;
    }
    if (!alert.highWarning.empty () && alert.highCritical.empty ()) {
        alert.highCritical = alert.highWarning;
    }
}

void
Device::addAlert (const std::string& quantity, const std::map<std::string,std::vector<std::string> >& variables)
{
    log_debug ("aa: device %s provides %s alert", assetName ().c_str (), quantity.c_str ());
    std::string prefix = daisychainPrefix () + quantity;
    DeviceAlert alert;
    alert.name = quantity;

    // Is there an existing alert which we can change?
    const auto &_existingalert = _alerts.find (quantity);
    bool updatingalert = false;
    DeviceAlert existingalert;
    if (_existingalert != _alerts.end ()) {
        existingalert = _existingalert->second; // Dereference operator in iterator
        if (existingalert.ruleRescanned) {
            log_debug ("aa: device %s, alert %s already known", assetName ().c_str (), quantity.c_str ());
            return;
        } else {
            // This entry is in the list, but was not refreshed in this
            // run of scanAlerts(). Initialize "alert" from this value
            // and below we will put it back into the list, overwriting
            // the old existing value.
            updatingalert = true;
            alert = existingalert;
        }
    } // else go on using the freshly made "alert" instance

    // does the device evaluation?
    {
        const auto& it = variables.find (prefix + ".status");
        if (it == variables.cend ()) {
            log_debug ("aa: device %s doesn't support %s.status", assetName ().c_str (), quantity.c_str ());
            return;
        }
    }

    // some devices provides ambient.temperature.(high|low)
    {
        const auto& it = variables.find (prefix + ".high");
        if (it != variables.cend ()) {
            alert.highWarning = it->second[0];
            alert.highCritical = it->second[0];
        }
    }
    {
        const auto& it = variables.find (prefix + ".low");
        if (it != variables.cend ()) {
            alert.lowWarning = it->second[0];
            alert.lowCritical = it->second[0];
        }
    }
    // some devices provides ambient.temperature.(high|low).(warning|critical)
    {
        const auto& it = variables.find (prefix + ".high.warning");
        if (it != variables.cend ()) alert.highWarning = it->second[0];
    }
    {
        const auto& it = variables.find (prefix + ".high.critical");
        if (it != variables.cend ()) alert.highCritical = it->second[0];
    }
    {
        const auto& it = variables.find (prefix + ".low.warning");
        if (it != variables.cend ()) alert.lowWarning = it->second[0];
    }
    {
        const auto& it = variables.find (prefix + ".low.critical");
        if (it != variables.cend ()) alert.lowCritical = it->second[0];
    }
    // if some limits are missing, use those present
    fixAlertLimits (alert);
    if (
        alert.lowWarning.empty () ||
        alert.lowCritical.empty () ||
        alert.highWarning.empty () ||
        alert.highCritical.empty ()
    ) {
        log_error ("aa: thresholds for %s are not present in %s", quantity.c_str (), assetName ().c_str ());
    } else {
        alert.ruleRescanned = true;
        if (updatingalert && alert.rulePublished) {
            // If anything changed, reset the flag to make the info known
            if (alert.lowWarning != existingalert.lowWarning)     alert.rulePublished = false;
            if (alert.highWarning != existingalert.highWarning)   alert.rulePublished = false;
            if (alert.lowCritical != existingalert.lowCritical)   alert.rulePublished = false;
            if (alert.highCritical != existingalert.highCritical) alert.rulePublished = false;
        }
        // If entry exists we must update at least the alert.ruleRescanned
        // otherwise we must add it to the list.
        _alerts[quantity] = alert;
    }
}

int
Device::scanCapabilities (nut::TcpClient& conn)
{
    log_debug ("aa: scanning capabilities for %s", assetName ().c_str ());
    if (!conn.isConnected ()) return 0;
    std::string prefix = daisychainPrefix ();
    int retval = -1;

    for (auto& it: _alerts) {
        it.second.ruleRescanned = false;
    }
    try {
        auto nutDevice = conn.getDevice (_nutName);
        if (! nutDevice.isOk ()) { throw std::runtime_error ("device " + assetName () + " is not configured in NUT yet"); }
        auto vars = nutDevice.getVariableValues ();
        if (vars.empty ()) return 0;
        if (vars.find (prefix + "ambient.temperature.status") != vars.cend ()) {
            addAlert ("ambient.temperature", vars);
            _scanned = true;
        }
        if (vars.find (prefix + "ambient.humidity.status") != vars.cend ()) {
            addAlert ("ambient.humidity", vars);
            _scanned = true;
        }
        for (int a=1; a<=3; a++) {
            std::string q = "input.L" + std::to_string (a) + ".current";
            if (vars.find (prefix + q + ".status") != vars.cend ()) {
                addAlert (q, vars);
                _scanned = true;
            }
            q = "input.L" + std::to_string (a) + ".voltage";
            if (vars.find (prefix + q + ".status") != vars.cend ()) {
                addAlert (q, vars);
                _scanned = true;
            }
        }
        for (int a=1; a<=1000; a++) {
            int found = 0;
            std::string q = "outlet.group." + std::to_string (a) + ".current";
            if (vars.find (prefix + q + ".status") != vars.cend ()) {
                addAlert (q, vars);
                ++found;
                _scanned = true;
            }
            q = "outlet.group." + std::to_string (a) + ".voltage";
            if (vars.find (prefix + q + ".status") != vars.cend ()) {
                addAlert (q, vars);
                ++found;
                _scanned = true;
            }
            if (!found) break;
        }
    } catch ( std::exception &e ) {
        log_error ("aa: Communication problem with %s (%s)", assetName ().c_str (), e.what () );
        retval = 0;
        goto cleanup;
    }
    retval = 1;

cleanup:
    for (auto it = _alerts.begin() ; it != _alerts.end() ; ) {
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

void
Device::publishAlerts (mlm_client_t *client, uint64_t ttl) {
    if (!client) return;
    log_debug ("aa: publishing %zu alerts on %s", _alerts.size (), assetName ().c_str ());
    for (auto& it: _alerts) {
        publishAlert (client, it.second, ttl);
    }
}

void
Device::publishAlert (mlm_client_t *client, DeviceAlert& alert, uint64_t ttl)
{
    if (!client) return;
    if (alert.status.empty ()) return;

    const char *state = "ACTIVE", *severity = NULL;
    std::string description;

    log_debug ("aa: alert status '%s'", alert.status.c_str ());
    if (alert.status == "good") {
        state = "RESOLVED";
        severity = "ok";
        description = TRANSLATE_ME ("%s is resolved", alert.name.c_str ());
    }
    else if (alert.status == "warning-low") {
        severity = "WARNING";
        description = TRANSLATE_ME ("%s is low", alert.name.c_str ());
    }
    else if (alert.status == "critical-low") {
        severity = "CRITICAL";
        description = TRANSLATE_ME ("%s is critically low", alert.name.c_str ());
    }
    else if (alert.status == "warning-high") {
        severity = "WARNING";
        description = TRANSLATE_ME ("%s is high", alert.name.c_str ());
    }
    else if (alert.status == "critical-high") {
        severity = "CRITICAL";
        description = TRANSLATE_ME ("%s is critically high", alert.name.c_str ());
    }
    std::string rule = alert.name + "@" + assetName ();

    if (!severity) {
        log_error ("aa: alert %s has unknown severity value %s. Set to WARNING.", rule.c_str (), alert.status.c_str ());
        severity = "WARNING";
    }

    log_debug ("aa: publishing alert %s", rule.c_str ());
    zmsg_t *message = fty_proto_encode_alert (
        NULL,               // aux
        alert.timestamp,    // timestamp
        ttl,
        rule.c_str (),      // rule
        assetName ().c_str (),// element
        state,              // state
        severity,           // severity
        description.c_str (), // description
        NULL                // action ?email
    );
    std::string topic = rule + "/" + severity + "@" + assetName ();
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
        return TRANSLATE_ME ("Power");
    else
    if (alert_name.find ("voltage") != std::string::npos)
        return TRANSLATE_ME ("Voltage");
    else
    if (alert_name.find ("current") != std::string::npos)
        return TRANSLATE_ME ("Current");
    else
        return "{}";
}

void
Device::publishRule (mlm_client_t *client, DeviceAlert& alert)
{
    if (!client || alert.rulePublished) return;

    zmsg_t *message = zmsg_new ();
    assert (message);

    char *ruleName = zsys_sprintf ("%s@%s", alert.name.c_str (), assetName ().c_str ());
    char *rule = zsys_sprintf (
        "{ \"threshold\" : {"
        "  \"rule_name\"     : \"%s\","
        "  \"rule_source\"   : \"NUT\","
        "  \"rule_class\"    : \"Device internal\","
        "  \"rule_hierarchy\": \"internal.device\","
        "  \"rule_desc\"     : %s,"
        "  \"target\"        : \"%s\","
        "  \"element\"       : \"%s\","
        "  \"values_unit\"   : \"%s\","
        "  \"values\"        : ["
        "    { \"low_warning\"  : \"%s\"},"
        "    { \"low_critical\" : \"%s\"},"
        "    { \"high_warning\"  : \"%s\"},"
        "    { \"high_critical\" : \"%s\"}"
        "    ],"
        "  \"results\"       : ["
        "    { \"low_critical\"  : { \"action\" : [{\"action\": \"EMAIL\"}, {\"action\": \"SMS\"}], \"severity\":\"CRITICAL\", \"description\" : {\"key\" : \"TRANSLATE_LUA ({{alert_name}} is critically low for {{ename}}.)\", \"variable\" : {\"alert_name\" : \"%s\", \"ename\" : { \"value\" : \"%s\", \"assetLink\" : \"%s\" } } } } },"
        "    { \"low_warning\"   : { \"action\" : [{\"action\": \"EMAIL\"}, {\"action\": \"SMS\"}], \"severity\":\"WARNING\" , \"description\" : {\"key\" : \"TRANSLATE_LUA ({{alert_name}} is low for {{ename}}.)\", \"variable\" : {\"alert_name\" : \"%s\", \"ename\" : { \"value\" : \"%s\", \"assetLink\" : \"%s\" } } } } },"
        "    { \"high_warning\"  : { \"action\" : [{\"action\": \"EMAIL\"}, {\"action\": \"SMS\"}], \"severity\":\"WARNING\" , \"description\" : {\"key\" : \"TRANSLATE_LUA ({{alert_name}} is high for {{ename}}.)\", \"variable\" : {\"alert_name\" : \"%s\", \"ename\" : { \"value\" : \"%s\", \"assetLink\" : \"%s\" } } } } },"
        "    { \"high_critical\" : { \"action\" : [{\"action\": \"EMAIL\"}, {\"action\": \"SMS\"}], \"severity\":\"CRITICAL\", \"description\" : {\"key\" : \"TRANSLATE_LUA ({{alert_name}} is critically high for {{ename}}.)\", \"variable\" : {\"alert_name\" : \"%s\", \"ename\" : { \"value\" : \"%s\", \"assetLink\" : \"%s\" } } } } } ] } } ",
        ruleName,
        s_rule_desc (alert.name).c_str (),
        ruleName,
        assetName ().c_str (),
        s_values_unit (alert.name).c_str (),
        alert.lowWarning.c_str (),
        alert.lowCritical.c_str (),
        alert.highWarning.c_str (),
        alert.highCritical.c_str (),
        alert.name.c_str (),
        assetName ().c_str (),
        assetName ().c_str (),
        alert.name.c_str (),
        assetName ().c_str (),
        assetName ().c_str (),
        alert.name.c_str (),
        assetName ().c_str (),
        assetName ().c_str (),
        alert.name.c_str (),
        assetName ().c_str (),
        assetName ().c_str ());

    log_debug ("aa: publishing rule %s", ruleName);
    zmsg_addstr (message, "ADD");
    zmsg_addstr (message, rule);
    if (mlm_client_sendto (client, "fty-alert-engine", "rfc-evaluator-rules", NULL, 1000, &message) == 0) {
        zmsg_t *resp = mlm_client_recv (client);
        char *result = zmsg_popstr (resp);
        char *reason = zmsg_popstr (resp);

        if (streq (result, "OK") || streq (reason, "ALREADY_EXISTS"))
            alert.rulePublished = true;
        else
            log_error ("Error %s when requesting %s to ADD rule \n%s.", reason, mlm_client_sender (client), rule);

        zstr_free (&reason);
        zstr_free (&result);
        zmsg_destroy (&resp);
    };

    zstr_free (&rule);
    zstr_free (&ruleName);
    zmsg_destroy (&message);
}

void
Device::update (nut::TcpClient& conn)
{
    auto nutDevice = conn.getDevice (_nutName);
    if (! nutDevice.isOk ()) return;
    for (auto &it: _alerts) {
        try {
            std::string prefix = daisychainPrefix ();
            auto value = nutDevice.getVariableValue (prefix + it.first + ".status");
            if (value.empty ()) {
                log_debug ("aa: %s on %s is not present", it.first.c_str (), assetName ().c_str ());
            } else {
                std::string newStatus =  value[0];
                log_debug ("aa: %s on %s is %s", it.first.c_str (), assetName ().c_str (), newStatus.c_str ());
                if (it.second.status != newStatus) {
                    it.second.timestamp = ::time (NULL);
                    it.second.status = newStatus;
                }
            }
        } catch (...) {}
    }
}

std::string Device::daisychainPrefix () const
{
    if (chain () == 0) return "";
    return "device." + std::to_string (chain ()) + ".";
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
    dev.addAlert ("ambient.temperature", nothing);
    printf (".");
    assert (dev._alerts.empty ());

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
    dev.addAlert ("ambient.temperature", alerts);
    dev.addAlert ("ambient.humidity", alerts);
    assert (dev._alerts.size () == 2);
    assert (dev._alerts["ambient.humidity"].lowWarning == "10");
    assert (dev._alerts["ambient.humidity"].lowCritical == "10");
    assert (dev._alerts["ambient.temperature"].lowWarning == "10");
    assert (dev._alerts["ambient.temperature"].lowCritical == "5");
    assert (dev._alerts["ambient.temperature"].highWarning == "80");
    assert (dev._alerts["ambient.temperature"].highCritical == "100");
    //  @end
    printf (" OK\n");
}
