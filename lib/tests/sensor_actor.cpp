#include "src/sensor_list.h"
#include "src/state_manager.h"
#include <catch2/catch.hpp>
#include <fty_proto.h>
#include <malamute.h>

TEST_CASE("sensor actor test")
{
    //  @selftest
    static const char* endpoint = "inproc://fty-sensor-actor-test.8df1z4";

    // malamute broker
    zactor_t* malamute = zactor_new(mlm_server, const_cast<char*>("Malamute"));
    REQUIRE(malamute);
    zstr_sendx(malamute, "BIND", endpoint, NULL);

    mlm_client_t* consumer = mlm_client_new();
    REQUIRE(consumer);
    mlm_client_connect(consumer, endpoint, 1000, "sensor-client");
    mlm_client_set_consumer(consumer, FTY_PROTO_STREAM_METRICS_SENSOR, ".*");

    mlm_client_t* producer = mlm_client_new();
    REQUIRE(producer);
    mlm_client_connect(producer, endpoint, 1000, "sensor-producer");
    mlm_client_set_producer(producer, FTY_PROTO_STREAM_METRICS_SENSOR);

    StateManager                       manager;
    Sensors                            sensors(manager.getReader());
    std::map<std::string, std::string> children;
    fty_proto_t*                       proto = fty_proto_new(FTY_PROTO_ASSET);
    REQUIRE(proto);
    fty_proto_set_name(proto, "sensor-1");
    fty_proto_set_operation(proto, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert(proto, "type", "device");
    fty_proto_aux_insert(proto, "subtype", "sensor");
    fty_proto_aux_insert(proto, "parent_name.1", "PRG");
    fty_proto_ext_insert(proto, "port", "1");
    fty_proto_ext_insert(proto, "endpoint.1.sub_address", "1");
    AssetState::Asset asset1(proto);
    fty_proto_destroy(&proto);
    sensors.sensors()["sensor1"] = Sensor(&asset1, nullptr, children, "nut", 1);
    sensors.sensors()["sensor1"].setHumidity("50");

    sensors.publish(producer, 300);

    zmsg_t* msg = mlm_client_recv(consumer);
    REQUIRE(msg);
    fty_proto_t* bmsg = fty_proto_decode(&msg);
    REQUIRE(bmsg);
    CHECK(streq(fty_proto_value(bmsg), "50"));
    CHECK(streq(fty_proto_type(bmsg), "humidity.1"));
    CHECK(fty_proto_ttl(bmsg) == 300);
    fty_proto_destroy(&bmsg);

    sensors.sensors()["sensor1"].setTemperature("28");
    sensors.sensors()["sensor1"].setHumidity("51");

    sensors.publish(producer, 300);

    msg = mlm_client_recv(consumer);
    REQUIRE(msg);
    bmsg = fty_proto_decode(&msg);
    REQUIRE(bmsg);
    fty_proto_print(bmsg);
    CHECK(streq(fty_proto_value(bmsg), "28"));
    CHECK(streq(fty_proto_type(bmsg), "temperature.1"));
    fty_proto_destroy(&bmsg);

    msg = mlm_client_recv(consumer);
    REQUIRE(msg);
    bmsg = fty_proto_decode(&msg);
    REQUIRE(bmsg);
    fty_proto_print(bmsg);
    CHECK(streq(fty_proto_value(bmsg), "51"));
    CHECK(streq(fty_proto_type(bmsg), "humidity.1"));
    fty_proto_destroy(&bmsg);

    sensors.sensors()["sensor1"].setInventory(
        {{"ambient.model", "Model 1"}, {"ambient.serial", "1111"}, {"ambient.name", "Ambient 1"}});
    sensors.advertiseInventory(producer);
    msg = mlm_client_recv(consumer);
    REQUIRE(msg);
    bmsg = fty_proto_decode(&msg);
    REQUIRE(bmsg);
    fty_proto_print(bmsg);
    REQUIRE(fty_proto_ext_size(bmsg) == 3);
    CHECK(streq(fty_proto_ext_string(bmsg, "ambient.model", ""), "Model 1"));
    CHECK(streq(fty_proto_ext_string(bmsg, "ambient.serial", ""), "1111"));
    CHECK(streq(fty_proto_ext_string(bmsg, "ambient.name", ""), "Ambient 1"));
    fty_proto_destroy(&bmsg);

    // gpio on EMP001
    std::vector<std::string> contacts;
    children.emplace("1", "sensorgpio-1");
    children.emplace("2", "sensorgpio-2");
    contacts.push_back("open");
    contacts.push_back("close");

    proto = fty_proto_new(FTY_PROTO_ASSET);
    REQUIRE(proto);
    fty_proto_set_name(proto, "sensor-2");
    fty_proto_set_operation(proto, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert(proto, "type", "device");
    fty_proto_aux_insert(proto, "subtype", "sensor");
    fty_proto_aux_insert(proto, "parent_name.1", "PRG");
    fty_proto_ext_insert(proto, "port", "4");
    fty_proto_ext_insert(proto, "endpoint.1.sub_address", "2");
    AssetState::Asset asset2(proto);
    fty_proto_destroy(&proto);
    sensors.sensors()["sensor1"] = Sensor(&asset2, nullptr, children, "nut", 4);
    sensors.sensors()["sensor1"].setContacts(contacts);

    sensors.publish(producer, 300);

    msg = mlm_client_recv(consumer);
    REQUIRE(msg);
    bmsg = fty_proto_decode(&msg);
    REQUIRE(bmsg);
    fty_proto_print(bmsg);
    CHECK(streq(fty_proto_type(bmsg), "status.GPI1.4"));
    fty_proto_destroy(&bmsg);

    msg = mlm_client_recv(consumer);
    REQUIRE(msg);
    bmsg = fty_proto_decode(&msg);
    REQUIRE(bmsg);
    fty_proto_print(bmsg);
    CHECK(streq(fty_proto_type(bmsg), "status.GPI2.4"));
    fty_proto_destroy(&bmsg);

    mlm_client_destroy(&producer);
    mlm_client_destroy(&consumer);
    zactor_destroy(&malamute);
}
