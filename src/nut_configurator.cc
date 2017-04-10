/*  =========================================================================
    nut_configurator - NUT configurator class

    Copyright (C) 2014 - 2016 Eaton

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
    nut_configurator - NUT configurator class
@discuss
@end
*/

#include "fty_nut_classes.h"

#include <string>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cxxtools/regex.h>
#include <cxxtools/jsondeserializer.h>

using namespace shared;

#define NUT_PART_STORE "/var/lib/fty/fty-nut/devices"

static const char * NUTConfigXMLPattern = "[[:blank:]]driver[[:blank:]]+=[[:blank:]]+\"netxml-ups\"";
/* TODO: This explicitly lists NUT MIB mappings for the static snmp-ups driver,
 * and only for Eaton devices, as it seems...
 * As we integrate DMF support, consider also/instead using mapping names from
 * there, if applicable.
 */
static const char * NUTConfigEpduPattern = "[[:blank:]](mibs[[:blank:]]+=[[:blank:]]+\"(eaton_epdu|aphel_genesisII|aphel_revelation|pulizzi_switched1|pulizzi_switched2)\"|"
                                           "desc[[:blank:]]+=[[:blank:]]+\"[^\"]+ epdu [^\"]+\")";
static const char * NUTConfigCanSnmpPattern = "[[:blank:]]driver[[:blank:]]+=[[:blank:]]+\"snmp-ups(-old|-dmf)?\"";

static const char * NUTConfigATSPattern = "[[:blank:]]mibs[[:blank:]]*=[[:blank:]]*\"[^\"]*ats[^\"]*\"";

std::vector<std::string>::const_iterator NUTConfigurator::stringMatch(const std::vector<std::string> &texts, const char *pattern) {
    log_debug("regex: %s", pattern );
    cxxtools::Regex reg( pattern, REG_EXTENDED | REG_ICASE );
    for( auto it = texts.begin(); it != texts.end(); ++it ) {
        if( reg.match( *it ) ) {
            log_debug("regex: match found");
            return it;
        }
    }
    log_debug("regex: not found");
    return texts.end();
}


bool NUTConfigurator::match( const std::vector<std::string> &texts, const char *pattern) {
    return stringMatch(texts,pattern) != texts.end();
}

bool NUTConfigurator::isEpdu( const std::vector<std::string> &texts) {
    return match( texts, NUTConfigEpduPattern );
}

bool NUTConfigurator::isAts( const std::vector<std::string> &texts) {
    return match( texts, NUTConfigATSPattern );
}

bool NUTConfigurator::isUps( const std::vector<std::string> &texts) {
    return ! (isEpdu(texts) || isAts (texts));
}

bool NUTConfigurator::canSnmp( const std::vector<std::string> &texts) {
    return match( texts, NUTConfigCanSnmpPattern );
}

bool NUTConfigurator::canXml( const std::vector<std::string> &texts) {
    return match( texts, NUTConfigXMLPattern );
}

std::vector<std::string>::const_iterator NUTConfigurator::getBestSnmpMib(const std::vector<std::string> &configs) {
    static const std::vector<std::string> snmpMibPriority = {
        "pw", "mge", ".+"
    };
    for( auto mib = snmpMibPriority.begin(); mib != snmpMibPriority.end(); ++mib ) {
        std::string pattern = ".+[[:blank:]]mibs[[:blank:]]+=[[:blank:]]+\"" + *mib + "\"";
        auto it = stringMatch( configs, pattern.c_str() );
        if( it != configs.end() ) return it;
    }
    return configs.end();
}

std::vector<std::string>::const_iterator NUTConfigurator::selectBest(const std::vector<std::string> &configs) {
    // don't do any complicated decision on empty/single set
    if( configs.size() <= 1 ) return configs.begin();

    log_debug("isEpdu: %i; isUps: %i; isAts: %i; canSnmp: %i; canXml: %i", isEpdu(configs), isUps(configs), isAts(configs), canSnmp(configs), canXml(configs) );
    if( canSnmp( configs ) && ( isEpdu( configs ) || isAts( configs ) ) ) {
        log_debug("SNMP capable EPDU => Use SNMP");
        return getBestSnmpMib( configs );
    } else {
        if( canXml( configs ) ) {
            log_debug("XML capable device => Use XML");
            return stringMatch( configs, NUTConfigXMLPattern );
        } else {
            log_debug("SNMP capable device => Use SNMP");
            return getBestSnmpMib( configs );
        }
    }
}

