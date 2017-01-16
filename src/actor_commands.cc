/*  =========================================================================
    actor_commands - actor commands

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
    actor_commands - actor commands
@discuss
@end
*/

#include "fty_nut_classes.h"

int
actor_commands (
        mlm_client_t *client,
        zmsg_t **message_p,
        bool& verbose,
        uint64_t& timeout,
        NUTAgent& nut_agent,
        nut_t *data,
        std::string& state_fullpath) {

    assert (message_p && *message_p);
    assert (data);
    zmsg_t *message = *message_p;

    char *cmd = zmsg_popstr (message);
    if (!cmd) {
        log_error (
                "Given `which == pipe` function `zmsg_popstr (msg)` returned NULL. "
                "Message received is most probably empty (has no frames).");
        zmsg_destroy (message_p);
        return 0;
    }

    int ret = 0;
    log_debug ("actor command = '%s'", cmd);
    if (streq (cmd, "$TERM")) {
        log_info ("Got $TERM");
        ret = 1;
    }
    else
    if (streq (cmd, "VERBOSE")) {
        verbose = true;
    }
    else
    if (streq (cmd, "CONNECT")) {
        char *endpoint = zmsg_popstr (message);
        if (!endpoint) {
            log_error (
                    "Expected multipart string format: CONNECT/endpoint/name. "
                    "Received CONNECT/nullptr");
            zstr_free (&cmd);
            zmsg_destroy (message_p);
            return 0;
        }
        char *name = zmsg_popstr (message);
        if (!name) {
            log_error (
                    "Expected multipart string format: CONNECT/endpoint/name. "
                    "Received CONNECT/endpoint/nullptr");
            zstr_free (&endpoint);
            zstr_free (&cmd);
            zmsg_destroy (message_p);
            return 0;
        }
        int rv = mlm_client_connect (client, endpoint, 1000, name);
        if (rv == -1) {
            log_error (
                    "mlm_client_connect (endpoint = '%s', timeout = '%d', address = '%s') failed",
                    endpoint, 1000, name);
        }
        else {
            nut_agent.setClient (client);
        }
        zstr_free (&endpoint);
        zstr_free (&name);
    }
    else
    if (streq (cmd, "PRODUCER")) {
        char *stream = zmsg_popstr (message);
        if (!stream) {
            log_error (
                    "Expected multipart string format: PRODUCER/stream. "
                    "Received PRODUCER/nullptr");
            zstr_free (&stream);
            zstr_free (&cmd);
            zmsg_destroy (message_p);
            return 0;
        }
        int rv = mlm_client_set_producer (client, stream);
        if (rv == -1) {
            log_error ("mlm_client_set_producer (stream = '%s') failed", stream);
        }
        zstr_free (&stream);
    }
    else
    if (streq (cmd, "CONSUMER")) {
        char *stream = zmsg_popstr (message);
        if (!stream) {
            log_error (
                    "Expected multipart string format: CONSUMER/stream/pattern. "
                    "Received CONSUMER/nullptr");
            zstr_free (&cmd);
            zmsg_destroy (message_p);
            return 0;
        }
        char *pattern = zmsg_popstr (message);
        if (!pattern) {
            log_error (
                    "Expected multipart string format: CONSUMER/stream/pattern. "
                    "Received CONSUMER/stream/nullptr");
            zstr_free (&stream);
            zstr_free (&cmd);
            zmsg_destroy (message_p);
            return 0;
        }
        int rv = mlm_client_set_consumer (client, stream, pattern);
        if (rv == -1) {
            log_error (
                    "mlm_client_set_consumer (stream = '%s', pattern = '%s') failed",
                    stream, pattern);
        }
        zstr_free (&pattern);
        zstr_free (&stream);
    }
    else
    if (streq (cmd, "CONFIGURE")) {
        char *mapping = zmsg_popstr (message);
        if (!mapping) {
            log_error (
                    "Expected multipart string format: CONFIGURE/mapping_file/state_file. "
                    "Received CONFIGURE/nullptr");
            zstr_free (&mapping);
            zstr_free (&cmd);
            zmsg_destroy (message_p);
            return 0;
        }
        char *state_file = zmsg_popstr (message);
        if (!state_file) {
            log_error (
                    "Expected multipart string format: CONFIGURE/mapping_file/state_file. "
                    "Received CONFIGURE/mapping_file/nullptr");
            zstr_free (&mapping);
            zstr_free (&state_file);
            zstr_free (&cmd);
            zmsg_destroy (message_p);
            return 0;
        }
        bool rv = nut_agent.loadMapping (mapping);
        if (rv == false) {
            log_error ("NUTAgent::loadMapping (mapping = '%s') failed", mapping);
        }
        state_fullpath.assign (state_file);
        int r = nut_load (data, state_file);
        if (r != 0) {
            log_warning ("Could not load state file '%s'.", state_file);
        }
        nut_agent.updateDeviceList (data);
        zstr_free (&mapping);
        zstr_free (&state_file);
    }
    else
    if (streq (cmd, "POLLING")) {
        char *polling = zmsg_popstr (message);
        if (!polling) {
            log_error (
                "Expected multipart string format: POLLING/value. "
                "Received POLLING/nullptr");
            zstr_free (&cmd);
            zmsg_destroy (message_p);
            return 0;
        }
        timeout = atoi(polling) * 1000;
        if (timeout == 0) {
            log_error ("invalid POLLING value '%s', using default instead", polling);
            timeout = 30000;
        }
        nut_agent.TTL (timeout * 2 / 1000);
        zstr_free (&polling);
    }
    else {
        log_warning ("Command '%s' is unknown or not implemented", cmd);
    }

    zstr_free (&cmd);
    zmsg_destroy (message_p);
    return ret;
}
//  --------------------------------------------------------------------------
//  Self test of this class

