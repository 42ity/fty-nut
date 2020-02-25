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

#include "fty_nut_library.h"

#define NUT_PART_STORE "/var/lib/fty/fty-nut/devices"

namespace fty
{
namespace nut
{

class ConfigurationManager
{
    public:

        ConfigurationManager(const std::string& dbConn);
        ~ConfigurationManager() = default;

        static std::string serializeConfig(const std::string& name, fty::nut::DeviceConfiguration& config);
        void automaticAssetConfigurationPrioritySort(fty_proto_t* asset);
        void scanAssetConfigurations(fty_proto_t* asset);

        fty::nut::DeviceConfigurations getAssetConfigurations(fty_proto_t* asset);
        std::tuple<fty::nut::DeviceConfigurations, std::set<secw::Id>> getAssetConfigurationsWithSecwDocuments(fty_proto_t* asset);
        void saveAssetConfigurations(const std::string& assetName, std::tuple<fty::nut::DeviceConfigurations, std::set<secw::Id>>& configsAsset);
        bool isConfigurationsChange(fty::nut::DeviceConfigurations& configsAssetToTest, fty::nut::DeviceConfigurations& configsAssetCurrent, bool initInProgress = false);
        bool updateAssetConfiguration(fty_proto_t* asset);
        bool removeAssetConfiguration(fty_proto_t* asset);
        void manageCredentialsConfiguration(const std::string& secwDocumentId, std::set<std::string>& assetListChange);

        void updateDeviceConfigurationFile(const std::string& name, fty::nut::DeviceConfiguration& config);
        void removeDeviceConfigurationFile(const std::string &name);

    private:
        messagebus::PoolWorker m_poolScanners;
        std::string m_dbConn;
        std::map<std::string, nutcommon::DeviceConfigurations> m_deviceConfigurationMap;
        std::map<std::string, std::set<secw::Id>> m_deviceCredentialsMap;
        std::mutex m_manageDriversMutex;
};

}
}

#ifdef __cplusplus
extern "C" {
#endif

//  Self test of this class
FTY_NUT_EXPORT void fty_nut_configuration_manager_test (bool verbose);

#ifdef __cplusplus
}
#endif

#endif
