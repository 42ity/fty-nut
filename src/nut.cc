/*  =========================================================================
    nut - agent nut structure

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
    nut - agent nut structure
@discuss
@end
*/

#include "agent_nut_classes.h"

//  Structure of our class

struct _nut_t {
    zhashx_t *assets;      // hash of messages ("device name", bios_proto_t*)
};


//  --------------------------------------------------------------------------
//  Create a new nut

nut_t *
nut_new (void)
{
    nut_t *self = (nut_t *) zmalloc (sizeof (nut_t));
    assert (self);
    //  Initialize class properties here
    self->assets = zhashx_new ();
    zhashx_set_destructor (self->assets, (zhashx_destructor_fn *) bios_proto_destroy);
    return self;
}


//  --------------------------------------------------------------------------
//  Destroy the nut

void
nut_destroy (nut_t **self_p)
{
    if (!self_p)
        return;
    if (*self_p) {
        nut_t *self = *self_p;
        //  Free class properties here
        //  Free object itself
        
        zhashx_destroy (&self->assets);

        free (self);
        *self_p = NULL;
    }
}

//  --------------------------------------------------------------------------
//  Store bios_proto_t message transfering ownership

void
nut_put (nut_t *self, bios_proto_t **message_p)
{
    assert (self);
    assert (message_p);

    bios_proto_t *message = *message_p;

    if (!message)
        return;
     
    zhashx_t *asset = (zhashx_t *) zhashx_lookup (self->assets, bios_proto_name (message));
    if (!asset) {
        int rv = zhashx_insert (self->assets, bios_proto_name (message), message);
        assert (rv == 0);
    }
    // TODO
    *message_p = NULL;    
}

//  --------------------------------------------------------------------------
//  Get list of asset names

zlistx_t *
nut_get_assets (nut_t *self)
{
    assert (self);
    return zhashx_keys (self->assets);
}

// Helper function for nut_asset_XXX
static const char *
nut_asset_get_string (nut_t *self, const char *asset_name, const char *ext_key)
{
    assert (self);
    assert (asset_name);
    assert (ext_key);
    
    bios_proto_t *asset = (bios_proto_t *) zhashx_lookup (self->assets, asset_name);
    if (!asset) {
        return NULL;
    }
    static const char *default_value = "";
    return bios_proto_ext_string (asset, ext_key, default_value);
}

//  --------------------------------------------------------------------------
// Returns ip address (well-known extended attribute 'ip.1') of given asset
// or NULL when asset_name does not exist
// or "" (empty string) when given asset does not have ip address specified

const char *
nut_asset_ip (nut_t *self, const char *asset_name)
{
    return nut_asset_get_string (self, asset_name, "ip.1");
}

//  --------------------------------------------------------------------------
// Returns daisychain number (well-known extended attribute '...') of give asset
// or NULL when asset_name does not exist
// or "" (empty string) when given 

const char *
nut_asset_daisychain (nut_t *self, const char *asset_name)
{
    return nut_asset_get_string (self, asset_name, "daisychain");
}


//  --------------------------------------------------------------------------
//  Print the nut

void
nut_print (nut_t *self)
{
    assert (self);
    bios_proto_t *asset = (bios_proto_t *) zhashx_first (self->assets);
    while (asset) {
        log_debug ("%s", (const char *) zhashx_cursor (self->assets));
        log_debug (
                "\t%s %s",
                bios_proto_name (asset),
                bios_proto_operation (asset));
        log_debug ("\tEXT:");
        zhash_t *ext = bios_proto_ext (asset);
        if (ext) {
            const char *attribute = (const char *) zhash_first (ext);
            while (attribute) {
                log_debug (                                        
                        "\t\t\"%s\" = \"%s\"",
                        zhash_cursor (ext), attribute);
                attribute = (const char *) zhash_next (ext);
            }
        }
        else {
            log_debug ("\t\t(null)");
        }
        log_debug ("\tAUX:");
        zhash_t *aux = bios_proto_aux (asset);
        if (aux) {
            const char *attribute = (const char *) zhash_first (aux);
            while (attribute) {
                log_debug (                                        
                        "\t\t\"%s\" = \"%s\"",
                        zhash_cursor (aux), attribute);
                attribute = (const char *) zhash_next (aux);
            }
        }
        else {
            log_debug ("\t\t(null)");
        }
        asset = (bios_proto_t *) zhashx_next (self->assets);
    }
}

