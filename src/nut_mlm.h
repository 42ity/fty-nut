/*  =========================================================================
    fty-nut - Malamute connection defaults

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

#ifndef NUT_MLM_H_INCLUDED
#define NUT_MLM_H_INCLUDED

#include <malamute.h>
#include <czmq.h>

// zactor_new takes a void* pointer
#define MLM_ENDPOINT_VOID (const_cast<void *>(static_cast<const void *>(MLM_ENDPOINT)))
#define ACTOR_NUT_NAME "fty-nut"
#define ACTOR_ALERT_NAME "bios-nut-alert"
#define ACTOR_ALERT_MB_NAME ACTOR_ALERT_NAME "-mb"
#define ACTOR_SENSOR_NAME "agent-nut-sensor"
#define ACTOR_CONFIGURATOR_NAME "nut-configurator"
#define ACTOR_CONFIGURATOR_MB_NAME ACTOR_CONFIGURATOR_NAME "-mb"

#define CONFIG_POLLING "nut/polling_interval"
#define ACTION_POLLING "POLLING"
#define ACTION_CONFIGURE "CONFIGURE"

#endif
