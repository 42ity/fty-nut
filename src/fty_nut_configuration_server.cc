/*  =========================================================================
    fty_nut_configuration_server - fty nut configuration actor

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
    fty_nut_configuration_server - fty nut configuration actor
@discuss
@end
*/

#include "fty_nut_classes.h"

#include <forward_list>
#include <regex>
#include <future>
#include <fty_common_nut_credentials.h>

namespace fty
{
namespace nut
{

using namespace DBAssetsDiscovery;

constexpr int SCAN_TIMEOUT = 5;

// Bunch of helper functions.

auto isConfSnmp =   [](const nutcommon::DeviceConfiguration& conf) -> bool { return conf.at("driver").find_first_of("snmp-ups") == 0; } ;
auto confSnmpVersion = [](const nutcommon::DeviceConfiguration& conf) -> int {
    if (!isConfSnmp(conf)) { return -1; }
    auto snmp_version = conf.find("snmp_version");
    if (snmp_version == conf.end() || snmp_version->second == "v1") { return 1; }
    else if (snmp_version->second == "v2c") { return 2; }
    else if (snmp_version->second == "v3") { return 3; }
    else { return 0; }
} ;
auto confSnmpMib =  [](const nutcommon::DeviceConfiguration& conf) -> std::string {
    return conf.count("mibs") > 0 ? conf.at("mibs") : "auto";
} ;
auto confSnmpSec =  [](const nutcommon::DeviceConfiguration& conf) -> std::string {
    return conf.count("secLevel") > 0 ? conf.at("secLevel") : "noAuthNoPriv";
} ;
auto confSnmpCom =  [](const nutcommon::DeviceConfiguration& conf) -> std::string {
    return conf.count("community") > 0 ? conf.at("community") : "public";
} ;

/**
 * \brief Functor to check if an element is before another in a collection.
 * \param start Start of collection.
 * \param end End of collection.
 * \param a First element to check.
 * \param b Second element to check.
 * \return True if a is before b in collection (missing elements are considered to be at the end of the collection).
 */
template <typename It, typename Val> bool isBefore(It start, It end , const Val& a, const Val& b) {
    const auto itA = std::find(start, end, a);
    const auto itB = std::find(start, end, b);
    return itA < itB;
}

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
        "snmp-ups-dmf"
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
 * \brief Scan asset for NUT driver configurations.
 *
 * The scan will detect the following drivers:
 * - netxml-ups
 * - snmp-ups (SNMPv1 and SNMPv3)
 * - snmp-ups-dmf (SNMPv1 and SNMPv3)
 *
 * \warning This won't return the list of all *working* NUT device configurations, as the list of handled NUT drivers is not exhaustive!
 *
 * \param pool PoolWorker to use.
 * \param asset fty_proto_t of asset to scan.
 * \param credentialsSnmpV1 SNMPv1 credentials to test.
 * \param credentialsSnmpV3 SNMPv3 credentials to test.
 * \return All detected and working NUT device configurations.
 */
nutcommon::DeviceConfigurations assetScanDrivers(messagebus::PoolWorker& pool, fty_proto_t *asset, const std::vector<nutcommon::CredentialsSNMPv1>& credentialsSnmpV1, const std::vector<nutcommon::CredentialsSNMPv3>& credentialsSnmpV3)
{
    nutcommon::DeviceConfigurations results;

    const auto addresses = getNetworkAddressesFromAsset(asset);

    std::forward_list<std::future<nutcommon::DeviceConfigurations>> futureResults;

    // Launch a bunch of scans in parallel.
    for (const auto& address : addresses) {
        nutcommon::ScanRangeOptions scanRangeOptions(address, SCAN_TIMEOUT);
        /// XXX: static_cast required because of deprecated functions creating overloaded functions.
        for (const auto& credential : credentialsSnmpV3) {
            futureResults.emplace_front(pool.schedule(static_cast<nutcommon::DeviceConfigurations(*)(const nutcommon::ScanRangeOptions&, const nutcommon::CredentialsSNMPv3&, bool)>(&nutcommon::scanDeviceRangeSNMPv3), scanRangeOptions, credential, false));
        }
        for (const auto& credential : credentialsSnmpV1) {
            futureResults.emplace_front(pool.schedule(static_cast<nutcommon::DeviceConfigurations(*)(const nutcommon::ScanRangeOptions&, const nutcommon::CredentialsSNMPv1&, bool)>(&nutcommon::scanDeviceRangeSNMPv1), scanRangeOptions, credential, false));
        }
        futureResults.emplace_front(pool.schedule(static_cast<nutcommon::DeviceConfigurations(*)(const nutcommon::ScanRangeOptions&)>(&nutcommon::scanDeviceRangeNetXML), scanRangeOptions));
    }

    /**
     * Grab all results.
     * FIXME: Rewrite with std::when_all once C++2x comes around.
     */
    for (auto& futureResult : futureResults) {
        auto result = futureResult.get();
        std::move(result.begin(), result.end(), std::back_inserter(results));
    }

    return results;
}

/**
 * \brief Extracts the fingerprint of a NUT device configuration.
 * \param configuration NUT device configuration.
 * \return NUT device configuration fingerprint.
 *
 * The generated extract uniquely identifies a NUT device configuration as far
 * as these properties are concerned:
 * - Driver type,
 * - Driver "subtype" (MIBs or whatever required information to access device),
 * - Port,
 * - Credentials.
 *
 * This allows reducing variants of a NUT device configuration to the same
 * driver fingerprint.
 *
 * \warning If a driver fingerprint is not recognized, the NUT device
 * configuration itself will be returned (which will uniquely identify itself).
 */
nutcommon::DeviceConfiguration extractConfigurationFingerprint(const nutcommon::DeviceConfiguration& configuration)
{
    nutcommon::DeviceConfiguration result;

    const static std::map<std::string, std::set<std::string>> fingerprintTemplates = {
        { "snmp-ups", {
            "driver", "port", "mibs", "snmp_version", "community", "secLevel", "secName", "authPassword", "authProtocol", "privPassword", "privProtocol"
        }},
        { "snmp-ups-dmf", {
            "driver", "port", "mibs", "snmp_version", "community", "secLevel", "secName", "authPassword", "authProtocol", "privPassword", "privProtocol"
        }},
        { "netxml-ups", {
            "driver", "port"
        }}
    };

    const auto& fingerprintTemplateIterator = fingerprintTemplates.find(configuration.at("driver"));
    if (fingerprintTemplateIterator != fingerprintTemplates.end()) {
        // Extract the template from the configuration.
        for (const auto& fingerprintKey : fingerprintTemplateIterator->second) {
            const auto &configurationKeyIterator = configuration.find(fingerprintKey);
            if (configurationKeyIterator != configuration.end()) {
                result[configurationKeyIterator->first] = configurationKeyIterator->second;
            }
        }
    }
    else {
        result = configuration;
    }

    return result;
}

struct ComputeAssetConfigurationUpdateResult {
    /// \brief Known, working configuration.
    nutcommon::DeviceConfigurations workingConfigurations;
    /// \brief Known, non-working configuration.
    nutcommon::DeviceConfigurations nonWorkingConfigurations;
    /// \brief Unknown, working configuration.
    nutcommon::DeviceConfigurations newConfigurations;
    /// \brief Unknown, unassessable configuration.
    nutcommon::DeviceConfigurations unknownStateConfigurations;
};

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
 * \brief Sort NUT driver configurations into categories from known and detected configurations.
 * \param knownConfigurations Known NUT device configurations in database.
 * \param detectedConfigurations Detected NUT device configurations at runtime.
 * \return All NUT driver configurations sorted into categories.
 */
ComputeAssetConfigurationUpdateResult computeAssetConfigurationUpdate(const nutcommon::DeviceConfigurations& knownConfigurations, const nutcommon::DeviceConfigurations& detectedConfigurations)
{
    ComputeAssetConfigurationUpdateResult result;

    // Fingerprints of everything we detected.
    std::set<nutcommon::DeviceConfiguration> detectedFingerprints;
    std::transform(
        detectedConfigurations.begin(),
        detectedConfigurations.end(),
        std::inserter(detectedFingerprints, detectedFingerprints.begin()),
        extractConfigurationFingerprint
    );

    // Fingerprints we matched in the database.
    std::set<nutcommon::DeviceConfiguration> matchedFingerprints;

    for (const auto& knownConfiguration : knownConfigurations) {
        if (canDeviceConfigurationWorkingStateBeAssessed(knownConfiguration)) {
            // This is a known NUT driver, classify it as working or non-working.
            // TODO: Don't work ???
            //const auto& detectedFingerprintIterator = detectedFingerprints.find(extractConfigurationFingerprint(knownConfiguration));
            nutcommon::DeviceConfiguration conf = extractConfigurationFingerprint(knownConfiguration);
            auto detectedFingerprintIterator = detectedFingerprints.end();
            auto it_detectedFingerprints = detectedFingerprints.begin();
            while (it_detectedFingerprints != detectedFingerprints.end()) {
                bool find = true;
                auto it = it_detectedFingerprints->begin();
                while (it != it_detectedFingerprints->end()) {
                    auto it_conf = conf.find(it->first);
                    if (it_conf == conf.end() || (it_conf != conf.end() && it_conf->second != it->second)) {
                        find = false;
                        break;
                    }
                    it ++;
                }
                if (find) {
                    detectedFingerprintIterator = it_detectedFingerprints;
                    break;
                }
                it_detectedFingerprints ++;
            }
            if (detectedFingerprintIterator != detectedFingerprints.end()) {
                // NUT driver configuration seems to work.
                result.workingConfigurations.push_back(knownConfiguration);

                matchedFingerprints.insert(*detectedFingerprintIterator);
            }
            else {
                // NUT driver configuration doesn't seem to work.
                result.nonWorkingConfigurations.push_back(knownConfiguration);
            }
        }
        else {
            // Unknown NUT driver configuration type.
            result.unknownStateConfigurations.push_back(knownConfiguration);
        }
    }

    /**
     * We classified known NUT device configurations, now we need to deal with
     * unknown and detected NUT device configurations.
     */
    std::set<nutcommon::DeviceConfiguration> unmatchedFingerprints;
    std::set_difference(
        detectedFingerprints.begin(), detectedFingerprints.end(),
        matchedFingerprints.begin(), matchedFingerprints.end(),
        std::inserter(unmatchedFingerprints, unmatchedFingerprints.begin())
    );

    for (const auto& detectedConfiguration : detectedConfigurations) {
        if (unmatchedFingerprints.count(extractConfigurationFingerprint(detectedConfiguration))) {
            // New and working device configuration.
            result.newConfigurations.push_back(detectedConfiguration);
        }
    }

    return result;
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
 * \brief Instanciate a device configuration template from a device.
 *
 * Device configuration templates can contain values derived from the asset with the following syntax:
 * - ${asset.aux.<auxiliary property key>} : value comes from asset auxilliary attributes
 * - ${asset.ext.<extended property key>} : value comes from asset extended attributes
 *
 * \param asset Asset to instanciate from.
 * \param template Device configuration template.
 * \return Instanciated device configuration template, or empty device configuration on error.
 */
nutcommon::DeviceConfiguration instanciateDeviceConfigurationFromTemplate(fty_proto_t* asset, const nutcommon::DeviceConfiguration& confTemplate)
{
    nutcommon::DeviceConfiguration result;

    // Instanciate each property in the template.
    for (const auto& property : confTemplate) {
        const static std::regex token(R"xxx(\$\{([^}]+)\})xxx", std::regex::optimize);
        std::string templatedValue = property.second;
        std::smatch matches;

        while (std::regex_search(templatedValue, matches, token)) {
            // We need to template the property value.
            auto str = matches[1].str();

            // Try to instanciate value.
            const char* value = nullptr;
            if (str.find_first_of("asset.ext.") == 0) {
                value = fty_proto_ext_string(asset, str.c_str()+10, nullptr);
            }
            else if (str.find_first_of("asset.aux.") == 0) {
                value = fty_proto_aux_string(asset, str.c_str()+10, nullptr);
            }

            // Bail out if value wasn't found.
            if (!value) {
                return {};
            }

            templatedValue.replace(matches.position(1)-2, matches.length(1)+3, value);
        }

        result.emplace(property.first, templatedValue);
    }

    return result;
}

/**
 * \brief Return the order of preference for an asset's driver configurations.
 * \param asset Asset to sort device configurations with.
 * \param configurations Device configurations to sort.
 * \return List of indexes of driver configurations, ordered from most to least preferred.
 */
std::vector<size_t> sortDeviceConfigurationPreferred(fty_proto_t* asset, const nutcommon::DeviceConfigurations& configurations) {
    // Initialize vector of indexes.
    std::vector<size_t> indexes(configurations.size());
    std::iota(indexes.begin(), indexes.end(), 0);

    std::sort(indexes.begin(), indexes.end(), [&configurations, &asset](size_t a, size_t b) {
        /**
         * This is a fairly complicated sort function. Here, we try to return
         * true if confA is better than confB.
         *
         * This to keep in mind:
         * - std::sort expects a total order.
         * - Total sort means if we return true for a condition, we must return false in the "mirror" condition.
         */

        // TODO: Don't work ???
        //const std::string type = fty_proto_type(asset);
        const std::string type = "ups";
        const auto& confA = configurations[a];
        const auto& confB = configurations[b];

        const static std::array<std::string, 4> upsDriverPriority  = { "dummy-ups", "netxml-ups", "snmp-ups", "snmp-ups-dmf" };
        const static std::array<std::string, 4> epduDriverPriority = { "dummy-ups", "snmp-ups", "snmp-ups-dmf", "netxml-ups" };
        const static std::array<std::string, 2> snmpMibPriority = { "pw", "mge" };
        const static std::array<std::string, 3> snmpSecPriority = { "authPriv", "authNoPriv", "noAuthNoPriv" };

        const bool isConfA_SNMP = isConfSnmp(confA);
        const bool isConfB_SNMP = isConfSnmp(confB);
        const int confA_SNMP_version = confSnmpVersion(confA);
        const int confB_SNMP_version = confSnmpVersion(confB);
        const std::string confA_SNMPv3_security = confSnmpSec(confA);
        const std::string confB_SNMPv3_security = confSnmpSec(confB);
        const std::string confA_SNMPv1_community = confSnmpCom(confA);
        const std::string confB_SNMPv1_community = confSnmpCom(confB);
        const std::string confA_SNMP_mib = confSnmpMib(confA);
        const std::string confB_SNMP_mib = confSnmpMib(confB);

        if (type == "ups") {
            if (confA.at("driver") != confB.at("driver")) {
                return isBefore(upsDriverPriority.begin(), upsDriverPriority.end(), confA.at("driver"), confB.at("driver"));
            }
        }
        else if (type == "epdu" || type == "pdu" || type == "sts") {
            if (confA.at("driver") != confB.at("driver")) {
                return isBefore(epduDriverPriority.begin(), epduDriverPriority.end(), confA.at("driver"), confB.at("driver"));
            }
        }

        // SNMP preferences.
        if (isConfA_SNMP && isConfB_SNMP) {
            // Prefer most recent SNMP version.
            if (confA_SNMP_version != confB_SNMP_version) {
                return confA_SNMP_version > confB_SNMP_version;
            }
            // Prefer most secure SNMPv3 security level.
            if (confA_SNMPv3_security != confB_SNMPv3_security) {
                return isBefore(snmpSecPriority.begin(), snmpSecPriority.end(), confA_SNMPv3_security, confB_SNMPv3_security);
            }
            // Perfer some MIBs over others.
            if (confA_SNMP_mib != confB_SNMP_mib) {
                return isBefore(snmpMibPriority.begin(), snmpMibPriority.end(), confA_SNMP_mib, confB_SNMP_mib);
            }
            // Prefer other communities than public.
            if (confA_SNMPv1_community != "public" && confB_SNMPv1_community == "public") { return true; }
            if (confA_SNMPv1_community == "public" && confB_SNMPv1_community != "public") { return false; }
        }

        // Fallback.
        return confA < confB;
    });

    return indexes;
}

/**
 * \brief Find which device configuration type a given device configuration best matches.
 * \param asset Asset to check with.
 * \param configuration Device configuration to match.
 * \param types Device configuration types to match against.
 * \return Iterator to best device configuration type match, or end of collection if no suitable match found.
 */
DeviceConfigurationInfoDetails::const_iterator matchDeviceConfigurationToBestDeviceConfigurationType(fty_proto_t* asset, const nutcommon::DeviceConfiguration& configuration, const DeviceConfigurationInfoDetails& types)
{
    auto bestMatch = types.end();

    for (auto itType = types.begin(); itType != types.end(); itType++) {
        // Instanciate device configuration type to have something to compare to.
        nutcommon::DeviceConfiguration instanciatedType = instanciateDeviceConfigurationFromTemplate(asset, itType->defaultAttributes);
        if (instanciatedType.empty()) {
            continue;
        }

        // Properties that have to match.
        if (instanciatedType.at("driver") != configuration.at("driver")) {
            continue;
        }
        if (instanciatedType.at("port") != configuration.at("port")) {
            continue;
        }
        if (getSecurityDocumentTypesFromDeviceConfiguration(configuration) != itType->secwDocumentTypes) {
            continue;
        }

        /**
         * TODO: make further checks to pick the best device configuration
         * type, not the first one that matches.
         */
        bestMatch = itType;
        break;
    }

    return bestMatch;
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

/**
 * \brief Request fty_proto_t from asset name.
 * \param assetName Asset name to request.
 * \return fty_proto_t (to be freed by caller) or nullptr.
 */
fty_proto_t* fetchProtoFromAssetName(const std::string& assetName)
{
    /// TODO: fetch fty_proto_t from asset name by querying fty-asset.
    /*fty_proto_t* asset = fty_proto_new(FTY_PROTO_ASSET);
    fty_proto_set_type(asset, "epdu");
    fty_proto_ext_insert(asset, "ip.1", "10.130.35.91");
    return asset;*/

    fty_proto_t *res = nullptr;

    const auto clientId = messagebus::getClientId("fty-nut-configuration-requester");
    MlmClientGuard mClient(mlm_client_new());
    mlm_client_connect(mClient.get(), MLM_ENDPOINT, 1000, clientId.c_str());

    // Request details of asset.
    zmsg_t *request = zmsg_new();
    ZuuidGuard uuid(zuuid_new());
    const char *uuid_canonical = zuuid_str_canonical(uuid.get());
    zmsg_addstr(request, "GET");
    zmsg_addstr(request, uuid_canonical);
    zmsg_addstr(request, assetName.c_str());
    int r = mlm_client_sendto(mClient.get(), AGENT_FTY_ASSET, "ASSET_DETAIL", nullptr, 1000, &request);
    if (r != 0) {
        log_error ("Request ASSET_DETAIL failed for %s (r: %d)", assetName.c_str(), r);
        zmsg_destroy(&request);
        return res;
    }
    zmsg_destroy(&request);
    ZpollerGuard poller(zpoller_new(mlm_client_msgpipe(mClient.get()), nullptr));
    if (zpoller_wait(poller.get(), 1000)) {
        //ZmsgGuard reply(mlm_client_recv(mClient.get()));
        zmsg_t *reply = mlm_client_recv(mClient.get());
        ZstrGuard uuid_recv(zmsg_popstr(reply));
        // Check uuid matching
        if (!streq(uuid_recv.get(), uuid_canonical)) {
            log_error ("UUID doesn't match (%s, %s, %s)", assetName.c_str(), uuid_canonical, uuid_recv.get());
        }
        // Test if it a fty_proto msg
        else if (fty_proto_is(reply)) {
            res = fty_proto_decode(&reply);
        }
        else {
            log_error("Reply not a fty_proto msg (%s)", assetName.c_str());
        }
        //fty_proto_print(res);
        zmsg_destroy(&reply);
    }
    return res;
}

nutcommon::DeviceConfiguration instanciateSecurityWalletDocument(const nutcommon::CredentialsSNMPv1& creds)
{
    return {{ "community", creds.community }};
}

nutcommon::DeviceConfiguration instanciateSecurityWalletDocument(const nutcommon::CredentialsSNMPv3& creds)
{
    nutcommon::DeviceConfiguration result {
        { "snmp_version", "v3" },
        { "secName", creds.secName },
        { "secLevel", creds.privPassword.empty() ? (creds.authPassword.empty() ? "noAuthNoPriv" : "authNoPriv") : "authPriv" },
    } ;

    if (!creds.authPassword.empty()) { result["authPassword"] = creds.authPassword; }
    if (!creds.authProtocol.empty()) { result["authProtocol"] = creds.authProtocol; }
    if (!creds.privPassword.empty()) { result["privPassword"] = creds.privPassword; }
    if (!creds.privProtocol.empty()) { result["privProtocol"] = creds.privProtocol; }

    return result;
}

nutcommon::DeviceConfiguration ftyToNutDeviceConfiguration(const DeviceConfigurationInfo& conf, const std::vector<nutcommon::CredentialsSNMPv1>& credentialsSNMPv1, const std::vector<nutcommon::CredentialsSNMPv3>& credentialsSNMPv3)
{
    nutcommon::DeviceConfiguration results = conf.attributes;

    /// XXX: improve security document instanciation.
    for (const auto& secwDocumentId : conf.secwDocumentIdList) {
        auto itCredSnmpV1 = std::find_if(credentialsSNMPv1.begin(), credentialsSNMPv1.end(),
            [&secwDocumentId](const nutcommon::CredentialsSNMPv1& cred) {
                return cred.documentId == secwDocumentId;
            }
        );
        auto itCredSnmpV3 = std::find_if(credentialsSNMPv3.begin(), credentialsSNMPv3.end(),
            [&secwDocumentId](const nutcommon::CredentialsSNMPv3& cred) {
                return cred.documentId == secwDocumentId;
            }
        );

        if (itCredSnmpV1 != credentialsSNMPv1.end()) {
            auto cred = instanciateSecurityWalletDocument(*itCredSnmpV1);
            results.insert(cred.begin(), cred.end());
        }
        else if (itCredSnmpV3 != credentialsSNMPv3.end()) {
            auto cred = instanciateSecurityWalletDocument(*itCredSnmpV3);
            results.insert(cred.begin(), cred.end());
        }
    }

    return results;
}

std::set<secw::Id> matchSecurityDocumentIDsFromDeviceConfiguration(const nutcommon::DeviceConfiguration& conf, const std::vector<nutcommon::CredentialsSNMPv1>& credsV1, const std::vector<nutcommon::CredentialsSNMPv3>& credsV3)
{
    std::set<secw::Id> result;

    auto itCommunity = conf.find("community");
    auto itSecName = conf.find("secName");

    if (itCommunity != conf.end()) {
        // Match SNMPv1 security document.
        for (const auto& credSNMPv1 : credsV1) {
            if (itCommunity->second == credSNMPv1.community) {
                result.emplace(credSNMPv1.documentId);
                break;
            }
        }
    }
    if (itSecName != conf.end()) {
        // Match SNMPv3 security document.
        auto itAuthProtocol = conf.find("authProtocol");
        auto itAuthPassword = conf.find("authPassword");
        auto itPrivProtocol = conf.find("privProtocol");
        auto itPrivPassword = conf.find("privPassword");

        const std::string& secName = itSecName->second;
        const std::string& authProtocol = (itAuthProtocol != conf.end()) ? itAuthProtocol->second : "";
        const std::string& authPassword = (itAuthPassword != conf.end()) ? itAuthPassword->second : "";
        const std::string& privProtocol = (itPrivProtocol != conf.end()) ? itPrivProtocol->second : "";
        const std::string& privPassword = (itPrivPassword != conf.end()) ? itPrivPassword->second : "";

        for (const auto& credSNMPv3 : credsV3) {
            if (credSNMPv3.secName == secName &&
                credSNMPv3.authProtocol == authProtocol &&
                credSNMPv3.authPassword == authPassword &&
                credSNMPv3.privProtocol == privProtocol &&
                credSNMPv3.privPassword == privPassword) {
                result.emplace(credSNMPv3.documentId);
                break;
            }
        }
    }

    return result;
}

nutcommon::DeviceConfigurations instanciateDatabaseConfigurations(const DeviceConfigurationInfos& dbConfs, fty_proto_t* asset, const std::vector<nutcommon::CredentialsSNMPv1>& credentialsSNMPv1, const std::vector<nutcommon::CredentialsSNMPv3>& credentialsSNMPv3)
{
    nutcommon::DeviceConfigurations result;
    std::transform(
        dbConfs.begin(),
        dbConfs.end(),
        std::back_inserter(result),
        [&asset, &credentialsSNMPv1, &credentialsSNMPv3](const DeviceConfigurationInfos::value_type& val) -> nutcommon::DeviceConfiguration {
            return instanciateDeviceConfigurationFromTemplate(asset,
                ftyToNutDeviceConfiguration(val, credentialsSNMPv1, credentialsSNMPv3)
            );
        }
    );
    return result;
}

// ConfigurationManager

ConfigurationManager::ConfigurationManager(const std::string& dbConn) : m_poolScanners(8), m_dbConn(dbConn)
{
    //std::string assetName = "epdu-7";
    std::string assetName = "ups-9";
    //std::string assetName = "ups-56";
    fty_proto_t* asset = fetchProtoFromAssetName(assetName);
    if (!asset) {
        throw std::runtime_error("Unknown asset " + assetName);
    }
    scanAssetConfigurations(asset);
    automaticAssetConfigurationPrioritySort(asset);

    fty_proto_destroy(&asset);
}

/**
 * \brief Reorder asset's driver configuration priorities according to the
 * software's preferences.
 * \param asset Asset to process.
 */
void ConfigurationManager::automaticAssetConfigurationPrioritySort(fty_proto_t* asset)
{
    auto conn = tntdb::connectCached(m_dbConn);
    const auto credentialsSNMPv1 = nutcommon::getCredentialsSNMPv1();
    const auto credentialsSNMPv3 = nutcommon::getCredentialsSNMPv3();
    const std::string assetName = fty_proto_name(asset);

    // Fetch all configurations to sort.
    const DeviceConfigurationInfos knownDatabaseConfigurations = get_all_config_list(conn, assetName);
    const nutcommon::DeviceConfigurations knownConfigurations = instanciateDatabaseConfigurations(knownDatabaseConfigurations, asset, credentialsSNMPv1, credentialsSNMPv3);

    // Compute new order of driver configuration.
    const std::vector<size_t> sortedIndexes = sortDeviceConfigurationPreferred(asset, knownConfigurations);
    std::vector<size_t> newConfigurationOrder;
    std::transform(sortedIndexes.begin(), sortedIndexes.end(), std::back_inserter(newConfigurationOrder),
        [&knownDatabaseConfigurations](size_t sortedIndex) -> size_t {
            return knownDatabaseConfigurations[sortedIndex].id;
        }
    );

    modify_config_priorities(conn, assetName, newConfigurationOrder);
}

/**
 * \brief Scan an asset and update driver configurations in database.
 * \param asset Asset to process.
 *
 * This method detects working configurations on the asset and updates the
 * driver configuration database in response. The basic workflow is:
 *  1. Scan the asset,
 *  2. Compute DB updates from detected and from known driver configurations,
 *  3. Mark existing configurations as working or non-working,
 *  4. Persist newly-discovered driver configurations in database.
 */
void ConfigurationManager::scanAssetConfigurations(fty_proto_t* asset)
{
    /// Step 0: Grab all data.
    auto conn = tntdb::connectCached(m_dbConn);
    const auto credentialsSNMPv1 = nutcommon::getCredentialsSNMPv1();
    const auto credentialsSNMPv3 = nutcommon::getCredentialsSNMPv3();
    const std::string assetName = fty_proto_name(asset);

    const DeviceConfigurationInfos knownDatabaseConfigurations = get_all_config_list(conn, assetName);
    const nutcommon::DeviceConfigurations knownConfigurations = instanciateDatabaseConfigurations(knownDatabaseConfigurations, asset, credentialsSNMPv1, credentialsSNMPv3);

    /// Step 1: Scan the asset.
    const auto detectedConfigurations = assetScanDrivers(m_poolScanners, asset, credentialsSNMPv1, credentialsSNMPv3);

    /// Step 2: Compute DB updates from detected and from known driver configurations.
    const auto results = computeAssetConfigurationUpdate(knownConfigurations, detectedConfigurations);
    log_debug("Summary of device configurations after scan for asset %s:\n%s", assetName.c_str(), serialize(results).c_str());

    /// Step 3: Mark existing configurations as working or non-working.
    for (const auto& updateOrder : std::vector<std::pair<const nutcommon::DeviceConfigurations&, bool>>({
        { results.workingConfigurations, true },
        { results.unknownStateConfigurations, true },
        { results.nonWorkingConfigurations, false },
    })) {
        // Match scan results to known driver configuration in database.
        for (const auto& configuration : updateOrder.first) {
            auto itConfigurationDatabase = knownConfigurations.begin();
            auto itKnownDatabaseConfiguration = knownDatabaseConfigurations.begin();

            for (; itConfigurationDatabase != knownConfigurations.end(); itConfigurationDatabase++, itKnownDatabaseConfiguration++) {
                if (isDeviceConfigurationSubsetOf(configuration, *itConfigurationDatabase)) {
                    // Found match, update database.
                    set_config_working(conn, itKnownDatabaseConfiguration->id, updateOrder.second);
                    log_info("Marked device configuration ID %u for asset %s as %s.",
                        itKnownDatabaseConfiguration->id,
                        assetName.c_str(),
                        updateOrder.second ? "working" : "non-working"
                    );
                    break;
                }
            }

            if (itConfigurationDatabase == knownConfigurations.end()) {
                log_warning("Failed to match known detected device configuration to what's in database, configuration ignored:\n%s", serialize(configuration).c_str());
            }
        }
    }

    /// Step 4: Persist newly-discovered driver configurations in database.
    const auto deviceConfigurationTypes = get_all_configuration_types(conn);
    for (const auto& newConfiguration : results.newConfigurations) {
        // Match new configuration to configuration type.
        const auto bestDeviceConfigurationType = matchDeviceConfigurationToBestDeviceConfigurationType(asset, newConfiguration, deviceConfigurationTypes);

        if (bestDeviceConfigurationType != deviceConfigurationTypes.end()) {
            // Save new configuration into database.
            const auto attributes = getAttributesFromDeviceConfiguration(newConfiguration, *bestDeviceConfigurationType);
            const auto secwIDs = matchSecurityDocumentIDsFromDeviceConfiguration(newConfiguration, credentialsSNMPv1, credentialsSNMPv3);
            const auto configID = insert_config(conn, assetName, bestDeviceConfigurationType->id, true, true, secwIDs, attributes);

            log_info("Instanciated device configuration type \"%s\" for asset %s (id=%u, driver=%s, port=%s, secwIDs={%s}).",
                bestDeviceConfigurationType->prettyName.c_str(),
                assetName.c_str(),
                configID,
                newConfiguration.at("driver").c_str(),
                newConfiguration.at("port").c_str(),
                serialize(secwIDs).c_str()
            );
        }
        else {
            log_warning("Failed to match new device configuration to device configuration type, configuration discarded:\n%s", serialize(newConfiguration).c_str());
        }
    }
}

/**
 * \brief Apply asset configuration in database.
 * \param asset Asset to process.
 */
void ConfigurationManager::applyAssetConfiguration(fty_proto_t* asset)
{
}

// ConfigurationConnector

ConfigurationConnector::Parameters::Parameters() :
    endpoint(MLM_ENDPOINT),
    agentName("fty-nut-configuration"),
    dbUrl(DBConn::url)
{
}

ConfigurationConnector::ConfigurationConnector(ConfigurationConnector::Parameters params) :
    m_parameters(params),
    m_manager(params.dbUrl),
    m_dispatcher({
    }),
    m_worker(0),
    m_msgBus(messagebus::MlmMessageBus(params.endpoint, params.agentName))
{
    m_msgBus->connect();
    m_msgBus->receive("ETN.Q.IPMCORE.NUTCONFIGURATION", std::bind(&ConfigurationConnector::handleRequest, this, std::placeholders::_1));
}

void ConfigurationConnector::handleRequest(messagebus::Message msg) {
    if ((msg.metaData().count(messagebus::Message::SUBJECT) == 0) ||
        (msg.metaData().count(messagebus::Message::CORRELATION_ID) == 0) ||
        (msg.metaData().count(messagebus::Message::REPLY_TO) == 0)) {
        log_error("Missing subject/correlationID/replyTo in request.");
    }
    else {
        m_worker.offload([this](messagebus::Message msg) {
            auto subject = msg.metaData()[messagebus::Message::SUBJECT];
            auto corrId = msg.metaData()[messagebus::Message::CORRELATION_ID];
            log_info("Received %s (%s) request.", subject.c_str(), corrId.c_str());

            try {
                auto result = m_dispatcher(subject, msg.userData());

                log_info("Request %s (%s) performed successfully.", subject.c_str(), corrId.c_str());
                sendReply(msg.metaData(), true, result);
            }
            catch (std::exception& e) {
                log_error("Exception while processing %s (%s): %s", subject.c_str(), corrId.c_str(), e.what());
                sendReply(msg.metaData(), false, { e.what() });
            }
        }, std::move(msg));
    }
}

void ConfigurationConnector::sendReply(const messagebus::MetaData& metadataRequest, bool status, const messagebus::UserData& dataReply) {
    messagebus::Message reply;

    reply.metaData() = {
        { messagebus::Message::CORRELATION_ID, metadataRequest.at(messagebus::Message::CORRELATION_ID) },
        { messagebus::Message::SUBJECT, metadataRequest.at(messagebus::Message::SUBJECT) },
        { messagebus::Message::STATUS, status ? "ok" : "ko" },
        { messagebus::Message::TO, metadataRequest.at(messagebus::Message::REPLY_TO) }
    } ;
    reply.userData() = dataReply;

    m_msgBus->sendReply("ETN.R.IPMCORE.NUTCONFIGURATION", reply);
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
fty_nut_configuration_server_test (bool verbose)
{
    std::cerr << " * fty_nut_configuration_server: " << std::endl;

    {
        struct TestCase {
            nutcommon::DeviceConfigurations knownConfigurations;
            nutcommon::DeviceConfigurations detectedConfigurations;

            /**
             * - Working
             * - Non-working
             * - New
             * - Unknown
             */
            fty::nut::ComputeAssetConfigurationUpdateResult expectedResult;
        };

        const std::vector<TestCase> testCases = {
            // No known configuration, everything detected should be new configurations.
            TestCase({
                {},
                nutcommon::parseScannerOutput(
R"xxx(SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="public"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="private"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",snmp_version="v3",secLevel="noAuthNoPriv",secName="public"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",snmp_version="v3",secLevel="authPriv",secName="private",authPassword="azertyui",privPassword="qsdfghjk",authProtocol="MD5",privProtocol="DES"
)xxx"),

                fty::nut::ComputeAssetConfigurationUpdateResult({
                    {},
                    {},
                    nutcommon::parseScannerOutput(
R"xxx(SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="public"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="private"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",snmp_version="v3",secLevel="noAuthNoPriv",secName="public"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",snmp_version="v3",secLevel="authPriv",secName="private",authPassword="azertyui",privPassword="qsdfghjk",authProtocol="MD5",privProtocol="DES"
)xxx"),
                    {},
                }),
            }),

            // Test all cases with non-overlapping fingerprints.
            TestCase({
                nutcommon::parseScannerOutput(
R"xxx(SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="public"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="private"
SNMP:driver="dummy-ups",port="10.130.33.140"
)xxx"),
                nutcommon::parseScannerOutput(
R"xxx(SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="public"
R"xxx(SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="suprise"
)xxx"),

                fty::nut::ComputeAssetConfigurationUpdateResult({
                    nutcommon::parseScannerOutput(
R"xxx(SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="public"
)xxx"),
                    nutcommon::parseScannerOutput(
R"xxx(SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="private"
)xxx"),
                    nutcommon::parseScannerOutput(
R"xxx(SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="suprise"
)xxx"),
                    nutcommon::parseScannerOutput(
R"xxx(SNMP:driver="dummy-ups",port="10.130.33.140"
)xxx"),
                }),
            }),


            // Test all cases with overlapping fingerprints.
            TestCase({
                nutcommon::parseScannerOutput(
R"xxx(SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="public"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="public",extra="extra"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="public",extra="extra",woohoo="woohoo"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="private",extra="extra"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="private",extra="extra",woohoo="woohoo"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="privateer",extra="extra"
SNMP:driver="dummy-ups",port="10.130.33.140"
)xxx"),
                nutcommon::parseScannerOutput(
R"xxx(SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="public"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="privateer"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="suprise"
)xxx"),

                fty::nut::ComputeAssetConfigurationUpdateResult({
                    nutcommon::parseScannerOutput(
R"xxx(SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="public"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="public",extra="extra"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="public",extra="extra",woohoo="woohoo"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="privateer",extra="extra"
)xxx"),
                    nutcommon::parseScannerOutput(
R"xxx(SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="private",extra="extra"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="private",extra="extra",woohoo="woohoo"
)xxx"),
                    nutcommon::parseScannerOutput(
R"xxx(SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="suprise"
)xxx"),
                    nutcommon::parseScannerOutput(
R"xxx(SNMP:driver="dummy-ups",port="10.130.33.140"
)xxx"),
                }),
            }),
        };

        for (size_t i = 0; i < testCases.size(); i++) {
            std::cerr << "  - computeAssetConfigurationUpdate case #" << (i+1) << ": ";
            const TestCase& testCase = testCases[i];

            auto result = fty::nut::computeAssetConfigurationUpdate(testCase.knownConfigurations, testCase.detectedConfigurations);
            for (const auto& it : std::vector<std::pair<const nutcommon::DeviceConfigurations&, const nutcommon::DeviceConfigurations&>>({
                { result.workingConfigurations,         testCase.expectedResult.workingConfigurations },
                { result.nonWorkingConfigurations,      testCase.expectedResult.nonWorkingConfigurations },
                { result.newConfigurations,             testCase.expectedResult.newConfigurations },
                { result.unknownStateConfigurations,    testCase.expectedResult.unknownStateConfigurations },
            })) {
                const std::set<nutcommon::DeviceConfiguration> resultSorted(it.first.begin(), it.first.end());
                const std::set<nutcommon::DeviceConfiguration> expectedSorted(it.second.begin(), it.second.end());
                assert(resultSorted == expectedSorted);
                std::cerr << resultSorted.size() << " ";
            }

            std::cerr << "OK" << std::endl;
        }
    }

    {
        std::cerr << "  - isDeviceConfigurationSubsetOf: ";

        const auto supersetConfigurations = nutcommon::parseScannerOutput(
R"xxx(SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="public"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="public",extra="extra"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="public",extra="extra",woohoo="woohoo"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="privateer",extra="extra"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",snmp_version="v3",secLevel="noAuthNoPriv",secName="public"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",snmp_version="v3",secLevel="authPriv",secName="private",authPassword="azertyui",privPassword="qsdfghjk",authProtocol="MD5",privProtocol="DES"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="-----------------------------------",mibs="eaton_epdu",snmp_version="v3",secLevel="authPriv",secName="private",authPassword="azertyui",privPassword="qsdfghjk",authProtocol="MD5",privProtocol="DES",extra="extra"
)xxx");
        const auto subsetConfigurations = nutcommon::parseScannerOutput(
R"xxx(SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="public"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",snmp_version="v3",secLevel="noAuthNoPriv",secName="public"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",snmp_version="v3",secLevel="authPriv",secName="private",authPassword="azertyui",privPassword="qsdfghjk",authProtocol="MD5",privProtocol="DES"
)xxx");
        const auto expectedResults = std::vector<std::pair<const nutcommon::DeviceConfiguration&, std::array<bool, 7>>> {
            { subsetConfigurations[0], {
                true, true, true, false, false, false, false
            }},
            { subsetConfigurations[1], {
                false, false, false, false, true, false, false
            }},
            { subsetConfigurations[2], {
                false, false, false, false, false, true, true
            }},
        } ;

        for (const auto& expectedResult : expectedResults) {
            for (size_t i = 0; i < supersetConfigurations.size(); i++) {
                assert(fty::nut::isDeviceConfigurationSubsetOf(expectedResult.first, supersetConfigurations[i]) == expectedResult.second[i]);
            }
        }

        std::cerr << "OK" << std::endl;
    }

    {
        std::cerr << "  - instanciateDeviceConfigurationFromTemplate: ";
        fty_proto_t* asset = fty_proto_new(FTY_PROTO_ASSET);
        fty_proto_ext_insert(asset, "ipv4.1", "10.130.32.117");
        fty_proto_ext_insert(asset, "snmp_port", "161");

        const static auto templateConf = nutcommon::DeviceConfiguration {
            { "driver", "snmp-ups" },
            { "port", "${asset.ext.ipv4.1}" },
            { "port-snmp", "snmp://${asset.ext.ipv4.1}:${asset.ext.snmp_port}/" },
        };
        const static auto expectedResult = nutcommon::DeviceConfiguration {
            { "driver", "snmp-ups" },
            { "port", "10.130.32.117" },
            { "port-snmp", "snmp://10.130.32.117:161/" },
        };
        const auto result = fty::nut::instanciateDeviceConfigurationFromTemplate(asset, templateConf);
        assert(result == expectedResult);

        const static auto expectedFailures = std::vector<nutcommon::DeviceConfiguration> {
            {
                { "driver", "snmp-ups" },
                { "port", "${asset.ext.ipv4.2}" },
            },
            {
                { "driver", "snmp-ups" },
                { "port", "${idunno}" },
            },
        };
        for (const auto& expectedFailure : expectedFailures) {
            const auto result = fty::nut::instanciateDeviceConfigurationFromTemplate(asset, expectedFailure);
            assert(result.empty());
        }

        fty_proto_destroy(&asset);
        std::cerr << "OK" << std::endl;
    }

    {
        std::cerr << "  - sortDeviceConfigurationPreferred: ";

        const auto configurations = nutcommon::parseScannerOutput(
R"xxx(SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="public"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",snmp_version="v3",secLevel="authPriv",secName="private",authPassword="azertyui",privPassword="qsdfghjk",authProtocol="MD5",privProtocol="DES"
XML:driver="netxml-ups",port="http://10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",snmp_version="v3",secLevel="noAuthNoPriv",secName="public"
R"xxx(SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="private"
)xxx");
        const static std::vector<size_t> expectedUpsResult = { 2, 1, 3, 4, 0 } ;
        const static std::vector<size_t> expectedEpduResult = { 1, 3, 4, 0, 2 } ;

        fty_proto_t* upsAsset = fty_proto_new(FTY_PROTO_ASSET);
        fty_proto_set_type(upsAsset, "ups");
        fty_proto_t* epduAsset = fty_proto_new(FTY_PROTO_ASSET);
        fty_proto_set_type(epduAsset, "epdu");

        const auto upsResult = fty::nut::sortDeviceConfigurationPreferred(upsAsset, configurations);
        const auto epduResult = fty::nut::sortDeviceConfigurationPreferred(epduAsset, configurations);
        assert(upsResult == expectedUpsResult);
        assert(epduResult == expectedEpduResult);

        fty_proto_destroy(&epduAsset);
        fty_proto_destroy(&upsAsset);
        std::cerr << "OK" << std::endl;
    }

    {
        std::cerr << "  - matchDeviceConfigurationToBestDeviceConfigurationType: ";

        fty_proto_t* asset = fty_proto_new(FTY_PROTO_ASSET);
        fty_proto_ext_insert(asset, "ipv4.1", "10.130.33.140");

        const auto configurations = nutcommon::parseScannerOutput(
R"xxx(SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="public"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",snmp_version="v3",secLevel="authPriv",secName="private",authPassword="azertyui",privPassword="qsdfghjk",authProtocol="MD5",privProtocol="DES"
XML:driver="netxml-ups",port="http://10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",snmp_version="v3",secLevel="noAuthNoPriv",secName="public"
R"xxx(SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="private"
)xxx");
        const fty::nut::DeviceConfigurationInfoDetails configurationTypes = {
            fty::nut::DeviceConfigurationInfoDetail({
                0,
                "NetXML protocol",
                {
                    { "driver", "netxml-ups" },
                    { "port", "http://${asset.ext.ipv4.1}" },
                },
                {},
            }),
            fty::nut::DeviceConfigurationInfoDetail({
                1,
                "SNMPv1 protocol",
                {
                    { "driver", "snmp-ups" },
                    { "port", "${asset.ext.ipv4.1}" },
                },
                {
                    "Snmpv1"
                },
            }),
            fty::nut::DeviceConfigurationInfoDetail({
                2,
                "SNMPv3 protocol",
                {
                    { "driver", "snmp-ups" },
                    { "port", "${asset.ext.ipv4.1}" },
                    { "snmp_version", "v3" },
                },
                {
                    "Snmpv3"
                },
            }),
        } ;

        for (const auto& testCases : std::vector<std::pair<const nutcommon::DeviceConfiguration&, size_t>>{
            { configurations[0], 1 },
            { configurations[1], 2 },
            { configurations[2], 0 },
            { configurations[3], 2 },
            { configurations[4], 1 },
        }) {
            auto it = fty::nut::matchDeviceConfigurationToBestDeviceConfigurationType(asset, testCases.first, configurationTypes);
            assert(it->id == testCases.second);
        }

        fty_proto_destroy(&asset);
        std::cerr << "OK" << std::endl;
    }
}
