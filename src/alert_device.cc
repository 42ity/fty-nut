#include "alert_device.h"
#include "agent_nut_library.h"
#include "logger.h"

void
Device::addAlert(const std::string& quantity, const std::map<std::string,std::vector<std::string> >& variables)
{
    log_debug ("aa: device %s provides %s alert", _name.c_str(), quantity.c_str());
    DeviceAlert alert;
    alert.name = quantity;

    if (_alerts.find (quantity) != _alerts.end()) {
        log_debug ("aa: device %s, alert %s already known", _name.c_str(), quantity.c_str());
        return;
    }

    // does the device evaluation?
    {
        const auto& it = variables.find(quantity + ".status");
        if (it == variables.cend ()) {
            log_debug ("aa: device %s doesn't support %s.status", _name.c_str(), quantity.c_str());
            return;
        }
    }
    // some devices provides ambient.temperature.(high|low)
    {
        const auto& it = variables.find(quantity + ".high");
        if (it != variables.cend ()) {
            alert.highWarning = it->second[0];
            alert.highCritical = it->second[0];
        }
    }
    {
        const auto& it = variables.find (quantity + ".low");
        if (it != variables.cend()) {
            alert.lowWarning = it->second[0];
            alert.lowCritical = it->second[0];
        }
    }
    // some devices provides ambient.temperature.(high|low).(warning|critical)
    {
        const auto& it = variables.find(quantity + ".high.warning");
        if (it != variables.cend()) alert.highWarning = it->second[0];
    }
    {
        const auto& it = variables.find(quantity + ".high.critical");
        if (it != variables.cend()) alert.highCritical = it->second[0];
    }
    {
        const auto& it = variables.find(quantity + ".low.warning");
        if (it != variables.cend()) alert.lowWarning = it->second[0];
    }
    {
        const auto& it = variables.find(quantity + ".low.critical");
        if (it != variables.cend()) alert.lowCritical = it->second[0];
    }
    if (
        alert.lowWarning.empty() ||
        alert.lowCritical.empty() ||
        alert.highWarning.empty() ||
        alert.highCritical.empty()
    ) {
        log_error("aa: thresholds for %s are not present in %s", quantity.c_str (), _name.c_str ());
    } else {
        _alerts[quantity] = alert;
    }
}

int
Device::scanCapabilities (nut::TcpClient& conn)
{
    log_debug ("aa: scanning capabilities for %s", _name.c_str());
    if (!conn.isConnected ()) return 0;
    
    _alerts.clear();
    try {
        auto nutDevice = conn.getDevice(_name);
        auto vars = nutDevice.getVariableValues();
        if (vars.find ("ambient.temperature") != vars.cend()) {
            addAlert ("ambient.temperature", vars);
        }
        if (vars.find ("ambient.humidity") != vars.cend()) {
            addAlert ("ambient.humidity", vars);
        }
        for (int a=1; a<=3; a++) {
            std::string q = "input.L" + std::to_string(a) + ".current";
            if (vars.find (q) != vars.cend()) {
                addAlert (q, vars);
            }
            q = "input.L" + std::to_string(a) + ".voltage";
            if (vars.find (q) != vars.cend()) {
                addAlert (q, vars);
            }
        }
        for (int a=1; a<=1000; a++) {
            int found = 0;
            std::string q = "outlet.group." + std::to_string(a) + ".current";
            if (vars.find (q) != vars.cend()) {
                addAlert (q, vars);
                ++found;
            }
            q = "outlet.group." + std::to_string(a) + ".voltage";
            if (vars.find (q) != vars.cend()) {
                addAlert (q, vars);
                ++found;
            }
            if (!found) break;
        }
    } catch ( std::exception &e ) {
        log_error("aa: Communication problem with %s (%s)", _name.c_str(), e.what() );
        return 0;
    }
    return 1;
}