void NUTConfigurator::systemctl( const std::string &operation, const std::string &service )
{
    std::vector<std::string> _argv = {"sudo", "systemctl", operation, service };
    SubProcess systemd( _argv );
    if( systemd.run() ) {
        int result = systemd.wait();
        log_info("sudo systemctl %s %s result: %i (%s)",
                 operation.c_str(),
                 service.c_str(),
                 result,
                 (result == 0 ? "ok" : "failed"));
    } else {
        log_error("can't run sudo systemctl %s %s command",
                  operation.c_str(),
                  service.c_str() );
    }
}

void NUTConfigurator::updateNUTConfig() {
    // Run the helper script
    std::vector<std::string> _argv = { "sudo", "fty-nutconfig" };
    SubProcess systemd( _argv );
    if( systemd.run() ) {
        int result = systemd.wait();
        log_info("sudo fty-nutconfig %i (%s)",
                 result,
                 (result == 0 ? "ok" : "failed"));
    } else {
        log_error("can't run sudo fty-nutconfig command");
    }
}

std::string NUTConfigurator::makeRule(std::string const &alert, std::string const &bit, std::string const &device, std::string const &description) const {
    return
        "{\n"
        "\"single\" : {\n"
        "    \"rule_name\"     :   \"" + alert + "-" + device + "\",\n"
        "    \"target\"        :   [\"status.ups@" + device + "\"],\n"
        "    \"element\"       :   \"" + device + "\",\n"
        "    \"results\"       :   [ {\"high_critical\"  : { \"action\" : [ \"EMAIL\" ], \"description\" : \""+description+"\" }} ],\n"
        "    \"evaluation\"    : \""
        " function has_bit(x,bit)"
        "     local mask = 2 ^ (bit - 1)"
        "     x = x % (2*mask)"
        "     if x >= mask then return true else return false end"
        " end"
        " function main(status)"
        "     if has_bit(status,"+bit+") then return HIGH_CRITICAL end"
        "     return OK"
        " end"
        "\"\n"
        "  }\n"
        "}";
}

std::vector<std::string> NUTConfigurator::createRules(std::string const &name) {
    std::vector<std::string> result;

    // bits OB - 5 LB - 7 BYPASS - 9

    result.push_back (makeRule ("onbattery","5",name,"UPS is running on battery!"));
    result.push_back (makeRule ("lowbattery","7",name,"Battery depleted!"));
    result.push_back (makeRule ("onbypass","9",name,"UPS is running on bypass!"));
    return result;
}

// compute hash (sha-1) of a file
static char*
s_digest (const char* file)
{
    assert (file);
    zdigest_t *digest = zdigest_new ();

    int fd = open (file, O_NOFOLLOW | O_RDONLY);
    if (fd == -1) {
        log_info ("Cannot open file '%s', digest won't be computed: %s", file, strerror (errno));
        return NULL;
    }
    std::string buffer = read_all (fd);
    close (fd);

    zdigest_update (digest, (byte*) buffer.c_str (), buffer.size ());
    char *ret = strdup (zdigest_string (digest));
    zdigest_destroy (&digest);
    return ret;
}

// compute hash (sha-1) of a std::stringstream
static char*
s_digest (const std::stringstream& s)
{
    zdigest_t *digest = zdigest_new ();
    zdigest_update (digest, (byte*) s.str ().c_str (), s.str ().size ());
    char *ret = strdup (zdigest_string (digest));
    zdigest_destroy (&digest);
    return ret;
}

