/*  =========================================================================
    fty_nut_configuration_server - fty nut configuration helper

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

#ifndef FTY_NUT_CONFIGURATION_HELPER_H_INCLUDED
#define FTY_NUT_CONFIGURATION_HELPER_H_INCLUDED

#include "fty_nut_library.h"

namespace fty
{
namespace nut
{

using namespace DBAssetsDiscovery;

using FtyProto = std::unique_ptr<fty_proto_t, std::function<void (fty_proto_t*)>>;

struct ComputeAssetConfigurationUpdateResult;

auto isConfSnmp =   [](const nutcommon::DeviceConfiguration& conf) -> bool { return conf.at("driver").find_first_of("snmp-ups") == 0; };
auto confSnmpVersion = [](const nutcommon::DeviceConfiguration& conf) -> int {
    if (!isConfSnmp(conf)) { return -1; }
    auto snmp_version = conf.find("snmp_version");
    if (snmp_version == conf.end() || snmp_version->second == "v1") { return 1; }
    else if (snmp_version->second == "v2c") { return 2; }
    else if (snmp_version->second == "v3") { return 3; }
    else { return 0; }
};
auto confSnmpMib =  [](const nutcommon::DeviceConfiguration& conf) -> std::string {
    return conf.count("mibs") > 0 ? conf.at("mibs") : "auto";
};
auto confSnmpSec =  [](const nutcommon::DeviceConfiguration& conf) -> std::string {
    return conf.count("secLevel") > 0 ? conf.at("secLevel") : "noAuthNoPriv";
};
auto confSnmpCom =  [](const nutcommon::DeviceConfiguration& conf) -> std::string {
    return conf.count("community") > 0 ? conf.at("community") : "public";
};

/**
 * \brief Functor to check if an element is before another in a collection.
 * \param start Start of collection.
 * \param end End of collection.
 * \param a First element to check.
 * \param b Second element to check.
 * \return True if a is before b in collection (missing elements are considered to be at the end of the collection).
 */
template <typename It, typename Val>
bool isBefore(It start, It end , const Val& a, const Val& b);

/**
 * \brief Check if we can assess a NUT driver configuration's working state.
 * \param configuration NUT driver configuration to assess.
 * \return True if it is assessable.
 *
 * Only drivers we know about can be assessed, as only they will be scanned by
 * assetScanDrivers().
 */
bool canDeviceConfigurationWorkingStateBeAssessed(const nutcommon::DeviceConfiguration& configuration);

/**
 * \brief Extract the security document types from a device configuration.
 * \param configuration Device configuration to analyse.
 * \return Set of security document types found in the device configuration.
 */
std::set<std::string> getSecurityDocumentTypesFromDeviceConfiguration(const nutcommon::DeviceConfiguration& configuration);

/**
 * \brief Extract all IP addresses from an asset.
 * \param proto Asset to extract IP addresses from.
 * \return List of IP addresses as strings.
 */
std::vector<std::string> getNetworkAddressesFromAsset(fty_proto_t* asset);

/**
 * \brief Pretty-print ComputeAssetConfigurationUpdateResult.
 * \param os Output stream.
 * \param results ComputeAssetConfigurationUpdateResult.
 * \return Output stream.
 */
std::string serialize(const ComputeAssetConfigurationUpdateResult& results);

/**
 * \brief Pretty-print set of security document IDs.
 * \param secwIDs Set of security document IDs to serialize.
 * \return String of security document IDs.
 */
std::string serialize(const std::set<secw::Id>& secwIDs);

std::string serialize(const nutcommon::DeviceConfiguration& conf);

/**
 * \brief Check if device configuration is a subset of another.
 * \param subset Device configuration subset.
 * \param superset Device configuration superset.
 * \return True iff subset of superset.
 */
bool isDeviceConfigurationSubsetOf(const nutcommon::DeviceConfiguration& subset, const nutcommon::DeviceConfiguration& superset);

/**
 * \brief Get non-default attributes from device configuration.
 * \param configuration Device configuration to extract non-default attributes from.
 * \param type Device configuration type work with.
 * \return Non-default attributes from device configuration.
 */
nutcommon::DeviceConfiguration getAttributesFromDeviceConfiguration(const nutcommon::DeviceConfiguration& configuration, const DeviceConfigurationInfoDetail& type);

}
}

#ifdef __cplusplus
extern "C" {
#endif

//  Self test of this class
FTY_NUT_EXPORT void fty_nut_configuration_helper_test (bool verbose);

#ifdef __cplusplus
}
#endif

#endif
