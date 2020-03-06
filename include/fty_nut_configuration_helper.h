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

// Break cyclic dependencies on headers by including our dependencies early.
#include <fty_common_nut.h>
#include <fty_proto.h>
#include <fty_security_wallet.h>

namespace fty
{
namespace nut
{

using namespace DBAssetsDiscovery;

using FtyProto = std::unique_ptr<fty_proto_t, std::function<void (fty_proto_t*)>>;
using SecwMap = std::map<secw::Id, secw::DocumentPtr>;

struct ComputeAssetConfigurationUpdateResult;

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
bool canDeviceConfigurationWorkingStateBeAssessed(const fty::nut::DeviceConfiguration& configuration);

/**
 * \brief Extract the security document types from a device configuration.
 * \param configuration Device configuration to analyse.
 * \return Set of security document types found in the device configuration.
 */
std::set<std::string> getSecurityDocumentTypesFromDeviceConfiguration(const fty::nut::DeviceConfiguration& configuration);

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
/**
 * \brief Pretty-print NUT driver configuration.
 * \param conf Configuration to serialize.
 * \return String of NUT device configuration.
 */
std::string serialize(const fty::nut::DeviceConfiguration& conf);

/**
 * \brief Check if device configuration is a subset of another.
 * \param subset Device configuration subset.
 * \param superset Device configuration superset.
 * \return True iff subset of superset.
 */
bool isDeviceConfigurationSubsetOf(const fty::nut::DeviceConfiguration& subset, const fty::nut::DeviceConfiguration& superset);

/**
 * \brief Get non-default attributes from device configuration.
 * \param configuration Device configuration to extract non-default attributes from.
 * \param type Device configuration type work with.
 * \return Non-default attributes from device configuration.
 */
fty::nut::DeviceConfiguration getAttributesFromDeviceConfiguration(const fty::nut::DeviceConfiguration& configuration, const DeviceConfigurationInfoDetail& type);

}
}

#endif
