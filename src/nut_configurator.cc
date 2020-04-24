/*  =========================================================================
    nut_configurator - NUT configurator class

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

/*
@header
    nut_configurator - NUT configurator class
@discuss
@end
*/

#include "nut_configurator.h"
#include <fty_common_mlm_subprocess.h>
#include <fty_common_filesystem.h>
#include <fty_log.h>
#include "cidr.h"

#include <cxxtools/jsondeserializer.h>
#include <cxxtools/regex.h>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <czmq.h>
#include <string>
#include <regex>

using namespace shared;

#define NUT_PART_STORE "/var/lib/fty/fty-nut/devices"

static std::string s_getPollingInterval()
{
    std::string polling = "30";
    zconfig_t *config = zconfig_load("/etc/fty-nut/fty-nut.cfg");
    if (config) {
        polling = zconfig_get(config, "nut/polling_interval", polling.c_str());
        zconfig_destroy(&config);
    }

    return polling;
}

static bool isEpdu(const nutcommon::DeviceConfiguration &config)
{
    static const std::set<std::string> epdusMibs = {
        { "eaton_epdu" }, { "aphel_genesisII" }, { "aphel_revelation" }, { "pulizzi_switched1" }, { "pulizzi_switched2" }, { "emerson_avocent_pdu" }
    } ;

    // Match MIBs.
    auto mibsIt = config.find("mibs");
    if (mibsIt != config.end()) {
        if (epdusMibs.count(mibsIt->second)) {
            return true;
        }
    }

    // Match description.
    auto descIt = config.find("desc");
    if (descIt != config.end()) {
        if (descIt->second.find("epdu") != std::string::npos) {
            return true;
        }
    }

    return false;
}

static bool isAts(const nutcommon::DeviceConfiguration &config)
{
    static const std::set<std::string> atsMibs = {
        { "eaton_ats16" }, { "eaton_ats16_g2" }, { "eaton_ats30" }, { "apc_ats" }
    } ;

    // Match MIBs.
    auto mibsIt = config.find("mibs");
    if (mibsIt != config.end()) {
        if (atsMibs.count(mibsIt->second)) {
            return true;
        }
    }

    return false;
}

static bool isUps(const nutcommon::DeviceConfiguration &config)
{
    return !(isEpdu(config) || isAts(config));
}

static bool canSnmp(const nutcommon::DeviceConfiguration &config)
{
    // Match MIBs.
    auto driverIt = config.find("driver");
    if (driverIt != config.end()) {
        if (driverIt->second == "snmp-ups" || driverIt->second == "snmp-ups-dmf" || driverIt->second == "snmp-ups-old") {
            return true;
        }
    }

    return false;
}

static bool canNetXml(const nutcommon::DeviceConfiguration &config)
{
    // Match driver.
    auto driverIt = config.find("driver");
    if (driverIt != config.end()) {
        if (driverIt->second == "netxml-ups") {
            return true;
        }
    }

    return false;
}

nutcommon::DeviceConfigurations::const_iterator NUTConfigurator::getBestSnmpMibConfiguration(const nutcommon::DeviceConfigurations &configs)
{
    // MIBs in order of priority.
    static const std::vector<std::regex> snmpMibPriority = {
        std::regex("pw") , std::regex("mge") , std::regex(".+")
    };

    for (const auto &regex : snmpMibPriority) {
        auto it = std::find_if(configs.cbegin(), configs.cend(), [&regex](const nutcommon::DeviceConfiguration &config) {
            // Match MIBs.
            auto mibsIt = config.find("mibs");
            if (mibsIt != config.end()) {
                if (std::regex_match(mibsIt->second, regex)) {
                    return true;
                }
            }
            return false;
        });

        if (it != configs.cend()) {
            return it;
        }
    }

    return configs.cend();
}

nutcommon::DeviceConfigurations::const_iterator NUTConfigurator::getNetXMLConfiguration(const nutcommon::DeviceConfigurations &configs)
{
    return std::find_if(configs.cbegin(), configs.cend(), [](const nutcommon::DeviceConfiguration &config) {
        auto mibsIt = config.find("driver");
        if (mibsIt != config.end()) {
            if (mibsIt->second == "netxml-ups") {
                return true;
            }
        }
        return false;
    });
}

