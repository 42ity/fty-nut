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

/**
 * \brief Functor to check if an element is before another in a collection.
 * \param start Start of collection.
 * \param end End of collection.
 * \param a First element to check.
 * \param b Second element to check.
 * \return True if a is before b in collection (missing elements are considered to be at the end of the collection).
 */
template <typename It, typename Val>
bool isBefore(It start, It end , const Val& a, const Val& b) {
    const auto itA = std::find(start, end, a);
    const auto itB = std::find(start, end, b);
    return itA < itB;
}

// FIXME: needed for compilation
template bool isBefore(std::string const*, std::string const*, std::string const&, std::string const&);

/**
 * \brief Check if we can assess a NUT driver configuration's working state.
 * \param configuration NUT driver configuration to assess.
 * \return True if it is assessable.
 *
 * Only drivers we know about can be assessed, as only they will be scanned by
 * assetScanDrivers().
 */
bool canDeviceConfigurationWorkingStateBeAssessed(const nutcommon::DeviceConfiguration& configuration)
{
    const static std::set<std::string> knownDrivers = {
        "netxml-ups",
        "snmp-ups",
        "snmp-ups-dmf",
        "dummy-snmp"
    } ;

    return knownDrivers.count(configuration.at("driver"));
}

/**
 * \brief Extract the security document types from a device configuration.
 * \param configuration Device configuration to analyse.
 * \return Set of security document types found in the device configuration.
 */
std::set<std::string> getSecurityDocumentTypesFromDeviceConfiguration(const nutcommon::DeviceConfiguration& configuration) {
    std::set<std::string> result;

    if (configuration.count("community")) {
        result.emplace("Snmpv1");
    }
    if (configuration.count("secName")) {
        result.emplace("Snmpv3");
    }

    return result;
}

/**
 * \brief Extract all IP addresses from an asset.
 * \param proto Asset to extract IP addresses from.
 * \return List of IP addresses as strings.
 */
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


/**
 * \brief Pretty-print ComputeAssetConfigurationUpdateResult.
 * \param os Output stream.
 * \param results ComputeAssetConfigurationUpdateResult.
 * \return Output stream.
 */
std::string serialize( const ComputeAssetConfigurationUpdateResult& results)
{
    std::stringstream ss;

    for (const auto& result : std::vector<std::pair<const char*, const nutcommon::DeviceConfigurations&>>({
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

/**
 * \brief Pretty-print set of security document IDs.
 * \param secwIDs Set of security document IDs to serialize.
 * \return String of security document IDs.
 */
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

std::string serialize(const nutcommon::DeviceConfiguration& conf) {
    std::stringstream ss;

    ss << conf;

    return ss.str();
}


/**
 * \brief Check if device configuration is a subset of another.
 * \param subset Device configuration subset.
 * \param superset Device configuration superset.
 * \return True iff subset of superset.
 */
bool isDeviceConfigurationSubsetOf(const nutcommon::DeviceConfiguration& subset, const nutcommon::DeviceConfiguration& superset)
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


/**
 * \brief Get non-default attributes from device configuration.
 * \param configuration Device configuration to extract non-default attributes from.
 * \param type Device configuration type work with.
 * \return Non-default attributes from device configuration.
 */
nutcommon::DeviceConfiguration getAttributesFromDeviceConfiguration(const nutcommon::DeviceConfiguration& configuration, const DeviceConfigurationInfoDetail& type)
{
    nutcommon::DeviceConfiguration result = configuration;

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

//  --------------------------------------------------------------------------
//  Self test of this class

// If your selftest reads SCMed fixture data, please keep it in
// src/selftest-ro; if your test creates filesystem objects, please
// do so under src/selftest-rw.
// The following pattern is suggested for C selftest code:
//    char *filename = NULL;
//    filename = zsys_sprintf ("%s/%s", SELFTEST_DIR_RO, "mytemplate.file");
//    assert (filename);
//    ... use the "filename" for I/O ...
//    zstr_free (&filename);
// This way the same "filename" variable can be reused for many subtests.
#define SELFTEST_DIR_RO "src/selftest-ro"
#define SELFTEST_DIR_RW "src/selftest-rw"

void
fty_nut_configuration_helper_test (bool verbose)
{
    std::cerr << " * fty_nut_configuration_helper: no test" << std::endl;
}
