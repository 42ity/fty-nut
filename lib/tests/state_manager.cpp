#include "src/state_manager.h"
#include <catch2/catch.hpp>

TEST_CASE("state manager test")
{
    StateManager          manager;
    StateManager::Writer& writer  = manager.getWriter();
    StateManager::Reader* reader1 = manager.getReader();
    StateManager::Reader* reader2 = manager.getReader();

    {
        // Empty initial state
        assert(reader1->getState().getPowerDevices().empty());
        assert(reader1->getState().getSensors().empty());
        assert(reader2->getState().getPowerDevices().empty());
        assert(reader2->getState().getSensors().empty());
    }
    {
        // Add one asset
        fty_proto_t* msg = fty_proto_new(FTY_PROTO_ASSET);
        assert(msg);
        fty_proto_set_name(msg, "ups-1");
        fty_proto_set_operation(msg, FTY_PROTO_ASSET_OP_CREATE);
        fty_proto_aux_insert(msg, "type", "device");
        fty_proto_aux_insert(msg, "subtype", "ups");
        fty_proto_ext_insert(msg, "ip.1", "192.0.2.1");
        writer.getState().updateFromProto(msg);
        fty_proto_destroy(&msg);
        // Not yet committed
        assert(manager.states().size() == 1);
        assert(reader1->getState().getPowerDevices().empty());
        assert(reader1->getState().getSensors().empty());
        assert(reader2->getState().getPowerDevices().empty());
        assert(reader2->getState().getSensors().empty());
        writer.commit();
        assert(manager.states().size() == 2);
        // Readers did not refresh their view yet
        assert(reader1->getState().getPowerDevices().empty());
        assert(reader1->getState().getSensors().empty());
        assert(reader2->getState().getPowerDevices().empty());
        assert(reader2->getState().getSensors().empty());
        assert(reader1->refresh());
        // If there are no updates, refresh() must return false
        assert(reader1->refresh() == false);
        // The get{PowerDevices,Sensors} reference is invalidated by the
        // refresh() call. In real usage, this is not so much of a problem,
        // because we refresh at the beginning of the loop body. In the
        // test, we need to be careful.
        auto& devs1 = reader1->getState().getPowerDevices();
        assert(devs1.size() == 1);
        assert(devs1.count("ups-1") == 1);
        assert(devs1.at("ups-1")->IP() == "192.0.2.1");
        assert(reader1->getState().ip2master("192.0.2.1") == "ups-1");
        assert(reader1->getState().getSensors().empty());
        // reader2 did not update yet
        assert(reader2->getState().getPowerDevices().empty());
        assert(reader2->getState().getSensors().empty());
    }
    {
        // Add another asset
        fty_proto_t* msg = fty_proto_new(FTY_PROTO_ASSET);
        assert(msg);
        fty_proto_set_name(msg, "epdu-2");
        fty_proto_set_operation(msg, FTY_PROTO_ASSET_OP_CREATE);
        fty_proto_aux_insert(msg, "type", "device");
        fty_proto_aux_insert(msg, "subtype", "epdu");
        fty_proto_ext_insert(msg, "ip.1", "192.0.2.2");
        // update via encoded zmsg
        zmsg_t* zmsg = fty_proto_encode(&msg);
        writer.getState().updateFromMsg(zmsg);
        writer.commit();
        assert(manager.states().size() == 3);
        assert(reader1->refresh());
        auto& devs1 = reader1->getState().getPowerDevices();
        assert(devs1.size() == 2);
        assert(devs1.count("ups-1") == 1);
        assert(devs1.at("ups-1")->IP() == "192.0.2.1");
        assert(reader1->getState().ip2master("192.0.2.1") == "ups-1");
        assert(devs1.count("epdu-2") == 1);
        assert(devs1.at("epdu-2")->IP() == "192.0.2.2");
        assert(reader1->getState().ip2master("192.0.2.2") == "epdu-2");
        assert(reader1->getState().getSensors().empty());
        // reader2 is two steps behind
        assert(reader2->getState().getPowerDevices().empty());
        assert(reader2->getState().getSensors().empty());
        // Refresh reader2
        assert(reader2->refresh());
        auto& devs2 = reader2->getState().getPowerDevices();
        assert(devs2.size() == 2);
        assert(devs2.count("ups-1") == 1);
        assert(devs2.at("ups-1")->IP() == "192.0.2.1");
        assert(reader2->getState().ip2master("192.0.2.1") == "ups-1");
        assert(devs2.count("epdu-2") == 1);
        assert(devs2.at("epdu-2")->IP() == "192.0.2.2");
        assert(reader2->getState().ip2master("192.0.2.2") == "epdu-2");
        assert(reader2->getState().getSensors().empty());
    }

    // Force a cleanup and check that we discarded the two old states
    manager.cleanup();
    assert(manager.states().size() == 1);

    {
        // Delete an asset
        fty_proto_t* msg = fty_proto_new(FTY_PROTO_ASSET);
        assert(msg);
        fty_proto_set_name(msg, "epdu-2");
        fty_proto_set_operation(msg, FTY_PROTO_ASSET_OP_DELETE);
        writer.getState().updateFromProto(msg);
        fty_proto_destroy(&msg);
        writer.commit();
        // reader1 should not see it
        assert(reader1->refresh());
        auto& devs1 = reader1->getState().getPowerDevices();
        assert(devs1.size() == 1);
        assert(devs1.count("ups-1") == 1);
        assert(devs1.at("ups-1")->IP() == "192.0.2.1");
        assert(reader1->getState().ip2master("192.0.2.1") == "ups-1");
        assert(reader1->getState().ip2master("192.0.2.2") == "");
        assert(reader1->getState().getSensors().empty());
        // reader2 still sees epdu-2
        auto& devs2 = reader2->getState().getPowerDevices();
        assert(devs2.size() == 2);
        assert(devs2.count("ups-1") == 1);
        assert(devs2.at("ups-1")->IP() == "192.0.2.1");
        assert(reader2->getState().ip2master("192.0.2.1") == "ups-1");
        assert(devs2.count("epdu-2") == 1);
        assert(devs2.at("epdu-2")->IP() == "192.0.2.2");
        assert(reader2->getState().ip2master("192.0.2.2") == "epdu-2");
        assert(reader2->getState().getSensors().empty());

        // reader3 is late to the party but should see the latest state
        StateManager::Reader* reader3 = manager.getReader();
        auto&                 devs3   = reader3->getState().getPowerDevices();
        assert(devs3.size() == 1);
        assert(devs3.count("ups-1") == 1);
        assert(devs3.at("ups-1")->IP() == "192.0.2.1");
        assert(reader3->getState().ip2master("192.0.2.1") == "ups-1");
        assert(reader3->getState().getSensors().empty());
        // First refresh() call must always return true
        assert(reader3->refresh());
        // Delete this reader explicitly and not in the StateManager dtor
        delete reader3;
    }
    {
        // Update an asset
        fty_proto_t* msg = fty_proto_new(FTY_PROTO_ASSET);
        assert(msg);
        fty_proto_set_name(msg, "ups-1");
        fty_proto_set_operation(msg, FTY_PROTO_ASSET_OP_UPDATE);
        fty_proto_aux_insert(msg, "type", "device");
        fty_proto_aux_insert(msg, "subtype", "ups");
        fty_proto_ext_insert(msg, "ip.1", "192.0.2.3");
        writer.getState().updateFromProto(msg);
        fty_proto_destroy(&msg);
        writer.commit();
        // reader1 still sees the old IP
        auto& devs1 = reader1->getState().getPowerDevices();
        assert(devs1.size() == 1);
        assert(devs1.count("ups-1") == 1);
        assert(devs1.at("ups-1")->IP() == "192.0.2.1");
        assert(reader1->getState().ip2master("192.0.2.1") == "ups-1");
        assert(reader2->getState().ip2master("192.0.2.3") == "");
        assert(reader1->getState().getSensors().empty());
        // reader2 catches up and sees the new IP
        assert(reader2->refresh());
        auto& devs2 = reader2->getState().getPowerDevices();
        assert(devs2.size() == 1);
        assert(devs2.count("ups-1") == 1);
        assert(devs2.at("ups-1")->IP() == "192.0.2.3");
        assert(reader2->getState().ip2master("192.0.2.1") == "");
        assert(reader2->getState().ip2master("192.0.2.3") == "ups-1");
        assert(reader2->getState().getSensors().empty());
    }
    {
        // Special case: commit when no reader is connected
        StateManager          manager2;
        StateManager::Writer& writer2 = manager2.getWriter();

        fty_proto_t* msg = fty_proto_new(FTY_PROTO_ASSET);
        assert(msg);
        fty_proto_set_name(msg, "ups-1");
        fty_proto_set_operation(msg, FTY_PROTO_ASSET_OP_CREATE);
        fty_proto_aux_insert(msg, "type", "device");
        fty_proto_aux_insert(msg, "subtype", "ups");
        fty_proto_ext_insert(msg, "ip.1", "192.0.2.1");
        writer2.getState().updateFromProto(msg);
        fty_proto_destroy(&msg);
        writer2.commit();

        StateManager::Reader* reader4 = manager2.getReader();
        auto&                 devs4   = reader4->getState().getPowerDevices();
        assert(devs4.size() == 1);
        assert(devs4.count("ups-1") == 1);
        assert(devs4.at("ups-1")->IP() == "192.0.2.1");
        delete reader4;

        // The reader list is empty again
        msg = fty_proto_new(FTY_PROTO_ASSET);
        assert(msg);
        fty_proto_set_name(msg, "epdu-2");
        fty_proto_set_operation(msg, FTY_PROTO_ASSET_OP_CREATE);
        fty_proto_aux_insert(msg, "type", "device");
        fty_proto_aux_insert(msg, "subtype", "epdu");
        fty_proto_ext_insert(msg, "ip.1", "192.0.2.2");
        writer2.getState().updateFromProto(msg);
        fty_proto_destroy(&msg);
        writer2.commit();
    }
}
