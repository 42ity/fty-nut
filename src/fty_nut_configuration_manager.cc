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
#include <bits/unique_ptr.h>

namespace fty {
namespace nut {

const static std::string NUT_PART_STORE("/var/lib/fty/fty-nut/devices");

static fty::nut::DeviceConfigurations assetUpsConfBlock(fty_proto_t* asset)
{
    fty::nut::DeviceConfigurations result;
    const char* upsconf = fty_proto_ext_string(asset, "upsconf_block", nullptr);

    if (upsconf && strlen(upsconf) > 2) {
        char separator = upsconf[0];
        std::string configurationFile(upsconf+1);
        std::replace(configurationFile.begin(), configurationFile.end(), separator, '\n');

        if (configurationFile.at(0) == '[') {
            result = fty::nut::parseConfigurationFile(configurationFile);
        }
        else {
            result = fty::nut::parseConfigurationFile(std::string("[") + fty_proto_name(asset) + "]\n" + configurationFile + "\n");
        }
    }

    return result;
}

ConfigurationManager::Parameters::Parameters() :
    dbConn(DBConn::url),
    nutRepositoryDirectory(NUT_PART_STORE),
    threadPoolScannerSize(1),
    scanDummyUps(false),
    preferDmfForSnmp(false)
{
}

std::mutex& ConfigurationManager::AssetMutex::operator[](const std::string& asset)
{
    std::lock_guard<std::mutex> lk(m_mutex);

    std::mutex& mutex = m_mutexes[asset];
    return mutex;
}

ConfigurationManager::ConfigurationManager(Parameters parameters) :
    m_parameters(parameters),
    m_poolScanners(m_parameters.threadPoolScannerSize),
    m_repositoryNut(m_parameters.nutRepositoryDirectory)
{
}

bool ConfigurationManager::processAsset(fty_proto_t* asset, const fty::nut::SecwMap& credentials, bool forceScan, bool forceSort)
{
    const std::string name              = fty_proto_name(asset);
    const std::string operation         = fty_proto_operation(asset);
    const std::string daisyChain        = fty_proto_ext_string(asset, "daisy_chain", "0");
    bool needsUpdate = false;

    std::lock_guard<std::mutex> lk(m_assetMutexes[name]);
    log_info("ConfigurationManager: processing asset %s (daisychain=%s).", name.c_str(), daisyChain.c_str());

    // Only handle host or standalone devices.
    if (daisyChain == "0" || daisyChain == "1") {
        auto configurationsInDatabase       = getAssetConfigurations(asset, credentials);
        const auto configurationsInMemory   = m_repositoryMemory.getConfigurations(name);
        const auto configurationsInUse      = m_repositoryNut.getConfigurations(name);

        const bool willScan = forceScan ||
            (operation == FTY_PROTO_ASSET_OP_CREATE) ||
            ((operation == FTY_PROTO_ASSET_OP_UPDATE) && (configurationsInDatabase != configurationsInMemory)) ||
            ((operation != FTY_PROTO_ASSET_OP_DELETE) && configurationsInDatabase.empty());
        const bool willSort = forceSort;

        if (willScan || willSort) {
            if (willScan) {
                scanAssetConfigurations(asset, credentials);
            }
            if (willSort) {
                automaticAssetConfigurationPrioritySort(asset, credentials);
            }

            configurationsInDatabase = getAssetConfigurations(asset, credentials);
        }

        // If NUT configuration in use is obsolete, update it.
        const auto configurationsToUse = computeAssetConfigurationsToUse(asset, configurationsInDatabase);
        if (configurationsInUse != configurationsToUse) {
            m_repositoryNut.setConfigurations(name, configurationsToUse);
            needsUpdate = true;
        }
        m_repositoryMemory.setConfigurations(name, operation != FTY_PROTO_ASSET_OP_DELETE ? configurationsInDatabase : fty::nut::DeviceConfigurations());
    }

    log_info("ConfigurationManager: processed asset %s, %s update.", name.c_str(), needsUpdate ? "requires" : "does not require");

    return needsUpdate;
}

std::vector<std::string> ConfigurationManager::purgeNotInList(const std::set<std::string>& assets)
{
    std::vector<std::string> result;

    for (const auto& assetName : m_repositoryNut.listDevices()) {
        if (assets.find(assetName) == assets.end()) {
            std::lock_guard<std::mutex> lk(m_assetMutexes[assetName]);

            log_warning("Purging NUT configuration for asset %s.", assetName.c_str());
            m_repositoryNut.setConfigurations(assetName, {});
            result.emplace_back(assetName);
        }
    }

    return result;
}

void ConfigurationManager::automaticAssetConfigurationPrioritySort(fty_proto_t* asset, const fty::nut::SecwMap& credentials)
{
    const std::string assetName = fty_proto_name(asset);
    auto conn = tntdb::connectCached(m_parameters.dbConn);

    // Fetch all configurations to sort.
    const auto knownDatabaseConfigurations = DBAssetsDiscovery::get_all_config_list(conn, assetName);
    const auto knownConfigurations = instanciateDatabaseConfigurations(knownDatabaseConfigurations, asset, credentials);

    // Compute new order of driver configuration.
    const std::vector<size_t> sortedIndexes = sortDeviceConfigurationPreferred(asset, knownConfigurations, m_parameters.preferDmfForSnmp);
    std::vector<size_t> newConfigurationOrder;
    std::transform(
        sortedIndexes.begin(),
        sortedIndexes.end(),
        std::back_inserter(newConfigurationOrder),
        [&knownDatabaseConfigurations](size_t sortedIndex) -> size_t {
            return knownDatabaseConfigurations[sortedIndex].id;
        }
    );

    // Persist new order.
    DBAssetsDiscovery::modify_config_priorities(conn, assetName, newConfigurationOrder);
}

void ConfigurationManager::scanAssetConfigurations(fty_proto_t* asset, const fty::nut::SecwMap& credentials)
{
    /// Step 0: Grab all data.
    auto conn = tntdb::connectCached(m_parameters.dbConn);
    const std::string assetName = fty_proto_name(asset);

    const auto knownDatabaseConfigurations = DBAssetsDiscovery::get_all_config_list(conn, assetName);
    const auto knownConfigurations = instanciateDatabaseConfigurations(knownDatabaseConfigurations, asset, credentials);

    /// Step 1: Scan the asset.
    const auto detectedConfigurations = assetScanDrivers(m_poolScanners, asset, credentials, m_parameters.scanDummyUps);

    /// Step 2: Compute DB updates from detected and from known driver configurations.
    const auto results = computeAssetConfigurationUpdate(knownConfigurations, detectedConfigurations);
    log_trace("Summary of device configurations after scan for asset %s:\n%s", assetName.c_str(), serialize(results).c_str());

    /// Step 3: Mark existing configurations as working or non-working.
    for (const auto& updateOrder : std::vector<std::pair<const fty::nut::DeviceConfigurations&, bool>>({
        { results.workingConfigurations, true },
        { results.nonWorkingConfigurations, false },
    })) {
        // Match scan results to known driver configuration in database.
        for (const auto& configuration : updateOrder.first) {
            auto itKnownDatabaseConfiguration = knownDatabaseConfigurations.begin();

            bool matched = false;
            for (const auto& knownConfiguration : knownConfigurations) {
                if (itKnownDatabaseConfiguration == knownDatabaseConfigurations.end()) {
                    break;
                }

                if (isDeviceConfigurationSubsetOf(configuration, knownConfiguration)) {
                    // Found match, update database.
                    matched = true;
                    DBAssetsDiscovery::set_config_working(conn, itKnownDatabaseConfiguration->id, updateOrder.second);
                    log_trace("Marked device configuration ID %u for asset %s as %s.",
                        itKnownDatabaseConfiguration->id,
                        assetName.c_str(),
                        updateOrder.second ? "working" : "non-working"
                    );
                    break;
                }

                ++itKnownDatabaseConfiguration;
            }

            if (!matched) {
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
            const auto secwIDs = fty::nut::matchSecurityDocumentIDsFromDeviceConfiguration(newConfiguration, credentials);
            const auto configID = insert_config(conn, assetName, bestDeviceConfigurationType->id, true, true, secwIDs, attributes);

            log_debug("Instanciated device configuration type \"%s\" for asset %s (id=%u, driver=%s, port=%s, secwIDs={%s}).",
                    bestDeviceConfigurationType->prettyName.c_str(),
                    assetName.c_str(),
                    configID,
                    newConfiguration.at("driver").c_str(),
                    newConfiguration.at("port").c_str(),
                    serialize(secwIDs).c_str()
                );
        } else {
            log_warning("Failed to match new device configuration to device configuration type, configuration discarded:\n%s", serialize(newConfiguration).c_str());
        }
    }
}

fty::nut::DeviceConfigurations ConfigurationManager::getAssetConfigurations(fty_proto_t* asset, const fty::nut::SecwMap& credentials)
{
    const std::string assetName = fty_proto_name(asset);
    fty::nut::DeviceConfigurations upsConfBlock = assetUpsConfBlock(asset);
    fty::nut::DeviceConfigurations configs;

    if (!upsConfBlock.empty()) {
        // Asset has an upsconf block, override with it.
        configs = upsConfBlock;
    }
    else {
        DBAssetsDiscovery::DeviceConfigurationInfos candidateDatabaseConfigurations;
        try {
            auto conn = tntdb::connectCached(m_parameters.dbConn);
            candidateDatabaseConfigurations = DBAssetsDiscovery::get_candidate_config_list(conn, assetName);

        } catch (std::runtime_error &e) {
            log_error("getAssetConfigurations: failed to get config for '%s': %s", assetName.c_str(), e.what());
        }

        configs = instanciateDatabaseConfigurations(candidateDatabaseConfigurations, asset, credentials);
    }

    return configs;
}

fty::nut::DeviceConfigurations ConfigurationManager::computeAssetConfigurationsToUse(fty_proto_t* asset, const fty::nut::DeviceConfigurations& availableConfigurations)
{
    const std::string status = fty_proto_aux_string(asset, "status", "nonactive");

    fty::nut::DeviceConfigurations result;

    if (status == "active") {
        fty::nut::DeviceConfigurations upsConfBlock = assetUpsConfBlock(asset);
        if (!upsConfBlock.empty()) {
            // Asset has an upsconf block, override with it.
            log_warning("Asset %s has an upsconf block, overriding normal configuration processing.", fty_proto_name(asset));
            result = upsConfBlock;
        }
        else {
            // For now, return the most eligible configuration or nothing.
            if (!availableConfigurations.empty()) {
                result = { availableConfigurations[0] };
            }
        }
    }

    return result;
}

}
}