bool NUTConfigurator::configure( const std::string &name, const AutoConfigurationInfo &info ) {
    log_debug("NUT configurator created");

    switch( info.operation ) {
    case asset_operation::INSERT:
    case asset_operation::UPDATE:
        {
            // get polling interval first
            std::string polling = "30";
            {
                zconfig_t *config = zconfig_load ("/etc/fty-nut/fty-nut.cfg");
                if (config) {
                    polling = zconfig_get (config, "nut/polling_interval", "30");
                    zconfig_destroy (&config);
                }
            }

            std::vector<std::string> configs;

            std::string IP = "127.0.0.1"; // Fake value for local-media devices or dummy-upses, either passed with an upsconf_block
                // TODO: (lib)nutscan supports local media like serial or USB,
                // as well as other remote protocols like IPMI. Use them later.
            auto ubit = info.attributes.find("upsconf_block");
            if( ubit != info.attributes.end() ) {
                // TODO: Refactor to optimize string manipulations
                std::string UBA = ubit->second; // UpsconfBlockAsset - as stored in contents of the asset
                char SEP = UBA.at(0);
                if ( SEP == '\0' || UBA.at(1) == '\0' ) {
                    log_info("device %s is configured with an empty explicit upsconf_block from its asset (adding asset name as NUT device-tag with no config)",
                        name.c_str());
                    configs = { "[" + name + "]\n\n" };
                } else {
                    // First character of the sufficiently long UB string
                    // defines the user-selected line separator character
                    std::string UBN = UBA.substr(1); //UpsconfBlockNut - with EOL chars, without leading SEP character
                    std::replace( UBN.begin(), UBN.end(), SEP, '\n' );
                    if ( UBN.at(0) == '[' ) {
                        log_info("device %s is configured with a complete explicit upsconf_block from its asset: \"%s\" including a custom NUT device-tag",
                            name.c_str(), UBN.c_str());
                        configs = { UBN + "\n" };
                    } else {
                        log_info("device %s is configured with a content-only explicit upsconf_block from its asset: \"%s\" (prepending asset name as NUT device-tag)",
                            name.c_str(), UBN.c_str());
                        configs = { "[" + name + "]\n" + UBN + "\n" };
                    }
                }
            } else {
                auto ipit = info.attributes.find("ip.1");
                if( ipit == info.attributes.end() ) {
                    log_error("device %s has no IP address", name.c_str() );
                    return true;
                }
                IP = ipit->second;

                std::vector <std::string> communities;
                zconfig_t *config = zconfig_load ("/etc/default/bios.cfg");
                if (config) {
                    zconfig_t *item = zconfig_locate (config, "snmp/community");
                    if (item) {
                        bool is_array = false;
                        zconfig_t *child = zconfig_child (item);
                        while (child) {
                            if (!streq (zconfig_value (child), "")) {
                                is_array = true;
                                communities.push_back (zconfig_value (child));
                            }
                            child = zconfig_next (child);
                        }
                        if (!is_array && !streq (zconfig_value (item), ""))
                            communities.push_back (zconfig_value (item));
                    }
                    zconfig_destroy (&config);
                }
                else {
                    log_warning ("Config file '%s' could not be read.", "/etc/default/bios.cfg");
                }
                communities.push_back ("public");

                bool use_dmf = false;
                auto use_dmfit = info.attributes.find ("upsconf_enable_dmf");
                if (use_dmfit != info.attributes.end () && use_dmfit->second == "true")
                    use_dmf = true;

                for (const auto& c : communities) {
                    log_debug("Trying community == %s", c.c_str());
                    if (nut_scan_snmp (name, CIDRAddress (IP), c, use_dmf, configs) == 0 && !configs.empty ()) {
                        break;
                    }
                }
                nut_scan_xml_http (name, CIDRAddress(IP), configs);
            }

            auto it = selectBest( configs );
            if( it == configs.end() ) {
                log_error("nut-scanner failed for device \"%s\" at IP address \"%s\", no suitable configuration found",
                    name.c_str(), IP.c_str() );
                return false; // try again later
            }
            std::string deviceDir = NUT_PART_STORE;
            mkdir_if_needed( deviceDir.c_str() );
            std::stringstream cfg;

            std::string config_name = std::string(NUT_PART_STORE) + path_separator() + name;
            char* digest_old = s_digest (config_name.c_str ());
            cfg << *it;
            {
                std::string s = *it;
                // prototypes expects std::vector <std::string> - lets create fake vector
                // this is not performance critical code anyway
                std::vector <std::string> foo = {s};
                if (isEpdu (foo) && canSnmp (foo)) {
                    log_debug ("add synchronous = yes");
                    cfg << "\tsynchronous = yes\n";
                }
                if (canXml (foo)) {
                    log_debug ("add timeout for XML driver");
                    cfg << "\ttimeout = 15\n";
                }
                log_debug ("add polling for driver");
                if (canSnmp (foo)) {
                    cfg << "\tpollfreq = " << polling << "\n";
                } else {
                    cfg << "\tpollinterval = " << polling << "\n";
                }
            }
            char* digest_new = s_digest (cfg);

            log_debug ("%s: digest_old=%s, digest_new=%s", config_name.c_str (), digest_old ? digest_old : "(null)", digest_new);
            if (!digest_old || !streq (digest_old, digest_new)) {
                std::ofstream cfgFile;
                cfgFile.open (config_name);
                cfgFile << cfg.str ();
                cfgFile.flush ();
                cfgFile.close ();
                log_info("creating new config file %s/%s", NUT_PART_STORE, name.c_str() );
                updateNUTConfig ();
                systemctl ("enable",  std::string("nut-driver@") + name);
                systemctl ("restart", std::string("nut-driver@") + name);
                systemctl ("reload-or-restart", "nut-server");
            }
            zstr_free (&digest_new);
            zstr_free (&digest_old);
            return true;
        }
    case asset_operation::DELETE:
    case asset_operation::RETIRE:
        {
            log_info("removing configuration file %s/%s", NUT_PART_STORE, name.c_str() );
            std::string fileName = std::string(NUT_PART_STORE)
                + path_separator()
                + name;
            remove( fileName.c_str() );
            updateNUTConfig();
            systemctl("stop",    std::string("nut-driver@") + name);
            systemctl("disable", std::string("nut-driver@") + name);
            systemctl("reload-or-restart", "nut-server");
            return true;
        }
    default:
        log_error("invalid configuration operation %" PRIi8, info.operation);
        return true; // true means do not try again this
    }
}

