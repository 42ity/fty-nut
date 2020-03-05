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

/*
@header
    fty_nut_configuration_repository - fty nut configuration repository
@discuss
@end
*/

#include "fty_nut_classes.h"

namespace fty {
namespace nut {

const std::string ConfigurationRepositoryDirectory::DEFAULT_NUT_CONFIGURATION_REPOSITORY = "/var/lib/fty/fty-nut/devices";

std::set<std::string> ConfigurationRepositoryMemory::listDevices()
{
    std::set<std::string> result;
    std::transform(
        m_configurations.begin(),
        m_configurations.end(),
        std::inserter(result, result.begin()),
        [](const RepositoryStore::value_type& device) -> std::string {
            return device.first;
        }
    );
    return result;
}

fty::nut::DeviceConfigurations ConfigurationRepositoryMemory::getConfigurations(const std::string& name)
{
    auto it = m_configurations.find(name);
    if (it == m_configurations.end()) {
        return {};
    }
    return it->second;
}

void ConfigurationRepositoryMemory::setConfigurations(const std::string& name, const fty::nut::DeviceConfigurations& configurations)
{
    if (configurations.empty()) {
        m_configurations.erase(name);
    }
    else {
        m_configurations[name] = configurations;
    }
}

ConfigurationRepositoryDirectory::ConfigurationRepositoryDirectory(const std::string& directory) :
    m_directoryLocation(directory)
{
    shared::mkdir_if_needed(m_directoryLocation.c_str());
}

std::set<std::string> ConfigurationRepositoryDirectory::listDevices()
{
    const auto files = shared::files_in_directory(m_directoryLocation.c_str());
    return std::set<std::string>(files.begin(), files.end());
}

fty::nut::DeviceConfigurations ConfigurationRepositoryDirectory::getConfigurations(const std::string& name)
{
    const std::string configurationPath = m_directoryLocation + shared::path_separator() + name;
    fty::nut::DeviceConfigurations results;

    std::ifstream file(configurationPath);
    if (file) {
        std::stringstream buffer;
        buffer << file.rdbuf();
        results = parseConfigurationFile(buffer.str());
        for (auto& result : results) {
            result.erase("name");
        }
    }

    return results;
}

void ConfigurationRepositoryDirectory::setConfigurations(const std::string& name, const fty::nut::DeviceConfigurations& configurations)
{
    const std::string configurationPath = m_directoryLocation + shared::path_separator() + name;

    if (configurations.empty()) {
        remove(configurationPath.c_str());
    }
    else {
        // TODO: handle multiple NUT configurations for one asset.
        fty::nut::DeviceConfiguration configuration = configurations[0];

        configuration["name"] = name;

        std::string oldConfiguration, newConfiguration;
        {
            std::stringstream buffer;
            buffer << std::ifstream(configurationPath).rdbuf();
            oldConfiguration = buffer.str();
        }
        {
            std::stringstream buffer;
            buffer << configuration;
            newConfiguration = buffer.str();
        }

        if (oldConfiguration != newConfiguration) {
            std::ofstream cfgFile(configurationPath);
            cfgFile << newConfiguration;
        }
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
fty_nut_configuration_repository_test(bool verbose)
{
    std::cerr << " * fty_nut_configuration_repository_test:" << std::endl;

    fty::nut::ConfigurationRepositoryMemory repoMem;
    fty::nut::ConfigurationRepositoryDirectory repoDir(SELFTEST_DIR_RW);

    for (auto& repoTestCase : std::vector<std::pair<const char*, fty::nut::ConfigurationRepository*>> {
        { "ConfigurationRepositoryMemory",      dynamic_cast<fty::nut::ConfigurationRepository*>(&repoMem) },
        { "ConfigurationRepositoryDirectory",   dynamic_cast<fty::nut::ConfigurationRepository*>(&repoDir) },
    }) {
        std::cerr << "  * " << repoTestCase.first << ": ";
        fty::nut::ConfigurationRepository* repo = repoTestCase.second;

        assert(repo->listDevices().empty());
        fty::nut::DeviceConfiguration testConf = {
            { "community", "private" },
            { "driver", "snmp-ups" },
            { "port", "127.0.0.1" },
        };

        repo->setConfigurations("ups-8", { testConf });
        assert(repo->listDevices() == std::set<std::string>({ "ups-8" }));
        assert(repo->getConfigurations("ups-8") == fty::nut::DeviceConfigurations({testConf}));

        testConf["mibs"] = "ietf";
        repo->setConfigurations("ups-9", { testConf });
        assert(repo->listDevices().size() == 2);
        assert(repo->getConfigurations("ups-9") == fty::nut::DeviceConfigurations({testConf}));

        repo->setConfigurations("ups-8", {});
        assert(repo->listDevices() == std::set<std::string>({ "ups-9" }));        
        repo->setConfigurations("ups-9", {});
        assert(repo->listDevices().empty());

        std::cerr << "OK" << std::endl;
    }
}
