/*  =========================================================================
    state_manager - Class maintaining the asset list

    Copyright (C) 2014 - 2017 Eaton

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
    state_manager - Class maintaining the asset list
@discuss
    Conceptually, the following invariant must hold for any reader:
    delete_counter_ <= read_counter_ <= write_counter_
    Since the counters may overflow, the inequalities need to be expressed like
    this:

    delete_counter_ != read_counter_  + 1  (Inv1)
    read_counter_   != write_counter_ + 1  (Inv2)
    write_counter_ + 1 != delete_counter_  (Inv3)

    or graphically like this

         +-- < -- delete_counter_ -- <= --+
         |                                |
         |                                |
         |                                |
      write_counter_ ---- >= -- read_counter_

    where the circle has UINT_MAX positions and the counters move clockwise.

    The semantics and ownership of the couters is as follows:

    The write_counter_ is incremented by the writer when it pushed a new state
    to the end of the queue. (cf. Inv3)
    The read_counter_ is incremented by the respective reader when it advances
    its iterator in the queue (cf. Inv2)
    The delete_counter_ is incremented by the writer when it pops an unused
    state off the front of the queue (cf. Inv 1)
    Because old states are eventually removed, none of the couters can be used
    as indices into the state queue.

    The readers_mutex_ needs to be held when manipulating the list of readers
    and when updating the end of the queue. After all the readers have been
    created, the mutex is therefore not contended and the operations are
    lock-free in practice.
@end
*/

#include <cassert>
#include <thread>

#include "state_manager.h"

StateManager::StateManager()
    : writer_(*this)
    , write_counter_(0)
    , delete_counter_(0)
{
    states_.push_back(uncommitted_);
}

StateManager::~StateManager()
{
    // The Reader dtor removes the respective readers_ entry, so we can't
    // simply iterate over readers_
    while (!readers_.empty())
        delete *(readers_.begin());
}

StateManager::Writer::Writer(StateManager& manager)
    : manager_(manager)
{
}

StateManager::Reader::Reader(StateManager& manager)
    : manager_(manager)
    // Called with readers_mutex_ held
    , current_view_(std::prev(manager_.states_.end()))
    , read_counter_(manager_.write_counter_.load())
    , first_refresh_(true)
{
}

StateManager::Reader* StateManager::getReader()
{
    std::lock_guard<std::mutex> lock(readers_mutex_);
    Reader *r = new Reader(*this);
    readers_.insert(r);
    return r;
}

void StateManager::putReader(Reader* r)
{
    std::lock_guard<std::mutex> lock(readers_mutex_);
    readers_.erase(r);
}

// Removes unused states from the front of the queue
void StateManager::cleanup()
{
    CounterInt dc = delete_counter_;

    while (true) {
        std::unique_lock<std::mutex> lock(readers_mutex_);
        for (auto r : readers_) {
            // Inv1
            if (dc == r->read_counter_)
                return;
        }
        // Inv3 - this could happen if there are no readers at all
        if (dc == write_counter_)
            return;
        lock.unlock();
        states_.pop_front();
        dc = ++delete_counter_;
    }
}

// Pushes uncommitted_ onto the back of the state queue, cleaning up as part
// of the process
void StateManager::commit()
{
    while (true) {
        // We cleanup from the writer thread at commit time and not from the
        // reader threads at refresh time, so as to do both allocations and
        // deallocations of the queue from a single thread
        cleanup();
        // Inv3: It is extremely unlikely and not even possible on 32bit, but
        // a stuck reader thread may cause the write_counter_ to overflow
        // and reach the value of delete_counter_. In such case, we busy loop
        // and pray
        if (write_counter_ + 1 == delete_counter_)
            std::this_thread::yield();
        else
            break;
    }
    uncommitted_.recompute();
    // For the Reader constructor, the update of the write_counter_ and the
    // queue must happen atomically. We could split the mutex into two, one
    // protecting the readers_ list and one ensuring this atomicity, but
    // it would have no effect in practice.
    std::lock_guard<std::mutex> lock(readers_mutex_);
    states_.push_back(uncommitted_);
    ++write_counter_;
}

// Updates current_view_ to refer to the most recent state
bool StateManager::Reader::refresh()
{
    bool ret = first_refresh_;
    first_refresh_ = false;
    // Inv2
    while (read_counter_ != manager_.write_counter_) {
        ret = true;
        ++current_view_;
        ++read_counter_;
    }
    return ret;
}

//  --------------------------------------------------------------------------
//  Self test of this class

class StateManagerTest {
public:
    static void test()
    {
        StateManager manager;
        StateManager::Writer& writer = manager.getWriter();
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
            fty_proto_t *msg = fty_proto_new(FTY_PROTO_ASSET);
            assert(msg);
            fty_proto_set_name(msg, "ups-1");
            fty_proto_set_operation(msg, FTY_PROTO_ASSET_OP_CREATE);
            fty_proto_aux_insert(msg, "type", "device");
            fty_proto_aux_insert(msg, "subtype", "ups");
            fty_proto_ext_insert(msg, "ip.1", "192.0.2.1");
            writer.getState().updateFromProto(msg);
            fty_proto_destroy(&msg);
            // Not yet committed
            assert(manager.states_.size() == 1);
            assert(reader1->getState().getPowerDevices().empty());
            assert(reader1->getState().getSensors().empty());
            assert(reader2->getState().getPowerDevices().empty());
            assert(reader2->getState().getSensors().empty());
            writer.commit();
            assert(manager.states_.size() == 2);
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
            fty_proto_t *msg = fty_proto_new(FTY_PROTO_ASSET);
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
            assert(manager.states_.size() == 3);
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
        assert(manager.states_.size() == 1);

        {
            // Delete an asset
            fty_proto_t *msg = fty_proto_new(FTY_PROTO_ASSET);
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
            auto& devs3 = reader3->getState().getPowerDevices();
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
            fty_proto_t *msg = fty_proto_new(FTY_PROTO_ASSET);
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
            StateManager manager2;
            StateManager::Writer& writer2 = manager2.getWriter();

            fty_proto_t *msg = fty_proto_new(FTY_PROTO_ASSET);
            assert(msg);
            fty_proto_set_name(msg, "ups-1");
            fty_proto_set_operation(msg, FTY_PROTO_ASSET_OP_CREATE);
            fty_proto_aux_insert(msg, "type", "device");
            fty_proto_aux_insert(msg, "subtype", "ups");
            fty_proto_ext_insert(msg, "ip.1", "192.0.2.1");
            writer2.getState().updateFromProto(msg);
            fty_proto_destroy(&msg);
            writer2.commit();

            StateManager::Reader *reader4 = manager2.getReader();
            auto& devs4 = reader4->getState().getPowerDevices();
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
};

void
state_manager_test (bool verbose)
{
    printf (" * state_manager: ");
    StateManagerTest::test();
    printf ("OK\n");
}
