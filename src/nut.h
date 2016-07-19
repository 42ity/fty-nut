/*  =========================================================================
    nut - agent nut structure

    Copyright (C) 2014 - 2015 Eaton

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

#ifndef NUT_H_INCLUDED
#define NUT_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _nut_t nut_t;

//  @interface
//  Create a new nut
AGENT_NUT_EXPORT nut_t *
    nut_new (void);

//  Destroy the nut
AGENT_NUT_EXPORT void
    nut_destroy (nut_t **self_p);

// are there changes to be saved?
AGENT_NUT_EXPORT bool
    nut_changed(nut_t *self);

//  Store bios_proto_t message transfering ownership
AGENT_NUT_EXPORT void
    nut_put (nut_t *self, bios_proto_t **message_p);

//  Get list of asset names
AGENT_NUT_EXPORT zlistx_t *
    nut_get_assets (nut_t *self);

// Returns ip address (well-known extended attribute 'ip.1') of given asset
// or NULL when asset_name does not exist
// or "" (empty string) when given asset does not have ip address specified
AGENT_NUT_EXPORT const char *
    nut_asset_ip (nut_t *self, const char *asset_name);

// Returns daisychain number (well-known extended attribute '...') of give asset
// or NULL when asset_name does not exist
// or "" (empty string) when given
AGENT_NUT_EXPORT const char *
    nut_asset_daisychain (nut_t *self, const char *asset_name);

// return port number of sensor of given asset
// or NULL when asset_name does not exist
// or "" (empty string) when given
AGENT_NUT_EXPORT const char *
    nut_asset_port (nut_ *self, const char *asset_name);

// return location number of sensor of given asset
// or NULL when asset_name does not exist
// or "" (empty string) when given
AGENT_NUT_EXPORT const char *
    nut_asset_location (nut_ *self, const char *asset_name);

//  Save nut to disk
//  If 'fullpath' is NULL does nothing
//  0 - success, -1 - error
AGENT_NUT_EXPORT int
    nut_save (nut_t *self, const char *fullpath);

//  Load nut from disk
//  If 'fullpath' is NULL does nothing
//  0 - success, -1 - error
AGENT_NUT_EXPORT int
    nut_load (nut_t *self, const char *fullpath);

//  Print the nut
AGENT_NUT_EXPORT void
    nut_print (nut_t *self);

//  Self test of this class
AGENT_NUT_EXPORT void
    nut_test (bool verbose);

//  @end

#ifdef __cplusplus
}
#endif

#endif
