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

#ifndef NUTDEVICE_H_INCLUDED
#define NUTDEVICE_H_INCLUDED

// Taken from 'core' repo, src/agents/nut/nut-driver.(h|c)
// Original authors: Tomas Halman, Karol Hrdina, Alena Chernikava

#include <map>
#include <vector>
#include <functional>
#include <nutclient.h>
#include <nut.h>

namespace nutclient = nut;

namespace drivers
{
namespace nut
{

struct NUTInventoryValue {
    bool changed;
    std::string value;
};

struct NUTPhysicalValue {
    bool changed;
    long int value;
    long int candidate;
};

// Class for keeping status information of one UPS/ePDU/...
// Keeps inventory, status and measurement values of one device as it is presented by NUT.
class NUTDevice {
    friend class NUTDeviceList;
 public:
    // Creates new NUTDevice with empty set of values without name.
    NUTDevice();

    // Creates new NUTDevice with empty set of values with name (name
    // corresponds with NUTs /etc/ups/ups.conf)
    NUTDevice(const char* name);
    NUTDevice(const std::string& name);
    NUTDevice(const char *asset_name, const char* nut_name, int daisy_chain_index);
    NUTDevice(const std::string& asset_name, const std::string& nut_name, int daisy_chain_index);

    // Returns true if there are some changes in device since last
    // statusMessage has been called.
    bool changed() const;

    // Returns true if property has changed since last check.
    bool changed(const char *name) const;
    bool changed(const std::string& name) const;

    // Sets status of all properties
    void setChanged(const bool status);

    // Set status of particular property
    void setChanged(const char *name, const bool status);
    void setChanged(const std::string& name,const bool status);

    /**
     * \brief Produces a std::string with device status in JSON format.
     * \return std::string
     * \see changed()
     *
     * Method returns string with device status message. Method returs the
     * actual status in json like std::string.
     *
     *    if( UPS.changed() ) {
     *        cout << UPS.toString() << endl;
     *        UPS.changed(false);
     *    }
     */
    std::string toString() const;

    // Returns true if this device reports particular property
    // (i.e. property exists for this device)
    bool hasProperty(const char *name) const;
    bool hasProperty(const std::string& name) const;

    // Returns true if this device reports particular physical (measurement) property.
    // (i.e. physical (measurement) property exists for this device)
    bool hasPhysics(const char *name) const;
    bool hasPhysics(const std::string& name) const;

    /**
     * \brief Method returns list of physical properties. If the parameter
     *        is true, only changed properties are returned. Otherways
     *        all properties are returned.
     * \return bool, true if property exists
     */
    std::map<std::string,int32_t> physics(bool onlyChanged) const;

    /**
     * \brief Method returns list of inventory properties. If the parameter
     *        is true, only changed properties are returned. Otherways
     *        all properties are returned.
     * \return bool, true if property exists
     */
    std::map<std::string,std::string> inventory(bool onlyChanged) const;

    /**
     * \brief method returns particular device property.
     * \return std::string, property value as a string or empty
     *         string ("") if property doesn't exists
     *
     *    if( UPS.hasProperty("voltage") ) {
     *        cout << "voltage " << UPS.property("voltage") << "\n";
     *    } else {
     *        cout << "voltage unknown\n";
     *    }
     */
    std::string property(const char *name) const;
    std::string property(const std::string& name) const;

    /**
     * \brief method returns all discovered properties of device.
     * \return std::map<std::string,std::string> property values
     *
     * Method transforms all properties (physical and inventory) to
     * map. Numeric values are converted to strings using itof() method.
     */
    std::map<std::string,std::string> properties() const;

    /**
     * \brief Forgot all physics and inventory data
     */
    void clear();

    /**
     * \brief Return the timestamp of last succesfull update (i. e. response from device)
     */
    time_t lastUpdate() const { return _lastUpdate; }

    /**
     * \brief get/set the device name like it is in assets
     */
    void assetName (const std::string& name);
    std::string assetName () const;
    /**
     * \brief get/set the device name like it is in nut
     */
    void nutName (const std::string& name);
    std::string nutName () const ;
    /**
     * \brief get/set the daisy-chain index
     */
    void daisyChainIndex(int index);
    int daisyChainIndex() const;

    ~NUTDevice();
 private:



    /**
     * \brief Updates physical or measurement value (like current or load) from float.
     *
     * Updates the value if new value is significantly differen (> threshold%). Flag _change is
     * set if new value is saved.
     */
    void updatePhysics(const std::string& varName, const float newValue);

