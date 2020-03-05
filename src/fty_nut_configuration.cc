/*  =========================================================================
    fty_nut_configuration - description

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
    fty_nut_configuration -
@discuss
@end
*/
#include "fty_nut_library.h"

#include <condition_variable>
#include <csignal>
#include <mutex>

#include "nut_mlm.h"

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

static void getConfig(zconfig_t* config, const std::string& keyName, std::string& keyValue)
{
    keyValue = zconfig_get(config, keyName.c_str(), keyValue.c_str());
}

static void getConfig(zconfig_t* config, const std::string& keyName, unsigned& keyValue)
{
    keyValue = std::stoul(zconfig_get(config, keyName.c_str(), std::to_string(keyValue).c_str()));
}

static void getConfig(zconfig_t* config, const std::string& keyName, bool& keyValue)
{
    keyValue = streq(zconfig_get(config, keyName.c_str(), keyValue ? "false" : "true"), "true");
}

int main (int argc, char *argv [])
{
    // Build database URL
    DBConn::dbpath();

    // Create default configuration
    std::string logConfig = "/etc/fty/ftylog.cfg";
    bool logVerbose = false;
    bool rescanOnStart = false;
    fty::nut::ConfigurationManager::Parameters configurationManagerParameters;
    fty::nut::ConfigurationConnector::Parameters configurationConnectorParameters;
    fty::nut::DriverConnector::Parameters driverConnectorParameters;

    std::string configFile;

    // Parse command-line arguments
    for (int argn = 1; argn < argc; argn++) {
        if (streq (argv [argn], "--help")
        ||  streq (argv [argn], "-h")) {
            puts ("fty-nut-configuration [options] ...");
            puts ("  --config / -c          configuration file");
            puts ("  --help / -h            this information");
            puts ("  --rescan / -r          rescan all assets on start");
            puts ("  --verbose / -v         verbose test output");
            return 0;
        }
        else
        if (streq (argv [argn], "--config")
            ||  streq (argv [argn], "-c")) {
            argn += 1;
            configFile = argv [argn];
        }
        else
        if (streq (argv [argn], "--rescan")
            ||  streq (argv [argn], "-r")) {
            rescanOnStart = true;
        }
        else
        if (streq (argv [argn], "--verbose")
        ||  streq (argv [argn], "-v"))
            logVerbose = true;
        else {
            std::cerr << "Unknown option: " << argv[argn] << std::endl;
            return 1;
        }
    }

    // Parse configuration file
    if (!configFile.empty()) {
        ZconfigGuard cfg(zconfig_load(configFile.c_str()));
        if (cfg) {
            getConfig(cfg.get(), "log/config",  logConfig);
            getConfig(cfg.get(), "log/verbose", logVerbose);

            getConfig(cfg.get(), "configuration/threadPoolScannerSize",             configurationManagerParameters.threadPoolScannerSize);
            getConfig(cfg.get(), "configuration/threadPoolMalamuteSize",            configurationConnectorParameters.threadPoolSize);
            getConfig(cfg.get(), "configuration/nutRepositoryDirectory",            configurationManagerParameters.nutRepositoryDirectory);

            getConfig(cfg.get(), "rescanPolicy/onStart",                            rescanOnStart);
            getConfig(cfg.get(), "rescanPolicy/onSecurityWalletCreate",             configurationConnectorParameters.rescanOnSecurityWalletCreate);
            getConfig(cfg.get(), "rescanPolicy/onSecurityWalletUpdate",             configurationConnectorParameters.rescanOnSecurityWalletUpdate);
            getConfig(cfg.get(), "rescanPolicy/onSecurityWalletDelete",             configurationConnectorParameters.rescanOnSecurityWalletDelete);

            getConfig(cfg.get(), "preferences/automaticPrioritySort",               configurationConnectorParameters.automaticPrioritySort);
            getConfig(cfg.get(), "preferences/preferDmfForSnmp",                    configurationManagerParameters.preferDmfForSnmp);
            getConfig(cfg.get(), "preferences/scanDummyUps",                        configurationManagerParameters.scanDummyUps);
        }
        else {
            std::cerr << "Couldn't load config file " << configFile << std::endl;
            return EXIT_FAILURE;
        }
    }

    // Set up logging.
    ManageFtyLog::setInstanceFtylog(configurationConnectorParameters.agentName, logConfig);
    if (!configFile.empty()) {
        log_info ("Loaded config file '%s'.", configFile.c_str());
    }

    if (logVerbose) {
        ManageFtyLog::getInstanceFtylog()->setVeboseMode();
        log_trace ("Verbose mode OK");
    }

    // Launch managers.
    fty::nut::ConfigurationManager  configurationManager(configurationManagerParameters);
    fty::nut::DriverManager         driverManager(configurationManagerParameters.nutRepositoryDirectory);

    // Launch workers.
    fty::nut::ConfigurationConnector configurationConnector(configurationConnectorParameters, configurationManager);
    fty::nut::DriverConnector       driverConnector(driverConnectorParameters, driverManager);

    if (rescanOnStart) {
        configurationConnector.triggerRescan();
    }

    // Wait until interrupt.
    setSignalHandler();
    std::unique_lock<std::mutex> lock(g_mutex);
    g_cv.wait(lock, [] { return g_exit; });

    return EXIT_SUCCESS;
}
