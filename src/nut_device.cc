/*  =========================================================================
    nutdevice - classes for communicating with NUT daemon

    Copyright (C) 2014 - 2015 Eaton                                        
                                                                           
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
    nutdevice - classes for communicating with NUT daemon
@discuss
@end
*/

#include <iostream>
#include <fstream>
#include <algorithm>
#include <exception>
#include <cxxtools/jsondeserializer.h>
#include <cxxtools/serializationerror.h>


#include "agent_nut_classes.h"

#define NUT_MEASUREMENT_REPEAT_AFTER    300     //!< (once in 5 minutes now (300s))

using namespace std;

namespace drivers
{
namespace nut
{

NUTDevice::NUTDevice() :
    _daisyChainIndex (0)
{
}

NUTDevice::NUTDevice(const char *name) :
    _assetName (name),
    _nutName (name),
    _daisyChainIndex (0)
{
}

NUTDevice::NUTDevice(const std::string& name) :
    _assetName (name),
    _nutName (name),
    _daisyChainIndex (0)
{
}

NUTDevice::NUTDevice(const char *asset_name, const char* nut_name, int daisy_chain_index) :
    _assetName (asset_name),
    _nutName (nut_name),
    _daisyChainIndex (daisy_chain_index)
{
}

NUTDevice::NUTDevice(const std::string& asset_name, const std::string& nut_name, int daisy_chain_index):
    _assetName (asset_name),
    _nutName (nut_name),
    _daisyChainIndex (daisy_chain_index)
{
}

void NUTDevice::nutName(const std::string& name) {
    _nutName = name;
}

std::string NUTDevice::nutName() const {
    return _nutName;
}

void NUTDevice::assetName(const std::string& name) {
    _assetName = name;
}

std::string NUTDevice::assetName() const {
    return _assetName;
}

void NUTDevice::daisyChainIndex (int index) {
    _daisyChainIndex = index;
}

int NUTDevice::daisyChainIndex () const {
    return _daisyChainIndex;
}

std::string NUTDevice::daisyPrefix() {
    if (_daisyChainIndex) {
        return "device." + std::to_string (_daisyChainIndex) + ".";
    }
    return "";
}

/**
 * change getters  
 */
bool NUTDevice::changed() const {
    for(auto &it : _physics ){
        if(it.second.changed) return true;
    }
    for(auto &it : _inventory ){
        if(it.second.changed) return true;
    }
    return false;
}

bool NUTDevice::changed(const char *name) const {
    auto iterP = _physics.find(name);
    if( iterP != _physics.end() ) {
        // this is a number, value exists
        return iterP->second.changed;
    }
    auto iterI = _inventory.find(name);
    if( iterI != _inventory.end() ) {
        // this is a inventory string, value exists
        return iterI->second.changed;
    }
    return false;
}

bool NUTDevice::changed(const std::string &name) const {
    return changed(name.c_str());
}

/**
 * change setters
 */
void NUTDevice::setChanged(const bool status) {
    for(auto &it : _physics ){
        it.second.changed = status;
    }
    for(auto &it : _inventory ){
        it.second.changed = status;
    }
}

void NUTDevice::setChanged(const char *name, const bool status) {
    auto iterP = _physics.find(name);
    if( iterP != _physics.end() ) {
        // this is a number, value exists
        iterP->second.changed = status;
    }
    auto iterI = _inventory.find(name);
    if( iterI != _inventory.end() ) {
        // this is a inventory string, value exists
        iterI->second.changed = status;
    }
}

void NUTDevice::setChanged(const std::string& name,const bool status){
    setChanged(name.c_str(),status);
}

void NUTDevice::updatePhysics(const std::string& varName, const float newValue) {
    long int newValueInt = round(newValue * 100.0);
    if( newValueInt > INT32_MAX  || newValueInt < INT32_MIN ) {
        // value is out of range (like gigawats), the measurement is invalid
        log_error("%s value exceeded the range on %s", varName.c_str(), _assetName.c_str() );
        _physics.erase( varName );
        return;
    }
    if( _physics.count( varName ) == 0 ) {
        // this is new value
        struct NUTPhysicalValue pvalue;
        pvalue.changed = true;
        pvalue.value = 0;
        pvalue.candidate = newValueInt;
        _physics[ varName ] = pvalue;
    } else {
        if (_physics[ varName ].value != newValueInt) {
            _physics[ varName ].candidate = newValueInt;
        }
    }
}

void NUTDevice::updatePhysics(const std::string& varName, std::vector<std::string>& values) {
    if( values.size() == 1 ) {
        // don't know how to handle multiple values
        // multiple values would be probably nonsence
        try {
            float value = std::stof(values[0]);
            updatePhysics(varName,value);
        } catch (...) {}
    }
}

void NUTDevice::commitChanges() {
    for( auto & item:  _physics ) {
        if( item.second.value != item.second.candidate ) {
            item.second.value = item.second.candidate;
            item.second.changed = true;
        }
    }
}

void NUTDevice::updateInventory(const std::string& varName, std::vector<std::string>& values) {
    std::string inventory = "";
    for(size_t i = 0 ; i < values.size() ; ++i ) {
        inventory += values[i];
        if( i < values.size() -1 ) {
            inventory += ", ";
        }
    }
    // inventory now looks like "value1, value2, value3"
    // NUT bug type pdu => epdu
    if( varName == "type" && inventory == "pdu" ) { inventory = "epdu"; }
    if( _inventory.count( varName ) == 0 ) {
        // this is new value
        struct NUTInventoryValue ivalue;
        ivalue.changed = true;
        ivalue.value = inventory;
        _inventory[ varName ] = ivalue;
    } else {
        if( _inventory[ varName ].value != inventory ) {
            _inventory[ varName ].value = inventory;
            _inventory[ varName ].changed = true;
        }
    }
}

void NUTDevice::update (std::map <std::string, std::vector <std::string>> vars,
                        std::function <const std::map <std::string, std::string>&(const char *)> mapping,
                        bool forceUpdate) {

    if( vars.empty() ) return;
    _lastUpdate = time(NULL);
    std::string prefix = daisyPrefix();

    // use transformation table first
    NUTValuesTransformation (prefix, vars);

    // walk trough physics
    for (const auto& item : mapping ("physicsMapping")) {
        if (vars.find (prefix + item.first) != vars.end ()) {
            // variable found in received data
            std::vector<std::string> values = vars[prefix + item.first];
            updatePhysics (item.second, values);
        }
        else {
            // iterating numbered items in physics
            // like outlet.1.voltage, outlet.2.voltage, ...
            int x = item.first.find(".#."); // is always in the middle: outlet.1.realpower
            int y = item.second.find(".#"); // can be at the end: outlet.voltage.#
            if( x > 0 && y > 0 ) {
                // this is something like outlet.#.realpower
                std::string nutprefix = item.first.substr(0,x+1);
                std::string nutsuffix = item.first.substr(x+2);
                std::string biosprefix = item.second.substr(0,y+1);
                std::string biossuffix = item.second.substr(y+2);
                std::string nutname,biosname;
                int i = 1;
                while(true) {
                    nutname = nutprefix + std::to_string(i) + nutsuffix;
                    biosname = biosprefix + std::to_string(i) + biossuffix;
                    if( vars.count(prefix + nutname) == 0 ) break; // variable out of scope
                    // variable found
                    std::vector<std::string> values = vars[prefix + nutname];
                    updatePhysics (biosname, values);
                    ++i;
                }
            }
        }
    }

    // walk trough inventory
    //for(size_t i = 0; i < inventoryMapping.size(); ++i) {
    for (const auto& item : mapping ("inventoryMapping")) {
        if( vars.find (prefix + item.first) != vars.end() ) {
            // variable found in received data
            std::vector<std::string> values = vars[prefix + item.first];
            updateInventory (item.second, values);
        } else {
            // iterating numbered items in physics
            // like outlet.1.voltage, outlet.2.voltage, ...
            int x = item.first.find(".#."); // is always in the middle: outlet.1.realpower
            int y = item.second.find(".#"); // can be at the end: outlet.voltage.#
            if( x > 0 && y > 0 ) {
                // this is something like outlet.#.realpower
                std::string nutprefix = item.first.substr(0,x+1);
                std::string nutsuffix = item.first.substr(x+2);
                std::string biosprefix = item.second.substr(0,y+1);
                std::string biossuffix = item.second.substr(y+2);
                std::string nutname,biosname;
                int i = 1;
                while(true) {
                    nutname = nutprefix + std::to_string(i) + nutsuffix;
                    biosname = biosprefix + std::to_string(i) + biossuffix;
                    if( vars.count(prefix + nutname) == 0 ) break; // variable out of scope
                    // variable found
                    std::vector<std::string> values = vars[prefix + nutname];
                    updateInventory(biosname, values);
                    ++i;
                }
            }
        }
    }
    commitChanges();
}

std::string NUTDevice::itof(const long int X) const {
    std::string num,dec,sig;
    long int AX;

    if( X < 0 ) {
        sig = "-";
    } else {         
        sig = "";
    }
    AX = abs(X);
    num = std::to_string( AX / 100 );
    dec = std::to_string( AX % 100 );
    if( dec.size() == 1 ) dec = "0" + dec;
    if( dec == "00" ) {
        return sig + num;
    } else {
        return sig + num + "." + dec;
    }
}

std::string NUTDevice::toString() const {
    std::string msg = "",val;
    for(auto it : _physics ){
        msg += "\"" + it.first + "\":" + itof(it.second.value) + ", ";
    }
    for(auto it : _inventory ){
        val = it.second.value;
        std::replace(val.begin(), val.end(),'"',' ');
        msg += "\"" + it.first + "\":\"" + val + "\", ";
    }
    if( msg.size() > 2 ) {
        msg = msg.substr(0, msg.size()-2 );
    }
    return "{" + msg + "}";
}

std::map<std::string,std::string> NUTDevice::properties() const {
    std::map<std::string,std::string> map;
    for(auto it : _physics ){
        map[ it.first ] = itof(it.second.value);
    }
    for(auto it : _inventory ){
        map[ it.first ] = it.second.value;
    }
    return map;
}

std::map<std::string,int32_t> NUTDevice::physics(bool onlyChanged) const {
    std::map<std::string,int32_t> map;
    for(auto it : _physics ){
        if( ( ! onlyChanged ) || it.second.changed ) {
            map[ it.first ] = it.second.value;
        }
    }
    return map;
}

std::map<std::string,std::string> NUTDevice::inventory(bool onlyChanged) const {
    std::map<std::string,std::string> map;
    for(auto it : _inventory ){
        if( ( ! onlyChanged ) || it.second.changed ) {        
            map[ it.first ] = it.second.value;
        }
    }
    return map;
}


bool NUTDevice::hasProperty(const char *name) const {
    if( _physics.count( name ) != 0 ) {
        // this is a number and value exists
        return true;
    }
    if( _inventory.count( name ) != 0 ) {
        // this is a inventory string, value exists
        return true;
    }
    return false;
}

bool NUTDevice::hasProperty(const std::string& name) const {
    return hasProperty(name.c_str());
}

bool NUTDevice::hasPhysics(const char *name) const {
    if( _physics.count( name ) != 0 ) {
        // this is a number and value exists
        return true;
    }
    return false;
}

bool NUTDevice::hasPhysics(const std::string& name) const {
    return hasPhysics(name.c_str());
}


    
std::string NUTDevice::property(const char *name) const {
    auto iterP = _physics.find(name);
    if( iterP != _physics.end() ) {
        // this is a number, value exists
        return itof(iterP->second.value);
    }
    auto iterI = _inventory.find(name);
    if( iterI != _inventory.end() ) {
        // this is a inventory string, value exists
        return iterI->second.value;
    }
    return "";
}

std::string NUTDevice::property(const std::string& name) const {
    return property(name.c_str());
}

void NUTDevice::NUTSetIfNotPresent (const std::string& prefix, std::map< std::string,std::vector<std::string> > &vars, const std::string &dst, const std::string &src)
{
    if (vars.find(prefix + dst) == vars.cend()) {
        const auto &it = vars.find(prefix + src);
        if (it != vars.cend()) vars[prefix + dst] = it->second;
    }
}

void NUTDevice::NUTRealpowerFromOutput (const std::string& prefix, std::map< std::string,std::vector<std::string> > &vars) {
    
    if (vars.find (prefix + "ups.realpower") != vars.end()) { return; }
    
    // use outlet.realpower if exists
    if (vars.find (prefix + "outlet.realpower") != vars.end()) {
        NUTSetIfNotPresent (prefix, vars, "ups.realpower", "outlet.realpower");
        log_debug("realpower of %s taken from outlet.realpower", _assetName.c_str ());
        return;
    }
    // sum the output.Lx.realpower
    if (vars.find (prefix + "output.L1.realpower") != vars.end ()) {
        int phases = 1;
        if (vars.find (prefix + "output.phases") != vars.end ()) {
            try {
                phases = std::stoi (vars [prefix + "output.phases"][0]);
            } catch(...) { }
        }
        double sum = 0.0;
        for (int i=1; i<= phases; i++) {
            auto it = vars.find (prefix + "output.L" + std::to_string(i) + ".realpower");
            if (it  == vars.end ()) {
                // even output is missing, can't compute
                break;
            }
            try {
                sum += std::stod (it->second[0]);
            } catch(...) {
                break;
            }
        }
        // we have sum
        log_debug("realpower of %s calculated as sum of output.Lx.realpower", _assetName.c_str ());
        std::vector<std::string> value;
        value.push_back (itof (round (sum * 100)));
        vars[prefix + "ups.realpower"] = value;
        return;
    }
    
    // if we have outlets, sum them
    if (vars.find (prefix + "outlet.1.realpower") != vars.end()) {
        double sum = 0.0;
        int count = 100;
        auto cntit = vars.find (prefix + "outlet.count");
        if (cntit != vars.end()) {
            try {
                count = std::stoi(cntit->second[0]);
            } catch(...) {}
        }
        for (int outlet = 1; outlet <= count; outlet++) {
            auto it = vars.find (prefix + "outlet." + std::to_string(outlet) + ".realpower");
            if (it  == vars.end ()) {
                // end of outlets
                break;
            }
            try {
                sum += std::stod (it->second[0]);
            } catch(...) {}
        }
        log_debug("realpower of %s calculated as sum of outlet.X.realpower", _assetName.c_str ());
        std::vector<std::string> value;
        value.push_back (itof (round (sum * 100)));
        vars[prefix + "ups.realpower"] = value;
        return;
    }
}

void NUTDevice::NUTValuesTransformation (const std::string &prefix, std::map< std::string,std::vector<std::string> > &vars ) {
    if( vars.empty() ) return ;

    // number of input phases
    if (vars.find (prefix + "input.phases") == vars.end ()) {
        vars [prefix + "input.phases"] = { "1" };
    }

    // number of output phases
    if (vars.find (prefix + "output.phases") == vars.end ()) {
        vars [prefix + "output.phases"] = { "1" };
    }
    {
        // pdu replace with epdu
        auto it = vars.find (prefix + "device.type");
        if( it != vars.end() ) {
            if( ! it->second.empty() && it->second[0] == "pdu" ) it->second[0] = "epdu";
        }
    }
    // sum the realpower from output information
    NUTRealpowerFromOutput (prefix, vars);
    // variables, that differs from ups to ups
    NUTSetIfNotPresent (prefix, vars, "ups.realpower", "input.realpower");
    NUTSetIfNotPresent (prefix, vars, "input.L1.realpower", "input.realpower");
    NUTSetIfNotPresent (prefix, vars, "input.L1.realpower", "ups.realpower");
    NUTSetIfNotPresent (prefix, vars, "output.L1.realpower", "output.realpower");
    // take input realpower and present it as output if output is not present
    // and also the opposite way
    for( const auto &variable: {"realpower", "L1.realpower", "L2.realpower", "L3.realpower"} ) {
        std::string outvar = "output."; outvar.append (variable);
        std::string invar = "input."; invar.append(variable);
        NUTSetIfNotPresent (prefix, vars, outvar, invar);
        NUTSetIfNotPresent (prefix, vars, invar, outvar);
    }
    // sum the realpower again if still not present
    // hope that missing output values have been filled
    // from input values
    NUTRealpowerFromOutput (prefix, vars);
}

void NUTDevice::clear() {
    if( ! _inventory.empty() || ! _physics.empty() ) {
        _inventory.clear();
        _physics.clear();
        log_error("Dropping all measurement/inventory data for %s", _assetName.c_str() );
    }
}

NUTDevice::~NUTDevice() {

}

NUTDeviceList::NUTDeviceList() {

}

void NUTDeviceList::updateDeviceList(nut_t * deviceState) {
    try {
        if (!deviceState) return;
        zlistx_t *devices = nut_get_assets (deviceState);
        if (!devices) return;

        _devices.clear();
        std::map<std::string, std::string> ip2master;
        {
            // make ip->master map
            const char *name = (char *)zlistx_first(devices);
            while (name) {
                const char* ip = nut_asset_ip (deviceState, name);
                const char* chain = nut_asset_daisychain (deviceState, name);
                if (ip == NULL || chain == NULL || streq (ip, "") ) {
                    // this is strange. No IP?
                    name = (char *)zlistx_next(devices);
                    continue;
                }
                if (streq (chain,"") || streq (chain,"1")) {
                    // this is master
                    ip2master[ip] = name;
                }
                name = (char *)zlistx_next(devices);
            }
        }
        {        
            const char *name = (char *)zlistx_first(devices);
            while (name) {
                const char* ip = nut_asset_ip (deviceState, name);
                if (!ip || streq (ip, "")) {
                    // this is strange. No IP?
                    name = (char *)zlistx_next(devices);
                    continue;
                }
                const char* chain_str = nut_asset_daisychain (deviceState, name);
                int chain = 0;
                if (chain_str) try { chain = std::stoi (chain_str); } catch(...) {};
                switch(chain) {
                case 0:
                    _devices[name] = NUTDevice(name);
                    break;
                case 1:
                    _devices[name] = NUTDevice(name, name, 1);
                    break;
                default:
                    const auto master_it = ip2master.find (ip);
                    if (master_it == ip2master.cend()) {
                        log_error ("Daisychain master for %s not found", name);
                    } else {
                        _devices[name] = NUTDevice(name, master_it->second.c_str (), chain);
                    }
                    break;
                }
                name = (char *)zlistx_next(devices);
            }
        }
        zlistx_destroy (&devices);
    } catch (const std::exception& e) {
        log_error ("exception while configuring device: %s", e.what ());
    }
}


void NUTDeviceList::updateDeviceStatus( bool forceUpdate ) {
    for(auto &device : _devices ) {
        try {
            nutclient::Device nutDevice = nutClient.getDevice(device.second.nutName());
            if (! nutDevice.isOk()) { throw std::runtime_error ("device " + device.second.assetName() + " is not configured in NUT yet"); }
            std::function <const std::map <std::string, std::string>&(const char *)> x = std::bind (&NUTDeviceList::get_mapping, this, std::placeholders::_1);
            device.second.update( nutDevice.getVariableValues(), x, forceUpdate );
        } catch ( std::exception &e ) {
            log_error("Communication problem with %s (%s)", device.first.c_str(), e.what() );
            if( time(NULL) - device.second.lastUpdate() > NUT_MEASUREMENT_REPEAT_AFTER/2 ) {
                // we are not communicating for a while. Let's drop the values.
                device.second.clear();
            }
        }
    }
}

bool NUTDeviceList::connect() {
    try {
        nutClient.connect("localhost",3493);
    } catch (...) {}
    return nutClient.isConnected();
}

void NUTDeviceList::disconnect() {
    try {
        nutClient.disconnect();
    } catch (...) {}
}

void NUTDeviceList::update( bool forceUpdate ) {
    if( connect() ) {
        updateDeviceStatus(forceUpdate);
        disconnect();
    }
}

size_t NUTDeviceList::size() const {
    return _devices.size();
}

NUTDevice& NUTDeviceList::operator[](const std::string &name) {
    return _devices[name];
}

std::map<std::string, NUTDevice>::iterator NUTDeviceList::begin() {
    return _devices.begin();
}

std::map<std::string, NUTDevice>::iterator NUTDeviceList::end() {
    return _devices.end();
}

bool NUTDeviceList::changed() const {
    for(auto  &it : _devices ) {
        if(it.second.changed() ) return true; 
    }
    return false;
}

static void 
s_deserialize_to_map (cxxtools::SerializationInfo& si, std::map <std::string, std::string>& m) {
    for (const auto& i : si) {
        std::string temp;
        if (i.category () != cxxtools::SerializationInfo::Category::Value) {
            log_warning ("While reading mapping configuration - Value of property '%s' is not json string.", i.name ().c_str ());
          continue;
        }
        try {
            i.getValue (temp);
        }
        catch (const cxxtools::SerializationError& e) {
            log_error ("Error deserializing value for property '%s'", i.name ().c_str ());
            continue;
        }
        m.emplace (std::make_pair (i.name (), temp));
    }
} 

void NUTDeviceList::load_mapping (const char *path_to_file)
{
    _mappingLoaded = false;
    if (!shared::is_file (path_to_file)) {
        log_error ("'%s' is not a file", path_to_file);
        return;
    }
    std::ifstream input (path_to_file);
    if (!input) {
        log_error ("Error opening file '%s'", path_to_file);
        return;
    }

    cxxtools::SerializationInfo si;
    cxxtools::JsonDeserializer deserializer (input);
    try {
        deserializer.deserialize (si);
    }
    catch (const std::exception& e) {
        log_error ("Error deserializing file '%s' to json", path_to_file);
        return;
    }

    cxxtools::SerializationInfo *physicsMappingMember = si.findMember ("physicsMapping");
    if (physicsMappingMember == NULL) {
        log_error ("Configuration file for mapping '%s' does not contain property 'physicsMapping'", path_to_file);
    }
    else {
        _physicsMapping.clear ();
        s_deserialize_to_map (*physicsMappingMember, _physicsMapping);
    }

    cxxtools::SerializationInfo *inventoryMappingMember = si.findMember ("inventoryMapping");
    if (inventoryMappingMember == NULL) {
        log_error ("Configuration file for mapping '%s' does not contain property 'inventoryMapping'", path_to_file);
    }
    else {
        _inventoryMapping.clear ();
        s_deserialize_to_map (*inventoryMappingMember, _inventoryMapping);
    }

    log_debug ("Number of entries loaded for physicsMapping '%zu'", _physicsMapping.size ());
    log_debug ("Number of entries loaded for inventoryMapping '%zu'", _inventoryMapping.size ());
    _mappingLoaded = true;
}

bool NUTDeviceList::mappingLoaded () 
{
    return _mappingLoaded;
}

const std::map <std::string, std::string>& NUTDeviceList::get_mapping (const char *mapping)
{
    if (!mapping)
        std::invalid_argument ("mapping is NULL");
    if (strcmp (mapping, "physicsMapping") == 0) {
        return _physicsMapping;
    }
    else if (strcmp (mapping, "inventoryMapping") == 0) {
        return _inventoryMapping;
    }
    throw std::invalid_argument ("mapping");
}

NUTDeviceList::~NUTDeviceList() {
    disconnect();
}

} // namespace drivers::nut
} // namespace drivers

//  --------------------------------------------------------------------------
//  Self test of this class

void
nut_device_test (bool verbose)
{
    printf (" * nutdevice: ");

    //  @selftest
    //  @end
    printf ("Empty test - OK\n");
}
