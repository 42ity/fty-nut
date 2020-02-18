/*  =========================================================================
    fty_nut_drivers_manager - fty nut drivers manager

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
    fty_nut_drivers_manager - fty nut drivers manager
@discuss
@end
*/

#include "fty_nut_library.h"
#include "fty_nut_classes.h"

namespace fty
{
namespace nut
{

ConfigurationDriversManager::ConfigurationDriversManager()
{
    m_manage_drivers_thread = std::thread(&ConfigurationDriversManager::manageDrivers, this);
}

void ConfigurationDriversManager::addConfigDriver(std::string asset_name)
{
    log_info("addConfigDriver: %s", asset_name.c_str());
    std::lock_guard<std::mutex> lk_start_drivers(m_start_drivers_mutex);
    m_start_drivers.insert("nut-driver@" + asset_name);
}

void ConfigurationDriversManager::removeConfigDriver(std::string asset_name)
{
    log_info("removeConfigDriver: %s", asset_name.c_str());
    std::lock_guard<std::mutex> lk_stop_drivers(m_stop_drivers_mutex);
    m_stop_drivers.insert("nut-driver@" + asset_name);
}

void ConfigurationDriversManager::manageDrivers()
{
    //while (!g_exit) {
    while (1) {
        std::this_thread::sleep_for(std::chrono::seconds(5));

        std::set<std::string> stop_drivers, start_drivers;
        {
            std::lock_guard<std::mutex> lk_start_drivers(m_start_drivers_mutex);
            std::lock_guard<std::mutex> lk_stop_drivers(m_stop_drivers_mutex);
            stop_drivers = m_stop_drivers;
            start_drivers = m_start_drivers;
            m_stop_drivers.clear();
            m_start_drivers.clear();
        }

        //std::unique_lock<std::mutex> lock(m_manage_drivers_mutex);
        if (!stop_drivers.empty() || !start_drivers.empty()) {
            if (!stop_drivers.empty()) {
                systemctl("disable", stop_drivers.begin(), stop_drivers.end());
                systemctl("stop", stop_drivers.begin(), stop_drivers.end());
            }

            updateNUTConfig();

            if (!start_drivers.empty()) {
                systemctl("restart", start_drivers.begin(), start_drivers.end());
                systemctl("enable",  start_drivers.begin(), start_drivers.end());
            }
            systemctl("reload-or-restart", "nut-server");
        }
    }
}

void ConfigurationDriversManager::systemctl(const std::string &operation, const std::string &service)
{
    systemctl(operation, &service, &service + 1);
}

template<typename It>
void ConfigurationDriversManager::systemctl(const std::string &operation, It first, It last)
{
    if (first == last)
        return;
    std::vector<std::string> _argv = { "sudo", "systemctl", operation };
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

void ConfigurationDriversManager::updateNUTConfig()
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
fty_nut_drivers_manager_test (bool verbose)
{
    std::cerr << " * fty_nut_drivers_manager: no test" << std::endl;
}
