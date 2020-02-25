/*  =========================================================================
    fty_nut_configuration_protect_asset - fty nut configuration protect asset

    Copyright (C) 2014 - 2020 Eaton

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

#ifndef FTY_NUT_CONFIGURATION_PROTECT_ASSET_H_INCLUDED
#define FTY_NUT_CONFIGURATION_PROTECT_ASSET_H_INCLUDED

#include "fty_nut_library.h"

namespace fty
{
namespace nut
{

class ProtectAsset
{
    public:
        ProtectAsset() = default;
        ~ProtectAsset() = default;
        /**
         * \brief Lock an asset for simultaneous access protection (blocking)
         * \param assetName Asset name to lock
         */
        void lock(std::string assetName);

        /**
         * \brief Unlock an asset for simultaneous access protection
         * \param assetName Asset name to unlock
         * \return True if protection found and unlocked
         *         False if protection not found
         */
        bool unlock(std::string assetName);

        /**
         * \brief Try locking an asset for simultaneous access protection (no blocking)
         * \param assetName Asset name to try locking
         * \return True if protection found and locking possible
         *         False if protection still locked or protection not found
         */
        bool trylock(std::string assetName);

       /**
        * \brief Remove asset protection for simultaneous access
        * \param assetName Asset name to remove protection
        * \return True if protection found and removed
        *         False if protection not removed (still locked) or protection not found
        */
        bool remove(std::string assetName);
    private:
        std::mutex m_assetMutex;
        std::map<std::string, std::shared_ptr<std::mutex>> m_assetMutexMap;
};

}
}

#ifdef __cplusplus
extern "C" {
#endif

//  Self test of this class
FTY_NUT_EXPORT void fty_nut_configuration_protect_asset_test (bool verbose);

#ifdef __cplusplus
}
#endif

#endif
