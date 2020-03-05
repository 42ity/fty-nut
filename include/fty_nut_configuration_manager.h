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

#ifndef FTY_NUT_CONFIGURATION_MANAGER_H_INCLUDED
#define FTY_NUT_CONFIGURATION_MANAGER_H_INCLUDED

#include "fty_nut_configuration_server.h"
#include "fty_nut_configuration_repository.h"
#include "fty_nut_library.h"

namespace fty
{
namespace nut
{

class ConfigurationManager
{
    public:
        struct Parameters
        {
            Parameters();

            std::string dbConn;
            std::string nutRepositoryDirectory;
            unsigned threadPoolScannerSize;
            bool scanDummyUps;
            bool preferDmfForSnmp;
        };

        ConfigurationManager(Parameters parameters);
        ~ConfigurationManager() = default;

        /**
         * \brief Handle asset update.
         * \param asset Asset to update.
         * \param credentials Security documents to use.
         * \param forceScan If set, force scanning.
         * \param forceSort If set, force re-ordering of configuration priorities.
         * \return True if NUT configuration has been updated, false otherwise.
         */
        bool processAsset(fty_proto_t* asset, const fty::nut::SecwMap& credentials, bool forceScan, bool forceSort);

        /**
         * \brief Purge NUT configurations not in the list.
         * \param assets NUT configurations to purge.
         * \return NUT configurations purged.
         */
        std::vector<std::string> purgeNotInList(const std::set<std::string>& assets);

    private:
        class AssetMutex
        {
            public:
                std::mutex& operator[](const std::string& asset);

            private:
                std::mutex m_mutex;
                std::map<std::string, std::mutex> m_mutexes;
        };

        /**
         * \brief Reorder asset's driver configuration priorities according to the software's preferences.
         * \param asset Asset to process.
         */
        void automaticAssetConfigurationPrioritySort(fty_proto_t* asset, const fty::nut::SecwMap& credentials);

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
        void scanAssetConfigurations(fty_proto_t* asset, const fty::nut::SecwMap& credentials);

        /**
         * \brief Get asset configurations.
         * \param asset Asset to process.
         * \return Configurations of asset.
         */
        fty::nut::DeviceConfigurations getAssetConfigurations(fty_proto_t* asset, const fty::nut::SecwMap& credentials);

        /**
         * \brief Select NUT driver configurations to use from available.
         * \param asset Asset to select with.
         * \param availableConfigurations Configurations to select from.
         * \return Selected configurations.
         */
        fty::nut::DeviceConfigurations computeAssetConfigurationsToUse(fty_proto_t* asset, const fty::nut::DeviceConfigurations& availableConfigurations);

        Parameters m_parameters;
        messagebus::PoolWorker m_poolScanners;
        ConfigurationRepositoryDirectory m_repositoryNut;
        ConfigurationRepositoryMemory m_repositoryMemory;
        AssetMutex m_assetMutexes;
};

}
}

#endif
