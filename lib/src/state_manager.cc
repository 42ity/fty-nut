/*  =========================================================================
    state_manager - Class maintaining the asset list

    Copyright (C) 2014 - 2020 Eaton

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

#include "state_manager.h"
#include <cassert>
#include <thread>

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
    Reader*                     r = new Reader(*this);
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
    bool ret       = first_refresh_;
    first_refresh_ = false;
    // Inv2
    while (read_counter_ != manager_.write_counter_) {
        ret = true;
        ++current_view_;
        ++read_counter_;
    }
    return ret;
}

StateManager::StatesList& StateManager::states()
{
    return states_;
}

