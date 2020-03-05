/*  =========================================================================
    fty_nut_configuration_repository - fty nut configuration repository

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

#ifndef FTY_NUT_CONFIGURATION_REPOSITORY_H_INCLUDED
#define FTY_NUT_CONFIGURATION_REPOSITORY_H_INCLUDED

#include "fty_nut_library.h"

namespace fty
{
namespace nut
{

class ConfigurationRepository
{
    public:
        virtual ~ConfigurationRepository() = default;

        virtual std::set<std::string> listDevices() = 0;

        virtual fty::nut::DeviceConfigurations getConfigurations(const std::string& name) = 0;
        virtual void setConfigurations(const std::string& name, const fty::nut::DeviceConfigurations& configurations) = 0;

    protected:
        ConfigurationRepository() = default;
};

class ConfigurationRepositoryMemory final : public ConfigurationRepository
{
    private:
        using RepositoryStore = std::map<std::string, fty::nut::DeviceConfigurations>;

    public:
        ConfigurationRepositoryMemory() = default;
        ~ConfigurationRepositoryMemory() = default;
    
        std::set<std::string> listDevices() override;

        fty::nut::DeviceConfigurations getConfigurations(const std::string& name) override;
        void setConfigurations(const std::string& name, const fty::nut::DeviceConfigurations& configurations) override;

    private:
        RepositoryStore m_configurations;
};

class ConfigurationRepositoryDirectory final : public ConfigurationRepository
{
    public:
        const static std::string DEFAULT_NUT_CONFIGURATION_REPOSITORY;

        ConfigurationRepositoryDirectory(const std::string& directory = DEFAULT_NUT_CONFIGURATION_REPOSITORY);
        ~ConfigurationRepositoryDirectory() = default;
    
        std::set<std::string> listDevices() override;

        fty::nut::DeviceConfigurations getConfigurations(const std::string& name) override;
        void setConfigurations(const std::string& name, const fty::nut::DeviceConfigurations& configurations) override;

    private:
        std::string m_directoryLocation;
};

}
}

#ifdef __cplusplus
extern "C" {
#endif

//  Self test of this class
FTY_NUT_EXPORT void fty_nut_configuration_repository_test (bool verbose);

#ifdef __cplusplus
}
#endif


#endif