//  --------------------------------------------------------------------------
//  Self test of this class


static bios_proto_t *
test_asset_new (
        const char *name,
        const char *operation
        )
{
    assert (name);
    assert (operation);

    bios_proto_t *asset = bios_proto_new (BIOS_PROTO_ASSET);
    bios_proto_set_name (asset, "%s", name);
    bios_proto_set_operation (asset, "%s", operation);
    return asset;
}

void
nut_test (bool verbose)
{
    printf (" * nut: \n");

    //  @selftest
    //  Simple create/destroy test
    nut_t *self = NULL;
    nut_destroy (&self);
    nut_destroy (NULL);
    self = nut_new ();
    assert (self);
    nut_destroy (&self);
    assert (self == NULL);
    nut_destroy (&self);

    //  Test methods 
    self = nut_new ();

    bios_proto_t *asset =  test_asset_new ("ups", BIOS_PROTO_ASSET_OP_CREATE);
    bios_proto_ext_insert (asset, "abc.d", "%s", " ups string 1");
    nut_put (self, &asset);

    asset =  test_asset_new ("epdu", BIOS_PROTO_ASSET_OP_CREATE);
    nut_put (self, &asset);

    asset =  test_asset_new ("ROZ.UPS33", BIOS_PROTO_ASSET_OP_CREATE);
    bios_proto_ext_insert (asset, "d.ef", "%s", "roz.ups33 string 1");
    nut_put (self, &asset);

    asset =  test_asset_new ("MBT.EPDU4", BIOS_PROTO_ASSET_OP_CREATE);
    bios_proto_ext_insert (asset, "hhh", "%s", "mbt.epdu4 string 1");
    nut_put (self, &asset);

    // nut_get_assets
    {
        zlistx_t *list = nut_get_assets (self);
        assert (list);

        const char *asset_name = (const char *) zlistx_first (list);
        assert (asset_name);
        assert (streq (asset_name, "epdu"));
        
        asset_name = (const char *) zlistx_next (list);
        assert (asset_name);
        assert (streq (asset_name, "MBT.EPDU4"));
     
        asset_name = (const char *) zlistx_next (list);
        assert (asset_name);
        assert (streq (asset_name, "ROZ.UPS33"));

        asset_name = (const char *) zlistx_next (list);
        assert (asset_name);
        assert (streq (asset_name, "ups"));

        asset_name = (const char *) zlistx_next (list);
        assert (asset_name == NULL);
        zlistx_destroy (&list);
    }

    // nut_asset_daisychain, nut_asset_ip
    {
        assert (nut_asset_ip (self, "non-existing-asset") == NULL);
        assert (nut_asset_daisychain (self, "non-existing-asset") == NULL);

        assert (streq (nut_asset_ip (self, "ups"), ""));
        assert (streq (nut_asset_daisychain (self, "ups"), ""));

        assert (streq (nut_asset_ip (self, "epdu"), ""));
        assert (streq (nut_asset_daisychain (self, "epdu"), ""));

        assert (streq (nut_asset_ip (self, "ROZ.UPS33"), ""));
        assert (streq (nut_asset_daisychain (self, "ROZ.UPS33"), ""));

        assert (streq (nut_asset_ip (self, "MBT.EPDU4"), ""));
        assert (streq (nut_asset_daisychain (self, "MBT.EPDU4"), ""));
    }


    // WIP
    /*
    asset =  test_asset_new ("epdu", "UPDATE"); 
    nut_put (self, &asset);

    asset =  test_asset_new ("ROZ.UPS33", "UPDATE");
    bios_proto_ext_insert (asset, "d.ef", "%s", "roz.ups33 string 1");
    nut_put (self, &asset);



    printf ("--- Printing ---\n");
    nut_print (self);    
    */

    /*
    ups
    bios_proto_ext_insert (asset, "ip.1", "%s", "1.1.2.3");

    ROZ.UPS33
    bios_proto_ext_insert (asset, "ip.1", "%s", "122.13.1.24");
    bios_proto_ext_insert (asset, "daisychain", "%s", "5");

    MBT.EPDU4
    bios_proto_ext_insert (asset, "ip.1", "%s", "4.3.2.1");
    bios_proto_ext_insert (asset, "daisychain", "%s", "3");
    */


    nut_destroy (&self);
    assert (self == NULL);

    //  @end
    printf ("OK\n");
}
