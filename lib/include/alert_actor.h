/*  =========================================================================
    alert_actor - actor handling device alerts and thresholds

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

#pragma once

#include <malamute.h>

int  alert_actor_commands(mlm_client_t* client, mlm_client_t* mb_client, zmsg_t** message, uint64_t& timeout);
void alert_actor(zsock_t* pipe, void* args);
void alert_actor_test(bool verbose);
