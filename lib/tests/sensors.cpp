#include <catch2/catch.hpp>
#include "src/state_manager.h"
#include "src/sensor_list.h"
#include <fty_proto.h>
#include <nutclientmem.h>

TEST_CASE("sensor list test", "")
{
    bool verbose = true;

    printf (" * sensor_list: ");

    StateManager manager;
    auto& writer = manager.getWriter();

    // [ups-1]: UPS mono
    fty_proto_t *asset = fty_proto_new (FTY_PROTO_ASSET);
    fty_proto_set_name (asset, "ups-1");
    fty_proto_set_operation (asset, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert (asset, "type", "device");
    fty_proto_aux_insert (asset, "subtype", "ups");
    fty_proto_ext_insert (asset, "ip.1", "1.1.1.1");
    writer.getState().updateFromProto(asset);
    fty_proto_destroy(&asset);

    // [sensor-1]: Sensor EMP01 connected to ups-1
    asset = fty_proto_new (FTY_PROTO_ASSET);
    fty_proto_set_name (asset, "sensor-1");
    fty_proto_set_operation (asset, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert (asset, "type", "device");
    fty_proto_aux_insert (asset, "subtype", "sensor");
    fty_proto_aux_insert (asset, "parent_name.1", "ups-1");
    fty_proto_ext_insert (asset, "port", "0");
    writer.getState().updateFromProto(asset);
    fty_proto_destroy(&asset);

    // [epdu-1]: EPDU daisy chain - master
    asset = fty_proto_new (FTY_PROTO_ASSET);
    fty_proto_set_name (asset, "epdu-1");
    fty_proto_set_operation (asset, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert (asset, "type", "device");
    fty_proto_aux_insert (asset, "subtype", "epdu");
    fty_proto_ext_insert (asset, "ip.1", "1.1.1.2");
    fty_proto_ext_insert (asset, "daisy_chain", "1");
    fty_proto_ext_insert (asset, "serial_no", "1111");
    writer.getState().updateFromProto(asset);
    fty_proto_destroy(&asset);

    // [epdu-2]: EPDU daisy chain - slave #1
    asset = fty_proto_new (FTY_PROTO_ASSET);
    fty_proto_set_name (asset, "epdu-2");
    fty_proto_set_operation (asset, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert (asset, "type", "device");
    fty_proto_aux_insert (asset, "subtype", "epdu");
    fty_proto_ext_insert (asset, "ip.1", "1.1.1.2");
    fty_proto_ext_insert (asset, "daisy_chain", "2");
    fty_proto_ext_insert (asset, "serial_no", "2222");
    writer.getState().updateFromProto(asset);
    fty_proto_destroy(&asset);

    // [sensor-2]: EMP02 sensor without sub address connected on epdu-2 (port=2)
    asset = fty_proto_new (FTY_PROTO_ASSET);
    fty_proto_set_name (asset, "sensor-2");
    fty_proto_set_operation (asset, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert (asset, "type", "device");
    fty_proto_aux_insert (asset, "subtype", "sensor");
    fty_proto_aux_insert (asset, "parent_name.1", "epdu-1");
    fty_proto_ext_insert (asset, "port", "2");
    writer.getState().updateFromProto(asset);
    fty_proto_destroy(&asset);

    // [sensor-3]: Sensor EMP02 with sub address connected on epdu-2 (sub_address=3)
    asset = fty_proto_new (FTY_PROTO_ASSET);
    fty_proto_set_name (asset, "sensor-3");
    fty_proto_set_operation (asset, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert (asset, "type", "device");
    fty_proto_aux_insert (asset, "subtype", "sensor");
    fty_proto_aux_insert (asset, "parent_name.1", "epdu-2");
    fty_proto_ext_insert(asset, "endpoint.1.sub_address", "3");
    writer.getState().updateFromProto(asset);
    fty_proto_destroy(&asset);

    // [sensorgpio-1]: Sensor gpio connected on sensor-2 (port=1)
    asset = fty_proto_new (FTY_PROTO_ASSET);
    fty_proto_set_name (asset, "sensorgpio-1");
    fty_proto_set_operation (asset, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert (asset, "type", "device");
    fty_proto_aux_insert (asset, "subtype", "sensorgpio");
    fty_proto_aux_insert (asset, "parent_name.1", "sensor-2");
    fty_proto_aux_insert (asset, "parent_name.2", "epdu-2");
    fty_proto_ext_insert (asset, "port", "1");
    writer.getState().updateFromProto(asset);
    fty_proto_destroy(&asset);

    writer.commit();

    nut::MemClientStub nutClient;

    auto setDeviceValue = [&nutClient](const std::string& device, const std::string& name, const std::string& value) -> void
    {
        nutClient.setDeviceVariable(device, name, value);
        std::vector<std::string> list_value = nutClient.getDeviceVariableValue(device, name);
        CHECK (list_value.size() == 1);
        CHECK (list_value[0] == value);
    };

	// Set values on epdu master
    setDeviceValue("epdu-1", "device.1.ambient.count", "3");
    setDeviceValue("epdu-1", "device.1.ambient.1.address", "1");
    setDeviceValue("epdu-1", "device.1.ambient.1.parent.serial", "1111");
    setDeviceValue("epdu-1", "device.1.ambient.2.address", "2");
    setDeviceValue("epdu-1", "device.1.ambient.2.parent.serial", "2222");
    setDeviceValue("epdu-1", "device.1.ambient.3.address", "3");
    setDeviceValue("epdu-1", "device.1.ambient.3.parent.serial", "2222");

    Sensors list(manager.getReader());
    list.updateSensorList (nutClient, nullptr);

    if (verbose) {
        printf("\n");
        printf("-- sensors map\n");
        for (auto& it : list.sensors()) {
            printf("%s\n", it.first.c_str());
        }

        printf("--\n");
        printf("list._sensors[\"sensor-1\"].sensorPrefix(): '%s'\n", list.sensors()["sensor-1"].sensorPrefix().c_str());
        printf("list._sensors[\"sensor-1\"].topicSuffix(): '%s'\n", list.sensors()["sensor-1"].topicSuffix().c_str());
        printf("list._sensors[\"sensor-1\"].nutPrefix(): '%s'\n", list.sensors()["sensor-1"].nutPrefix().c_str());
        printf("list._sensors[\"sensor-1\"].nutIndex(): %d\n", list.sensors()["sensor-1"].nutIndex());
        printf("list._sensors[\"sensor-1\"].location(): '%s'\n", list.sensors()["sensor-1"].location().c_str());
        printf("list._sensors[\"sensor-1\"].subAddress(): '%s'\n", list.sensors()["sensor-1"].subAddress().c_str());

        printf("--\n");
        printf("list._sensors[\"sensor-2\"].sensorPrefix(): '%s'\n", list.sensors()["sensor-2"].sensorPrefix().c_str());
        printf("list._sensors[\"sensor-2\"].topicSuffix(): '%s'\n", list.sensors()["sensor-2"].topicSuffix().c_str());
        printf("list._sensors[\"sensor-2\"].nutPrefix(): '%s'\n", list.sensors()["sensor-2"].nutPrefix().c_str());
        printf("list._sensors[\"sensor-2\"].nutIndex(): %d\n", list.sensors()["sensor-2"].nutIndex());
        printf("list._sensors[\"sensor-2\"].location(): '%s'\n", list.sensors()["sensor-2"].location().c_str());
        printf("list._sensors[\"sensor-2\"].subAddress(): '%s'\n", list.sensors()["sensor-2"].subAddress().c_str());
        printf("list._sensors[\"sensor-2\"].chain(): %d\n", list.sensors()["sensor-2"].chain());
    }

    CHECK (list.sensors().size() == 3);

    CHECK (list.sensors()["sensor-1"].sensorPrefix() == "ambient.");
    CHECK (list.sensors()["sensor-1"].topicSuffix() == ".0@ups-1");
    CHECK (list.sensors()["sensor-1"].nutPrefix() == "ambient.");
    CHECK (list.sensors()["sensor-1"].nutIndex() == 0);
    CHECK (list.sensors()["sensor-1"].location() == "ups-1");
    CHECK (list.sensors()["sensor-1"].subAddress() == "");

    CHECK (list.sensors()["sensor-2"].sensorPrefix() == "device.2.ambient.2.");
    CHECK (list.sensors()["sensor-2"].topicSuffix() == ".2@epdu-2");
    CHECK (list.sensors()["sensor-2"].nutPrefix() == "device.1.ambient.2.");
    CHECK (list.sensors()["sensor-2"].nutIndex() == 2);
    CHECK (list.sensors()["sensor-2"].location() == "epdu-2");
    CHECK (list.sensors()["sensor-2"].subAddress() == "2");
    CHECK (list.sensors()["sensor-2"].chain() == 2);

    CHECK (list.sensors()["sensor-3"].sensorPrefix() == "device.2.ambient.");  // no port bad value
    CHECK (list.sensors()["sensor-3"].topicSuffix() == ".3@epdu-2");
    CHECK (list.sensors()["sensor-3"].nutPrefix() == "device.1.ambient.3.");
    CHECK (list.sensors()["sensor-3"].nutIndex() == 3);
    CHECK (list.sensors()["sensor-3"].location() == "epdu-2");
    CHECK (list.sensors()["sensor-3"].subAddress() == "3");
    CHECK (list.sensors()["sensor-3"].chain() == 2);

    //  @end
    printf ("OK\n");
}