void
Device::publishAlerts (mlm_client_t *client) {
    if (!client) return;
    log_debug("aa: publishing %zu alerts", _alerts.size ());
    for (auto& it: _alerts) {
        publishAlert (client, it.second);
    }
}

void
Device::publishAlert (mlm_client_t *client, DeviceAlert& alert)
{
    if (!client) return;
    const char *state = "ACTIVE", *severity = NULL;

    if (alert.status == "good") {
        state = "RESOLVED";
        severity = "ok";
    }
    else if (alert.status == "warning-low") {
        severity = "WARNING";
    }
    else if (alert.status == "critical-low") {
        severity = "CRITICAL";
    }
    else if (alert.status == "warning-high") {
        severity = "WARNING";
    }
    else if (alert.status == "critical-high") {
        severity = "CRITICAL";
    }
    std::string rule = alert.name + "@" + _name;
    std::string description = alert.name + " exceeded the limit";

    if (!severity) {
        log_error ("aa: alert %s has unknown severity value %s. Set to WARNING.", rule.c_str (), alert.status.c_str ());
        severity = "WARNING";
    }

    log_debug("aa: publishing alert %s", rule.c_str ());
    zmsg_t *message = bios_proto_encode_alert(
        NULL,               // aux
        rule.c_str (),      // rule
        _name.c_str (),     // element
        state,              // state
        severity,           // severity
        description.c_str (), // description
        alert.timestamp,    // timestamp
        ""                  // action ?email
    );
    if (message) {
        mlm_client_send (client, rule.c_str (), &message);
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

void
Device::publishRule (mlm_client_t *client, DeviceAlert& alert)
{
    if (!client || alert.rulePublished) return;

    zmsg_t *message = zmsg_new();
    assert (message);

    std::string description = alert.name + " exceeded the limit";
    std::string ruleName = alert.name + "@" + _name;
    std::string rule =
        "{ \"threshold\" : {"
        "  \"rule_name\"     : \"" + ruleName + "\","
        "  \"rule_source\"   : \"NUT\","
        "  \"rule_class\"    : \"Device internal\","
        "  \"target\"        : \"" + ruleName + "\","
        "  \"element\"       : \"" + _name + "\","
        "  \"values\"        : ["
        "    { \"low_warning\"  : \"" + alert.lowWarning + "\"},"
        "    { \"low_critical\" : \"" + alert.lowCritical + "\"},"
        "    { \"high_warning\"  : \"" + alert.highWarning + "\"},"
        "    { \"high_critical\" : \"" + alert.highCritical + "\"}"
        "    ],"
        "  \"results\"       : ["
        "    { \"low_critical\" : { \"action\" : [], \"description\" : \"" + description + "\" }},"
        "    { \"low_warning\"  : { \"action\" : [], \"description\" : \"" + description + "\"}},"
        "    {\"high_warning\"  : { \"action\" : [], \"description\" : \"" + description + "\" }},"
        "    {\"high_critical\" : { \"action\" : [], \"description\" : \"" + description + "\" } }"
        "  ] } }";
    log_debug("aa: publishing rule %s", ruleName.c_str ());
    zmsg_addstr (message, "ADD");
    zmsg_addstr (message, rule.c_str ());
    if (mlm_client_sendto (client, "alert-agent", "rfc-evaluator-rules", NULL, 1000, &message) == 0) {
        alert.rulePublished = true;
    };
    zmsg_destroy (&message);
}

void
Device::update (nut::TcpClient& conn)
{
    auto nutDevice = conn.getDevice(_name);
    for (auto &it: _alerts) {
        try {
            auto value = nutDevice.getVariableValue (it.first + ".status");
            if (!value.empty ()) {
                std::string newStatus =  value[0];
                log_debug ("aa: %s on %s is %s", it.first.c_str (), _name.c_str (), newStatus.c_str());
                if (it.second.status != newStatus) {
                    it.second.timestamp = ::time(NULL);
                    it.second.status = newStatus;
                }
            }
        } catch (...) {}
    }
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
