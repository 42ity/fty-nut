/*  =========================================================================
    fty_nut_command - nut server command

    Copyright (C) 2014 - 2018 Eaton

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
    fty_nut_command - daemon to issue commands to nut-server (upsd)
@discuss
@end
*/

#include "fty_nut_command_server.h"

#include "nut_mlm.h"

const char *NUT_USER_ENV = "NUT_USER";
const char *NUT_PASS_ENV = "NUT_PASSWD";

int main (int argc, char *argv [])
{
    std::string nutHost = "localhost";
    std::string nutUsername = getenv(NUT_USER_ENV) ? getenv(NUT_USER_ENV) : "";
    std::string nutPassword = getenv(NUT_PASS_ENV) ? getenv(NUT_PASS_ENV) : "";
    std::string logConfig = "/etc/fty/ftylog.cfg";
    std::string configFile;

    bool verbose = false;
    int argn;
    ManageFtyLog::setInstanceFtylog("fty-nut-command");

    for (argn = 1; argn < argc; argn++) {
        if (streq (argv [argn], "--help")
        ||  streq (argv [argn], "-h")) {
            puts ("fty-nut-command [options] ...");
            puts ("  --config / -c          configuration file");
            puts ("  --help / -h            this information");
            puts ("  --verbose / -v         verbose test output");
            return 0;
        }
        else
        if (streq (argv [argn], "--verbose")
        ||  streq (argv [argn], "-v"))
            verbose = true;
        else
        if (streq (argv [argn], "--config")
            ||  streq (argv [argn], "-c")) {
            argn += 1;
            configFile = argv [argn];
        }
        else {
            printf ("Unknown option: %s\n", argv [argn]);
            return 1;
        }
    }

    if (configFile != "") {
        log_info ("Loading config file '%s'...", configFile.c_str());
        zconfig_t *cfg = zconfig_load(configFile.c_str());
        if (cfg) {
            logConfig = zconfig_get(cfg, "log/config", "");
            nutHost        = zconfig_get(cfg, "nut/host", nutHost.c_str());
            nutUsername    = zconfig_get(cfg, "nut/username", nutUsername.c_str());
            nutPassword    = zconfig_get(cfg, "nut/password", nutPassword.c_str());
            log_info ("Config file loaded.");
        }
        else {
            log_info ("Couldn't load config file.");
        }
    }

    ManageFtyLog::getInstanceFtylog()->setConfigFile(logConfig);
    if (verbose)
        ManageFtyLog::getInstanceFtylog()->setVeboseMode();

    // Build database URL
    DBConn::dbpath();

    log_info ("fty_nut_command  ");
    zactor_t *server = zactor_new (fty_nut_command_server, MLM_ENDPOINT_VOID);

    zstr_sendm (server, "CONFIGURATION");
    zstr_sendm (server, nutHost.c_str());
    zstr_sendm (server, nutUsername.c_str());
    zstr_sendm (server, nutPassword.c_str());
    zstr_send (server, DBConn::url.c_str());

    int r = EXIT_SUCCESS;

    // code from src/malamute.c, under MPL
    //  Accept and print any message back from server
    while (true) {
        ZstrGuard message (zstr_recv (server));
        if (message) {
            if (streq (message, "NUT_CONNECTION_FAILURE")) {
                log_fatal ("Failed to communicate with NUT server, aborting...");
                r = EXIT_FAILURE;
                break;
            }
            else {
                puts (message.get());
            }
        }
        else {
            puts ("interrupted");
            break;
        }
    }

    zactor_destroy (&server);
    return r;
}
