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

#ifndef STATE_MANAGER_H_INCLUDED
#define STATE_MANAGER_H_INCLUDED

/*
 * The StateManager stores fty-nut's view of existing assets. It allows one
 * thread to update it via the Writer class and N threads to read the state via
 * the Reader class. Writer updates and reads are lock-less, creating or
 * deleting a reader needs to acquire a mutex. All the readers need to call the
 * refresh() method periodically, to discard old state information. A stuck
 * reader will cause the memory consumption to grow beyond limis.
 *
 * The API is to be used as follows:
 *
 * StateManager NutStateManager;
 *
 * Writer thread:
 * StateManager::Reader& writer NutStateManager.getWriter();
 *
 * while (messages on the ASSETS stream) {
 *     // These manipulations are not immediately visible to the readers
 *     Asset asset = new Asset;
 *     writer.getState().insert({name1, asset});
 *     writer.getState().erase(name2);
 *
 *     // Make the new state available to readers
 *     writer.commit();
 * }
 *
 * Reader threads (multiple instances):
 * StateManager::Reader *reader NutStateManager.getReader();
 * while (...) {
 *     // Check if state has changed, free memory if no other reader is using
 *     // the old state. Any previously obtained iterators get invalidated.
 *     if (reader->refresh()) {
 *         // Adapt to the new asset state
 *         for (auto device : reader->getState().getPowerDevices()) {
 *             ...
 *         }
 *     }
 * }
 *
 * The returned Reader and Writer objects are only valid throughout the
 * lifetime of the StateManager instance, the easiest is therefore to create
 * the StateManager as a global object. The Reader poiners can be delete()d
 * if no longer needed. Otherwise, the StateManager destructor will delete
 * them.
 */

#include <atomic>
#include <memory>
#include <mutex>
#include <list>
#include <map>
#include <set>

struct Asset {
    std::string property;
};

typedef std::map<std::string, std::shared_ptr<Asset> > AssetsState;

class StateManagerTest;

class StateManager {
private:
    typedef std::list<AssetsState> StatesList;
    typedef unsigned int CounterInt;
    typedef std::atomic<CounterInt> Counter;
public:
    class Reader {
    public:
        Reader(const Reader&) = delete;
        ~Reader()
        {
            manager_.putReader(this);
        }
        bool refresh();
        const AssetsState& getState() const
        {
            return *current_view_;
        }
    private:
        explicit Reader(StateManager& manager);
        StateManager& manager_;
        StateManager::StatesList::const_iterator current_view_;
        Counter read_counter_;
        bool first_refresh_;
        friend class StateManager;
    };

    class Writer {
    public:
        Writer(const Writer&) = delete;
        void commit()
        {
            manager_.commit();
        }
        AssetsState& getState()
        {
            return manager_.getUncommittedAssets();
        }
    private:
        explicit Writer(StateManager& manager);
        StateManager& manager_;
        friend class StateManager;
    };

    StateManager();
    ~StateManager();
    Writer& getWriter()
    {
        return writer_;
    }
    Reader* getReader();
    void putReader(Reader* reader);
private:
    AssetsState& getUncommittedAssets()
    {
        return uncommitted_;
    }
    void commit();
    void cleanup();
    AssetsState uncommitted_;
    StatesList states_;
    Writer writer_;
    std::mutex readers_mutex_;
    std::set<Reader*> readers_;
    Counter write_counter_, delete_counter_;
    friend class StateManagerTest;
};

// fty_nut_server.cc
extern StateManager NutStateManager;

//  Self test of this class
void state_manager_test (bool verbose);

#endif
