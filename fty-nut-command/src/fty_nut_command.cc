/*  =========================================================================
    fty_nut_command - nut server command

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
    fty_nut_command - daemon to issue commands to nut-server (upsd)
@discuss
@end
*/

#include "fty_nut.h"
#include "../lib/src/nut_mlm.h"

//#include "fty_nut_command_server.h"
//#include "nut_mlm.h"

#include <czmq.h>
#include <fty_log.h>
#include <fty_common_mlm.h>
#include <fty_common_db.h>

#include <csignal>
#include <mutex>

volatile bool g_exit = false;
std::condition_variable g_cv;
std::mutex g_mutex;

void sigHandler(int)
{
    g_exit = true;
    g_cv.notify_one();
}

void setSignalHandler()
{
    struct sigaction sigIntHandler;
    sigIntHandler.sa_handler = sigHandler;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;
    sigaction(SIGINT, &sigIntHandler, nullptr);
}

int main (int argc, char *argv [])
{
    // Build database URL
    DBConn::dbpath();

    // Create default configuration
    ftynut::NutCommandConnector::Parameters commandParameters;
    std::string logConfig = FTY_COMMON_LOGGING_DEFAULT_CFG;
    std::string logVerbose = "false";
    std::string configFile;

    // Parse command-line arguments
    for (int argn = 1; argn < argc; argn++) {
        if (streq (argv [argn], "--help")
        ||  streq (argv [argn], "-h")) {
            puts ("fty-nut-command [options] ...");
            puts ("  --config / -c          configuration file");
            puts ("  --help / -h            this information");
            puts ("  --verbose / -v         verbose test output");
            return EXIT_SUCCESS;
        }
        else
        if (streq (argv [argn], "--verbose")
        ||  streq (argv [argn], "-v"))
            logVerbose = "true";
        else
        if (streq (argv [argn], "--config")
            ||  streq (argv [argn], "-c")) {
            argn += 1;
            configFile = argv [argn];
        }
        else {
            std::cerr << "Unknown option: " << argv[argn] << std::endl;
            return EXIT_FAILURE;
        }
    }

    // Parse configuration file
    if (!configFile.empty()) {
        ZconfigGuard cfg(zconfig_load(configFile.c_str()));
        if (cfg) {
            std::map<std::string, std::string&> properties {
                { "log/config", logConfig },
                { "log/verbose", logVerbose },
                { "nut/host", commandParameters.nutHost },
                { "nut/username", commandParameters.nutUsername },
                { "nut/password", commandParameters.nutPassword }
            } ;
            for (auto& property : properties) {
                property.second = zconfig_get(cfg.get(), property.first.c_str(), property.second.c_str());
            }
        }
        else {
            std::cerr << "Couldn't load config file " << configFile << std::endl;
            return EXIT_FAILURE;
        }
    }

    // Set up logging.
    ManageFtyLog::setInstanceFtylog(commandParameters.agentName, logConfig);
    if (!configFile.empty()) {
        log_info ("Loaded config file '%s'.", configFile.c_str());
    }

    bool verbose;
    std::stringstream(logVerbose) >> verbose;

    if (verbose) {
        ManageFtyLog::getInstanceFtylog()->setVerboseMode();
        log_trace ("Verbose mode OK");
    }

    // Launch workers.
    ftynut::NutCommandConnector nutCommandConnector(commandParameters);

    // Wait until interrupt.
    setSignalHandler();
    std::unique_lock<std::mutex> lock(g_mutex);
    g_cv.wait(lock, [] { return g_exit; });

    return EXIT_SUCCESS;
}
