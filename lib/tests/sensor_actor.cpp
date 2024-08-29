#include "src/sensor_list.h"
#include "src/state_manager.h"
#include <catch2/catch.hpp>
#include <fty_proto.h>
#include <fty_shm.h>
#include <malamute.h>

TEST_CASE("sensor actor test")
{
    //  @selftest
    static const char* endpoint = "inproc://fty-sensor-actor-test.8df1z4";
    static const char* FTY_PROTO_STREAM_ASSETS_TEST = "ASSETS_TEST";

    const char* SELFTEST_DIR_RW = ".";

    fty_shm_set_test_dir(SELFTEST_DIR_RW);
    fty_shm_set_default_polling_interval(2);

    // malamute broker
    zactor_t* malamute = zactor_new(mlm_server, const_cast<char*>("Malamute"));
    REQUIRE(malamute);
    zstr_sendx(malamute, "BIND", endpoint, NULL);

    mlm_client_t* consumer = mlm_client_new();
    REQUIRE(consumer);
    mlm_client_connect(consumer, endpoint, 1000, "sensor-client");
    mlm_client_set_consumer(consumer, FTY_PROTO_STREAM_ASSETS_TEST, ".*");

    mlm_client_t* producer = mlm_client_new();
    REQUIRE(producer);
    mlm_client_connect(producer, endpoint, 1000, "sensor-producer");
    mlm_client_set_producer(producer, FTY_PROTO_STREAM_ASSETS_TEST);

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

    sensors.publish(300);

    // wait calculation
    sleep(1);

    {
        fty_proto_t* m;
        fty::shm::shmMetrics resultT;
        fty::shm::read_metrics("sensor-1", "humidity.*", resultT);
        REQUIRE(resultT.size() == 1);
        m = resultT.get(0);
        REQUIRE(m);
        fty_proto_print(m);
        CHECK(streq(fty_proto_value(m), "50"));
        CHECK(streq(fty_proto_type(m), "humidity.default"));
        CHECK(fty_proto_ttl(m) == 300);
        m = nullptr;
    }

    sensors.sensors()["sensor1"].setTemperature("28");
    sensors.sensors()["sensor1"].setHumidity("51");

    sensors.publish(300);

    // wait calculation
    sleep(1);

    {
        fty_proto_t* m;
        fty::shm::shmMetrics resultT;
        fty::shm::read_metrics("sensor-1", "humidity.*", resultT);
        REQUIRE(resultT.size() == 1);
        m = resultT.get(0);
        REQUIRE(m);
        fty_proto_print(m);
        CHECK(streq(fty_proto_value(m), "51"));
        CHECK(streq(fty_proto_type(m), "humidity.default"));
        CHECK(fty_proto_ttl(m) == 300);
        m = nullptr;
    }

    {
        fty_proto_t* m;
        fty::shm::shmMetrics resultT;
        fty::shm::read_metrics("sensor-1", "temperature.*", resultT);
        REQUIRE(resultT.size() == 1);
        m = resultT.get(0);
        REQUIRE(m);
        fty_proto_print(m);
        CHECK(streq(fty_proto_value(m), "28"));
        CHECK(streq(fty_proto_type(m), "temperature.default"));
        CHECK(fty_proto_ttl(m) == 300);
        m = nullptr;
    }

    sensors.sensors()["sensor1"].setInventory(
        {{"ambient.model", "Model 1"}, {"ambient.serial", "1111"}, {"ambient.name", "Ambient 1"}});
    sensors.advertiseInventory(producer);
    zmsg_t* msg = mlm_client_recv(consumer);
    REQUIRE(msg);
    fty_proto_t* bmsg = fty_proto_decode(&msg);
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

    sensors.publish(300);

    // wait calculation
    sleep(1);

    {
        fty_proto_t* m;
        fty::shm::shmMetrics resultT;
        fty::shm::read_metrics("sensorgpio-1", "status.*", resultT);
        REQUIRE(resultT.size() == 1);
        m = resultT.get(0);
        REQUIRE(m);
        fty_proto_print(m);
        CHECK(streq(fty_proto_value(m), "open"));
        CHECK(streq(fty_proto_type(m), "status.GPI1"));
        CHECK(fty_proto_ttl(m) == 300);
        m = nullptr;
    }

    {
        fty_proto_t* m;
        fty::shm::shmMetrics resultT;
        fty::shm::read_metrics("sensorgpio-2", "status.*", resultT);
        REQUIRE(resultT.size() == 1);
        m = resultT.get(0);
        REQUIRE(m);
        fty_proto_print(m);
        CHECK(streq(fty_proto_value(m), "close"));
        CHECK(streq(fty_proto_type(m), "status.GPI2"));
        CHECK(fty_proto_ttl(m) == 300);
        m = nullptr;
    }

    fty_shm_delete_test_dir();

    mlm_client_destroy(&producer);
    mlm_client_destroy(&consumer);
    zactor_destroy(&malamute);
}
