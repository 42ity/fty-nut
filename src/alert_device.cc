#include "alert_device.h"
#include "agent_nut_library.h"
#include "logger.h"

void
Device::addAlert(const std::string& quantity, const std::map<std::string,std::vector<std::string> >& variables)
{
    log_debug ("aa: device %s provides %s alert", _name.c_str(), quantity.c_str());
    device_alert_t alert;
    alert.name = quantity;

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
            std::string q = "oulet.group." + std::to_string(a) + ".current";
            if (vars.find (q) != vars.cend()) {
                addAlert (q, vars);
            } else {
                // assume end of outlet groups if value is missing
                break;
            }
            q = "outlet.group." + std::to_string(a) + ".voltage";
            if (vars.find (q) != vars.cend()) {
                addAlert (q, vars);
            }
        }
    } catch ( std::exception &e ) {
        log_error("Communication problem with %s (%s)", _name.c_str(), e.what() );
        return 0;
    }
    return 1;
}

void
Device::update (nut::TcpClient& conn)
{
    auto nutDevice = conn.getDevice(_name);
    for (auto &it: _alerts) {
        try {
            auto value = nutDevice.getVariableValue (it.first + ".status");
            if (value.size ()) {
                log_debug ("aa: %s on %s is %s", it.first.c_str (), _name.c_str (), value[0].c_str());
                it.second.status = value[0];
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
    //  @end
    printf ("Empty test - OK\n");
}
