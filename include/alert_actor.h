/*  =========================================================================
    alert_actor - actor handling device alerts and thresholds

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

#ifndef ALERT_ACTOR_H_INCLUDED
#define ALERT_ACTOR_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif


#include "malamute.h"

AGENT_NUT_EXPORT int
alert_actor_commands (
    mlm_client_t *client,
    zmsg_t **message,
    bool& verbose,
    uint64_t& timeout
);

AGENT_NUT_EXPORT void
alert_actor (zsock_t *pipe, void *args);

AGENT_NUT_EXPORT void
alert_actor_test (bool verbose);

#ifdef __cplusplus
}
#endif

#endif