bool Configurator::configure(
    const std::string &name,
    const AutoConfigurationInfo &info )
{
    log_error("don't know how to configure device %s type %" PRIu32 "/%" PRIu32, name.c_str(), info.type, info.subtype );
    return true;
}

std::vector<std::string> Configurator::createRules(std::string const &name) {
    std::vector<std::string> result;
    return result;
}

Configurator * ConfigFactory::getConfigurator( uint32_t type, uint32_t subtype ) {
    if( type == asset_type::DEVICE && ( subtype == asset_subtype::UPS || subtype == asset_subtype::EPDU || subtype == asset_subtype::STS ) ) {
        return new NUTConfigurator();
    }
    // if( type == "server" ) return ServerConfigurator();
    // if( type == "wheelbarrow" ) retrun WheelBarrowConfigurator();
    return new Configurator();
}

bool ConfigFactory::configureAsset( const std::string &name, AutoConfigurationInfo &info) {
    log_debug("configuration attempt device name %s type %" PRIu32 "/%" PRIu32, name.c_str(), info.type, info.subtype );
    Configurator *C = getConfigurator( info.type, info.subtype );
    bool result = C->configure( name, info );
    delete C;
    return result;
}

std::vector<std::string> ConfigFactory::getNewRules( const std::string &name, AutoConfigurationInfo &info) {
    log_debug("rules attempt device name %s type %" PRIu32 "/%" PRIu32, name.c_str(), info.type, info.subtype );
    Configurator *C = getConfigurator( info.type, info.subtype );
    std::vector<std::string> result = C->createRules (name);
    delete C;
    return result;
}


void
nut_configurator_test (bool verbose)
{
}

