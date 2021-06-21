#include <catch2/catch.hpp>
#include "src/state_manager.h"
#include "src/sensor_list.h"
#include <fty_proto.h>
#include <nutclient.h>

TEST_CASE("sensor list test", "[.]")
{
    StateManager manager;
    auto&        writer = manager.getWriter();

    fty_proto_t* asset = fty_proto_new(FTY_PROTO_ASSET);
    REQUIRE(asset);
    fty_proto_set_name(asset, "ups-1");
    fty_proto_set_operation(asset, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert(asset, "type", "device");
    fty_proto_aux_insert(asset, "subtype", "ups");
    fty_proto_ext_insert(asset, "ip.1", "1.1.1.1");
    writer.getState().updateFromProto(asset);
    fty_proto_destroy(&asset);

    asset = fty_proto_new(FTY_PROTO_ASSET);
    REQUIRE(asset);
    fty_proto_set_name(asset, "sensor-1");
    fty_proto_set_operation(asset, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert(asset, "type", "device");
    fty_proto_aux_insert(asset, "subtype", "sensor");
    fty_proto_aux_insert(asset, "parent_name.1", "ups-1");
    writer.getState().updateFromProto(asset);
    fty_proto_destroy(&asset);

    asset = fty_proto_new(FTY_PROTO_ASSET);
    REQUIRE(asset);
    fty_proto_set_name(asset, "epdu-1");
    fty_proto_set_operation(asset, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert(asset, "type", "device");
    fty_proto_aux_insert(asset, "subtype", "epdu");
    fty_proto_ext_insert(asset, "ip.1", "1.1.1.2");
    fty_proto_ext_insert(asset, "daisy_chain", "1");
    writer.getState().updateFromProto(asset);
    fty_proto_destroy(&asset);

    asset = fty_proto_new(FTY_PROTO_ASSET);
    REQUIRE(asset);
    fty_proto_set_name(asset, "epdu-2");
    fty_proto_set_operation(asset, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert(asset, "type", "device");
    fty_proto_aux_insert(asset, "subtype", "epdu");
    fty_proto_ext_insert(asset, "ip.1", "1.1.1.2");
    fty_proto_ext_insert(asset, "daisy_chain", "2");
    writer.getState().updateFromProto(asset);
    fty_proto_destroy(&asset);

    asset = fty_proto_new(FTY_PROTO_ASSET);
    REQUIRE(asset);
    fty_proto_set_name(asset, "sensor-2");
    fty_proto_set_operation(asset, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert(asset, "type", "device");
    fty_proto_aux_insert(asset, "subtype", "sensor");
    fty_proto_aux_insert(asset, "parent_name.1", "epdu-2");
    fty_proto_ext_insert(asset, "port", "5");
    writer.getState().updateFromProto(asset);
    fty_proto_destroy(&asset);

    asset = fty_proto_new(FTY_PROTO_ASSET);
    REQUIRE(asset);
    fty_proto_set_name(asset, "sensorgpio-1");
    fty_proto_set_operation(asset, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert(asset, "type", "device");
    fty_proto_aux_insert(asset, "subtype", "sensorgpio");
    fty_proto_aux_insert(asset, "parent_name.1", "sensor-2");
    fty_proto_aux_insert(asset, "parent_name.2", "epdu-2");
    fty_proto_ext_insert(asset, "port", "1");
    writer.getState().updateFromProto(asset);
    fty_proto_destroy(&asset);

    writer.commit();

    nut::TcpClient nutClient;
    nutClient.connect("localhost", 3493);
    Sensors list(manager.getReader());
    list.updateSensorList(nutClient, nullptr);
    nutClient.disconnect();
    REQUIRE(list.sensors().size() == 2);

    CHECK(list.sensors()["sensor-1"].sensorPrefix() == "ambient.");
    CHECK(list.sensors()["sensor-1"].topicSuffix() == ".0@ups-1");
    CHECK(list.sensors()["sensor-1"].nutPrefix() == "ambient.");
    CHECK(list.sensors()["sensor-1"].nutIndex() == 0);
    CHECK(list.sensors()["sensor-1"].location() == "ups-1");
    CHECK(list.sensors()["sensor-1"].subAddress() == "");

    CHECK(list.sensors()["sensor-2"].sensorPrefix() == "device.2.ambient.5.");
    CHECK(list.sensors()["sensor-2"].topicSuffix() == ".5@epdu-2");
    CHECK(list.sensors()["sensor-2"].nutPrefix() == "device.1.ambient.5.");
    CHECK(list.sensors()["sensor-2"].nutIndex() == 5);
    CHECK(list.sensors()["sensor-2"].location() == "epdu-2");
    CHECK(list.sensors()["sensor-2"].subAddress() == "");
    CHECK(list.sensors()["sensor-2"].chain() == 2);
}
