/*  =========================================================================
    nut_agent - NUT daemon wrapper

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
    nut_agent - NUT daemon wrapper
@discuss
@end
*/
#include "ups_status.h"
#include "nut_agent.h"
#include <fty_log.h>
#include <string>
#include <fty_shm.h>

#include <cmath>

const std::map<std::string, std::string> NUTAgent::_units =
{
    { "temperature", "C" },
    { "realpower",   "W" },
    { "voltage",     "V" },
    { "current",     "A" },
    { "load",        "%" },
    { "charge",      "%" },
    { "frequency",   "Hz"},
    { "power",       "VA" },
    { "runtime",     "s" },
    { "timer",       "s" },
    { "delay",       "s" },
};

const std::vector<std::string> alarmsList =
{
    "Replace battery!",
    "Shutdown imminent!",
    "Fan failure!",
    "No battery installed!",
    "Battery voltage too low!",
    "Battery voltage too high!",
    "Battery charger fail!",
    "Temperature too high!",
    "Internal UPS fault!",
    "Awaiting power!",
    "Automatic bypass mode!",
    "Manual bypass mode!",
    "Communication fault!",
    "Fuse fault!"
};

NUTAgent::NUTAgent(StateManager::Reader *reader)
    : _state_reader(reader)
{
}

bool NUTAgent::loadMapping (const char *path_to_file)
{
    if ( !path_to_file )
        return false;
    _conf = path_to_file;
    _deviceList.load_mapping (_conf.c_str ());
    return _deviceList.mappingLoaded ();
}

bool NUTAgent::isMappingLoaded () const
{
    return _deviceList.mappingLoaded ();
}

void NUTAgent::setClient (mlm_client_t *client)
{
    if (!_client) {
       _client = client;
    }
}
void NUTAgent::setiClient (mlm_client_t *client)
{
    if (!_iclient) {
       _iclient = client;
    }
}

void NUTAgent::onPoll ()
{
    if (_client)
        advertisePhysics ();
    if (_iclient)
        advertiseInventory ();
}

void NUTAgent::updateDeviceList ()
{
    if (_state_reader->refresh())
        _deviceList.updateDeviceList (_state_reader->getState());
}

int NUTAgent::send (const std::string& subject, zmsg_t **message_p)
{
    fty_proto_t *m_decoded = fty_proto_decode(message_p);
    zmsg_destroy(message_p);
    *message_p = fty_proto_encode(&m_decoded);

    int rv = mlm_client_send (_client, subject.c_str (), message_p);
    if (rv == -1) {
        log_error ("mlm_client_send (subject = '%s') failed", subject.c_str ());
    }
    zmsg_destroy (message_p);
    return rv;
}

//MVY: a hack for inventory messages
int NUTAgent::isend (const std::string& subject, zmsg_t **message_p)
{
    fty_proto_t *m_decoded = fty_proto_decode(message_p);
    zmsg_destroy(message_p);
    *message_p = fty_proto_encode(&m_decoded);

    int rv = mlm_client_send (_iclient, subject.c_str (), message_p);
    if (rv == -1) {
        log_error ("mlm_client_send (subject = '%s') failed", subject.c_str ());
    }
    zmsg_destroy (message_p);
    return rv;
}

std::string NUTAgent::physicalQuantityShortName (const std::string& longName) const
{
    size_t i = longName.find ('.');
    if (i == std::string::npos) {
       return longName;
    }
    return longName.substr (0, i);
}

std::string NUTAgent::physicalQuantityToUnits (const std::string& quantity) const {
    auto it = _units.find(quantity);
    if (it == _units.end ()) {
        return "";
    }
    return it->second;
}