    /**
     * \brief Updates physical or measurement value from vector.
     *
     * Calculates the value with first value from vector (NUT returns vectors of
     * values).
     */
    void updatePhysics(const std::string& varName, std::vector<std::string>& values);

    /**
     * \brief Updates inventory value.
     *
     * Updates the value with values from vector (NUT returns vectors of
     * values). values are connected like "value1, value2, value3". Flag _change is
     * set if new value is different from old one.
     */
    void updateInventory(const std::string& varName, std::vector<std::string>& values);

    /**
     * \brief Updates all values from NUT.
     */
    void update (std::map<std::string,std::vector<std::string>> vars,
                 std::function <const std::map <std::string, std::string>&(const char *)> mapping,
                 bool forceUpdate = false );

    /**
     * \brief Set variable dst with value from src if dst not present and src is
     *
     * This method is used to normalize the NUT output from different drivers/devices.
     */
    void NUTSetIfNotPresent (const std::string& prefix, std::map< std::string,std::vector<std::string> > &vars, const std::string &dst, const std::string &src);

    /**
     * \brief Commit chages for changed calculated by updatePhysics.
     */
    void commitChanges();

    /**
     * \brief prefix of device in daisy chain
     *
     * \return std::string result is "" or device.X. where X if index in chain
     */
    std::string daisyPrefix();

    /**
     * \brief map of physical values.
     *
     * Values are multiplied by 100 and stored as integer
     */
    std::map<std::string, NUTPhysicalValue> _physics;
    //! \brief map of inventory values
    std::map<std::string, NUTInventoryValue> _inventory;

    //! \brief device name from assets
    std::string _assetName;
    //! \brief device name in nut
    std::string _nutName;
    //! \brief daisy-chain index
    int _daisyChainIndex;

    //! \brief Transformation of our integer (x100) back
    std::string itof(const long int) const;
    //! \brief calculate ups.realpower from output.Lx.realpower if not present
    void NUTRealpowerFromOutput (const std::string& prefix,  std::map< std::string,std::vector<std::string> > &vars);
    //! \brief NUT values transformation function
    void NUTValuesTransformation (const std::string& prefix, std::map< std::string,std::vector<std::string> > &vars);
    //! \brief last succesfull communication timestamp
    time_t _lastUpdate = 0;
};

/**
 * \brief NUTDeviceList is class for holding list of NUTDevice objects.
 */
class NUTDeviceList {
 public:
    NUTDeviceList();

    /**
     * \brief Loads mapping from configuration file 'path_to_file'
     *
     * Overwrites old values on successfull deserialization from json configuration file
     */
    void load_mapping (const char *path_to_file);

    bool mappingLoaded ();

    /**
     * \brief Returns requested mapping
     */
    const std::map <std::string, std::string>& get_mapping (const char *mapping);

    /**
     * \brief Reads status information from NUT daemon.
     *
     * Method reads values from NUT and updates information of particular
     * devices. Newly discovered davices are added to list, removed devices
     * are also removed from list.
     */
    void update( bool forceUpdate = false );

    /**
     * \brief Returns true if there is at least one device claiming change.
     */
    bool changed() const;

    /**
     * \brief returns the size of device list (number of devices)
     */
    size_t size() const;

    //! \brief get the NUTDevice object by name
    NUTDevice& operator[](const std::string &name);

    //! \brief get the iterators, to be able to go trough list of devices
    std::map<std::string, NUTDevice>::iterator begin();
    std::map<std::string, NUTDevice>::iterator end();

    //! \brief update list of NUT devices
    void updateDeviceList(nut_t * deviceState);

    ~NUTDeviceList();

 private:
    // see http://www.networkupstools.org/docs/user-manual.chunked/apcs01.html
    std::map <std::string, std::string> _physicsMapping; //!< physics mapping
    std::map <std::string, std::string> _inventoryMapping; //!< inventory mapping

    //! \brief Connection to NUT daemon
    nutclient::TcpClient nutClient;

    //! \brief list of NUT devices
    std::map<std::string, NUTDevice> _devices;

    //! \brief connect to NUT daemon
    bool connect();

    //! \brief disconnect from NUT daemon
    void disconnect();

    //! \brief update status of NUT devices
    void updateDeviceStatus( bool forceUpdate = false );

    bool _mappingLoaded = false;
};



} // namespace drivers::nut
} // namespace drivers


//  Self test of this class
AGENT_NUT_EXPORT void
    nutdevice_test (bool verbose);
//  @end


#endif