nutcommon::DeviceConfigurations::const_iterator NUTConfigurator::selectBestConfiguration(const nutcommon::DeviceConfigurations &configs)
{
    bool bIsEpdu    = std::any_of(configs.begin(), configs.end(), isEpdu);
    bool bIsUps     = std::any_of(configs.begin(), configs.end(), isUps);
    bool bIsAts     = std::any_of(configs.begin(), configs.end(), isAts);
    bool bCanSnmp   = std::any_of(configs.begin(), configs.end(), canSnmp);
    bool bCanNetXml = std::any_of(configs.begin(), configs.end(), canNetXml);
    log_debug("Configurations: %d; isEpdu: %i; isUps: %i; isAts: %i; canSnmp: %i; canNetXml: %i.", configs.size(), bIsEpdu, bIsUps, bIsAts, bCanSnmp, bCanNetXml);

    nutcommon::DeviceConfigurations::const_iterator bestConfig = configs.begin();

    if (bCanSnmp && (bIsEpdu || bIsAts)) {
        log_debug("SNMP capable ePDU/ATS => Use SNMP.");
        bestConfig = getBestSnmpMibConfiguration(configs);
    } else {
        if (bCanNetXml) {
            log_debug("NetXML capable device => Use NetXML.");
            bestConfig = getNetXMLConfiguration(configs);
        }
        else if (bCanSnmp) {
            log_debug("SNMP capable device => Use SNMP.");
            bestConfig = getBestSnmpMibConfiguration(configs);
        }
        else {
            log_debug("Unsure of device type => Use first configuration.");
        }
    }

    return bestConfig;
}

void NUTConfigurator::systemctl( const std::string &operation, const std::string &service )
{
    systemctl(operation, &service, &service + 1);
}

template<typename It>
void NUTConfigurator::systemctl( const std::string &operation, It first, It last)
{
    if (first == last)
        return;
    std::vector<std::string> _argv = {"sudo", "systemctl", operation };
    // FIXME: Split the argument list into chunks if its size is close to
    // sysconf(_SC_ARG_MAX). Note that the limit is reasonably high on modern
    // kernels (stack size / 4, i.e. 2MB typically), so we will only hit it
    // with with five digit device counts.
    _argv.insert(_argv.end(), first, last);
    MlmSubprocess::SubProcess systemd( _argv );
    if( systemd.run() ) {
        int result = systemd.wait();
        log_info("sudo systemctl %s result %i (%s) for following units",
                 operation.c_str(),
                 result,
                 (result == 0 ? "ok" : "failed"));
        for (It it = first; it != last; ++it)
            log_info(" - %s", it->c_str());
    } else {
        log_error("can't run sudo systemctl %s for following units",
                  operation.c_str());
        for (It it = first; it != last; ++it)
            log_error(" - %s", it->c_str());
    }
}

void NUTConfigurator::updateNUTConfig()
{
    // Run the helper script
    std::vector<std::string> _argv = { "sudo", "fty-nutconfig" };
    MlmSubprocess::SubProcess systemd( _argv );
    if( systemd.run() ) {
        int result = systemd.wait();
        if (result == 0) {
            log_info("Command 'sudo fty-nutconfig' succeeded.");
        }
        else {
            log_error("Command 'sudo fty-nutconfig' failed with status=%i.", result);
        }
    } else {
        log_error("Can't run command 'sudo fty-nutconfig'.");
    }
}

nutcommon::DeviceConfigurations NUTConfigurator::getConfigurationFromUpsConfBlock(const std::string &name, const AutoConfigurationInfo &info)
{
    nutcommon::DeviceConfigurations configs;

    std::string UBA = info.asset->upsconf_block(); // UpsconfBlockAsset - as stored in contents of the asset.
    char SEP = UBA.at(0);
    if (SEP == '\0' || UBA.at(1) == '\0') {
        log_info("Device '%s' is configured with an empty explicit upsconf_block from its asset (adding asset name as NUT device-tag with no config).", name.c_str());
        configs = { { { "name", name } } };
    }
    else {
        // First character of the sufficiently long UB string defines the user-selected line separator character.
        std::string UBN = UBA.substr(1); //UpsconfBlockNut - with EOL chars, without leading SEP character.
        std::replace(UBN.begin(), UBN.end(), SEP, '\n');
        if ( UBN.at(0) == '[' ) {
            log_info("Device '%s' is configured with a complete explicit upsconf_block from its asset, including a custom NUT device-tag:\n%s", name.c_str(), UBN.c_str());
            configs = nutcommon::parseConfigurationFile(UBN);
        } else {
            log_info("Device '%s' is configured with a content-only explicit upsconf_block from its asset (prepending asset name as NUT device-tag):\n%s", name.c_str(), UBN.c_str());
            configs = nutcommon::parseConfigurationFile(std::string("[") + name + "]\n" + UBN + "\n");
        }
    }

    return configs;
}

