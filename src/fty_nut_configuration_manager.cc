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

#define NUT_PART_STORE "/var/lib/fty/fty-nut/devices"

namespace fty
{
namespace nut
{

ConfigurationManager::ConfigurationManager(const std::string& dbConn) : m_poolScanners(20), m_dbConn(dbConn)
{
    m_manage_drivers_thread = std::thread(&ConfigurationManager::manageDrivers, this);
}

std::string ConfigurationManager::serialize_config(std::string name, nutcommon::DeviceConfiguration& config) {
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
void ConfigurationManager::updateDeviceConfigurationFile(const std::string& name, nutcommon::DeviceConfiguration& config)
{
    const std::string configFilePath = std::string(NUT_PART_STORE) + shared::path_separator() + name;
    shared::mkdir_if_needed(NUT_PART_STORE);

    // Complete configuration.
    config["name"] = name;   // FIXME: Put in defaut attributes ???

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
 * \brief Remove asset configuration in config file.
 * \param asset Asset to process.
 * \param config Config to process.
 */
void ConfigurationManager::removeDeviceConfigurationFile(const std::string &name)
{
    const std::string filePath = std::string(NUT_PART_STORE) + shared::path_separator() + name;
    log_info("Removing configuration file '%s'.", filePath.c_str());

    remove(filePath.c_str());
}

/**
 * \brief Apply asset configuration.
 * \param asset Asset to process.
 */
void ConfigurationManager::applyAssetConfiguration(fty_proto_t* asset)
{
    std::string assetName;
    try {
        auto conn = tntdb::connectCached(m_dbConn);
        assetName = fty_proto_name(asset);

        // Get candidate configurations and take the first one
        const DeviceConfigurationInfos candidateDatabaseConfigurations = get_candidate_config_list(conn, assetName);
        log_info("applyAssetConfiguration: [%s] config candidate size=%d", assetName.c_str(), candidateDatabaseConfigurations.size());
        if (candidateDatabaseConfigurations.size() > 0) {

            const auto credentialsSNMPv1 = nutcommon::getCredentialsSNMPv1();
            const auto credentialsSNMPv3 = nutcommon::getCredentialsSNMPv3();

            // FIXME:
            //DeviceConfigurationInfo config = candidateDatabaseConfigurations.at(0);
            nutcommon::DeviceConfigurations configs = instanciateDatabaseConfigurations(
                candidateDatabaseConfigurations, asset, credentialsSNMPv1, credentialsSNMPv3);

            if (configs.size() > 0) {
                m_manage_drivers_mutex.lock();
                // Make sure that no config exist for this asset before adding new
                auto itr = m_deviceConfigurationMap.find(assetName);
                if (itr != m_deviceConfigurationMap.end()) {
                    m_deviceConfigurationMap.erase(itr);
                }
                m_deviceConfigurationMap.insert(std::make_pair(assetName, configs));
                m_manage_drivers_mutex.unlock();

                m_start_drivers_mutex.lock();
                m_start_drivers.insert(assetName);
                m_start_drivers_mutex.unlock();

                // Save the first configuration into config file
                log_trace("\nSave config:\n%s", serialize_config("", configs.at(0)).c_str());
                this->updateDeviceConfigurationFile(assetName, configs.at(0));
            }
        }
    }
    catch (std::runtime_error &e) {
        log_error("applyAssetConfiguration: failed to apply config for '%s': %s", assetName.c_str(), e.what());
    }
}

/**
 * \brief Update asset configuration
 * \param asset Asset to process.
 */
void ConfigurationManager::updateAssetConfiguration(fty_proto_t* asset)
{
    std::string assetName;
    try {
        auto conn = tntdb::connectCached(m_dbConn);
        assetName = fty_proto_name(asset);
        std::string status = fty_proto_aux_string(asset, "status", "");

        bool need_update = false;
        auto itr = m_deviceConfigurationMap.find(assetName);
        // Update if no configurations
        if (itr == m_deviceConfigurationMap.end()) {
            if (status == "active") {
               need_update = true;
            }
        }
        else {
            if (status == "active") {
                // Test if existing configurations have changed
                nutcommon::DeviceConfigurations configs_asset_current = itr->second;

                // Get candidate configurations and take the first one
                const DeviceConfigurationInfos candidateDatabaseConfigurations = get_candidate_config_list(conn, assetName);
                log_info("applyAssetConfiguration: [%s] config candidate size=%d", assetName.c_str(), candidateDatabaseConfigurations.size());
                if (candidateDatabaseConfigurations.size() > 0) {
                    const auto credentialsSNMPv1 = nutcommon::getCredentialsSNMPv1();
                    const auto credentialsSNMPv3 = nutcommon::getCredentialsSNMPv3();

                    //DeviceConfigurationInfo config = candidateDatabaseConfigurations.at(0);
                    nutcommon::DeviceConfigurations configs_asset_new = instanciateDatabaseConfigurations(
                        candidateDatabaseConfigurations, asset, credentialsSNMPv1, credentialsSNMPv3);

                    // FIXME: New message bus lib
                    //FtyProto asset_current(fetchProtoFromAssetName(assetName), [](fty_proto_t *p) -> void { fty_proto_destroy(&p); });
                    //nutcommon::DeviceConfigurations configs_asset_current = instanciateDatabaseConfigurations(
                    //    candidateDatabaseConfigurations, asset_current.get(), credentialsSNMPv1, credentialsSNMPv3);

                    auto it_configs_asset_new = configs_asset_new.begin();
                    auto it_configs_asset_current = configs_asset_current.begin();
                    // for each config FIXME: First one only  ?
                    if (configs_asset_new.size() != configs_asset_current.size()) {
                        need_update = true;
                    }
                    else {
                        // For each candidate configuration
                        while (it_configs_asset_new != configs_asset_new.end() && it_configs_asset_current != configs_asset_current.end()) {
                            if (*it_configs_asset_new != *it_configs_asset_current) {
                                need_update = true;
                                log_trace("\nNew config:\n%s", serialize_config("", *it_configs_asset_new).c_str());
                                log_trace("\nCurrent config:\n%s", serialize_config("", *it_configs_asset_current).c_str());
                                break;
                            }
                            it_configs_asset_new ++;
                            it_configs_asset_current ++;
                        }
                    }
                }
            }
            else if (status == "nonactive") {
                need_update = true;
            }
        }
        // Apply asset modification only if necessary (rescan is made in this case)
        if (need_update) {
            log_info("applyAssetConfiguration: [%s] need update", assetName.c_str());

            if (status == "active") {
                // FIXME: Be done in applyAssetConfiguration
                /*m_manage_drivers_mutex.lock();
                auto itr = m_deviceConfigurationMap.find(assetName);
                if (itr != m_deviceConfigurationMap.end()) {
                    m_deviceConfigurationMap.erase(itr);
                }
                m_deviceConfigurationMap.insert(std::make_pair(assetName, configs_asset_new));
                m_manage_drivers_mutex.unlock();*/

                this->scanAssetConfigurations(asset);
                this->automaticAssetConfigurationPrioritySort(asset);
                this->applyAssetConfiguration(asset);
            }
            else if (status == "nonactive") {
                this->removeAssetConfiguration(asset);
            }
        }
    }
    catch (std::runtime_error &e) {
        log_error("updateAssetConfiguration: failed to update config for '%s': %s", assetName.c_str(), e.what());
    }
}

/**
 * \brief Remove asset configuration
 * \param asset Asset to process.
 */
void ConfigurationManager::removeAssetConfiguration(fty_proto_t* asset)
{
    std::string assetName;
    try {
        auto conn = tntdb::connectCached(m_dbConn);
        assetName = fty_proto_name(asset);
        log_info("removeAssetConfiguration: remove [%s]", assetName.c_str());
        m_manage_drivers_mutex.lock();
        auto itr = m_deviceConfigurationMap.find(assetName);
        if (itr != m_deviceConfigurationMap.end()) {
            m_deviceConfigurationMap.erase(itr);
        }
        m_manage_drivers_mutex.unlock();

        m_stop_drivers_mutex.lock();
        m_stop_drivers.insert(assetName);
        m_stop_drivers_mutex.unlock();

        // FIXME: Not needed, it was already done by the asset agent
        // Remove config in database
        //remove_all_config(conn, assetName);

        // Remove config file
        this->removeDeviceConfigurationFile(assetName);
    }
    catch (std::runtime_error &e) {
        log_error("removeAssetConfiguration: failed to remove config for '%s': %s", assetName.c_str(), e.what());
    }
}

void ConfigurationManager::manageDrivers()
{
    while(1) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        //std::unique_lock<std::mutex> lock(m_manage_drivers_mutex);
        if (!m_stop_drivers.empty() || !m_start_drivers.empty()) {
            if (!m_stop_drivers.empty()) {
                m_stop_drivers_mutex.lock();
                systemctl("disable", m_stop_drivers.begin(), m_stop_drivers.end());
                systemctl("stop", m_stop_drivers.begin(), m_stop_drivers.end());
                m_stop_drivers.clear();
                m_stop_drivers_mutex.unlock();
            }

            updateNUTConfig();

            if (!m_start_drivers.empty()) {
                m_start_drivers_mutex.lock();
                systemctl("restart", m_start_drivers.begin(), m_start_drivers.end());
                systemctl("enable",  m_start_drivers.begin(), m_start_drivers.end());
                m_start_drivers.clear();
                m_start_drivers_mutex.unlock();
            }
            systemctl("reload-or-restart", "nut-server");
        }
    }
}

void ConfigurationManager::systemctl(const std::string &operation, const std::string &service)
{
    systemctl(operation, &service, &service + 1);
}

template<typename It>
void ConfigurationManager::systemctl(const std::string &operation, It first, It last)
{
    if (first == last)
        return;
    std::vector<std::string> _argv = {"sudo", "systemctl", operation };
    _argv.insert(_argv.end(), first, last);
    MlmSubprocess::SubProcess systemd(_argv);
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

void ConfigurationManager::updateNUTConfig()
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