void NUTAgent::advertisePhysics ()
{
    _deviceList.update (true);
    for (auto& device : _deviceList) {
        std::string subject;
        auto measurements = device.second.physics (false); // take  NOT only changed
        for (const auto& measurement : measurements) {
            std::string type = physicalQuantityShortName (measurement.first);
            std::string units = physicalQuantityToUnits (type);

            int r = fty::shm::write_metric(device.second.assetName (), measurement.first, measurement.second, units, _ttl);
            if( r !=0)
              log_error("failed to send measurement %s@%s", measurement.first.c_str(), device.second.assetName().c_str());
            device.second.setChanged (measurement.first, false);
        }
        // 'load' computing
        // BIOS-1185 start
        // if it is epdu, that doesn't provide load.default,
        // but it is still could be calculated (because input.current is known) then do this
        if (device.second.subtype() == "epdu"
             && measurements.count ("load.default") == 0 )
        {
            if ( measurements.count ("load.input.L1") != 0 ) {
                std::string value = measurements.at("load.input.L1");
                int r = fty::shm::write_metric(device.second.assetName (), "load.default", value, "%", _ttl);
                if( r != 0 )
                      log_error("failed to send measurement %s result %i", subject.c_str(), r);
            }
            else if ( measurements.count ("current.input.L1") != 0 ) // it is a mapped value!!!!!!!!!!!
            {
                // try to compute it
                // 1. Determine the MAX value
                double max_value = NAN;
                if ( measurements.count ("current.input.nominal") == 1 ) {
                    try {
                        max_value = std::stof (measurements.at("current.input.nominal"));
                        log_debug ("load.default: max_value %lf from UPS", max_value);
                    } catch (...) {}
                } else {
                    max_value = device.second.maxCurrent();
                    log_debug ("load.default: max_value %lf from user", max_value);
                }
                // 2. if MAX value is known -> do work, otherwise skip
                if (!std::isnan(max_value)) {
                    double value = 0;
                    try {
                        value = stof (measurements.at("current.input.L1"));
                    } catch (...) {};
                    char buffer [50];
                    // 3. compute a real value
                    sprintf (buffer, "%lf", value*100/max_value); // because it is %!!!!
                    // 4. form message
                    // 5. send the messsage
                    int r = fty::shm::write_metric(device.second.assetName (), "load.default", buffer, "%", _ttl);
                    if( r != 0 )
                        log_error("failed to send measurement %s result %i", subject.c_str(), r);
                }
            }
        }

        // send alarms as bitmap
        bool has_alarms = false;
        if (device.second.hasProperty ("ups.alarm")) {
            const auto &alarms = device.second.property ("ups.alarm");
            uint16_t bitfield = 0;
            int bit = 0;
            int internal_failure_bit = 0;
            for (const auto &i : alarmsList) {
                if (alarms.find(i) != std::string::npos) {
                    bitfield |= (1 << bit);
                    has_alarms = true;
                }
                if (i == "Internal UPS fault!") {
                    internal_failure_bit = bit;
                }
                bit++;
            }
            // TODO FIXME I hate this kind of fix, but it was to be quick and dirty
            if (alarms.find("Internal failure!") != std::string::npos) {
                bitfield |= (1 << internal_failure_bit);
            }
            int r = fty::shm::write_metric(device.second.assetName (), "ups.alarm", std::to_string (bitfield), "", _ttl);
            if( r != 0 )
                log_error("failed to send measurement %s result %i", subject.c_str(), r);
            device.second.setChanged ("ups.alarm", false);
        }
        // send status and "in progress" test result as a bitmap
        if (device.second.hasProperty ("status.ups")) {
            std::string status_s = device.second.property ("status.ups");
            log_debug("%s status.ups: \"%s\"", device.second.assetName().c_str(), status_s.c_str());
            if (!status_s.empty()) { // fix IPMVAL-1889 (empty on data-stale)
                std::string test_s = (device.second.hasProperty ("ups.test.result")?
                    device.second.property ("ups.test.result"):
                    "no test initiated");
                uint16_t    status_i = upsstatus_to_int (status_s, test_s);
                if (has_alarms) {
                    status_i |= STATUS_ALARM;
                }
                int r = fty::shm::write_metric(device.second.assetName (), "status.ups", std::to_string(status_i), " ", _ttl);
                if( r != 0 )
                    log_error("failed to send measurement %s result %i", subject.c_str(), r);
                device.second.setChanged ("status.ups", false);
            }
        }

        //send epdu outlet status as bitmap
        for (int i = 1; i != 100; i++) {
            std::string property = "status.outlet." + std::to_string (i);
            // assumption, if outlet.10 does not exists, outlet.11 does not as well
            if (!device.second.hasProperty (property))
                break;
            std::string status_s = device.second.property (property);
            uint16_t    status_i = status_s == "on" ? 42 : 0;

            int r = fty::shm::write_metric(device.second.assetName (), property, std::to_string (status_i), " ", _ttl);
            if( r != 0 )
                log_error("failed to send measurement %s result %i", subject.c_str(), r);
            device.second.setChanged (property, false);
        }
    }
}

void NUTAgent::advertiseInventory()
{
    bool advertiseAll = false;
    if (_inventoryTimestamp_ms + NUT_INVENTORY_REPEAT_AFTER_MS < static_cast<uint64_t> (zclock_mono ())) {
        advertiseAll = true;
        _inventoryTimestamp_ms = static_cast<uint64_t> (zclock_mono ());
    }
    for (auto& device : _deviceList) {
        std::string log;
        zhash_t *inventory = zhash_new ();
        // !advertiseAll = advetise_Not_OnlyChanged
        for (auto& item : device.second.inventory (!advertiseAll) ) {
            if (item.first == "status.ups") {
                // this value is not advertised as inventory information
                continue;
            }
            zhash_insert (inventory, item.first.c_str (), (void *) item.second.c_str ()) ;
            log += item.first + " = \"" + item.second + "\"; ";
            device.second.setChanged (item.first, false);
        }
        if (zhash_size (inventory) == 0) {
            zhash_destroy (&inventory);
            continue;
        }

        zmsg_t *message = fty_proto_encode_asset (
                NULL,
                device.second.assetName().c_str(),
                "inventory",
                inventory);

        if (message) {
            std::string topic = "inventory@" + device.second.assetName();
            log_debug ("new inventory message '%s': %s", topic.c_str(), log.c_str());
            int r = isend (topic, &message);
            if( r != 0 )
                log_error ("failed to send inventory %s result %i", topic.c_str(), r);
            zmsg_destroy (&message);
        }
        zhash_destroy (&inventory);
    }
}

//  --------------------------------------------------------------------------
//  Self test of this class

void
nut_agent_test (bool verbose)
{
    printf (" * nut_agent: ");

    //  @selftest
    //  @end
    printf ("OK\n");
}
