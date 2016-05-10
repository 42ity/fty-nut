/*  =========================================================================
    bios_agent_nut - description

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

/*
@header
    bios_agent_nut - 
@discuss
@end
*/
#include <getopt.h>

#include "agent_nut_classes.h"

#define str(x) #x

static const char *AGENT_NAME = "agent-nut";
static const char *ENDPOINT = "ipc://@/malamute";

#define DEFAULT_LOG_LEVEL LOG_WARNING

void usage () {
    puts ("bios-agent-nut [options] ...\n"
          "  --log-level / -l       bios log level\n"
          "                         overrides setting in env. variable BIOS_LOG_LEVEL\n"
          "  --mapping-file / -m    NUT-to-BIOS mapping file\n"
          "  --polling / -p         polling interval in seconds [30]\n"
          "  --verbose / -v         verbose test output\n"
          "  --help / -h            this information\n"
          );
}

int get_log_level (const char *level) {
    if (streq (level, str(LOG_DEBUG))) {
        return LOG_DEBUG;
    }
    else
    if (streq (level, str(LOG_INFO))) {
        return LOG_INFO;
    }
    else
    if (streq (level, str(LOG_WARNING))) {
        return LOG_WARNING;
    }
    else
    if (streq (level, str(LOG_ERR))) {
        return LOG_ERR;
    }
    else
    if (streq (level, str(LOG_CRIT))) {
        return LOG_CRIT;
    }
    return -1;
}

int main (int argc, char *argv [])
{
    int help = 0;
    int verbose = 0;
    int log_level = -1;
    std::string mapping_file;
    const char* polling = "30";

    while (true) {
        static struct option long_options[] =
        {
            {"help",            no_argument,        0,  1},
            {"verbose",         no_argument,        0,  1},
            {"log-level",       required_argument,  0,  'l'},
            {"mapping-file",    required_argument,  0,  'm'},
            {"polling",         required_argument,  0,  'p'},
            {0,                 0,                  0,  0}
        };

        int option_index = 0;
        int c = getopt_long (argc, argv, "hvl:m:p:", long_options, &option_index);
        if (c == -1)
            break;
        switch (c) {
            case 'l':
            {
                log_level = get_log_level (optarg);
                break;
            }
            case 'm':
            {
                mapping_file.assign (optarg);
                break;
            }
            case 'v':
            {
                verbose = 1;
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
        usage ();
        return EXIT_FAILURE;
    }

    // log_level cascade (priority ascending)
    //  1. default value
    //  2. env. variable 
    //  3. command line argument
    //  4. actor message - NOT IMPLEMENTED SO FAR 
    if (log_level == -1) {
        char *env_log_level = getenv ("BIOS_LOG_LEVEL");
        if (env_log_level) {
            log_level = get_log_level (env_log_level);
            if (log_level == -1)
                log_level = DEFAULT_LOG_LEVEL;
        }
        else {
            log_level = DEFAULT_LOG_LEVEL;
        }
    }
    log_set_level (log_level);

    log_info ("bios_agent_nut - NUT (Network UPS Tools) wrapper/daemon");
    
    zactor_t *nut_server = zactor_new (bios_nut_server, (void *) NULL);
    if (!nut_server) {
        log_critical ("zactor_new (task = 'bios_nut_server', args = 'NULL') failed");
        return -1;
    }

    zactor_t *nut_device_alert = zactor_new (alert_actor, (void *) NULL);
    if (!nut_device_alert) {
        log_critical ("zactor_new (task = 'nut_device_server', args = 'NULL') failed");
        return -1;
    }
    
    if (verbose) {
        zstr_sendx (nut_server, "VERBOSE", NULL);
    }
    zstr_sendx (nut_server, "CONFIGURE", mapping_file.c_str (), NULL);
    zstr_sendx (nut_server, "POLLING", polling, NULL);
    zstr_sendx (nut_server, "CONNECT", ENDPOINT, AGENT_NAME, NULL);
    zstr_sendx (nut_server, "PRODUCER", BIOS_PROTO_STREAM_METRICS, NULL);

    zpoller_t *poller = zpoller_new(nut_server, nut_device_alert, NULL);
    assert(poller);
    
    while (true) {
        void *which = zpoller_wait(poller, -1);
        if( which && ! zsys_interrupted) {
            char *message = zstr_recv (which);
            if (message) {
                puts (message);
                zstr_free (&message);
            }
        } else {
            puts ("interrupted");
            break;
        }
    }

    zpoller_destroy (&poller);
    zactor_destroy (&nut_server);
    zactor_destroy (&nut_device_alert);
    return 0;
}
