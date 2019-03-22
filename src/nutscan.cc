/*  =========================================================================
    nutscan - Wrapper around nut-scanner tool

    Copyright (C) 2014 - 2017 Eaton

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
    nutscan - Wrapper around nut-scanner tool
@discuss
@end
*/

#include <fty_common_mlm_subprocess.h>
#include "nutscan.h"
#include <fty_log.h>

#include <sstream>
#include <string>
#include <vector>

/**
 * \brief Parse the output of nut-scanner
 *
 * \param name - name of device in the result
 * \param inp  - input stream
 * \param out  - vector of string with output
 */
static
void
s_parse_nut_scanner_output(
        const std::string& name,
        int timeout,
        std::istream& inp,
        std::vector<std::string>& out)
{
    std::stringstream buf;
    bool got_name = false;

    std::string line;
    while (std::getline(inp, line)) {
        if (line.size() == 0 || line[0] == '\n')
            continue;

        if (line[0] == '[') {
            // New snippet begins, flush old data to out (if any)
            if (buf.tellp() > 0) {
                out.push_back(buf.str());
                buf.clear();
                buf.str("");
            }
            // Do not flush the name into buf here just yet -
            // do so if we have nontrivial config later on
            got_name = true;
        }
        else {
            if (got_name) {
                buf << '[' << name << ']' << std::endl;
                got_name = false;
            }
            if (buf.tellp() > 0)
                buf << line;
        }
    }

    if (got_name && buf.tellp() == 0)
        log_error ("While parsing nut-scanner output for '%s', got a section tag but no other data", name.c_str());

    if (buf.tellp() > 0) {
        out.push_back(buf.str());
        buf.clear();
        buf.str("");
    }
}

/**
 * \brief run nut-scanner binary and return the output
 */
static
int
s_run_nut_scanner(
    const std::string& name,
    const MlmSubprocess::Argv& args,
    int timeout,
    std::vector<std::string>& result)
{
    std::string strOut;
    std::string strErr;

    int ret = MlmSubprocess::output(args, strOut, strErr, timeout);

    if (ret == 0) {
        log_info("Execution of nut-scanner SUCCEEDED with code %d.", ret);
        std::istringstream inp{strOut};
        s_parse_nut_scanner_output(name, inp, out);
    }
    else {
        log_error("Execution of nut-scanner FAILED with code %d.", ret);
    }

    if (!strOut.empty()) {
        log_debug("Standard output:");
        log_debug("%s", strOut.c_str());
    }
    if (!strErr.empty()) {
        log_debug("Standard error:");
        log_debug("%s", strErr.c_str());
    }

    return ret == 0 ? 0 : -1;
}

std::vector<SNMPv3Credentials> fetch_snmpv3_credentials()
{
    std::vector<SNMPv3Credentials> creds;

    zconfig_t *config = zconfig_load(FTY_DEFAULT_CFG_FILE);
    if (config) {
        zconfig_t *item = zconfig_locate(config, "snmpv3");
        if (item) {
            zconfig_t *child = zconfig_child(item);
            while (child) {
                const char *userName = zconfig_get(child, "username", nullptr);
                const char *authPassword = zconfig_get(child, "authPassword", "");
                const char *authProtocol = zconfig_get(child, "authProtocol", "");
                const char *privPassword = zconfig_get(child, "privPassword", "");
                const char *privProtocol = zconfig_get(child, "privProtocol", "");

                if (userName) {
                    creds.emplace_back(userName, authPassword, authProtocol, privPassword, privProtocol);
                }

                child = zconfig_next(child);
            }
        }
    }
    else {
        log_warning("Config file '%s' could not be read.", FTY_DEFAULT_CFG_FILE);
    }
    zconfig_destroy(&config);

    return creds;
}

std::vector<SNMPv1Credentials> fetch_snmpv1_credentials()
{
    std::vector<SNMPv1Credentials> creds;

    zconfig_t *config = zconfig_load(FTY_DEFAULT_CFG_FILE);
    if (config) {
        item = zconfig_locate (config, "snmp/community");
        if (item) {
            zconfig_t *child = zconfig_child(item);
            while (child) {
                const char *community = zconfig_value (child);
                creds.emplace_back(community);
            }
        }
    }
    else {
        log_warning("Config file '%s' could not be read.", FTY_DEFAULT_CFG_FILE);
    }
    zconfig_destroy(&config);

    // Fallback.
    if (creds.empty()) {
        creds.emplace_back("public");
    }

    return creds;
}

