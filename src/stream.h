/*  =========================================================================
    stream - stream deliver command

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

#ifndef STREAM_H_INCLUDED
#define STREAM_H_INCLUDED

#include <malamute.h>

#include "nut_agent.h"
#include "nut.h"

#ifdef __cplusplus
extern "C" {
#endif

//  @interface
//  Handle stream deliver command
void stream_deliver_handle (
            mlm_client_t *client,
            NUTAgent& nut_agent,
            nut_t *data,
            zmsg_t **message_p);

//  Self test of this class
void stream_test (bool verbose);

//  @end

#ifdef __cplusplus
}
#endif

#endif
