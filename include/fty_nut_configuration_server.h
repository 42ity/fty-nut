/*  =========================================================================
    fty_nut_configuration_server - fty nut configuration actor

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

#ifndef FTY_NUT_CONFIGURATION_SERVER_H_INCLUDED
#define FTY_NUT_CONFIGURATION_SERVER_H_INCLUDED

#include "fty_nut_configuration_helper.h"
#include "fty_nut_library.h"

namespace fty
{
namespace nut
{

using namespace DBAssetsDiscovery;

typedef struct ComputeAssetConfigurationUpdateResult {
    /// \brief Known, working configuration.
    fty::nut::DeviceConfigurations workingConfigurations;
    /// \brief Known, non-working configuration.
    fty::nut::DeviceConfigurations nonWorkingConfigurations;
    /// \brief Unknown, working configuration.
    fty::nut::DeviceConfigurations newConfigurations;
    /// \brief Unknown, unassessable configuration.
    fty::nut::DeviceConfigurations unknownStateConfigurations;
} ComputeAssetConfigurationUpdateResult;

/**
 * \brief Scan asset for NUT driver configurations.
 *
 * The scan will detect the following drivers:
 * - netxml-ups
 * - snmp-ups (SNMPv1 and SNMPv3)
 * - snmp-ups-dmf (SNMPv1 and SNMPv3)
 *
 * \warning This won't return the list of all *working* NUT device configurations, as the list of handled NUT drivers is not exhaustive!
 *
 * \param pool PoolWorker to use.
 * \param asset fty_proto_t of asset to scan.
 * \param credentials Credentials to test.
 * \return All detected and working NUT device configurations.
 */
fty::nut::DeviceConfigurations assetScanDrivers(messagebus::PoolWorker& pool, fty_proto_t *asset, const fty::nut::SecwMap& credentials, const bool scanDummyUps);

/**
 * \brief Sort NUT driver configurations into categories from known and detected configurations.
 * \param knownConfigurations Known NUT device configurations in database.
 * \param detectedConfigurations Detected NUT device configurations at runtime.
 * \return All NUT driver configurations sorted into categories.
 */
ComputeAssetConfigurationUpdateResult computeAssetConfigurationUpdate(const fty::nut::DeviceConfigurations& knownConfigurations, const fty::nut::DeviceConfigurations& detectedConfigurations);

/**
 * \brief Return the order of preference for an asset's driver configurations.
 * \param asset Asset to sort device configurations with.
 * \param configurations Device configurations to sort.
 * \param prioritizeDmfDriver Prioritize DMF driver when applicable.
 * \return List of indexes of driver configurations, ordered from most to least preferred.
 */
std::vector<size_t> sortDeviceConfigurationPreferred(fty_proto_t* asset, const fty::nut::DeviceConfigurations& configurations, const bool prioritizeDmfDriver = false);

/**
 * \brief Find which device configuration type a given device configuration best matches.
 * \param asset Asset to check with.
 * \param configuration Device configuration to match.
 * \param types Device configuration types to match against.
 * \return Iterator to best device configuration type match, or end of collection if no suitable match found.
 */
DeviceConfigurationInfoDetails::const_iterator matchDeviceConfigurationToBestDeviceConfigurationType(fty_proto_t* asset, const fty::nut::DeviceConfiguration& configuration, const DeviceConfigurationInfoDetails& types);

/**
 * \brief Instanciate database configurations as NUT driver configurations.
 * \param dbConfs Database configurations to instanciate.
 * \param asset Asset to instanciate with.
 * \param credentials Credentials to instanciate with.
 * \return List of ready-to-use NUT device configurations.
 */
fty::nut::DeviceConfigurations instanciateDatabaseConfigurations(const DBAssetsDiscovery::DeviceConfigurationInfos& dbConfs, fty_proto_t* asset, const fty::nut::SecwMap& credentials);

/**
 * \brief Match security documents from driver configuration.
 * \param conf Device configuration to search.
 * \param credentials Security documents to match.
 * \return Set of security document IDs matched.
 */
std::set<secw::Id> matchSecurityDocumentIDsFromDeviceConfiguration(const fty::nut::DeviceConfiguration& conf, const fty::nut::SecwMap& credentials);

#if 0
/**
 * \brief Request fty_proto_t from asset name.
 * \param assetName Asset name to request.
 * \return fty_proto_t (to be freed by caller) or nullptr.
 */
fty_proto_t* fetchProtoFromAssetName(const std::string& assetName);
#endif

}
}

#ifdef __cplusplus
extern "C" {
#endif

//  Self test of this class
FTY_NUT_EXPORT void fty_nut_configuration_server_test (bool verbose);

#ifdef __cplusplus
}
#endif

#endif