#define STDERR_EMPTY \
    {\
    fseek (fp, 0L, SEEK_END);\
    uint64_t sz = ftell (fp);\
    fclose (fp);\
    if (sz > 0)\
        printf("STDERR_EMPTY() check failed, please review the stderr.txt in workspace root\n");\
    assert (sz == 0);\
    }

#define STDERR_NON_EMPTY \
    {\
    fseek (fp, 0L, SEEK_END);\
    uint64_t sz = ftell (fp);\
    fclose (fp);\
    if (sz == 0)\
        printf("STDERR_NON_EMPTY() check failed\n");\
    assert (sz > 0);\
    }

void
actor_commands_test (bool verbose)
{
    printf (" * actor_commands: \n");

    //  @selftest
    static const char* endpoint = "ipc://fty-nut-server-test";

    // malamute broker
    zactor_t *malamute = zactor_new (mlm_server, (void*) "Malamute");
    assert (malamute);
    if (verbose)
        zstr_send (malamute, "VERBOSE");
    zstr_sendx (malamute, "BIND", endpoint, NULL);

    mlm_client_t *client = mlm_client_new ();
    assert (client);

    zmsg_t *message = NULL;
    bool actor_verbose = false;

    NUTAgent nut_agent;
    nut_t *data = nut_new ();
    std::string state_file;
    uint64_t actor_polling = 0;

    // --------------------------------------------------------------
    FILE *fp = freopen ("stderr.txt", "w+", stderr);
    // empty message - expected fail
    message = zmsg_new ();
    assert (message);
    int rv = actor_commands (client, &message, actor_verbose, actor_polling, nut_agent, data, state_file);
    assert (rv == 0);
    assert (message == NULL);
    assert (actor_verbose == false);
    assert (actor_polling == 0);
    assert (nut_agent.isMappingLoaded () == false);
    assert (nut_agent.isClientSet () == false);
    assert (nut_agent.TTL () == 60);
    assert (state_file.empty ());

    STDERR_NON_EMPTY

    // --------------------------------------------------------------
    fp = freopen ("stderr.txt", "w+", stderr);
    // empty string - expected fail
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "");
    rv = actor_commands (client, &message, actor_verbose, actor_polling, nut_agent, data, state_file);
    assert (rv == 0);
    assert (message == NULL);
    assert (actor_verbose == false);
    assert (actor_polling == 0);
    assert (nut_agent.isMappingLoaded () == false);
    assert (nut_agent.isClientSet () == false);
    assert (nut_agent.TTL () == 60);
    assert (state_file.empty ());

    STDERR_NON_EMPTY

    // --------------------------------------------------------------
    fp = freopen ("stderr.txt", "w+", stderr);
    // unknown command - expected fail
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "MAGIC!");
    rv = actor_commands (client, &message, actor_verbose, actor_polling, nut_agent, data, state_file);
    assert (rv == 0);
    assert (message == NULL);
    assert (actor_verbose == false);
    assert (actor_polling == 0);
    assert (nut_agent.isMappingLoaded () == false);
    assert (nut_agent.isClientSet () == false);
    assert (nut_agent.TTL () == 60);
    assert (state_file.empty ());

    STDERR_NON_EMPTY

    // --------------------------------------------------------------
    fp = freopen ("stderr.txt", "w+", stderr);
    // CONFIGURE - expected fail
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "CONFIGURE");
    // missing mapping_file here
    // missing state_file here
    rv = actor_commands (client, &message, actor_verbose, actor_polling, nut_agent, data, state_file);
    assert (rv == 0);
    assert (message == NULL);
    assert (actor_verbose == false);
    assert (actor_polling == 0);
    assert (nut_agent.isMappingLoaded () == false);
    assert (nut_agent.isClientSet () == false);
    assert (nut_agent.TTL () == 60);
    assert (state_file.empty ());

    STDERR_NON_EMPTY

    // --------------------------------------------------------------
    fp = freopen ("stderr.txt", "w+", stderr);
    // CONFIGURE - expected fail
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "CONFIGURE");
    zmsg_addstr (message, "sdfwwed");
    // missing state_file here
    rv = actor_commands (client, &message, actor_verbose, actor_polling, nut_agent, data, state_file);
    assert (rv == 0);
    assert (message == NULL);
    assert (actor_verbose == false);
    assert (actor_polling == 0);
    assert (nut_agent.isMappingLoaded () == false);
    assert (nut_agent.isClientSet () == false);
    assert (nut_agent.TTL () == 60);
    assert (state_file.empty ());

    STDERR_NON_EMPTY

    // --------------------------------------------------------------
    fp = freopen ("stderr.txt", "w+", stderr);
    // CONNECT - expected fail
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "CONNECT");
    zmsg_addstr (message, endpoint);
    // missing name here
    rv = actor_commands (client, &message, actor_verbose, actor_polling, nut_agent, data, state_file);
    assert (rv == 0);
    assert (message == NULL);
    assert (actor_verbose == false);
    assert (actor_polling == 0);
    assert (nut_agent.isMappingLoaded () == false);
    assert (nut_agent.isClientSet () == false);
    assert (nut_agent.TTL () == 60);
    assert (state_file.empty ());

    STDERR_NON_EMPTY

    // --------------------------------------------------------------
    fp = freopen ("stderr.txt", "w+", stderr);
    // CONNECT - expected fail
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "CONNECT");
    // missing endpoint here
    // missing name here
    rv = actor_commands (client, &message, actor_verbose, actor_polling, nut_agent, data, state_file);
    assert (rv == 0);
    assert (message == NULL);
    assert (actor_verbose == false);
    assert (actor_polling == 0);
    assert (nut_agent.isMappingLoaded () == false);
    assert (nut_agent.isClientSet () == false);
    assert (nut_agent.TTL () == 60);
    assert (state_file.empty ());

    STDERR_NON_EMPTY

    // --------------------------------------------------------------
    fp = freopen ("stderr.txt", "w+", stderr);
    // CONNECT - expected fail; bad endpoint
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "CONNECT");
    zmsg_addstr (message, "ipc://fty-nut-server-BAD");
    zmsg_addstr (message, "test-agent");
    rv = actor_commands (client, &message, actor_verbose, actor_polling, nut_agent, data, state_file);
    assert (rv == 0);
    assert (message == NULL);
    assert (actor_verbose == false);
    assert (actor_polling == 0);
    assert (nut_agent.isMappingLoaded () == false);
    assert (nut_agent.isClientSet () == false);
    assert (nut_agent.TTL () == 60);
    assert (state_file.empty ());

    STDERR_NON_EMPTY

    // --------------------------------------------------------------
    fp = freopen ("stderr.txt", "w+", stderr);
    // PRODUCER - expected fail
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "PRODUCER");
    // missing stream here
    rv = actor_commands (client, &message, actor_verbose, actor_polling, nut_agent, data, state_file);
    assert (rv == 0);
    assert (message == NULL);
    assert (actor_verbose == false);
    assert (actor_polling == 0);
    assert (nut_agent.isMappingLoaded () == false);
    assert (nut_agent.isClientSet () == false);
    assert (nut_agent.TTL () == 60);
    assert (state_file.empty ());

    STDERR_NON_EMPTY

    // --------------------------------------------------------------
    fp = freopen ("stderr.txt", "w+", stderr);
    // CONSUMER - expected fail
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "CONSUMER");
    zmsg_addstr (message, "some-stream");
    // missing pattern here
    rv = actor_commands (client, &message, actor_verbose, actor_polling, nut_agent, data, state_file);
    assert (rv == 0);
    assert (message == NULL);
    assert (actor_verbose == false);
    assert (actor_polling == 0);
    assert (nut_agent.isMappingLoaded () == false);
    assert (nut_agent.isClientSet () == false);
    assert (nut_agent.TTL () == 60);
    assert (state_file.empty ());

    STDERR_NON_EMPTY

    // --------------------------------------------------------------
    fp = freopen ("stderr.txt", "w+", stderr);
    // CONSUMER - expected fail
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "CONSUMER");
    // missing stream here
    // missing pattern here
    rv = actor_commands (client, &message, actor_verbose, actor_polling, nut_agent, data, state_file);
    assert (rv == 0);
    assert (message == NULL);
    assert (actor_verbose == false);
    assert (actor_polling == 0);
    assert (nut_agent.isMappingLoaded () == false);
    assert (nut_agent.isClientSet () == false);
    assert (nut_agent.TTL () == 60);
    assert (state_file.empty ());

    STDERR_NON_EMPTY

    // --------------------------------------------------------------
    fp = freopen ("stderr.txt", "w+", stderr);
    // POLLING - expected fail
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "POLLING");
    // missing value here
    rv = actor_commands (client, &message, actor_verbose, actor_polling, nut_agent, data, state_file);
    assert (rv == 0);
    assert (message == NULL);
    assert (actor_verbose == false);
    assert (actor_polling == 0);
    assert (nut_agent.isMappingLoaded () == false);
    assert (nut_agent.isClientSet () == false);
    assert (nut_agent.TTL () == 60);
    assert (state_file.empty ());

    STDERR_NON_EMPTY

    // --------------------------------------------------------------
    fp = freopen ("stderr.txt", "w+", stderr);
    // POLLING - expected fail (in a sense)
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "POLLING");
    zmsg_addstr (message, "a14s2"); // Bad value
    rv = actor_commands (client, &message, actor_verbose, actor_polling, nut_agent, data, state_file);
    assert (rv == 0);
    assert (message == NULL);
    assert (actor_verbose == false);
    assert (actor_polling == 30000);
    assert (nut_agent.isMappingLoaded () == false);
    assert (nut_agent.isClientSet () == false);
    assert (nut_agent.TTL () == 60);
    assert (state_file.empty ());

    STDERR_NON_EMPTY

    // The original client still waiting on the bad endpoint for malamute
    // server to show up. Therefore we must destroy and create it again.
    mlm_client_destroy (&client);
    client = mlm_client_new ();
    assert (client);
    // re-set actor_polling to zero again (so we don't have to remember
    // to assert to the previous value)
    actor_polling = 0;

    // --------------------------------------------------------------
    // touch the state-file for test below (should exist, may be empty)
    fp = fopen ("src/selftest_state_file", "w+");
    if (fp)
        fclose (fp);

    // Prepare the error logger
    fp = freopen ("stderr.txt", "w+", stderr);

    // VERBOSE
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "VERBOSE");
    rv = actor_commands (client, &message, actor_verbose, actor_polling, nut_agent, data, state_file);
    assert (rv == 0);
    assert (message == NULL);
    assert (actor_verbose == true);
    assert (actor_polling == 0);
    assert (nut_agent.isMappingLoaded () == false);
    assert (nut_agent.isClientSet () == false);
    assert (nut_agent.TTL () == 60);
    assert (state_file.empty ());

    // CONFIGURE
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "CONFIGURE");
    zmsg_addstr (message, "src/fty-nut.cfg");
    zmsg_addstr (message, "src/selftest_state_file");
    rv = actor_commands (client, &message, actor_verbose, actor_polling, nut_agent, data, state_file);
    assert (rv == 0);
    assert (message == NULL);
    assert (actor_verbose == true);
    assert (actor_polling == 0);
    assert (nut_agent.isMappingLoaded () == true);
    assert (nut_agent.isClientSet () == false);
    assert (nut_agent.TTL () == 60);
    assert (state_file == "src/selftest_state_file");

    // $TERM
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "$TERM");
    rv = actor_commands (client, &message, actor_verbose, actor_polling, nut_agent, data, state_file);
    assert (rv == 1);
    assert (message == NULL);
    assert (actor_verbose == true);
    assert (actor_polling == 0);
    assert (nut_agent.isMappingLoaded () == true);
    assert (nut_agent.isClientSet () == false);
    assert (nut_agent.TTL () == 60);

    // CONNECT
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "CONNECT");
    zmsg_addstr (message, endpoint);
    zmsg_addstr (message, "test-agent");
    rv = actor_commands (client, &message, actor_verbose, actor_polling, nut_agent, data, state_file);
    assert (rv == 0);
    assert (message == NULL);
    assert (actor_verbose == true);
    assert (actor_polling == 0);
    assert (nut_agent.isMappingLoaded () == true);
    assert (nut_agent.isClientSet () == true);
    assert (nut_agent.TTL () == 60);

    // CONSUMER
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "CONSUMER");
    zmsg_addstr (message, "some-stream");
    zmsg_addstr (message, ".+@.+");
    rv = actor_commands (client, &message, actor_verbose, actor_polling, nut_agent, data, state_file);
    assert (rv == 0);
    assert (message == NULL);
    assert (actor_verbose == true);
    assert (actor_polling == 0);
    assert (nut_agent.isMappingLoaded () == true);
    assert (nut_agent.isClientSet () == true);
    assert (nut_agent.TTL () == 60);

    // PRODUCER
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "PRODUCER");
    zmsg_addstr (message, "some-stream");
    rv = actor_commands (client, &message, actor_verbose, actor_polling, nut_agent, data, state_file);
    assert (rv == 0);
    assert (message == NULL);
    assert (actor_verbose == true);
    assert (actor_polling == 0);
    assert (nut_agent.isMappingLoaded () == true);
    assert (nut_agent.isClientSet () == true);
    assert (nut_agent.TTL () == 60);

    // POLLING
    message = zmsg_new ();
    assert (message);
    zmsg_addstr (message, "POLLING");
    zmsg_addstr (message, "150");
    rv = actor_commands (client, &message, actor_verbose, actor_polling, nut_agent, data, state_file);
    assert (rv == 0);
    assert (message == NULL);
    assert (actor_verbose == true);
    assert (actor_polling == 150000);
    assert (nut_agent.isMappingLoaded () == true);
    assert (nut_agent.isClientSet () == true);
    assert (nut_agent.TTL () == 300);

    STDERR_EMPTY

    nut_destroy (&data);
    zmsg_destroy (&message);
    mlm_client_destroy (&client);
    zactor_destroy (&malamute);

    //  @end

    printf ("OK\n");
}
