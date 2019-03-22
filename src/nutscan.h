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

#ifndef NUTSCAN_H_INCLUDED
#define NUTSCAN_H_INCLUDED

#include "cidr.h"

struct SNMPv1Credentials
{
        SNMPv1Credentials(const std::string& comm) : community(comm) {}

        std::string community;
};

struct SNMPv3Credentials
{
        SNMPv3Credentials(const std::string& user,
                const std::string& authPass, const std::string& authProto,
                const std::string& privPass, const std::string& privProto
        ) : username(user),
                authPassword(authPass), authProtocol(authProto),
                privPassword(privPass), authProtocol(privProto) {}

        std::string username;
        std::string authPassword;
        std::string authProtocol;
        std::string privPassword;
        std::string privProtocol;
};

std::vector<SNMPv3Credentials> fetch_snmpv3_credentials();
std::vector<SNMPv1Credentials> fetch_snmpv1_credentials();

int
nut_scan_snmpv3(
        const std::string& name,
        const CIDRAddress& ip_address_start,
        const CIDRAddress& ip_address_end,
        const SNMPv3Credentials &credentials,
        bool use_dmf,
        int timeout,
        std::vector<std::string>& out);

int
nut_scan_snmpv1(
        const std::string& name,
        const CIDRAddress& ip_address_start,
        const CIDRAddress& ip_address_end,
        const SNMPv1Credentials &credentials,
        bool use_dmf,
        int timeout,
        std::vector<std::string>& out);

int
nut_scan_xml_http(
        const std::string& name,
        const CIDRAddress& ip_address_start,
        const CIDRAddress& ip_address_end,
        int timeout,
        std::vector<std::string>& out);

#endif
