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

#ifndef FTY_NUT_DRIVERS_MANAGER_H_INCLUDED
#define FTY_NUT_DRIVERS_MANAGER_H_INCLUDED

#include "fty_nut_library.h"

namespace fty
{
namespace nut
{

class ConfigurationDriversManager
{
    public:

        ConfigurationDriversManager();
        ~ConfigurationDriversManager() = default;

        void manageDrivers();
        template<typename It> void systemctl(const std::string &operation, It first, It last);
        void systemctl(const std::string &operation, const std::string &service);
        void updateNUTConfig();
        void addConfigDriver(std::string asset_name);
        void removeConfigDriver(std::string asset_name);

    private:
        std::set<std::string> m_start_drivers;
        std::set<std::string> m_stop_drivers;
        std::mutex m_start_drivers_mutex;
        std::mutex m_stop_drivers_mutex;
        std::thread m_manage_drivers_thread;
};

}
}

#ifdef __cplusplus
extern "C" {
#endif

//  Self test of this class
FTY_NUT_EXPORT void fty_nut_drivers_manager_test (bool verbose);

#ifdef __cplusplus
}
#endif

#endif
