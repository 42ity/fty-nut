/*  =========================================================================
    actor_commands - actor commands

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

#include "actor_commands.h"
#include "nut_agent.h"
#include "nut_mlm.h"
#include <fty_common_mlm.h>
#include <fty_log.h>

//returns 1 if $TERM, else 0
int actor_commands(zmsg_t** message_p, uint64_t& timeout, NUTAgent& nut_agent)
{
    assert(message_p && *message_p);
    zmsg_t* message = *message_p;

    char* cmd = zmsg_popstr(message);
    log_debug("XXXXXXXXXXXXXXXXXX actor command = '%s'", cmd);

    int ret = 0;

    if (!cmd) {
        log_error(
            "Given `which == pipe` function `zmsg_popstr (msg)` returned NULL. "
            "Message received is most probably empty (has no frames).");
    }
    else if (streq(cmd, "$TERM")) {
        log_info("Got $TERM");
        ret = 1;
    }
    else if (streq(cmd, ACTION_CONFIGURE)) {
        char* mapping = zmsg_popstr(message);
        if (!mapping) {
            log_error(
                "Expected multipart string format: CONFIGURE/mapping_file. "
                "Received CONFIGURE/nullptr");
        }
        else {
            bool rv = nut_agent.loadMapping(mapping);
            if (rv == false) {
                log_error("NUTAgent::loadMapping (mapping = '%s') failed", mapping);
            }
        }
        zstr_free(&mapping);
    }
    else if (streq(cmd, ACTION_POLLING)) {
        char* polling = zmsg_popstr(message);
        if (!polling) {
            log_error(
                "Expected multipart string format: POLLING/value. "
                "Received POLLING/nullptr");
        }
        else {
            char* end;
            timeout = std::strtoul(polling, &end, 10) * 1000;
            if (timeout == 0) {
                log_error("invalid POLLING value '%s', using default instead", polling);
                timeout = 30000;
            }
            nut_agent.TTL(int(timeout * 2 / 1000));
        }
        zstr_free(&polling);
    }
    else {
        log_warning("Command '%s' is unknown or not implemented", cmd);
    }

    zstr_free(&cmd);
    zmsg_destroy(message_p);

    return ret;
}
