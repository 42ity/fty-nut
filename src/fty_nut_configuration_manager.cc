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

ConfigurationManager::ConfigurationManager(const std::string& dbConn) : m_poolScanners(20), m_dbConn(dbConn)
{
}

/**
 * \brief Serialize configuration into string.
 * \param name Asset name to process (optional).
 * \param config Config to process.
 */
std::string ConfigurationManager::serializeConfig(const std::string& name, fty::nut::DeviceConfiguration& config)
{
    std::stringstream ss;
    //ss << config << std::endl;
    if (!name.empty()) ss << "[" << name << "]" << "\n";
    for (auto& p : config) {
        ss << p.first << " = \"" << p.second << "\"\n";
    }
    return ss.str();
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
    const nutcommon::DeviceConfigurations knownConfigurations = instanciateDatabaseConfigurations(
        knownDatabaseConfigurations, asset, credentialsSNMPv1, credentialsSNMPv3);

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
    const nutcommon::DeviceConfigurations knownConfigurations = instanciateDatabaseConfigurations(
        knownDatabaseConfigurations, asset, credentialsSNMPv1, credentialsSNMPv3);

    /// Step 1: Scan the asset.
    const auto detectedConfigurations = assetScanDrivers(m_poolScanners, asset, credentialsSNMPv1, credentialsSNMPv3);

    /// Step 2: Compute DB updates from detected and from known driver configurations.
    const auto results = computeAssetConfigurationUpdate(knownConfigurations, detectedConfigurations);

    log_debug("Summary of device configurations after scan for asset %s:\n%s", assetName.c_str(), serialize(results).c_str());

    /// Step 3: Mark existing configurations as working or non-working.
    for (const auto& updateOrder : std::vector < std::pair<const nutcommon::DeviceConfigurations&, bool>>({
            { results.workingConfigurations, true},
            { results.unknownStateConfigurations, true},
            { results.nonWorkingConfigurations, false},
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
                log_warning("Failed to match known detected device configuration to what's in database, configuration ignored:\n%s",
                        serialize(configuration).c_str());
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
        } else {
            log_warning("Failed to match new device configuration to device configuration type, configuration discarded:\n%s",
                    serialize(newConfiguration).c_str());
        }
    }
}

/**
 * \brief Get asset configurations.
 * \param asset Asset to process.
 * \return Configurations of asset.
 */
fty::nut::DeviceConfigurations ConfigurationManager::getAssetConfigurations(fty_proto_t* asset)
{
    fty::nut::DeviceConfigurations configs;
    std::string assetName = fty_proto_name(asset);
    try {
        auto conn = tntdb::connectCached(m_dbConn);

        // Get candidate configurations and take the first one
        const DeviceConfigurationInfos candidateDatabaseConfigurations = get_candidate_config_list(conn, assetName);
        log_info("getAssetConfigurations: [%s] config candidate size=%d", assetName.c_str(), candidateDatabaseConfigurations.size());
        if (candidateDatabaseConfigurations.size() > 0) {

            const auto credentialsSNMPv1 = nutcommon::getCredentialsSNMPv1();
            const auto credentialsSNMPv3 = nutcommon::getCredentialsSNMPv3();

            // FIXME:
            //DeviceConfigurationInfo config = candidateDatabaseConfigurations.at(0);
            configs = instanciateDatabaseConfigurations(
                    candidateDatabaseConfigurations, asset, credentialsSNMPv1, credentialsSNMPv3);
        }
    } catch (std::runtime_error &e) {
        log_error("getAssetConfigurations: failed to get config for '%s': %s", assetName.c_str(), e.what());
    }
    return configs;
}

/**
 * \brief Get asset configurations and secured document ids.
 * \param asset Asset to process.
 * \param[out] configs Configurations of asset.
 * \param[out] secw_document_id_list Secure document ids.
 */
std::tuple<fty::nut::DeviceConfigurations, std::set<secw::Id>> ConfigurationManager::getAssetConfigurationsWithSecwDocuments(fty_proto_t* asset)
{
    fty::nut::DeviceConfigurations configs;
    std::set<secw::Id> secwDocumentIdList;
    std::string assetName = fty_proto_name(asset);
    try {
        auto conn = tntdb::connectCached(m_dbConn);

        // Get candidate configurations and take the first one
        const DBAssetsDiscovery::DeviceConfigurationInfos candidateDatabaseConfigurations = DBAssetsDiscovery::get_candidate_config_list(conn, assetName);
        log_info("getAssetConfigurationsWithSecwDocuments: [%s] config candidate size=%d", assetName.c_str(), candidateDatabaseConfigurations.size());
        if (candidateDatabaseConfigurations.size() > 0) {

            const auto credentialsSNMPv1 = nutcommon::getCredentialsSNMPv1();
            const auto credentialsSNMPv3 = nutcommon::getCredentialsSNMPv3();

            // FIXME:
            //DeviceConfigurationInfo config = candidateDatabaseConfigurations.at(0);
            configs = instanciateDatabaseConfigurations(
                    candidateDatabaseConfigurations, asset, credentialsSNMPv1, credentialsSNMPv3);

            for (auto configuration : candidateDatabaseConfigurations) {
                for (auto secwDocumentId : configuration.secwDocumentIdList) {
                    secwDocumentIdList.insert(secwDocumentId);
                }
            }
        }
    } catch (std::runtime_error &e) {
        log_error("getAssetConfigurationsWithSecwDocuments: failed to get config for '%s': %s", assetName.c_str(), e.what());
    }
    std::tuple<fty::nut::DeviceConfigurations, std::set<secw::Id>> res = std::make_tuple(configs, secwDocumentIdList);
    return res;
}

/**
 * \brief Save asset configuration.
 * \param asset Asset to process.
 * \param configsAsset Configurations to save.
 */
void ConfigurationManager::saveAssetConfigurations(
        const std::string& assetName,
        std::tuple<fty::nut::DeviceConfigurations, std::set<secw::Id>>& configsAsset)
{
    try {
        fty::nut::DeviceConfigurations configs = std::get<0>(configsAsset);
        if (!configs.empty()) {
            std::unique_lock<std::mutex> lockManageDrivers(m_manageDriversMutex);
            // Make sure that no config exist for this asset before adding new
            auto itr = m_deviceConfigurationMap.find(assetName);
            if (itr != m_deviceConfigurationMap.end()) {
                m_deviceConfigurationMap.erase(itr);
            }
            m_deviceConfigurationMap.insert(std::make_pair(assetName, configs));

            auto itr_cred_map = m_deviceCredentialsMap.find(assetName);
            if (itr_cred_map != m_deviceCredentialsMap.end()) {
                m_deviceCredentialsMap.erase(itr_cred_map);
            }
            std::set<secw::Id> secwDocumentIdList = std::get<1>(configsAsset);
            m_deviceCredentialsMap.insert(std::make_pair(assetName, secwDocumentIdList));
        }
    } catch (std::runtime_error &e) {
        log_error("saveAssetConfigurations: failed to apply config for '%s': %s", assetName.c_str(), e.what());
    }
}

/**
 * \brief Compare current configurations with input asset configurations
 * \param configs_asset_to_test Configurations of asset to test.
 * \param configs_asset_current Current configurations of asset.
 * \param init_in_progress Flag to indicate if init is in progress
 */
bool ConfigurationManager::isConfigurationsChange(
        fty::nut::DeviceConfigurations& configsAssetToTest,
        fty::nut::DeviceConfigurations& configsAssetCurrent,
        bool initInProgress)
{
    bool isChanging = false;

    auto it_configsAssetToTest = configsAssetToTest.begin();
    auto it_configsAssetCurrent = configsAssetCurrent.begin();
    // Test if size of configurations are different
    // Note: During init, test only first configuration
    if ((initInProgress && (configsAssetCurrent.size() == 0 || configsAssetToTest.size() == 0)) ||
            (!initInProgress && configsAssetCurrent.size() != configsAssetToTest.size())) {
        isChanging = true;
    } else {
        // For each candidate configuration
        while (it_configsAssetToTest != configsAssetToTest.end() && it_configsAssetCurrent != configsAssetCurrent.end()) {
            if (*it_configsAssetToTest != *it_configsAssetCurrent) {
                isChanging = true;
                log_trace("isConfigurationChange: Current config: %s", serializeConfig("", *it_configsAssetCurrent).c_str());
                log_trace("isConfigurationChange: Test config: %s", serializeConfig("", *it_configsAssetToTest).c_str());
                break;
            }
            // Note: During init, only scan first configuration
            if (!initInProgress) {
                it_configsAssetToTest++;
                it_configsAssetCurrent++;
            } else {
                break;
            }
        }
    }
    return isChanging;
}

/**
 * \brief Update asset configuration
 * \param asset Asset to process.
 */
bool ConfigurationManager::updateAssetConfiguration(fty_proto_t* asset)
{
    std::string assetName = fty_proto_name(asset);;
    try {
        std::string status = fty_proto_aux_string(asset, "status", "");

        bool needUpdate = false;
        std::unique_lock<std::mutex> lockManageDrivers(m_manageDriversMutex);
        auto itr = m_deviceConfigurationMap.find(assetName);
        // Update if no configurations
        if (itr == m_deviceConfigurationMap.end()) {
            if (status == "active") {
                needUpdate = true;
            }
        } else {
            if (status == "active") {
                fty::nut::DeviceConfigurations configs_asset_current = getAssetConfigurations(asset);
                // Test if existing configurations have changed
                needUpdate = isConfigurationsChange(itr->second, configs_asset_current);
            } else if (status == "nonactive") {
                needUpdate = true;
            }
        }
        lockManageDrivers.unlock();

        // Apply asset modification only if necessary (rescan is made in this case)
        if (needUpdate) {
            log_info("applyAssetConfiguration: [%s] need update", assetName.c_str());

            if (status == "active") {
                scanAssetConfigurations(asset);
                automaticAssetConfigurationPrioritySort(asset);
                std::tuple<fty::nut::DeviceConfigurations, std::set<secw::Id>> configs_asset = getAssetConfigurationsWithSecwDocuments(asset);
                saveAssetConfigurations(assetName, configs_asset);
                fty::nut::DeviceConfigurations configs = std::get<0>(configs_asset);
                if (!configs.empty()) {
                    // Save the first configuration into config file
                    log_trace("Save config: %s", serializeConfig("", configs.at(0)).c_str());
                    updateDeviceConfigurationFile(assetName, configs.at(0));
                    return true;
                }
                return false;
            } else if (status == "nonactive") {
                return removeAssetConfiguration(asset);
            }
        }
    } catch (std::runtime_error &e) {
        log_error("updateAssetConfiguration: failed to update config for '%s': %s", assetName.c_str(), e.what());
    }
    return false;
}

/**
 * \brief Remove asset configuration
 * \param asset Asset to process.
 */
bool ConfigurationManager::removeAssetConfiguration(fty_proto_t* asset)
{
    std::string assetName = fty_proto_name(asset);;
    try {
        auto conn = tntdb::connectCached(m_dbConn);
        log_info("removeAssetConfiguration: remove [%s]", assetName.c_str());
        std::unique_lock<std::mutex> lock_manage_drivers(m_manageDriversMutex);
        auto itr = m_deviceConfigurationMap.find(assetName);
        if (itr != m_deviceConfigurationMap.end()) {
            m_deviceConfigurationMap.erase(itr);
        }
        auto itr_cred_map = m_deviceCredentialsMap.find(assetName);
        if (itr_cred_map != m_deviceCredentialsMap.end()) {
            m_deviceCredentialsMap.erase(itr_cred_map);
        }
        lock_manage_drivers.unlock();

        // Remove config file
        removeDeviceConfigurationFile(assetName);
        return true;
    } catch (std::runtime_error &e) {
        log_error("removeAssetConfiguration: failed to remove config for '%s': %s", assetName.c_str(), e.what());
    }
    return false;
}

/**
 * \brief Manage credentials change into configuration.
 * \param secw_document_id Secured document id.
 * \param[out] asset_list_change List of change asset
 */
void ConfigurationManager::manageCredentialsConfiguration(const std::string& secwDocumentId, std::set<std::string>& assetListChange)
{
    // Found all assets which depend of the input secured document id
    std::unique_lock<std::mutex> lockManageDrivers(m_manageDriversMutex);
    std::set<std::string> assetList;
    for (auto it = m_deviceCredentialsMap.begin(); it != m_deviceCredentialsMap.end(); ++it) {
        if (it->second.find(secw::Id(secwDocumentId)) != it->second.end()) {
            assetList.insert(it->first);
            log_info("manageCredentialsConfiguration: find '%s' document in the asset '%s'", secwDocumentId.c_str(), it->first.c_str());
        }
    }
    lockManageDrivers.unlock();

    // Rescan all assets which depends of the input secured document id
    for (auto assetName : assetList) {
        log_info("manageCredentialsConfiguration: rescan '%s'", assetName.c_str());

        lockManageDrivers.lock();
        auto itr = m_deviceConfigurationMap.find(assetName);
        if (itr != m_deviceConfigurationMap.end()) {
            fty_proto_t *asset = fetchProtoFromAssetName(assetName);
            // Test if configuration change
            fty::nut::DeviceConfigurations configs = getAssetConfigurations(asset);
            if (isConfigurationsChange(itr->second, configs)) {
                lockManageDrivers.unlock();
                log_info("manageCredentialsConfiguration: [%s] need update", assetName.c_str());
                scanAssetConfigurations(asset);
                automaticAssetConfigurationPrioritySort(asset);
                std::tuple<fty::nut::DeviceConfigurations, std::set<secw::Id>> newConfigsAsset = getAssetConfigurationsWithSecwDocuments(asset);
                saveAssetConfigurations(assetName, newConfigsAsset);
                fty::nut::DeviceConfigurations newConfigs = std::get<0>(newConfigsAsset);
                if (!newConfigs.empty()) {
                    // Save the first configuration into config file
                    log_trace("Save config: %s", serializeConfig("", newConfigs.at(0)).c_str());
                    updateDeviceConfigurationFile(assetName, newConfigs.at(0));
                    assetListChange.insert(assetName);
                }
                continue;
            }
        }
        lockManageDrivers.unlock();
    }
}

/**
 * \brief Update asset configuration in config file.
 * \param asset Asset to process.
 * \param config Config to process.
 */
void ConfigurationManager::updateDeviceConfigurationFile(const std::string& name, nutcommon::DeviceConfiguration& config)
{
    const std::string configFilePath = std::string(NUT_PART_STORE) + shared::path_separator() + name;
    shared::mkdir_if_needed(NUT_PART_STORE);

    // Complete configuration.
    config["name"] = name; // FIXME: Put in defaut attributes ???

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
    } else {
        log_info("Configuration file '%s' unchanged, no actions to perform.", configFilePath.c_str());
    }
}

/**
 * \brief Remove asset configuration in config file.
 * \param asset Asset to process.
 * \param config Config to process.
 */
void ConfigurationManager::removeDeviceConfigurationFile(const std::string& name)
{
    const std::string filePath = std::string(NUT_PART_STORE) + shared::path_separator() + name;
    log_info("Removing configuration file '%s'.", filePath.c_str());

    remove(filePath.c_str());
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
fty_nut_configuration_manager_test(bool verbose)
{
    std::cerr << " * fty_nut_configuration_manager: no test" << std::endl;
}
