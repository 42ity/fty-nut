/*  =========================================================================
    fty_nut_configurator - nut server configurator

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
    fty_nut_configurator - daemon to configure nut-server (upsd) from results of nut scanner
@discuss
@end
*/

#include "fty_nut.h"
#include "../lib/src/nut_mlm.h"

//#include "fty_nut_configurator_server.h"
//#include "nut_mlm.h"

#include <czmq.h>
#include <fty_log.h>
#include <fty_common_mlm.h>

int main (int argc, char *argv [])
{
    bool verbose = false;
    int argn;
    char *log_config = NULL;
    const char *default_log_config = FTY_COMMON_LOGGING_DEFAULT_CFG;
    ManageFtyLog::setInstanceFtylog("fty-nut-configurator");

    for (argn = 1; argn < argc; argn++) {
        if (streq (argv [argn], "--help")
        ||  streq (argv [argn], "-h")) {
            puts ("fty-nut-configurator [options] ...");
            puts ("  --verbose / -v         verbose test output");
            puts ("  --help / -h            this information");
            puts ("  --config / -c          log configuration ");
            return EXIT_SUCCESS;
        }
        else
        if (streq (argv [argn], "--verbose")
        ||  streq (argv [argn], "-v"))
            verbose = true;
        else
        if (streq (argv [argn], "--config")
            ||  streq (argv [argn], "-c")) {
            argn += 1;
            log_config = argv [argn];
        }
        else {
            printf ("Unknown option: %s\n", argv [argn]);
            return EXIT_FAILURE;
        }
    }
    if (log_config == NULL)
        log_config = const_cast<char*>(default_log_config);

    ManageFtyLog::getInstanceFtylog()->setConfigFile(std::string(log_config));
    if (verbose)
        ManageFtyLog::getInstanceFtylog()->setVerboseMode();

    zactor_t *server = zactor_new (fty_nut_configurator_server, MLM_ENDPOINT_VOID);
    if (!server) {
        log_fatal("fty_nut_configurator: failed to create server");
        return EXIT_FAILURE;
    }

    log_info ("fty_nut_configurator started");

    // code from src/malamute.c, under MPL
    //  Accept and print any message back from server
    while (true) {
        char *message = zstr_recv (server);
        if (message) {
            puts (message);
            free (message);
        }
        else {
            puts ("interrupted");
            break;
        }
    }

    log_info ("fty_nut_configurator ended");

    zactor_destroy (&server);

    return EXIT_SUCCESS;
}
