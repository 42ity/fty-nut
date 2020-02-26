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

void ConfigurationDriversControl::addControl(const std::string& controlName)
{
    std::lock_guard<std::mutex> lkControlDrivers(m_controlDriversMutex);
    m_controlDrivers.insert(controlName);
}

std::set<std::string> ConfigurationDriversControl::clearControl() {
    std::set<std::string> controlDrivers;
    std::lock_guard<std::mutex> lkControlDrivers(m_controlDriversMutex);
    controlDrivers = m_controlDrivers;
    m_controlDrivers.clear();
    return controlDrivers;
}

ConfigurationDriversManager::ConfigurationDriversManager(volatile bool &exit) : m_exit(exit)
{
    m_manageDriversThread = std::thread(&ConfigurationDriversManager::manageDrivers, this);
}

void ConfigurationDriversManager::addConfigDriver(const std::string& assetName)
{
    log_info("addConfigDriver: %s", assetName.c_str());
    m_startDrivers.addControl("nut-driver@" + assetName);
}

void ConfigurationDriversManager::removeConfigDriver(const std::string& assetName)
{
    log_info("removeConfigDriver: %s", assetName.c_str());
    m_stopDrivers.addControl("nut-driver@" + assetName);
}

void ConfigurationDriversManager::manageDrivers()
{
    while (!m_exit) {
        std::this_thread::sleep_for(std::chrono::seconds(5));

        std::set<std::string> stopDrivers = m_stopDrivers.clearControl();
        std::set<std::string> startDrivers = m_startDrivers.clearControl();

        if (!stopDrivers.empty() || !startDrivers.empty()) {
            if (!stopDrivers.empty()) {
                systemctl("disable", stopDrivers.begin(), stopDrivers.end());
                systemctl("stop", stopDrivers.begin(), stopDrivers.end());
            }

            updateNUTConfig();

            if (!startDrivers.empty()) {
                systemctl("restart", startDrivers.begin(), startDrivers.end());
                systemctl("enable",  startDrivers.begin(), startDrivers.end());
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
