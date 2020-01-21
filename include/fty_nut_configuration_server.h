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

#include "fty_nut_library.h"

namespace fty
{
namespace nut
{
    
using namespace DBAssetsDiscovery;
    
typedef struct ComputeAssetConfigurationUpdateResult {
    /// \brief Known, working configuration.
    nutcommon::DeviceConfigurations workingConfigurations;
    /// \brief Known, non-working configuration.
    nutcommon::DeviceConfigurations nonWorkingConfigurations;
    /// \brief Unknown, working configuration.
    nutcommon::DeviceConfigurations newConfigurations;
    /// \brief Unknown, unassessable configuration.
    nutcommon::DeviceConfigurations unknownStateConfigurations;
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
 * \param credentialsSnmpV1 SNMPv1 credentials to test.
 * \param credentialsSnmpV3 SNMPv3 credentials to test.
 * \return All detected and working NUT device configurations.
 */
nutcommon::DeviceConfigurations assetScanDrivers(messagebus::PoolWorker& pool, fty_proto_t *asset, const std::vector<nutcommon::CredentialsSNMPv1>& credentialsSnmpV1, const std::vector<nutcommon::CredentialsSNMPv3>& credentialsSnmpV3);

/**
 * \brief Sort NUT driver configurations into categories from known and detected configurations.
 * \param knownConfigurations Known NUT device configurations in database.
 * \param detectedConfigurations Detected NUT device configurations at runtime.
 * \return All NUT driver configurations sorted into categories.
 */
ComputeAssetConfigurationUpdateResult computeAssetConfigurationUpdate(const nutcommon::DeviceConfigurations& knownConfigurations, const nutcommon::DeviceConfigurations& detectedConfigurations);

nutcommon::DeviceConfigurations instanciateDatabaseConfigurations(const DeviceConfigurationInfos& dbConfs, fty_proto_t* asset, const std::vector<nutcommon::CredentialsSNMPv1>& credentialsSNMPv1, const std::vector<nutcommon::CredentialsSNMPv3>& credentialsSNMPv3);

/**
 * \brief Find which device configuration type a given device configuration best matches.
 * \param asset Asset to check with.
 * \param configuration Device configuration to match.
 * \param types Device configuration types to match against.
 * \return Iterator to best device configuration type match, or end of collection if no suitable match found.
 */
DeviceConfigurationInfoDetails::const_iterator matchDeviceConfigurationToBestDeviceConfigurationType(fty_proto_t* asset, const nutcommon::DeviceConfiguration& configuration, const DeviceConfigurationInfoDetails& types);

/**
 * \brief Return the order of preference for an asset's driver configurations.
 * \param asset Asset to sort device configurations with.
 * \param configurations Device configurations to sort.
 * \return List of indexes of driver configurations, ordered from most to least preferred.
 */
std::vector<size_t> sortDeviceConfigurationPreferred(fty_proto_t* asset, const nutcommon::DeviceConfigurations& configurations);

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
