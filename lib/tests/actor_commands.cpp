#include "src/actor_commands.h"
#include "src/nut_agent.h"
#include "src/nut_mlm.h"
#include "src/state_manager.h"
#include <catch2/catch.hpp>
#include <fty_log.h>
#include <fty_nut.h>
#include <malamute.h>

#define STDERR_EMPTY                                                                                                   \
    {                                                                                                                  \
        fseek(fp, 0L, SEEK_END);                                                                                       \
        uint64_t sz = ftell(fp);                                                                                       \
        fclose(fp);                                                                                                    \
        if (sz > 0)                                                                                                    \
            printf("STDERR_EMPTY() check failed, please review stderr.txt\n");                                         \
        assert(sz == 0);                                                                                               \
    }

#define STDERR_NON_EMPTY                                                                                               \
    {                                                                                                                  \
        fseek(fp, 0L, SEEK_END);                                                                                       \
        uint64_t sz = uint64_t(ftell(fp));                                                                             \
        fclose(fp);                                                                                                    \
        if (sz == 0)                                                                                                   \
            printf("STDERR_NON_EMPTY() check failed\n");                                                               \
        assert(sz > 0);                                                                                                \
    }

#define SELFTEST_RO "selftest-ro"

TEST_CASE("actor commands test")
{
    //  @selftest
    static const char* endpoint   = "ipc://fty-nut-server-test";
    static const char* logErrPath = "stderr.txt";

    ManageFtyLog::setInstanceFtylog("fty-nut-command-test");

    // malamute broker
    zactor_t* malamute = zactor_new(mlm_server, const_cast<char*>("Malamute"));
    REQUIRE(malamute);
    zstr_sendx(malamute, "BIND", endpoint, nullptr);

    mlm_client_t* client = mlm_client_new();
    REQUIRE(client);
    REQUIRE(mlm_client_connect(client, endpoint, 5000, "test-agent") == 0);

    zmsg_t* message = nullptr;

    StateManager manager;
    NUTAgent     nut_agent(manager.getReader());
    uint64_t     actor_polling = 0;

    // --------------------------------------------------------------
    FILE* fp = freopen(logErrPath, "w+", stderr);
    REQUIRE(fp);
    // empty message - expected fail
    message = zmsg_new();
    REQUIRE(message);
    int rv = actor_commands(client, &message, actor_polling, nut_agent);
    REQUIRE(rv == 0);
    CHECK(message == nullptr);
    CHECK(actor_polling == 0);
    CHECK(nut_agent.isMappingLoaded() == false);
    CHECK(nut_agent.TTL() == 60);

    STDERR_NON_EMPTY

    // --------------------------------------------------------------
    fp = freopen(logErrPath, "w+", stderr);
    REQUIRE(fp);
    // empty string - expected fail
    message = zmsg_new();
    REQUIRE(message);
    zmsg_addstr(message, "");
    rv = actor_commands(client, &message, actor_polling, nut_agent);
    REQUIRE(rv == 0);
    CHECK(message == nullptr);
    CHECK(actor_polling == 0);
    CHECK(nut_agent.isMappingLoaded() == false);
    CHECK(nut_agent.TTL() == 60);

    STDERR_NON_EMPTY

    // --------------------------------------------------------------
    fp = freopen(logErrPath, "w+", stderr);
    REQUIRE(fp);
    // unknown command - expected fail
    message = zmsg_new();
    REQUIRE(message);
    zmsg_addstr(message, "MAGIC!");
    rv = actor_commands(client, &message, actor_polling, nut_agent);
    REQUIRE(rv == 0);
    CHECK(message == nullptr);
    CHECK(actor_polling == 0);
    CHECK(nut_agent.isMappingLoaded() == false);
    CHECK(nut_agent.TTL() == 60);

    STDERR_NON_EMPTY

    // --------------------------------------------------------------
    fp = freopen(logErrPath, "w+", stderr);
    REQUIRE(fp);
    // CONFIGURE - expected fail
    message = zmsg_new();
    REQUIRE(message);
    zmsg_addstr(message, ACTION_CONFIGURE);
    // missing mapping_file here
    rv = actor_commands(client, &message, actor_polling, nut_agent);
    REQUIRE(rv == 0);
    CHECK(message == nullptr);
    CHECK(actor_polling == 0);
    CHECK(nut_agent.isMappingLoaded() == false);
    CHECK(nut_agent.TTL() == 60);

    STDERR_NON_EMPTY

    // --------------------------------------------------------------
    fp = freopen(logErrPath, "w+", stderr);
    REQUIRE(fp);
    // POLLING - expected fail
    message = zmsg_new();
    REQUIRE(message);
    zmsg_addstr(message, ACTION_POLLING);
    // missing value here
    rv = actor_commands(client, &message, actor_polling, nut_agent);
    REQUIRE(rv == 0);
    CHECK(message == nullptr);
    CHECK(actor_polling == 0);
    CHECK(nut_agent.isMappingLoaded() == false);
    CHECK(nut_agent.TTL() == 60);

    STDERR_NON_EMPTY

    // --------------------------------------------------------------
    fp = freopen(logErrPath, "w+", stderr);
    REQUIRE(fp);
    // POLLING - expected fail (in a sense)
    message = zmsg_new();
    REQUIRE(message);
    zmsg_addstr(message, ACTION_POLLING);
    zmsg_addstr(message, "a14s2"); // Bad value
    rv = actor_commands(client, &message, actor_polling, nut_agent);
    REQUIRE(rv == 0);
    CHECK(message == nullptr);
    CHECK(actor_polling == 30000);
    CHECK(nut_agent.isMappingLoaded() == false);
    CHECK(nut_agent.TTL() == 60);

    STDERR_NON_EMPTY

    // The original client still waiting on the bad endpoint for malamute
    // server to show up. Therefore we must destroy and create it again.
    mlm_client_destroy(&client);
    client = mlm_client_new();
    REQUIRE(client);
    // re-set actor_polling to zero again (so we don't have to remember
    // to assert to the previous value)
    actor_polling = 0;

    // Prepare the error logger
    fp = freopen(logErrPath, "w+", stderr);
    REQUIRE(fp);

    // CONFIGURE
    message = zmsg_new();
    REQUIRE(message);
    zmsg_addstr(message, ACTION_CONFIGURE);
    zmsg_addstr(message, SELFTEST_RO "/mapping.conf");
    rv = actor_commands(client, &message, actor_polling, nut_agent);
    REQUIRE(rv == 0);
    CHECK(message == nullptr);
    CHECK(actor_polling == 0);
    CHECK(nut_agent.isMappingLoaded() == true);
    CHECK(nut_agent.TTL() == 60);

    // $TERM
    message = zmsg_new();
    REQUIRE(message);
    zmsg_addstr(message, "$TERM");
    rv = actor_commands(client, &message, actor_polling, nut_agent);
    REQUIRE(rv == 1);
    CHECK(message == nullptr);
    CHECK(actor_polling == 0);
    CHECK(nut_agent.isMappingLoaded() == true);
    CHECK(nut_agent.TTL() == 60);

    // POLLING
    message = zmsg_new();
    REQUIRE(message);
    zmsg_addstr(message, ACTION_POLLING);
    zmsg_addstr(message, "150");
    rv = actor_commands(client, &message, actor_polling, nut_agent);
    REQUIRE(rv == 0);
    CHECK(message == nullptr);
    CHECK(actor_polling == 150000);
    CHECK(nut_agent.isMappingLoaded() == true);
    CHECK(nut_agent.TTL() == 300);

    STDERR_NON_EMPTY

    zmsg_destroy(&message);
    mlm_client_destroy(&client);
    zactor_destroy(&malamute);
}
