/*  =========================================================================
    fty_nut - NUT (Network UPS Tools) daemon wrapper/proxy

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
    fty_nut - fty-nut main
@discuss
@end
 */

#include "fty_nut.h"
#include "../lib/src/nut_mlm.h"

/*
#include "fty_nut_server.h"
#include "sensor_actor.h"
#include "alert_actor.h"
#include "fty_proto.h"
#include "nut_mlm.h"
*/

#include <fty_log.h>
#include <fty_common_mlm.h>

#include <getopt.h>
#include <stdio.h>
#include <czmq.h>
#include <string>

#define str(x) #x

void usage() {
    puts("fty-nut [options] ...\n"
            "  --config / -c          path to config file\n"
            "  --mapping-file / -m    NUT-to-BIOS mapping file\n"
            "  --polling / -p         polling interval in seconds [30]\n"
            "  --verbose / -v         verbose output\n"
            "  --help / -h            this information\n"
            );
}


int main(int argc, char *argv []) {
    int help = 0;
    //    int log_level = -1;
    std::string mapping_file;
    const char* polling = NULL;
    const char *config_file = "/etc/fty-nut/fty-nut.cfg";
    zconfig_t *config = NULL;

    ManageFtyLog::setInstanceFtylog("fty-nut", FTY_COMMON_LOGGING_DEFAULT_CFG);

    // Some systems define struct option with non-"const" "char *"
#if defined(__GNUC__) || defined(__GNUG__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"
#endif
    static const char *short_options = "hvl:m:p:s:c:";
    static struct option long_options[] = {
        {"help", no_argument, 0, 1},
        {"verbose", no_argument, 0, 1},
        {"config", required_argument, 0, 'c'},
        {"mapping-file", required_argument, 0, 'm'},
        {"state-file", required_argument, 0, 's'},
        {"polling", required_argument, 0, 'p'},
        {NULL, 0, 0, 0}
    };
#if defined(__GNUC__) || defined(__GNUG__)
#pragma GCC diagnostic pop
#endif

    while (true) {
        int option_index = 0;
        int c = getopt_long(argc, argv, short_options, long_options, &option_index);
        if (c == -1)
            break;
        switch (c) {
            case 'c':
            {
                config_file = optarg;
                break;
            }
            case 'm':
            {
                mapping_file.assign(optarg);
                break;
            }
            case 's':
            {
                fprintf(stderr, "The --state-file option is obsolete\n");
                break;
            }
            case 'v':
            {
                ManageFtyLog::getInstanceFtylog()->setVerboseMode();
                break;
            }
            case 'p':
            {
                if (!optarg) {
                    printf("invalid polling interval '%s'\n", optarg);
                    return EXIT_FAILURE;
                }
                polling = optarg;
                break;
            }
            case 'h':
            default:
            {
                help = 1;
                break;
            }
        }
    }
    if (help) {
        usage();
        return EXIT_FAILURE;
    }

    // Process configuration file
    config = zconfig_load(config_file);
    if (!config) {
        log_error("Failed to load config file %s", config_file);
        exit(EXIT_FAILURE);
    }

    std::string logConfigFile = std::string (zconfig_get (config, "log/config", ""));
    if (!logConfigFile.empty ())
    {
      ManageFtyLog::getInstanceFtylog ()->setConfigFile (logConfigFile);
    }

    // VERBOSE
    if (streq(zconfig_get(config, "server/verbose", "false"), "true")) {
        ManageFtyLog::getInstanceFtylog()->setVerboseMode();
    }
    // POLLING
    polling = zconfig_get(config, CONFIG_POLLING, "30");

    log_info("fty_nut - NUT (Network UPS Tools) wrapper/daemon");

    zactor_t *nut_server = zactor_new(fty_nut_server, MLM_ENDPOINT_VOID);
    if (!nut_server) {
        log_fatal("zactor_new (task = 'fty_nut_server', args = 'NULL') failed");
        return EXIT_FAILURE;
    }

    zactor_t *nut_device_alert = zactor_new(alert_actor, MLM_ENDPOINT_VOID);
    if (!nut_device_alert) {
        log_fatal("zactor_new (task = 'nut_device_server', args = 'NULL') failed");
        return EXIT_FAILURE;
    }

    zactor_t *nut_sensor = zactor_new(sensor_actor, MLM_ENDPOINT_VOID);
    if (!nut_sensor) {
        log_fatal("zactor_new (task = 'nut_sensor', args = 'NULL') failed");
        return EXIT_FAILURE;
    }

    zstr_sendx(nut_server, ACTION_CONFIGURE, mapping_file.c_str(), NULL);
    zstr_sendx(nut_server, ACTION_POLLING, polling, NULL);

    zstr_sendx(nut_device_alert, ACTION_POLLING, polling, NULL);

    zstr_sendx(nut_sensor, ACTION_CONFIGURE, mapping_file.c_str(), NULL);
    zstr_sendx(nut_sensor, ACTION_POLLING, polling, NULL);

    zpoller_t *poller = zpoller_new(nut_server, nut_device_alert, nut_sensor, NULL);
    assert(poller);

    while (!zsys_interrupted)
    {
        void *which = zpoller_wait(poller, 10000);
        if (which == NULL) {
            if (zpoller_terminated(poller) || zsys_interrupted) {
                break;
            }
        }
        else {
            char *msg = zstr_recv(which);
            if (msg) {
                puts(msg);
                zstr_free(&msg);
            }
        }

        if (zconfig_has_changed(config)) {
            log_debug("Config file has changed, reload config and propagate polling value");
            zconfig_destroy(&config);
            config = zconfig_load(config_file);
            if (config) {
                polling = zconfig_get(config, CONFIG_POLLING, "30");
                zstr_sendx(nut_server, ACTION_POLLING, polling, NULL);
                zstr_sendx(nut_device_alert, ACTION_POLLING, polling, NULL);
                zstr_sendx(nut_sensor, ACTION_POLLING, polling, NULL);
            } else {
                log_error("Failed to load config file %s", config_file);
                break;
            }
        }
    }

    zpoller_destroy(&poller);
    zactor_destroy(&nut_server);
    zactor_destroy(&nut_device_alert);
    zactor_destroy(&nut_sensor);
    if (config) {
        zconfig_destroy(&config);
    }

    return EXIT_SUCCESS;
}
