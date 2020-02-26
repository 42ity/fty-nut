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

/*
@header
    fty_nut_configuration_protect_asset - fty nut configuration protect asset
@discuss
@end
*/

#include "fty_nut_library.h"

namespace fty
{
namespace nut
{

/**
* \brief Lock an asset for simultaneous access protection (blocking)
* \param assetName Asset name to lock
*/
void ProtectAsset::lock(std::string assetName) {
    std::unique_lock<std::mutex> lock_asset(m_assetMutex);
    auto it = m_assetMutexMap.find(assetName);
    if (it == m_assetMutexMap.end()) {
        auto ret = m_assetMutexMap.insert(
            std::pair<std::string, std::shared_ptr<std::mutex>>(assetName, std::shared_ptr<std::mutex>(new std::mutex())));
        it = ret.first;
    }
    lock_asset.unlock();
    it->second.get()->lock();
}

/**
* \brief Unlock an asset for simultaneous access protection
* \param assetName Asset name to unlock
* \return True if protection found and unlocked
*         False if protection not found
*/
bool ProtectAsset::unlock(std::string assetName) {
    std::unique_lock<std::mutex> lock_asset(m_assetMutex);
    auto it = m_assetMutexMap.find(assetName);
    if (it != m_assetMutexMap.end()) {
        it->second.get()->unlock();
        return true;
    }
    return false;
}

/**
* \brief Try locking an asset for simultaneous access protection (no blocking)
* \param assetName Asset name to try locking
* \return True if protection found and locking possible
*         False if protection still locked or protection not found
*/
bool ProtectAsset::trylock(std::string assetName) {
    std::unique_lock<std::mutex> lock_asset(m_assetMutex);
    auto it = m_assetMutexMap.find(assetName);
    if (it != m_assetMutexMap.end()) {
        if (it->second.get()->try_lock()) {
            it->second.get()->unlock();
            return true;
        }
    }
    return false;
}

/**
* \brief Remove asset protection for simultaneous access
* \param assetName Asset name to remove protection
* \return True if protection found and removed
*         False if protection not removed (still locked) or protection not found
*/
bool ProtectAsset::remove(std::string assetName) {
    std::unique_lock<std::mutex> lock_asset(m_assetMutex);
    auto it = m_assetMutexMap.find(assetName);
    if (it != m_assetMutexMap.end()) {
        int retry = 10;
        while (retry-- > 0) {
            if (it->second.get()->try_lock()) {
                it->second.get()->unlock();
                m_assetMutexMap.erase(it);
                return true;
            }
        }
    }
    return false;
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

uint g_count_test = 1000;
fty::nut::ProtectAsset g_protectAssetTest;

struct t_elemt_test {
    std::string name;
    uint value;
};

using MapTest = std::vector<t_elemt_test>;

void lock_element_test(uint id, uint pauseMs, t_elemt_test &elentTest)
{
    std::cout << "#" << id << " :lock_element_test " << elentTest.name << std::endl;
    g_protectAssetTest.lock(elentTest.name);
    elentTest.value ++;
    std::this_thread::sleep_for(std::chrono::milliseconds(pauseMs));
    g_protectAssetTest.unlock(elentTest.name);
    std::cout << "#" << id << " :unlock_element_test " << elentTest.name << std::endl;
}

void lock_boucle_element_test(uint id, t_elemt_test &elentTest)
{
    uint i = 0;
    std::cout << "#" << id << " :lock_boucle_element_test " << elentTest.name << std::endl;
    while (i++ < g_count_test) {
        g_protectAssetTest.lock(elentTest.name);
        elentTest.value ++;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        g_protectAssetTest.unlock(elentTest.name);
    }
}

void add_element_test(int id, t_elemt_test &elentTest)
{
    uint i = 0;
    while (i++ < 100) {
        //std::cout << "#" << id << " :add_element_test " << elentTest.name << " " << elentTest.value << std::endl;
        g_protectAssetTest.lock(elentTest.name);
        elentTest.value ++;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        g_protectAssetTest.unlock(elentTest.name);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void remove_element_test(int id, t_elemt_test &elentTest)
{
    uint i = 0;
    while (i++ < 100) {
        g_protectAssetTest.remove(elentTest.name);
        //std::cout << "#" << id << " :remove_element_test " << elentTest.name << " " << elentTest.value << " res=" << res << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void
fty_nut_configuration_protect_asset_test (bool verbose)
{
    std::cerr << " * fty_nut_configuration_protect_asset:\n\n" << std::endl;

    // Protect asset - Basic test
    {
        t_elemt_test element_1 = { "element_1", 0 };
        std::thread threadBlockTest1(lock_element_test, 1, 5000, std::ref(element_1));
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        assert(element_1.value == 1);
        std::thread threadBlockTest2(lock_element_test, 2, 1000, std::ref(element_1));

        int timeout = 1;
        while (timeout++ < 5) {
            assert(element_1.value == 1);
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        assert(element_1.value == 2);
        threadBlockTest1.join();
        threadBlockTest2.join();
        std::cerr << "\nProtect asset - Basic test: OK\n\n" << std::endl;
    }

    // Protect asset - Add / remove test
    {
        uint nb_thread = 2;
        t_elemt_test element = { "element_1", 0 };
        std::vector<std::thread> threadAddTest;
        std::vector<std::thread> threadRemoveTest;
        for(uint nb = 0; nb < nb_thread; nb++) {
            threadAddTest.push_back(std::thread(add_element_test, nb, std::ref(element)));
            std::cout << "Init add " << nb << std::endl;
        }
        for(uint nb = 0; nb < nb_thread; nb++) {
            threadRemoveTest.push_back(std::thread(remove_element_test, nb, std::ref(element)));
            std::cout << "Init remove " << nb << std::endl;
        }
        for (std::thread &th : threadAddTest) {
           th.join();
        }
        for (std::thread &th : threadRemoveTest) {
           th.join();
        }
        std::cout << "\nProtect asset - Add / remove test: OK\n\n" << std::endl;
    }

    // Protect asset - Concurrent access on element test
    {
        uint nbThread = 20;
        MapTest mapTest;
        t_elemt_test element1 = { "element_1", 0 };
        t_elemt_test element2 = { "element_2", 0 };
        mapTest.push_back(element1);
        mapTest.push_back(element2);
        std::vector<std::thread> threadTest;
        for(uint nb = 0; nb < nbThread; nb++) {
            threadTest.push_back(std::thread(lock_boucle_element_test, nb, std::ref(mapTest.at(nb % 2))));
            std::cout << "Init " << nb << std::endl;
        }
        for (std::thread &th : threadTest) {
           th.join();
        }
        for (auto &test : mapTest) {
            std::cout << test.name << "=" << test.value << std::endl;
            assert(test.value == g_count_test * nbThread / 2);
        }
        std::cout << "\nProtect asset - Concurrent access on element test: OK\n\n" << std::endl;
    }
}
