/*  =========================================================================
    fty_nut_configuration_helper - fty nut configuration helper

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
    fty_nut_configuration_helper - fty nut configuration helper
@discuss
@end
*/

#include "fty_nut_library.h"

namespace fty
{
namespace nut
{

std::mutex g_asset_mutex;

/**
 * \brief Lock an asset for simultaneous access protection (blocking)
 * \param asset_mutex_map Asset mutex map
 * \param asset_name Asset name to lock
 */
void protect_asset_lock(t_asset_mutex_map& asset_mutex_map, std::string asset_name) {
    std::unique_lock<std::mutex> lock_asset(g_asset_mutex);
    auto it = asset_mutex_map.find(asset_name);
    if (it == asset_mutex_map.end()) {
        auto ret = asset_mutex_map.insert(
            std::pair<std::string, std::shared_ptr<std::mutex>>(asset_name, std::shared_ptr<std::mutex>(new std::mutex())));
        it = ret.first;
    }
    lock_asset.unlock();
    it->second.get()->lock();
}

/**
 * \brief Unlock an asset for simultaneous access protection
 * \param asset_mutex_map Asset mutex map
 * \param asset_name Asset name to unlock
 * \return True if protection found and unlocked
 *         False if protection not found
 */
bool protect_asset_unlock(t_asset_mutex_map& asset_mutex_map, std::string asset_name) {
    std::unique_lock<std::mutex> lock_asset(g_asset_mutex);
    auto it = asset_mutex_map.find(asset_name);
    if (it != asset_mutex_map.end()) {
        it->second.get()->unlock();
        return true;
    }
    return false;
}

/**
 * \brief Try locking an asset for simultaneous access protection (no blocking)
 * \param asset_mutex_map Asset mutex map
 * \param asset_name Asset name to try locking
 * \return True if protection found and locking possible
 *         False if protection still locked or protection not found
 */
bool protect_asset_try_lock(t_asset_mutex_map& asset_mutex_map, std::string asset_name) {
    std::unique_lock<std::mutex> lock_asset(g_asset_mutex);
    auto it = asset_mutex_map.find(asset_name);
    if (it != asset_mutex_map.end()) {
        if (it->second.get()->try_lock()) {
            it->second.get()->unlock();
            return true;
        }
    }
    return false;
}

/**
 * \brief Remove asset protection for simultaneous access
 * \param asset_mutex_map Asset mutex map
 * \param asset_name Asset name to remove protection
 * \return True if protection found and removed
 *         False if protection not removed (still locked) or protection not found
 */
bool protect_asset_remove(t_asset_mutex_map& asset_mutex_map, std::string asset_name) {
    std::unique_lock<std::mutex> lock_asset(g_asset_mutex);
    auto it = asset_mutex_map.find(asset_name);
    if (it != asset_mutex_map.end()) {
        int retry = 10;
        while (retry-- > 0) {
            if (it->second.get()->try_lock()) {
                it->second.get()->unlock();
                asset_mutex_map.erase(it);
                return true;
            }
        }
    }
    return false;
}

/**
 * \brief Functor to check if an element is before another in a collection.
 * \param start Start of collection.
 * \param end End of collection.
 * \param a First element to check.
 * \param b Second element to check.
 * \return True if a is before b in collection (missing elements are considered to be at the end of the collection).
 */
template <typename It, typename Val>
bool isBefore(It start, It end , const Val& a, const Val& b) {
    const auto itA = std::find(start, end, a);
    const auto itB = std::find(start, end, b);
    return itA < itB;
}

// FIXME: needed for compilation
template bool isBefore(std::string const*, std::string const*, std::string const&, std::string const&);

/**
 * \brief Check if we can assess a NUT driver configuration's working state.
 * \param configuration NUT driver configuration to assess.
 * \return True if it is assessable.
 *
 * Only drivers we know about can be assessed, as only they will be scanned by
 * assetScanDrivers().
 */
bool canDeviceConfigurationWorkingStateBeAssessed(const nutcommon::DeviceConfiguration& configuration)
{
    const static std::set<std::string> knownDrivers = {
        "netxml-ups",
        "snmp-ups",
        "snmp-ups-dmf",
        "dummy-snmp"
    } ;

    return knownDrivers.count(configuration.at("driver"));
}

/**
 * \brief Extract the security document types from a device configuration.
 * \param configuration Device configuration to analyse.
 * \return Set of security document types found in the device configuration.
 */
std::set<std::string> getSecurityDocumentTypesFromDeviceConfiguration(const nutcommon::DeviceConfiguration& configuration) {
    std::set<std::string> result;

    if (configuration.count("community")) {
        result.emplace("Snmpv1");
    }
    if (configuration.count("secName")) {
        result.emplace("Snmpv3");
    }

    return result;
}

/**
 * \brief Extract all IP addresses from an asset.
 * \param proto Asset to extract IP addresses from.
 * \return List of IP addresses as strings.
 */
std::vector<std::string> getNetworkAddressesFromAsset(fty_proto_t* asset)
{
    const static std::array<std::string, 2> prefixes = {
        "ip.",
        "ipv6."
    } ;

    // Fetch all network addresses.
    std::vector<std::string> addresses;
    for (const auto& prefix : prefixes) {
        const char* address;
        for (size_t i = 1; (address = fty_proto_ext_string(asset, (prefix + std::to_string(i)).c_str(), nullptr)); i++) {
            addresses.emplace_back(address);
        }
    }

    return addresses;
}


/**
 * \brief Pretty-print ComputeAssetConfigurationUpdateResult.
 * \param os Output stream.
 * \param results ComputeAssetConfigurationUpdateResult.
 * \return Output stream.
 */
std::string serialize( const ComputeAssetConfigurationUpdateResult& results)
{
    std::stringstream ss;

    for (const auto& result : std::vector<std::pair<const char*, const nutcommon::DeviceConfigurations&>>({
        { "Working configurations:", results.workingConfigurations },
        { "Non-working configurations:", results.nonWorkingConfigurations },
        { "New configurations:", results.newConfigurations },
        { "Unknown state configurations:", results.unknownStateConfigurations },
    })) {
        ss << result.first << std::endl;
        for (const auto& configuration : result.second) {
            ss << configuration << std::endl;
        }
    }

    return ss.str();
}

/**
 * \brief Pretty-print set of security document IDs.
 * \param secwIDs Set of security document IDs to serialize.
 * \return String of security document IDs.
 */
std::string serialize(const std::set<secw::Id>& secwIDs) {
    std::stringstream ss;

    for (auto itSecwID = secwIDs.begin(); itSecwID != secwIDs.end(); itSecwID++) {
        if (itSecwID != secwIDs.begin()) {
            ss << "; ";
        }
        ss << *itSecwID;
    }

    return ss.str();
}

std::string serialize(const nutcommon::DeviceConfiguration& conf) {
    std::stringstream ss;

    ss << conf;

    return ss.str();
}


/**
 * \brief Check if device configuration is a subset of another.
 * \param subset Device configuration subset.
 * \param superset Device configuration superset.
 * \return True iff subset of superset.
 */
bool isDeviceConfigurationSubsetOf(const nutcommon::DeviceConfiguration& subset, const nutcommon::DeviceConfiguration& superset)
{
    for (const auto& itSubset : subset) {
        // Field "desc" is not important, skip it.
        if (itSubset.first == "desc") {
            continue;
        }

        auto itSuperset = superset.find(itSubset.first);
        if (itSuperset == superset.end() || itSubset != (*itSuperset)) {
            return false;
        }
    }

    return true;
}


/**
 * \brief Get non-default attributes from device configuration.
 * \param configuration Device configuration to extract non-default attributes from.
 * \param type Device configuration type work with.
 * \return Non-default attributes from device configuration.
 */
nutcommon::DeviceConfiguration getAttributesFromDeviceConfiguration(const nutcommon::DeviceConfiguration& configuration, const DeviceConfigurationInfoDetail& type)
{
    nutcommon::DeviceConfiguration result = configuration;

    // Remove default attributes.
    for (const auto defaultAttribute : type.defaultAttributes) {
        result.erase(defaultAttribute.first);
    }

    // Remove extra+security document attributes.
    const static std::array<const char*, 9> extraAttributes {
         // Mandatory properties
        "device",
        "port",
         // SNMPv1
        "community",
         // SNMPv3
        "secLevel",
        "secName",
        "authPassword",
        "authProtocol",
        "privPassword",
        "privProtocol",
    };

    for (const auto& extraAttribute : extraAttributes) {
        result.erase(extraAttribute);
    }

    return result;
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

uint g_nb_count_test = 1000;
fty::nut::t_asset_mutex_map g_asset_mutex_map_test;

struct t_elemt_test {
    std::string name;
    uint value;
};

using MapTest = std::vector<t_elemt_test>;

void lock_element_test(uint id, uint pause_ms, t_elemt_test &elent_test)
{
    std::cout << "#" << id << " :lock_element_test " << elent_test.name << std::endl;
    fty::nut::protect_asset_lock(g_asset_mutex_map_test, elent_test.name);
    elent_test.value ++;
    std::this_thread::sleep_for(std::chrono::milliseconds(pause_ms));
    fty::nut::protect_asset_unlock(g_asset_mutex_map_test, elent_test.name);
    std::cout << "#" << id << " :unlock_element_test " << elent_test.name << std::endl;
}

void lock_boucle_element_test(uint id, t_elemt_test &elent_test)
{
    uint i = 0;
    std::cout << "#" << id << " :lock_boucle_element_test " << elent_test.name << std::endl;
    while (i++ < g_nb_count_test) {
        fty::nut::protect_asset_lock(g_asset_mutex_map_test, elent_test.name);
        elent_test.value ++;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        fty::nut::protect_asset_unlock(g_asset_mutex_map_test, elent_test.name);
    }
}

void add_element_test(int id, t_elemt_test &elent_test)
{
    uint i = 0;
    while (i++ < 100) {
        //std::cout << "#" << id << " :add_element_test " << elent_test.name << " " << elent_test.value << std::endl;
        fty::nut::protect_asset_lock(g_asset_mutex_map_test, elent_test.name);
        elent_test.value ++;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        fty::nut::protect_asset_unlock(g_asset_mutex_map_test, elent_test.name);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void remove_element_test(int id, t_elemt_test &elent_test)
{
    uint i = 0;
    while (i++ < 100) {
        fty::nut::protect_asset_remove(g_asset_mutex_map_test, elent_test.name);
        //std::cout << "#" << id << " :remove_element_test " << elent_test.name << " " << elent_test.value << " res=" << res << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void
fty_nut_configuration_helper_test (bool verbose)
{
    std::cerr << " * fty_nut_configuration_helper:\n\n" << std::endl;

    // Protect asset - Basic test
    {
        t_elemt_test element_1 = { "element_1", 0 };
        std::thread thread_block1_test(lock_element_test, 1, 5000, std::ref(element_1));
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        assert(element_1.value == 1);
        std::thread thread_block2_test(lock_element_test, 2, 1000, std::ref(element_1));

        int timeout = 1;
        while (timeout++ < 5) {
            assert(element_1.value == 1);
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        assert(element_1.value == 2);
        thread_block1_test.join();
        thread_block2_test.join();
        std::cerr << "\nProtect asset - Basic test: OK\n\n" << std::endl;
    }

    // Protect asset - Add / remove test
    {
        uint nb_thread = 2;
        t_elemt_test element = { "element_1", 0 };
        std::vector<std::thread> thread_add_test;
        std::vector<std::thread> thread_remove_test;
        for(uint nb = 0; nb < nb_thread; nb++) {
            thread_add_test.push_back(std::thread(add_element_test, nb, std::ref(element)));
            std::cout << "Init add " << nb << std::endl;
        }
        for(uint nb = 0; nb < nb_thread; nb++) {
            thread_remove_test.push_back(std::thread(remove_element_test, nb, std::ref(element)));
            std::cout << "Init remove " << nb << std::endl;
        }
        for (std::thread &th : thread_add_test) {
           th.join();
        }
        for (std::thread &th : thread_remove_test) {
           th.join();
        }
        std::cout << "\nProtect asset - Add / remove test: OK\n\n" << std::endl;
    }

    // Protect asset - Concurrent access on element test
    {
        uint nb_thread = 20;
        MapTest map_test;
        t_elemt_test element_1 = { "element_1", 0 };
        t_elemt_test element_2 = { "element_2", 0 };
        map_test.push_back(element_1);
        map_test.push_back(element_2);
        std::vector<std::thread> thread_test;
        for(uint nb = 0; nb < nb_thread; nb++) {
            thread_test.push_back(std::thread(lock_boucle_element_test, nb, std::ref(map_test.at(nb % 2))));
            std::cout << "Init " << nb << std::endl;
        }
        for (std::thread &th : thread_test) {
           th.join();
        }
        for (auto &test : map_test) {
            std::cout << test.name << "=" << test.value << std::endl;
            assert(test.value == g_nb_count_test * nb_thread / 2);
        }
        std::cout << "\nProtect asset - Concurrent access on element test: OK\n\n" << std::endl;
    }
}

