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

#include "fty_nut_configuration_helper.h"

#include "fty_nut_library.h"
#include "fty_nut_classes.h"
#include <fty_common_nut_credentials.h>

#include <forward_list>
#include <regex>
#include <future>
#include <fty_common_nut_credentials.h>

namespace fty
{
namespace nut
{

constexpr int SCAN_TIMEOUT = 5;

//
// Private functions.
//

auto isConfSnmp =   [](const fty::nut::DeviceConfiguration& conf) -> bool { return conf.at("driver").find_first_of("snmp-ups") == 0; };
auto confSnmpVersion = [](const fty::nut::DeviceConfiguration& conf) -> int {
    if (!isConfSnmp(conf)) { return -1; }
    auto snmp_version = conf.find("snmp_version");
    if (snmp_version == conf.end() || snmp_version->second == "v1") { return 1; }
    else if (snmp_version->second == "v2c") { return 2; }
    else if (snmp_version->second == "v3") { return 3; }
    else { return 0; }
};
auto confSnmpMib =  [](const fty::nut::DeviceConfiguration& conf) -> std::string {
    return conf.count("mibs") > 0 ? conf.at("mibs") : "auto";
};
auto confSnmpSec =  [](const fty::nut::DeviceConfiguration& conf) -> std::string {
    return conf.count("secLevel") > 0 ? conf.at("secLevel") : "noAuthNoPriv";
};
auto confSnmpCom =  [](const fty::nut::DeviceConfiguration& conf) -> std::string {
    return conf.count("community") > 0 ? conf.at("community") : "public";
};

/**
 * \brief Helper to scan for a dummy-ups in repeater mode.
 * \param port Port to scan.
 * \return NUT driver configurations found.
 */
static fty::nut::DeviceConfigurations testDummyDriverDevice(const std::string &port)
{
    constexpr unsigned loop_nb = 1;
    constexpr unsigned loop_iter_time_sec = 10;

    fty::nut::DeviceConfigurations result;

    fty::nut::KeyValues values = fty::nut::dumpDevice("dummy-ups", port, loop_nb, loop_iter_time_sec);
    if (!values.empty()) {
        result.emplace_back(fty::nut::DeviceConfiguration {
            std::make_pair<>("driver", values["driver.name"]),
            std::make_pair<>("port", values["driver.parameter.port"]),
            std::make_pair<>("desc", "dummy-ups in repeater mode"),
            std::make_pair<>("synchronous", "yes"),
        });
    }

    return result;
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
static fty::nut::DeviceConfiguration extractConfigurationFingerprint(const fty::nut::DeviceConfiguration& configuration)
{
    fty::nut::DeviceConfiguration result;

    const static std::map<std::string, std::set<std::string>> fingerprintTemplates = {
        { "snmp-ups", {
            "driver", "port", "mibs", "snmp_version", "community", "secLevel", "secName", "authPassword", "authProtocol", "privPassword", "privProtocol"
        }},
        { "snmp-ups-dmf", {
            "driver", "port", "mibs", "snmp_version", "community", "secLevel", "secName", "authPassword", "authProtocol", "privPassword", "privProtocol"
        }},
        { "netxml-ups", {
            "driver", "port"
        }},
        { "dummy-snmp", {
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
static fty::nut::DeviceConfiguration instanciateDeviceConfigurationFromTemplate(fty_proto_t* asset, const fty::nut::DeviceConfiguration& confTemplate)
{
    fty::nut::DeviceConfiguration result;

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

// FIXME: To put in fty-commun-nut ??
static fty::nut::DeviceConfiguration ftyToNutDeviceConfiguration(const DBAssetsDiscovery::DeviceConfigurationInfo& conf, const fty::nut::SecwMap& credentials)
{
    fty::nut::DeviceConfiguration results = conf.attributes;

    // FIXME: improve security document instanciation.
    for (const auto& secw_document_id : conf.secwDocumentIdList) {
        auto itCred = credentials.find(secw_document_id);

        if (itCred != credentials.end()) {
            auto cred = fty::nut::convertSecwDocumentToKeyValues(itCred->second, conf.attributes.at("driver"));
            results.insert(cred.begin(), cred.end());
        }
    }

    return results;
}


//
// Public functions.
//

fty::nut::DeviceConfigurations assetScanDrivers(messagebus::PoolWorker& pool, fty_proto_t *asset, const fty::nut::SecwMap& credentials, const bool scanDummyUps)
{
    fty::nut::DeviceConfigurations results;

    const auto addresses = getNetworkAddressesFromAsset(asset);

    std::forward_list<std::future<fty::nut::DeviceConfigurations>> futureResults;

    // Launch a bunch of scans in parallel.
    for (const auto& address : addresses) {
        for (const auto& credential : credentials) {
            secw::Snmpv1Ptr snmpv1 = secw::Snmpv1::tryToCast(credential.second);
            secw::Snmpv3Ptr snmpv3 = secw::Snmpv3::tryToCast(credential.second);

            if (snmpv1 || snmpv3) {
                std::vector<secw::DocumentPtr> doc { credential.second };
                futureResults.emplace_front(pool.schedule(fty::nut::scanDevice, fty::nut::SCAN_PROTOCOL_SNMP, address, SCAN_TIMEOUT, doc));
                futureResults.emplace_front(pool.schedule(fty::nut::scanDevice, fty::nut::SCAN_PROTOCOL_SNMP_DMF, address, SCAN_TIMEOUT, doc));
            }
        }
        std::vector<secw::DocumentPtr> doc;
        futureResults.emplace_front(pool.schedule(fty::nut::scanDevice, fty::nut::SCAN_PROTOCOL_NETXML, address, SCAN_TIMEOUT, doc));

        // Scan dummy ups only if option is enabled in config
        if (scanDummyUps) {
            std::string name = fty_proto_ext_string(asset, "name", "");
            // FIXME: For multi node: asset.ext.contact_name@asset.ext.contact_email
            //std::string contact_name = fty_proto_ext_string(asset, "contact_name", "");
            //std::string contact_email = fty_proto_ext_string(asset, "contact_email", "");
            //const std::string param = contact_name + "@" + contact_email;
            const std::string param = name + "@" + address;   /* "asset.ext.name@asset.ext.ip.1" (e.g MBT.G3MI.3PH.dummy-snmp@bios-nut-proxy.mbt.lab.etn.com */
            futureResults.emplace_front(pool.schedule(testDummyDriverDevice, param));
        }
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

ComputeAssetConfigurationUpdateResult computeAssetConfigurationUpdate(const fty::nut::DeviceConfigurations& knownConfigurations, const fty::nut::DeviceConfigurations& detectedConfigurations)
{
    ComputeAssetConfigurationUpdateResult result;

    // Fingerprints of everything we detected.
    std::set<fty::nut::DeviceConfiguration> detectedFingerprints;
    std::transform(
        detectedConfigurations.begin(),
        detectedConfigurations.end(),
        std::inserter(detectedFingerprints, detectedFingerprints.begin()),
        extractConfigurationFingerprint
    );

    // Fingerprints we matched in the database.
    std::set<fty::nut::DeviceConfiguration> matchedFingerprints;

    for (const auto& knownConfiguration : knownConfigurations) {
        if (canDeviceConfigurationWorkingStateBeAssessed(knownConfiguration)) {
            // This is a known NUT driver, classify it as working or non-working.
            // TODO: Don't work ???
            //const auto& detectedFingerprintIterator = detectedFingerprints.find(extractConfigurationFingerprint(knownConfiguration));
            fty::nut::DeviceConfiguration conf = extractConfigurationFingerprint(knownConfiguration);
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
    std::set<fty::nut::DeviceConfiguration> unmatchedFingerprints;
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

std::vector<size_t> sortDeviceConfigurationPreferred(fty_proto_t* asset, const fty::nut::DeviceConfigurations& configurations, const bool prioritizeDmfDriver)
{
    // Initialize vector of indexes.
    std::vector<size_t> indexes(configurations.size());
    std::iota(indexes.begin(), indexes.end(), 0);

    std::sort(indexes.begin(), indexes.end(), [&configurations, &asset, prioritizeDmfDriver](size_t a, size_t b) {
        /**
         * This is a fairly complicated sort function. Here, we try to return
         * true if confA is better than confB.
         *
         * This to keep in mind:
         * - std::sort expects a total order.
         * - Total sort means if we return true for a condition, we must return false in the "mirror" condition.
         */

        const std::string type = fty_proto_aux_string(asset, "subtype", "");
        const auto& confA = configurations[a];
        const auto& confB = configurations[b];

        const static std::array<std::string, 4> upsDriverPriority = { "dummy-ups", "netxml-ups",
            prioritizeDmfDriver ? "snmp-ups-dmf" : "snmp-ups" , prioritizeDmfDriver ? "snmp-ups" : "snmp-ups-dmf" };
        const static std::array<std::string, 4> epduDriverPriority = { "dummy-ups",
            prioritizeDmfDriver ? "snmp-ups-dmf" : "snmp-ups", prioritizeDmfDriver ? "snmp-ups" : "snmp-ups-dmf", "netxml-ups" };
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
                return isBefore(snmpMibPriority.begin(), snmpMibPriority.end(), confA_SNMP_mib, confB_SNMP_mib) ||
                    (confA_SNMP_mib != "ietf" && confB_SNMP_mib == "ietf");
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

DBAssetsDiscovery::DeviceConfigurationInfoDetails::const_iterator matchDeviceConfigurationToBestDeviceConfigurationType(fty_proto_t* asset, const fty::nut::DeviceConfiguration& configuration, const DBAssetsDiscovery::DeviceConfigurationInfoDetails& types)
{
    auto bestMatch = types.end();

    for (auto itType = types.begin(); itType != types.end(); itType++) {
        // Instanciate device configuration type to have something to compare to.
        fty::nut::DeviceConfiguration instanciatedType = instanciateDeviceConfigurationFromTemplate(asset, itType->defaultAttributes);
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

fty::nut::DeviceConfigurations instanciateDatabaseConfigurations(const DBAssetsDiscovery::DeviceConfigurationInfos& dbConfs, fty_proto_t* asset, const fty::nut::SecwMap& credentials)
{
    fty::nut::DeviceConfigurations result;
    std::transform(
        dbConfs.begin(),
        dbConfs.end(),
        std::back_inserter(result),
        [&asset, &credentials](const DBAssetsDiscovery::DeviceConfigurationInfos::value_type& val) -> fty::nut::DeviceConfiguration {
            return instanciateDeviceConfigurationFromTemplate(asset,
                ftyToNutDeviceConfiguration(val, credentials)
            );
        }
    );
    return result;
}

std::set<secw::Id> matchSecurityDocumentIDsFromDeviceConfiguration(const fty::nut::DeviceConfiguration& conf, const fty::nut::SecwMap& credentials)
{
    std::set<secw::Id> ids;

    for (const auto& credential : credentials) {
        auto instanciatedCredential = fty::nut::convertSecwDocumentToKeyValues(credential.second, conf.at("driver"));
        if (!instanciatedCredential.empty() && std::includes(conf.begin(), conf.end(), instanciatedCredential.begin(), instanciatedCredential.end())) {
            ids.emplace(credential.first);
        }
    }

    return ids;
}

#if 0
fty_proto_t* fetchProtoFromAssetName(const std::string& assetName)
{
    fty_proto_t *ftyProto = nullptr;
    try {
        std::string publisherName("fty-nut-configuration-server-publisher");
        std::unique_ptr<messagebus::MessageBus> publisher(messagebus::MlmMessageBus(MLM_ENDPOINT, publisherName.c_str()));
        publisher->connect();
        // Get asset details
        messagebus::Message message;
        std::string uuid = messagebus::generateUuid();
        message.userData().push_back("GET");
        message.userData().push_back(uuid.c_str());
        message.userData().push_back(assetName.c_str());

        message.metaData().clear();
        message.metaData().emplace(messagebus::Message::RAW, "");
        message.metaData().emplace(messagebus::Message::CORRELATION_ID, uuid);
        message.metaData().emplace(messagebus::Message::SUBJECT, "ASSET_DETAIL");
        message.metaData().emplace(messagebus::Message::FROM, publisherName);
        message.metaData().emplace(messagebus::Message::TO, "asset-agent");
        message.metaData().emplace(messagebus::Message::REPLY_TO, publisherName);
        log_info("fetchProtoFromAssetName: Get asset details for %s", assetName.c_str());
        messagebus::Message response = publisher->request("asset-agent", message, 10); /* timeout 10 sec */
        if (!response.userData().empty()) {
            std::string uuid_read = response.userData().front();
            if (uuid_read.empty()) {
                log_error("fetchProtoFromAssetName: uuid empty");
                return nullptr;
            }
            // Check uuid matching
            if (uuid_read.compare(uuid) != 0) {
                log_error("UUID doesn't match (%s, %s, %s)", assetName.c_str(), uuid.c_str(), uuid_read.c_str());
                return nullptr;
            }
            response.userData().pop_front();
        }
        else {
            log_error("fetchProtoFromAssetName: no uuid defined");
            return nullptr;
        }
        // Extract data in message
        std::string data;
        if (!response.userData().empty()) {
            data = response.userData().front();
        }
        else {
            log_error("fetchProtoFromAssetName: no data defined");
            return nullptr;
        }
        zmsg_t* zmsg = zmsg_new();
        zmsg_addmem(zmsg, data.c_str(), data.length());
        if (!is_fty_proto(zmsg)) {
            zmsg_destroy(&zmsg);
            log_warning("Response to an ASSET_DETAIL message is not fty_proto");
            return nullptr;
        }
        ftyProto = fty_proto_decode(&zmsg);
    }
    catch (std::exception& e) {
        log_error("Exception while processing message: %s", e.what());
    }
    return ftyProto;
}
#endif

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
            fty::nut::DeviceConfigurations knownConfigurations;
            fty::nut::DeviceConfigurations detectedConfigurations;

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
                fty::nut::parseScannerOutput(
R"xxx(SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="public"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="private"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",snmp_version="v3",secLevel="noAuthNoPriv",secName="public"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",snmp_version="v3",secLevel="authPriv",secName="private",authPassword="azertyui",privPassword="qsdfghjk",authProtocol="MD5",privProtocol="DES"
)xxx"),

                fty::nut::ComputeAssetConfigurationUpdateResult({
                    {},
                    {},
                    fty::nut::parseScannerOutput(
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
                fty::nut::parseScannerOutput(
R"xxx(SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="public"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="private"
SNMP:driver="dummy-ups",port="10.130.33.140"
)xxx"),
                fty::nut::parseScannerOutput(
R"xxx(SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="public"
R"xxx(SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="suprise"
)xxx"),

                fty::nut::ComputeAssetConfigurationUpdateResult({
                    fty::nut::parseScannerOutput(
R"xxx(SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="public"
)xxx"),
                    fty::nut::parseScannerOutput(
R"xxx(SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="private"
)xxx"),
                    fty::nut::parseScannerOutput(
R"xxx(SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="suprise"
)xxx"),
                    fty::nut::parseScannerOutput(
R"xxx(SNMP:driver="dummy-ups",port="10.130.33.140"
)xxx"),
                }),
            }),


            // Test all cases with overlapping fingerprints.
            TestCase({
                fty::nut::parseScannerOutput(
R"xxx(SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="public"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="public",extra="extra"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="public",extra="extra",woohoo="woohoo"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="private",extra="extra"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="private",extra="extra",woohoo="woohoo"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="privateer",extra="extra"
SNMP:driver="dummy-ups",port="10.130.33.140"
)xxx"),
                fty::nut::parseScannerOutput(
R"xxx(SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="public"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="privateer"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="suprise"
)xxx"),

                fty::nut::ComputeAssetConfigurationUpdateResult({
                    fty::nut::parseScannerOutput(
R"xxx(SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="public"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="public",extra="extra"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="public",extra="extra",woohoo="woohoo"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="privateer",extra="extra"
)xxx"),
                    fty::nut::parseScannerOutput(
R"xxx(SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="private",extra="extra"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="private",extra="extra",woohoo="woohoo"
)xxx"),
                    fty::nut::parseScannerOutput(
R"xxx(SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="suprise"
)xxx"),
                    fty::nut::parseScannerOutput(
R"xxx(SNMP:driver="dummy-ups",port="10.130.33.140"
)xxx"),
                }),
            }),
        };

        for (size_t i = 0; i < testCases.size(); i++) {
            std::cerr << "  - computeAssetConfigurationUpdate case #" << (i+1) << ": ";
            const TestCase& testCase = testCases[i];

            auto result = fty::nut::computeAssetConfigurationUpdate(testCase.knownConfigurations, testCase.detectedConfigurations);
            for (const auto& it : std::vector<std::pair<const fty::nut::DeviceConfigurations&, const fty::nut::DeviceConfigurations&>>({
                { result.workingConfigurations,         testCase.expectedResult.workingConfigurations },
                { result.nonWorkingConfigurations,      testCase.expectedResult.nonWorkingConfigurations },
                { result.newConfigurations,             testCase.expectedResult.newConfigurations },
                { result.unknownStateConfigurations,    testCase.expectedResult.unknownStateConfigurations },
            })) {
                const std::set<fty::nut::DeviceConfiguration> resultSorted(it.first.begin(), it.first.end());
                const std::set<fty::nut::DeviceConfiguration> expectedSorted(it.second.begin(), it.second.end());
                assert(resultSorted == expectedSorted);
                std::cerr << resultSorted.size() << " ";
            }

            std::cerr << "OK" << std::endl;
        }
    }

    {
        std::cerr << "  - isDeviceConfigurationSubsetOf: ";

        const auto supersetConfigurations = fty::nut::parseScannerOutput(
R"xxx(SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="public"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="public",extra="extra"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="public",extra="extra",woohoo="woohoo"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="privateer",extra="extra"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",snmp_version="v3",secLevel="noAuthNoPriv",secName="public"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",snmp_version="v3",secLevel="authPriv",secName="private",authPassword="azertyui",privPassword="qsdfghjk",authProtocol="MD5",privProtocol="DES"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="-----------------------------------",mibs="eaton_epdu",snmp_version="v3",secLevel="authPriv",secName="private",authPassword="azertyui",privPassword="qsdfghjk",authProtocol="MD5",privProtocol="DES",extra="extra"
)xxx");
        const auto subsetConfigurations = fty::nut::parseScannerOutput(
R"xxx(SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="public"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",snmp_version="v3",secLevel="noAuthNoPriv",secName="public"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",snmp_version="v3",secLevel="authPriv",secName="private",authPassword="azertyui",privPassword="qsdfghjk",authProtocol="MD5",privProtocol="DES"
)xxx");
        const auto expectedResults = std::vector<std::pair<const fty::nut::DeviceConfiguration&, std::array<bool, 7>>> {
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

        const static auto templateConf = fty::nut::DeviceConfiguration {
            { "driver", "snmp-ups" },
            { "port", "${asset.ext.ipv4.1}" },
            { "port-snmp", "snmp://${asset.ext.ipv4.1}:${asset.ext.snmp_port}/" },
        };
        const static auto expectedResult = fty::nut::DeviceConfiguration {
            { "driver", "snmp-ups" },
            { "port", "10.130.32.117" },
            { "port-snmp", "snmp://10.130.32.117:161/" },
        };
        const auto result = fty::nut::instanciateDeviceConfigurationFromTemplate(asset, templateConf);
        assert(result == expectedResult);

        const static auto expectedFailures = std::vector<fty::nut::DeviceConfiguration> {
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

        const auto configurations = fty::nut::parseScannerOutput(
R"xxx(SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="public"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",snmp_version="v3",secLevel="authPriv",secName="private",authPassword="azertyui",privPassword="qsdfghjk",authProtocol="MD5",privProtocol="DES"
XML:driver="netxml-ups",port="http://10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19"
SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",snmp_version="v3",secLevel="noAuthNoPriv",secName="public"
R"xxx(SNMP:driver="snmp-ups",port="10.130.33.140",desc="EPDU MA 0U (C20 16A 1P)20XC13:4XC19",mibs="eaton_epdu",community="private"
)xxx");
        const static std::vector<size_t> expectedUpsResult = { 2, 1, 3, 4, 0 } ;
        const static std::vector<size_t> expectedEpduResult = { 1, 3, 4, 0, 2 } ;

        fty_proto_t* upsAsset = fty_proto_new(FTY_PROTO_ASSET);
        fty_proto_aux_insert(upsAsset, "type", "device");
        fty_proto_aux_insert(upsAsset, "subtype", "ups");
        fty_proto_t* epduAsset = fty_proto_new(FTY_PROTO_ASSET);
        fty_proto_aux_insert(epduAsset, "type", "device");
        fty_proto_aux_insert(epduAsset, "subtype", "epdu");

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

        const auto configurations = fty::nut::parseScannerOutput(
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
                {},
            }),
            fty::nut::DeviceConfigurationInfoDetail({
                1,
                "SNMPv1 protocol",
                {
                    { "driver", "snmp-ups" },
                    { "port", "${asset.ext.ipv4.1}" },
                },
                {},
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
                {},
                {
                    "Snmpv3"
                },
            }),
        } ;

        for (const auto& testCases : std::vector<std::pair<const fty::nut::DeviceConfiguration&, size_t>>{
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
