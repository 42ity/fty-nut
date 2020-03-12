/*  =========================================================================
    fty_nut_driver_manager - fty nut driver manager

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
    fty_nut_driver_manager - fty nut driver manager
@discuss
@end
*/

#include "fty_nut_library.h"
#include "fty_nut_classes.h"

namespace fty
{
namespace nut
{

DriverManager::DriverManager(const std::string& nutDirectory) :
    m_nutRepository(nutDirectory),
    m_exit(false),
    m_updateThread(&DriverManager::updateMainloop, this)
{
}

DriverManager::~DriverManager()
{
    m_exit = true;
    m_updateThread.join();
}

void DriverManager::refreshDrivers(const std::vector<std::string>& assetNames)
{
    const auto knownDrivers = m_nutRepository.listDevices();

    std::lock_guard<std::mutex> lk(m_mutex);
    for (const auto& assetName : assetNames) {
        if (knownDrivers.count(assetName)) {
            log_trace("Scheduling startup of NUT driver for asset %s.", assetName.c_str());
            m_startDrivers.insert("nut-driver@" + assetName);
        }
        else {
            log_trace("Scheduling shutdown of NUT driver for asset %s.", assetName.c_str());
            m_stopDrivers.insert("nut-driver@" + assetName);
        }
    }
}

void DriverManager::updateMainloop()
{
    while (!m_exit.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(5));

        // Safely grab work.
        std::set<std::string> stopDrivers;
        std::set<std::string> startDrivers;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            stopDrivers = m_stopDrivers;
            startDrivers = m_startDrivers;
            m_stopDrivers.clear();
            m_startDrivers.clear();
        }

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

void DriverManager::systemctl(const std::string &operation, const std::string &service)
{
    systemctl(operation, &service, &service + 1);
}

template<typename It>
void DriverManager::systemctl(const std::string &operation, It first, It last)
{
    if (first == last)
        return;
    std::vector<std::string> _argv = { "sudo", "systemctl", operation };
    _argv.insert(_argv.end(), first, last);
    MlmSubprocess::SubProcess systemd(_argv);
    if( systemd.run() ) {
        int result = systemd.wait();
        log_debug("sudo systemctl %s result %i (%s) for following units",
                 operation.c_str(),
                 result,
                 (result == 0 ? "ok" : "failed"));
        for (It it = first; it != last; ++it)
            log_debug(" - %s", it->c_str());
    } else {
        log_error("can't run sudo systemctl %s for following units",
                  operation.c_str());
        for (It it = first; it != last; ++it)
            log_error(" - %s", it->c_str());
    }
}

void DriverManager::updateNUTConfig()
{
    // Run the helper script
    std::vector<std::string> _argv = { "sudo", "fty-nutconfig" };
    MlmSubprocess::SubProcess systemd( _argv );
    if( systemd.run() ) {
        int result = systemd.wait();
        if (result == 0) {
            log_debug("Command 'sudo fty-nutconfig' succeeded.");
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