int
nut_scan_snmpv3(
        const std::string& name,
        const CIDRAddress& ip_address_start,
        const CIDRAddress& ip_address_end,
        const SNMPv3Credentials &credentials,
        bool use_dmf,
        int timeout,
        std::vector<std::string>& out)
{
    if (::getenv ("BIOS_NUT_USE_DMF")) {
        use_dmf = true;
    }

    MlmSubprocess::Argv args = { "nut-scanner", "--quiet", "--start_ip", ip_address_start.toString(), "--end_ip", ip_address_end.toString(), "--secName", credentials.username };

    /**
     * There are three possible cases:
     *  - noAuthNoPriv (nothing provided)
     *  - authNoPriv (authentification password provided)
     *  - authPriv (authentification and privacy password provided)
     * Both authentification and privacy passwords may optionally provide a specific protocol to use.
     */
    if (!credentials.authPassword.empty()) {
        args.emplace_back("--authPassword");
        args.emplace_back(credentials.authPassword);
        if (!credentials.authProtocol.empty()) {
            args.emplace_back("--authProtocol");
            args.emplace_back(credentials.authProtocol);
        }

        if (!credentials.privPassword.empty()) {
            args.emplace_back("--privPassword");
            args.emplace_back(credentials.privPassword);
            if (!credentials.privProtocol.empty()) {
                args.emplace_back("--privProtocol");
                args.emplace_back(credentials.privProtocol);
            }

            args.emplace_back("--secLevel");
            args.emplace_back("authPriv");
        }
        else {
            args.emplace_back("--secLevel");
            args.emplace_back("authNoPriv");
        }
    }
    else {
        args.emplace_back("--secLevel");
        args.emplace_back("noAuthNoPriv");
    }

    args.emplace_back(use_dmf ? "-z" : "--snmp_scan");

    log_info("nut-scanning SNMPv3 devices from %s to %s username '%s' using %s mode, timeout %d.",
        ip_address_start.toString().c_str(),
        ip_address_end.toString().c_str(),
        credentials.username.c_str(),
        use_dmf ? "DMF", "legacy",
        timeout
    );

    return s_run_nut_scanner(name, args, timeout, out);
}

int
nut_scan_snmpv1(
        const std::string& name,
        const CIDRAddress& ip_address_start,
        const CIDRAddress& ip_address_end,
        const SNMPv1Credentials &credentials,
        bool use_dmf,
        int timeout,
        std::vector<std::string>& out)
{
    if (::getenv ("BIOS_NUT_USE_DMF")) {
        use_dmf = true;
    }

    MlmSubprocess::Argv args = { "nut-scanner", "--quiet", "--start_ip", ip_address_start.toString(), "--end_ip", ip_address_end.toString(), "--community", credentials.community };

    args.emplace_back(use_dmf ? "-z" : "--snmp_scan");

    log_info("nut-scanning SNMPv1 devices from %s to %s community '%s' using %s mode, timeout %d.",
        ip_address_start.toString().c_str(),
        ip_address_end.toString().c_str(),
        credentials.community.c_str(),
        use_dmf ? "DMF", "legacy",
        timeout
    );

    return s_run_nut_scanner(name, args, timeout, out);
}

int
nut_scan_xml_http(
        const std::string& name,
        const CIDRAddress& ip_address_start,
        const CIDRAddress& ip_address_end,
        int timeout,
        std::vector<std::string>& out)
{
    MlmSubprocess::Argv args = { "nut-scanner", "--quiet", "--start_ip", ip_address_start.toString(), "--end_ip", ip_address_end.toString(), "--xml_scan" };
    
    log_info("nut-scanning NetXML devices from %s to %s, timeout %d.",
        ip_address_start.toString().c_str(),
        ip_address_end.toString().c_str(),
        timeout
    );

    return s_run_nut_scanner(name, args, timeout, out);
}
