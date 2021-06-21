#include <catch2/catch.hpp>
#include <malamute.h>
#include <fty_proto.h>
#include "src/asset_state.h"
#include "src/state_manager.h"
#include "src/alert_device_list.h"


TEST_CASE("alert actor test")
{
    static const char* endpoint = "ipc://fty-alert-actor";

    // malamute broker
    zactor_t* malamute = zactor_new(mlm_server, const_cast<char*>("Malamute"));
    REQUIRE(malamute);
    zstr_sendx(malamute, "BIND", endpoint, NULL);

    fty_proto_t* msg = fty_proto_new(FTY_PROTO_ASSET);
    REQUIRE(msg);
    fty_proto_set_name(msg, "mydevice");
    fty_proto_set_operation(msg, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert(msg, "type", "device");
    fty_proto_aux_insert(msg, "subtype", "ups");
    fty_proto_ext_insert(msg, "ip.1", "192.0.2.1");
    std::shared_ptr<AssetState::Asset> asset(new AssetState::Asset(msg));
    fty_proto_destroy(&msg);

    Device                                          dev(asset);
    std::map<std::string, std::vector<std::string>> alerts = {
        {"ambient.temperature.status", {"critical-high", "", ""}},
        {"ambient.temperature.high.warning", {"80", "", ""}},
        {"ambient.temperature.high.critical", {"100", "", ""}},
        {"ambient.temperature.low.warning", {"10", "", ""}},
        {"ambient.temperature.low.critical", {"5", "", ""}},
    };
    dev.addAlert("ambient.temperature", alerts);
    dev.alerts()["ambient.temperature"].status = "critical-high";
    StateManager manager;
    Devices      devs(manager.getReader());
    devs.devices()["mydevice"] = dev;

    mlm_client_t* client = mlm_client_new();
    REQUIRE(client);
    mlm_client_connect(client, endpoint, 1000, "agent-nut-alert");
    mlm_client_set_producer(client, FTY_PROTO_STREAM_ALERTS_SYS);

    mlm_client_t* rfc_evaluator = mlm_client_new();
    REQUIRE(rfc_evaluator);
    mlm_client_connect(rfc_evaluator, endpoint, 1000, "fty-alert-engine");

    mlm_client_t* alert_list = mlm_client_new();
    REQUIRE(alert_list);
    mlm_client_connect(alert_list, endpoint, 1000, "alert-list");
    mlm_client_set_consumer(alert_list, FTY_PROTO_STREAM_ALERTS_SYS, ".*");

    zpoller_t* poller = zpoller_new(
        mlm_client_msgpipe(client), mlm_client_msgpipe(rfc_evaluator), mlm_client_msgpipe(alert_list), NULL);
    REQUIRE(poller);

    mlm_client_sendtox(rfc_evaluator, "agent-nut-alert", "rfc-evaluator-rules", "OK", NULL);
    devs.publishRules(client);

    // check rule message
    {
        // recvrule
        void* which = zpoller_wait(poller, 1000);
        CHECK(which);
        zmsg_t* msg1 = mlm_client_recv(rfc_evaluator);
        REQUIRE(msg1);
        CHECK(streq(mlm_client_subject(rfc_evaluator), "rfc-evaluator-rules"));

        // rule command
        char* item = zmsg_popstr(msg1);
        CHECK(item);
        CHECK(streq(item, "ADD"));
        zstr_free(&item);

        // rule json
        item = zmsg_popstr(msg1);
        CHECK(item);
        CHECK(item[0] == '{');
        zstr_free(&item);

        zmsg_destroy(&msg1);
    }
    // check alert message
    devs.publishAlerts(client);
    {
        // receive alert
        void* which = zpoller_wait(poller, 1000);
        CHECK(which);
        zmsg_t* msg1 = mlm_client_recv(alert_list);
        REQUIRE(msg1);
        CHECK(fty_proto_is(msg1));
        fty_proto_t* bp = fty_proto_decode(&msg1);
        CHECK(bp);

        // is alert
        CHECK(streq(fty_proto_command(bp), "ALERT"));

        // is active
        CHECK(streq(fty_proto_state(bp), "ACTIVE"));

        // severity
        CHECK(streq(fty_proto_severity(bp), "CRITICAL"));

        // element
        CHECK(streq(fty_proto_name(bp), "mydevice"));

        fty_proto_destroy(&bp);
        zmsg_destroy(&msg1);
    }
    devs.devices()["mydevice"].alerts()["ambient.temperature"].status = "good";
    devs.publishAlerts(client);
    // check alert message
    {
        // receive resolved
        void* which = zpoller_wait(poller, 1000);
        REQUIRE(which);
        zmsg_t* msg1 = mlm_client_recv(alert_list);
        REQUIRE(msg1);
        CHECK(fty_proto_is(msg1));
        fty_proto_t* bp = fty_proto_decode(&msg1);
        CHECK(bp);
        CHECK(streq(fty_proto_command(bp), "ALERT"));

        // is resolved
        CHECK(streq(fty_proto_state(bp), "RESOLVED"));

        fty_proto_destroy(&bp);
        zmsg_destroy(&msg1);
    }

    zpoller_destroy(&poller);
    mlm_client_destroy(&client);
    mlm_client_destroy(&alert_list);
    mlm_client_destroy(&rfc_evaluator);
    zactor_destroy(&malamute);
}
