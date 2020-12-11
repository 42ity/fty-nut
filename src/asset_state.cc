/*  =========================================================================
    asset_state - list of known assets

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
    asset_state - list of known assets
@discuss
@end
*/

#include "asset_state.h"
#include <fty_common_mlm.h>
#include <fty_log.h>

#include <iterator>
#include <cmath>

AssetState::Asset::Asset(fty_proto_t* message)
{
    name_ = fty_proto_name(message);
    IP_ = fty_proto_ext_string(message, "ip.1", "");
    port_ = fty_proto_ext_string(message, "port", "");
    subtype_ = fty_proto_aux_string(message, "subtype", "");
    location_ = fty_proto_aux_string(message, "parent_name.1", "");
    const char *block = fty_proto_ext_string(message, "upsconf_block", NULL);
    if (block) {
        upsconf_block_ = block;
        have_upsconf_block_ = true;
    } else {
        have_upsconf_block_ = false;
    }
    const char *dmf = fty_proto_ext_string(message, "upsconf_enable_dmf", "");
    upsconf_enable_dmf_ = strcmp(dmf, "true") == 0;
    max_current_ = NAN;
    try {
        max_current_ = std::stod(fty_proto_ext_string(message,
                    "max_current", ""));
    } catch (...) { }
    max_power_ = NAN;
    try {
        max_power_ = std::stod(fty_proto_ext_string(message, "max_power", ""));
    } catch (...) { }
    daisychain_ = 0;
    try {
        daisychain_ = std::stoi(fty_proto_ext_string(message,
                    "daisy_chain", ""));
    } catch (...) { }
    zhash_t* ext = fty_proto_get_ext(message);
    for (auto val = reinterpret_cast<char* const>(zhash_first(ext)); val; val = reinterpret_cast<char* const>(zhash_next(ext))) {
        if (strncmp(zhash_cursor(ext), "endpoint.1.", 11) == 0) {
            endpoint_.emplace(zhash_cursor(ext)+11, val);
        }
    }
    zhash_destroy(&ext);
    proto_ = fty_proto_dup(message);
}

bool AssetState::handleAssetMessage(fty_proto_t* message)
{
    std::string name(fty_proto_name(message));
    std::string operation(fty_proto_operation(message));
    if (operation == FTY_PROTO_ASSET_OP_DELETE ||
        operation == FTY_PROTO_ASSET_OP_RETIRE ||
        !streq(fty_proto_aux_string (message, FTY_PROTO_ASSET_STATUS, "active"), "active")) {
        return (powerdevices_.erase(name) > 0 || sensors_.erase(name) > 0);
    }

    std::string type(fty_proto_aux_string (message, "type", ""));
    if (type != "device")
        return false;
    std::string subtype(fty_proto_aux_string (message, "subtype", ""));
    AssetMap* map;
    if (subtype == "epdu" || subtype == "ups" || subtype == "sts")
        map = &powerdevices_;
    else if (subtype == "sensor") {
        // skip sensors connected to rackcontrollers
        if (streq (fty_proto_aux_string(message, "parent_name.1", ""), "rackcontroller-0"))
            return false;
        map = &sensors_;
    }
    else if (subtype == "sensorgpio") {
        // skip gpi sensors connected to rackcontrollers
        if (streq (fty_proto_aux_string(message, "parent_name.1", ""), "rackcontroller-0"))
            return false;
        if (streq (fty_proto_aux_string(message, "parent_name.2", ""), "rackcontroller-0"))
            return false;
        map = &sensors_;
    }
    else
        return false;
    if (operation != FTY_PROTO_ASSET_OP_CREATE &&
            operation != FTY_PROTO_ASSET_OP_UPDATE) {
        log_error("unknown asset operation '%s'. Skipping.",
                operation.c_str());
        return false;
    }
    (*map)[name] = std::shared_ptr<Asset>(new Asset(message));
    return true;
}

// Destroys passed message
bool AssetState::handleLicensingMessage(fty_proto_t* message)
{
    assert (fty_proto_id(message) == FTY_PROTO_METRIC);
    if (streq (fty_proto_name(message), "rackcontroller-0") && streq (fty_proto_type(message), "monitoring.global")) {
        try {
            license_limit_ = std::stoi(fty_proto_value(message));
            return true;
        } catch (...) { }
    }
    return false;
}

bool AssetState::updateFromProto(fty_proto_t* message)
{
    // proto messages are always assumed to be asset updates
    if (fty_proto_id (message) == FTY_PROTO_ASSET) {
        return handleAssetMessage(message);
    } else if (fty_proto_id (message) == FTY_PROTO_METRIC) {
        return handleLicensingMessage(message);
    }
    return false;
}

bool AssetState::updateFromMsg(zmsg_t* message)
{
    bool ret = false;
    if (is_fty_proto(message)) {
        fty_proto_t *proto = fty_proto_decode (&message);
        if (!proto) {
            zmsg_destroy(&message);
            return false;
        }
        ret = updateFromProto (proto);
        fty_proto_destroy(&proto);
    }
    return ret;
}

void AssetState::recompute()
{
    ip2master_.clear();
    for (auto i : powerdevices_) {
        const std::string& ip = i.second->IP();
        if (ip == "") {
            // this is strange. No IP?
            continue;
        }
        if (i.second->daisychain() <= 1) {
            // this is master
            ip2master_[ip] = i.first;
        }
    }

    // Check if we can monitor
    allowed_powerdevices_.clear();

    if(m_allowMonitoring)
    {
        AssetMap::const_iterator end = powerdevices_.end();
        allowed_powerdevices_ = AssetMap(powerdevices_.cbegin(), end);

        log_info("Monitoring enable, %i devices will be monitored", allowed_powerdevices_.size());
    }
    else
    {
        log_info("Monitoring disabled by licensing");
    }
}

const std::string& AssetState::ip2master(const std::string& ip) const
{
    static const std::string empty;

    const auto i = ip2master_.find(ip);
    if (i == ip2master_.cend())
        return empty;
    return i->second;
}
