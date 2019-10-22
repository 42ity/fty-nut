/*  =========================================================================
    fty_nut_command_server - fty nut command actor

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

#ifndef FTY_NUT_COMMAND_SERVER_H_INCLUDED
#define FTY_NUT_COMMAND_SERVER_H_INCLUDED

#include "fty_nut_library.h"

namespace ftynut {
    /**
     * \brief NUT command manager for 42ity.
     *
     * This class provides 42ity-type power commands with NUT as a backend.
     * It converts incoming requests to NUT commands and tracks completion
     * of 42ity power commands.
     */
    class NutCommandManager {
        public:
            using CompletionCallback = std::function<void(std::string, bool)>;

            NutCommandManager(const std::string& nutHost, const std::string& nutUsername, const std::string& nutPassword, const std::string& dbConn, CompletionCallback callback);
            ~NutCommandManager() = default;

            void submitWork(const std::string &correlationId, const dto::commands::Commands &jobs);

        private:
            /**
             * \brief NUT command worker for NutCommandManager.
             *
             * This is the worker that handles everything NUT related. It
             * submits and track completion of NUT commands.
             */
            class NutCommandWorker {
                public:
                    using CompletionCallback = std::function<void(nut::TrackingID, bool)>;
                    using NutTrackingIds = std::set<nut::TrackingID>;

                    NutCommandWorker(const std::string& nutHost, const std::string& nutUsername, const std::string& nutPassword, CompletionCallback callback);
                    ~NutCommandWorker();

                    NutTrackingIds submitWork(const dto::commands::Commands &jobs);

                private:
                    void mainloop();

                    std::string m_nutHost;
                    std::string m_nutUsername;
                    std::string m_nutPassword;
                    std::string m_dbConn;
                    CompletionCallback m_callback;
                    std::thread m_worker;

                    // This mutex protects everything in this section.
                    std::mutex m_mutex;
                    std::list<nut::TrackingID> m_pendingCommands;
                    std::condition_variable m_cv;
                    volatile bool m_stopped;
            };

            /**
             * \brief Context for 42ity power command request.
             */
            struct Job {
                Job(const std::string &corrId, NutCommandWorker::NutTrackingIds ids) : correlationId(corrId), ids(ids), success(true) {}
                ~Job() = default;

                std::string correlationId;
                NutCommandWorker::NutTrackingIds ids;
                bool success;
            } ;

            void completionCallback(nut::TrackingID id, bool result);

            NutCommandWorker m_worker;
            CompletionCallback m_callback;
            std::string m_dbConn;

            // This mutex protects everything this section.
            std::mutex m_mutex;
            std::list<Job> m_jobs;
    };

    /**
     * \brief Bus connector for NutCommandManager.
     *
     * This connects the command manager to the rest of the system. It collects
     * command requests and send responses.
     */
    class NutCommandConnector {
        public:
            struct Parameters {
                Parameters();

                std::string endpoint;
                std::string agentName;

                std::string nutHost;
                std::string nutUsername;
                std::string nutPassword;

                std::string dbUrl;
            };

            NutCommandConnector(Parameters params);
            ~NutCommandConnector();

            void handleRequest(messagebus::Message msg);

        private:
            void completionCallback(std::string correlationId, bool result);
            void buildAndSendReply(const messagebus::MetaData &sender, bool result);

            Parameters m_parameters;
            NutCommandManager m_manager;
            messagebus::MessageBus *m_msgBus;

            // This mutex protects everything this section.
            std::mutex m_mutex;
            std::map<std::string, messagebus::MetaData> m_pendingJobs;
    };
}

#ifdef __cplusplus
extern "C" {
#endif

//  @interface
//  Create a fty_nut_configurator_server
void fty_nut_command_server (zsock_t *pipe, void *args);

//  Self test of this class
void fty_nut_command_server_test (bool verbose);

//  @end

#ifdef __cplusplus
}
#endif

#endif
