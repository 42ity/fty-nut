/*  =========================================================================
    fty_nut_configuration_manager - fty nut configuration manager

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
    fty_nut_configuration_manager - fty nut configuration manager
@discuss
@end
*/

#include "fty_nut_configuration_server.h"
#include "fty_nut_library.h"
#include "fty_nut_classes.h"
#include <fty_common_nut_credentials.h>

#include <forward_list>
#include <regex>
#include <future>

#define NUT_PART_STORE "/var/lib/fty/fty-nut/devices"

namespace fty
{
namespace nut
{

ConfigurationManager::ConfigurationManager(const std::string& dbConn) : m_poolScanners(8), m_dbConn(dbConn)
{
    //std::string assetName = "epdu-7";
    //std::string assetName = "ups-9";
    //std::string assetName = "ups-56";
    /*fty_proto_t* asset = fetchProtoFromAssetName(assetName);
    if (!asset) {
        throw std::runtime_error("Unknown asset " + assetName);
    }
    scanAssetConfigurations(asset);
    automaticAssetConfigurationPrioritySort(asset);

    fty_proto_destroy(&asset);*/
}

/**
 * \brief Reorder asset's driver configuration priorities according to the
 * software's preferences.
 * \param asset Asset to process.
 */
void ConfigurationManager::automaticAssetConfigurationPrioritySort(fty_proto_t* asset)
{
    auto conn = tntdb::connectCached(m_dbConn);
    const auto credentialsSNMPv1 = nutcommon::getCredentialsSNMPv1();
    const auto credentialsSNMPv3 = nutcommon::getCredentialsSNMPv3();
    const std::string assetName = fty_proto_name(asset);

    // Fetch all configurations to sort.
    const DeviceConfigurationInfos knownDatabaseConfigurations = get_all_config_list(conn, assetName);
    const nutcommon::DeviceConfigurations knownConfigurations = instanciateDatabaseConfigurations(knownDatabaseConfigurations, asset, credentialsSNMPv1, credentialsSNMPv3);

    // Compute new order of driver configuration.
    const std::vector<size_t> sortedIndexes = sortDeviceConfigurationPreferred(asset, knownConfigurations);
    std::vector<size_t> newConfigurationOrder;
    std::transform(sortedIndexes.begin(), sortedIndexes.end(), std::back_inserter(newConfigurationOrder),
        [&knownDatabaseConfigurations](size_t sortedIndex) -> size_t {
            return knownDatabaseConfigurations[sortedIndex].id;
        }
    );

    modify_config_priorities(conn, assetName, newConfigurationOrder);
}

/**
 * \brief Scan an asset and update driver configurations in database.
 * \param asset Asset to process.
 *
 * This method detects working configurations on the asset and updates the
 * driver configuration database in response. The basic workflow is:
 *  1. Scan the asset,
 *  2. Compute DB updates from detected and from known driver configurations,
 *  3. Mark existing configurations as working or non-working,
 *  4. Persist newly-discovered driver configurations in database.
 */
void ConfigurationManager::scanAssetConfigurations(fty_proto_t* asset)
{
    /// Step 0: Grab all data.
    auto conn = tntdb::connectCached(m_dbConn);
    const auto credentialsSNMPv1 = nutcommon::getCredentialsSNMPv1();
    const auto credentialsSNMPv3 = nutcommon::getCredentialsSNMPv3();
    const std::string assetName = fty_proto_name(asset);

    const DeviceConfigurationInfos knownDatabaseConfigurations = get_all_config_list(conn, assetName);
    const nutcommon::DeviceConfigurations knownConfigurations = instanciateDatabaseConfigurations(knownDatabaseConfigurations, asset, credentialsSNMPv1, credentialsSNMPv3);

    /// Step 1: Scan the asset.
    const auto detectedConfigurations = assetScanDrivers(m_poolScanners, asset, credentialsSNMPv1, credentialsSNMPv3);

    /// Step 2: Compute DB updates from detected and from known driver configurations.
    const auto results = computeAssetConfigurationUpdate(knownConfigurations, detectedConfigurations);

    log_debug("Summary of device configurations after scan for asset %s:\n%s", assetName.c_str(), serialize(results).c_str());

    /// Step 3: Mark existing configurations as working or non-working.
    for (const auto& updateOrder : std::vector<std::pair<const nutcommon::DeviceConfigurations&, bool>>({
        { results.workingConfigurations, true },
        { results.unknownStateConfigurations, true },
        { results.nonWorkingConfigurations, false },
    })) {
        // Match scan results to known driver configuration in database.
        for (const auto& configuration : updateOrder.first) {
            auto itConfigurationDatabase = knownConfigurations.begin();
            auto itKnownDatabaseConfiguration = knownDatabaseConfigurations.begin();

            for (; itConfigurationDatabase != knownConfigurations.end(); itConfigurationDatabase++, itKnownDatabaseConfiguration++) {
                if (isDeviceConfigurationSubsetOf(configuration, *itConfigurationDatabase)) {
                    // Found match, update database.
                    set_config_working(conn, itKnownDatabaseConfiguration->id, updateOrder.second);
                    log_info("Marked device configuration ID %u for asset %s as %s.",
                        itKnownDatabaseConfiguration->id,
                        assetName.c_str(),
                        updateOrder.second ? "working" : "non-working"
                    );
                    break;
                }
            }

            if (itConfigurationDatabase == knownConfigurations.end()) {
                log_warning("Failed to match known detected device configuration to what's in database, configuration ignored:\n%s", serialize(configuration).c_str());
            }
        }
    }

    /// Step 4: Persist newly-discovered driver configurations in database.
    const auto deviceConfigurationTypes = get_all_configuration_types(conn);
    for (const auto& newConfiguration : results.newConfigurations) {
        // Match new configuration to configuration type.
        const auto bestDeviceConfigurationType = matchDeviceConfigurationToBestDeviceConfigurationType(asset, newConfiguration, deviceConfigurationTypes);

        if (bestDeviceConfigurationType != deviceConfigurationTypes.end()) {
            // Save new configuration into database.
            const auto attributes = getAttributesFromDeviceConfiguration(newConfiguration, *bestDeviceConfigurationType);
            const auto secwIDs = nutcommon::matchSecurityDocumentIDsFromDeviceConfiguration(newConfiguration, credentialsSNMPv1, credentialsSNMPv3);
            const auto configID = insert_config(conn, assetName, bestDeviceConfigurationType->id, true, true, secwIDs, attributes);

            log_info("Instanciated device configuration type \"%s\" for asset %s (id=%u, driver=%s, port=%s, secwIDs={%s}).",
                bestDeviceConfigurationType->prettyName.c_str(),
                assetName.c_str(),
                configID,
                newConfiguration.at("driver").c_str(),
                newConfiguration.at("port").c_str(),
                serialize(secwIDs).c_str()
            );
        }
        else {
            log_warning("Failed to match new device configuration to device configuration type, configuration discarded:\n%s", serialize(newConfiguration).c_str());
        }
    }
}

/**
 * \brief Update asset configuration in config file.
 * \param asset Asset to process.
 * \param config Config to process.
 */
void ConfigurationManager::updateDeviceConfigurationFile(const std::string &name, nutcommon::DeviceConfiguration config)
{
    const std::string configFilePath = std::string(NUT_PART_STORE) + shared::path_separator() + name;

    shared::mkdir_if_needed(NUT_PART_STORE);

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
    }
    else {
        log_info("Configuration file '%s' unchanged, no actions to perform.", configFilePath.c_str());
    }
}


/**
 * \brief Apply asset configuration in database.
 * \param asset Asset to process.
 */
void ConfigurationManager::applyAssetConfiguration(fty_proto_t* asset)
{
    auto conn = tntdb::connectCached(m_dbConn);
    const std::string assetName = fty_proto_name(asset);

    // Get candidate configurations and take the first one
    const DeviceConfigurationInfos candidateDatabaseConfigurations = get_candidate_config_list(conn, assetName);
    if (candidateDatabaseConfigurations.size() > 0) {
        DeviceConfigurationInfo config = candidateDatabaseConfigurations.at(0);
        // Save configuration into config file
        updateDeviceConfigurationFile(assetName, config.attributes);
    }
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
fty_nut_configuration_manager_test (bool verbose)
{
    std::cerr << " * fty_nut_configuration_manager: no test" << std::endl;
}