nutcommon::DeviceConfigurations NUTConfigurator::getConfigurationFromScanningDevice(const std::string &name, const AutoConfigurationInfo &info)
{
    const int scanTimeout = 10;
    const std::string& IP = info.asset->IP();
    nutcommon::DeviceConfigurations configs;

    if (IP.empty()) {
        log_error("Device '%s' has no IP address, cannot scan it.", name.c_str());
    }
    else {
        const bool use_dmf = info.asset->upsconf_enable_dmf();

        // SNMPv3 scan
        {
            const auto credentials = nutcommon::getCredentialsSNMPv3();

            for (const auto& credential : credentials) {
                log_info("Scanning SNMPv3 protocol (security name '%s') at '%s'...", credential.secName.c_str(), IP.c_str());
                if (nutcommon::scanDeviceRangeSNMPv3(nutcommon::ScanRangeOptions(IP, scanTimeout), credential, use_dmf, configs) == 0 && !configs.empty()) {
                    log_info("SNMPv3 credential with security name '%s' at '%s' is suitable, bail out of SNMP scanning.", credential.secName.c_str(), IP.c_str());
                    break;
                }
            }
        }
        // SNMPv1 scan - (only if SNMPv3 yielded nothing)
        if (configs.empty())
        {
            const auto credentials = nutcommon::getCredentialsSNMPv1();

            for (const auto& credential : credentials) {
                log_info("Scanning SNMPv1 protocol (community '%s') at '%s'...", credential.community.c_str(), IP.c_str());
                if (nutcommon::scanDeviceRangeSNMPv1(nutcommon::ScanRangeOptions(IP, scanTimeout), credential, use_dmf, configs) == 0 && !configs.empty()) {
                    log_info("SNMPv1 community '%s' at '%s' is suitable, bail out of SNMP scanning.", credential.community.c_str(), IP.c_str());
                    break;
                }
            }
        }
        // NetXML scan
        {
            log_info("Scanning NetXML protocol at '%s'...", IP.c_str());
            nutcommon::scanDeviceRangeNetXML(nutcommon::ScanRangeOptions(IP, scanTimeout), configs);
        }
    }

    return configs;
}

void NUTConfigurator::updateDeviceConfiguration(const std::string &name, const AutoConfigurationInfo &info, nutcommon::DeviceConfiguration config)
{
    const std::string polling = s_getPollingInterval();
    const std::string configFilePath = std::string(NUT_PART_STORE) + path_separator() + name;

    // Complete configuration.
    config["name"] = name;
    if (isEpdu(config) && canSnmp(config)) {
        config["synchronous"] = "yes";
    }
    if (canNetXml(config)) {
        config["timeout"] = "15";
    }
    if (canSnmp(config)) {
        config["pollfreq"] = polling;
    }
    else {
        config["pollinterval"] = polling;
    }

    mkdir_if_needed(NUT_PART_STORE);

    // Get old and create new configuration strings.
    std::string oldConfiguration, newConfiguration;
    {
        std::ifstream file(configFilePath);
        std::stringstream buffer;
        buffer << file.rdbuf();
        oldConfiguration = buffer.str();
    }
    {
        std::stringstream buffer;
        buffer << config;
        newConfiguration = buffer.str();
    }

    if (oldConfiguration != newConfiguration) {
        log_info("Configuration file '%s' is outdated, creating new one.", configFilePath.c_str());

        std::ofstream cfgFile(configFilePath);
        cfgFile << newConfiguration;
        cfgFile.flush();
        cfgFile.close();

        start_drivers_.insert("nut-driver@" + name);
    }
    else {
        log_info("Configuration file '%s' unchanged, no actions to perform.", configFilePath.c_str());
    }
}

bool NUTConfigurator::configure(const std::string &name, const AutoConfigurationInfo &info)
{
    log_debug("Auto-configuring device '%s'...", name.c_str());

    nutcommon::DeviceConfigurations configs;

    if (info.asset->have_upsconf_block()) {
        // Device has a predefined configuration block.
        configs = getConfigurationFromUpsConfBlock(name, info);
    }
    else {
        // Device has to be scanned.
        configs = getConfigurationFromScanningDevice(name, info);
    }

    auto it = selectBestConfiguration(configs);
    if (it == configs.end()) {
        log_error("No suitable configuration found for device '%s'.", name.c_str());
        return false; // Try again later.
    }

    updateDeviceConfiguration(name, info, *it);
    return true;
}

void NUTConfigurator::erase(const std::string &name)
{
    const std::string filePath = std::string(NUT_PART_STORE) + path_separator() + name;
    log_info("Removing configuration file '%s'.", filePath.c_str());

    remove(filePath.c_str());
    stop_drivers_.insert("nut-driver@" + name);
}

void NUTConfigurator::commit()
{
    if (manage_systemctl) {
        systemctl("disable", stop_drivers_.begin(),  stop_drivers_.end());
        systemctl("stop",    stop_drivers_.begin(),  stop_drivers_.end());
    } else {
        log_info("Updating NUT configs, expecting it to manage the service units as needed.");
    }
    updateNUTConfig();
    if (manage_systemctl) {
        systemctl("restart", start_drivers_.begin(), start_drivers_.end());
        systemctl("enable",  start_drivers_.begin(), start_drivers_.end());
        if (!stop_drivers_.empty() || !start_drivers_.empty())
            systemctl("reload-or-restart", "nut-server");
    }
    stop_drivers_.clear();
    start_drivers_.clear();
}

bool NUTConfigurator::known_assets(std::vector<std::string>& assets)
{
    return shared::is_file_in_directory(NUT_PART_STORE, assets);
}

void
nut_configurator_test (bool verbose)
{
}
