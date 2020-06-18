/*  =========================================================================
    fty_nut_configuration_helper - fty nut configuration helper

    Copyright (C) 2014 - 2018 Eaton

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
    fty_nut_configuration_helper - fty nut configuration helper
@discuss
@end
*/

#include "fty_nut_library.h"

namespace fty
{
namespace nut
{

template <typename It, typename Val>
bool isBefore(It start, It end , const Val& a, const Val& b) {
    const auto itA = std::find(start, end, a);
    const auto itB = std::find(start, end, b);
    return itA < itB;
}

// FIXME: needed for compilation
template bool isBefore(std::string const*, std::string const*, std::string const&, std::string const&);

bool canDeviceConfigurationWorkingStateBeAssessed(const fty::nut::DeviceConfiguration& configuration)
{
    const static std::set<std::string> knownDrivers = {
        "netxml-ups",
        "snmp-ups",
        "snmp-ups-dmf",
        "dummy-snmp"
    } ;

    return knownDrivers.count(configuration.at("driver"));
}

std::set<std::string> getSecurityDocumentTypesFromDeviceConfiguration(const fty::nut::DeviceConfiguration& configuration) {
    std::set<std::string> result;

    if (configuration.count("community")) {
        result.emplace("Snmpv1");
    }
    if (configuration.count("secName")) {
        result.emplace("Snmpv3");
    }

    return result;
}

std::vector<std::string> getNetworkAddressesFromAsset(fty_proto_t* asset)
{
    const static std::array<std::string, 2> prefixes = {
        "ip.",
        "ipv6."
    } ;

    // Fetch all network addresses.
    std::vector<std::string> addresses;
    for (const auto& prefix : prefixes) {
        const char* address;
        for (size_t i = 1; (address = fty_proto_ext_string(asset, (prefix + std::to_string(i)).c_str(), nullptr)); i++) {
            addresses.emplace_back(address);
        }
    }

    return addresses;
}

std::string serialize(const ComputeAssetConfigurationUpdateResult& results)
{
    std::stringstream ss;

    for (const auto& result : std::vector<std::pair<const char*, const fty::nut::DeviceConfigurations&>>({
        { "Working configurations:", results.workingConfigurations },
        { "Non-working configurations:", results.nonWorkingConfigurations },
        { "New configurations:", results.newConfigurations },
        { "Unknown state configurations:", results.unknownStateConfigurations },
    })) {
        ss << result.first << std::endl;
        for (const auto& configuration : result.second) {
            ss << configuration << std::endl;
        }
    }

    return ss.str();
}

std::string serialize(const std::set<secw::Id>& secwIDs) {
    std::stringstream ss;

    for (auto itSecwID = secwIDs.begin(); itSecwID != secwIDs.end(); itSecwID++) {
        if (itSecwID != secwIDs.begin()) {
            ss << "; ";
        }
        ss << *itSecwID;
    }

    return ss.str();
}

std::string serialize(const fty::nut::DeviceConfiguration& conf) {
    std::stringstream ss;

    ss << conf;

    return ss.str();
}

bool isDeviceConfigurationSubsetOf(const fty::nut::DeviceConfiguration& subset, const fty::nut::DeviceConfiguration& superset)
{
    for (const auto& itSubset : subset) {
        // Field "desc" is not important, skip it.
        if (itSubset.first == "desc") {
            continue;
        }

        auto itSuperset = superset.find(itSubset.first);
        if (itSuperset == superset.end() || itSubset != (*itSuperset)) {
            return false;
        }
    }

    return true;
}

fty::nut::DeviceConfiguration getAttributesFromDeviceConfiguration(const fty::nut::DeviceConfiguration& configuration, const DeviceConfigurationInfoDetail& type)
{
    fty::nut::DeviceConfiguration result = configuration;

    // Remove default attributes.
    for (const auto defaultAttribute : type.defaultAttributes) {
        result.erase(defaultAttribute.first);
    }

    // Remove extra+security document attributes.
    const static std::array<const char*, 9> extraAttributes {
         // Mandatory properties
        "device",
        "port",
         // SNMPv1
        "community",
         // SNMPv3
        "secLevel",
        "secName",
        "authPassword",
        "authProtocol",
        "privPassword",
        "privProtocol",
    };

    for (const auto& extraAttribute : extraAttributes) {
        result.erase(extraAttribute);
    }

    return result;
}

}
}
