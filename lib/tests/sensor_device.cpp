#include <catch2/catch.hpp>
#include <fty_proto.h>
#include "src/asset_state.h"
#include "src/sensor_list.h"

TEST_CASE("sensor device test")
{
    //  @selftest
    // epdu master
    fty_proto_t* proto = fty_proto_new(FTY_PROTO_ASSET);
    REQUIRE(proto);
    fty_proto_set_name(proto, "epdu_m");
    fty_proto_set_operation(proto, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert(proto, "type", "device");
    fty_proto_aux_insert(proto, "subtype", "epdu");
    fty_proto_aux_insert(proto, "parent_name.1", "ups");
    fty_proto_ext_insert(proto, "daisy_chain", "1");
    AssetState::Asset epdu_m(proto);
    fty_proto_destroy(&proto);
    // epdu slave #1
    proto = fty_proto_new(FTY_PROTO_ASSET);
    REQUIRE(proto);
    fty_proto_set_name(proto, "epdu_1");
    fty_proto_set_operation(proto, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert(proto, "type", "device");
    fty_proto_aux_insert(proto, "subtype", "epdu");
    fty_proto_aux_insert(proto, "parent_name.1", "ups");
    fty_proto_ext_insert(proto, "daisy_chain", "2");
    AssetState::Asset epdu_1(proto);
    fty_proto_destroy(&proto);

    std::map<std::string, std::string> children;

    // sensor emp01 connected to standalone ups
    proto = fty_proto_new(FTY_PROTO_ASSET);
    REQUIRE(proto);
    fty_proto_set_name(proto, "a");
    fty_proto_set_operation(proto, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert(proto, "type", "device");
    fty_proto_aux_insert(proto, "subtype", "sensor");
    fty_proto_aux_insert(proto, "parent_name.1", "ups");
    AssetState::Asset asset_a(proto);
    fty_proto_destroy(&proto);
    Sensor a(&asset_a, nullptr, children);
    CHECK(a.sensorPrefix() == "ambient.");
    CHECK(a.topicSuffix() == ".0@ups");
    CHECK(a.nutPrefix() == "ambient.");
    CHECK(a.nutIndex() == 0);

    // sensor emp02 connected to standalone ups
    proto = fty_proto_new(FTY_PROTO_ASSET);
    REQUIRE(proto);
    fty_proto_set_name(proto, "b");
    fty_proto_set_operation(proto, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert(proto, "type", "device");
    fty_proto_aux_insert(proto, "subtype", "sensor");
    fty_proto_aux_insert(proto, "parent_name.1", "ups");
    fty_proto_ext_insert(proto, "port", "2");
    fty_proto_ext_insert(proto, "endpoint.1.sub_address", "1");
    AssetState::Asset asset_b(proto);
    fty_proto_destroy(&proto);
    Sensor b(&asset_b, nullptr, children, "ups", 2);
    CHECK(b.sensorPrefix() == "ambient.2.");
    CHECK(b.topicSuffix() == ".2@ups");
    CHECK(b.nutPrefix() == "ambient.2.");
    CHECK(b.nutIndex() == 2);
    CHECK(b.subAddress() == "1");

    // sensor emp01 connected to daisy-chain host
    proto = fty_proto_new(FTY_PROTO_ASSET);
    REQUIRE(proto);
    fty_proto_set_name(proto, "c");
    fty_proto_set_operation(proto, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert(proto, "type", "device");
    fty_proto_aux_insert(proto, "subtype", "sensor");
    fty_proto_aux_insert(proto, "parent_name.1", "epdu_m");
    fty_proto_ext_insert(proto, "endpoint.1.sub_address", "2");
    AssetState::Asset asset_c(proto);
    fty_proto_destroy(&proto);
    Sensor c(&asset_c, &epdu_m, children, "epdu_m", 0);
    CHECK(c.sensorPrefix() == "device.1.ambient.");
    CHECK(c.topicSuffix() == ".0@epdu_m");
    CHECK(c.nutPrefix() == "device.1.ambient.");
    CHECK(c.nutIndex() == 1);
    CHECK(c.subAddress() == "2");

    // sensor emp01 connected to daisy-chain device 1
    proto = fty_proto_new(FTY_PROTO_ASSET);
    REQUIRE(proto);
    fty_proto_set_name(proto, "d");
    fty_proto_set_operation(proto, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert(proto, "type", "device");
    fty_proto_aux_insert(proto, "subtype", "sensor");
    fty_proto_aux_insert(proto, "parent_name.1", "epdu_1");
    fty_proto_ext_insert(proto, "endpoint.1.sub_address", "2");
    AssetState::Asset asset_d(proto);
    fty_proto_destroy(&proto);
    Sensor d(&asset_d, &epdu_1, children, "epdu_m", 0);
    CHECK(d.sensorPrefix() == "device.2.ambient.");
    CHECK(d.topicSuffix() == ".0@epdu_1");
    CHECK(d.nutPrefix() == "device.2.ambient.");
    CHECK(d.nutIndex() == 2);
    CHECK(d.subAddress() == "2");

    // sensor emp02 connected to daisy-chain master
    proto = fty_proto_new(FTY_PROTO_ASSET);
    REQUIRE(proto);
    fty_proto_set_name(proto, "e");
    fty_proto_set_operation(proto, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert(proto, "type", "device");
    fty_proto_aux_insert(proto, "subtype", "sensor");
    fty_proto_aux_insert(proto, "parent_name.1", "epdu_m");
    fty_proto_ext_insert(proto, "port", "3");
    fty_proto_ext_insert(proto, "endpoint.1.sub_address", "8");
    AssetState::Asset asset_e(proto);
    fty_proto_destroy(&proto);
    Sensor e(&asset_e, &epdu_m, children, "epdu_m", 3);
    CHECK(e.sensorPrefix() == "device.1.ambient.3.");
    CHECK(e.topicSuffix() == ".3@epdu_m");
    CHECK(e.nutPrefix() == "device.1.ambient.3.");
    CHECK(e.nutIndex() == 3);
    CHECK(e.subAddress() == "8");

    // sensor emp02 connected to daisy-chain device 1
    proto = fty_proto_new(FTY_PROTO_ASSET);
    REQUIRE(proto);
    fty_proto_set_name(proto, "f");
    fty_proto_set_operation(proto, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert(proto, "type", "device");
    fty_proto_aux_insert(proto, "subtype", "sensor");
    fty_proto_aux_insert(proto, "parent_name.1", "epdu_1");
    fty_proto_ext_insert(proto, "port", "5");
    fty_proto_ext_insert(proto, "endpoint.1.sub_address", "12");
    AssetState::Asset asset_f(proto);
    fty_proto_destroy(&proto);
    Sensor f(&asset_f, &epdu_1, children, "epdu_m", 5);
    CHECK(f.sensorPrefix() == "device.2.ambient.5.");
    CHECK(f.topicSuffix() == ".5@epdu_1");
    CHECK(f.nutPrefix() == "device.1.ambient.5.");
    CHECK(f.nutIndex() == 5);
    CHECK(f.subAddress() == "12");
}
