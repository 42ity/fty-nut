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

// 0 - OK, 1 - throw it away
static int 
filter_message (bios_proto_t *message)
{
    assert (message);
    const char *type = bios_proto_aux_string (message, "type", NULL);
    const char *subtype = bios_proto_aux_string (message, "subtype", NULL);
    if (!type || !subtype)
        return 1;
    if (streq (type, "device") &&
        (streq (subtype, "epdu") || streq (subtype, "ups")))
        return 0;
    return 1;
}

static void
zhash_merge (zhash_t *source, zhash_t *target)
{
    assert (target);
    if (!source)
        return;
    
    const char *item = (const char *) zhash_first (source);
    while (item) {
        zhash_update (target, (const char *) zhash_cursor (source), (char *) item);
        item = (const char *) zhash_next (source);
    }
}

void
nut_put (nut_t *self, bios_proto_t **message_p)
{
    assert (self);
    assert (message_p);

    bios_proto_t *message = *message_p;

    if (!message)
        return;

    if (filter_message (message)) {
        bios_proto_destroy (message_p);
        return;
    }
    

    bios_proto_t *asset = (bios_proto_t *) zhashx_lookup (self->assets, bios_proto_name (message));
    if (!asset) {
        int rv = zhashx_insert (self->assets, bios_proto_name (message), message);
        *message_p = NULL;    
        assert (rv == 0);
        return;
    }
    if (streq (bios_proto_operation (message), BIOS_PROTO_ASSET_OP_CREATE)) {
        zhashx_update (self->assets, bios_proto_name (message), message);
    }
    else
    if (streq (bios_proto_operation (message), BIOS_PROTO_ASSET_OP_UPDATE)) {
        bios_proto_set_operation (asset, "%s", bios_proto_operation (message));
        if (!bios_proto_ext (asset)) {
            zhash_t *ext = zhash_new ();
            zhash_autofree (ext);
            bios_proto_set_ext (asset, &ext);
        }
        zhash_merge (bios_proto_ext (message), bios_proto_ext (asset));
        if (!bios_proto_aux (asset)) {
            zhash_t *aux = zhash_new ();
            zhash_autofree (aux);
            bios_proto_set_aux (asset, &aux);
        }
        zhash_merge (bios_proto_aux (message), bios_proto_aux (asset));
        bios_proto_destroy (message_p);
    }
    else
    if (streq (bios_proto_operation (message), BIOS_PROTO_ASSET_OP_DELETE) ||
        streq (bios_proto_operation (message), BIOS_PROTO_ASSET_OP_RETIRE)) {
        zhashx_delete (self->assets, bios_proto_name (message));
        bios_proto_destroy (message_p);
    }
    else {
        log_error (
                "unknown asset operation '%s'. Skipping.",
                bios_proto_operation (message) ? bios_proto_operation (message) : "(null)");
        bios_proto_destroy (message_p);
    }
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
    return nut_asset_get_string (self, asset_name, "daisy_chain");
}

//  --------------------------------------------------------------------------
//  Save nut to disk
//  If 'fullpath' is NULL does nothing
//  0 - success, -1 - error

int
nut_save (nut_t *self, const char *fullpath)
{
    assert (self);
    if (!fullpath)
        return 0;

    zfile_t *file = zfile_new (NULL, fullpath);
    if (!file) {
        log_error ("zfile_new (path = NULL, file = '%s') failed.", fullpath);
        return -1;
    }

    zfile_remove (file);

    if (zfile_output (file) == -1) {
        log_error ("zfile_output () failed; filename = '%s'", zfile_filename (file, NULL));
        zfile_close (file);
        zfile_destroy (&file);
        return -1;
    }

    zchunk_t *chunk = zchunk_new (NULL, 0); // TODO: this can be tweaked to
                                            // avoid a lot of allocs
    assert (chunk);

    bios_proto_t *asset = (bios_proto_t *) zhashx_first (self->assets);
    while (asset) {
        bios_proto_t *duplicate = bios_proto_dup (asset);
        assert (duplicate);
        zmsg_t *zmessage = bios_proto_encode (&duplicate); // duplicate destroyed here
        assert (zmessage);

        byte *buffer = NULL;
        uint64_t size = zmsg_encode (zmessage, &buffer);
        zmsg_destroy (&zmessage);

        assert (buffer);
        assert (size > 0);

        // prefix
        zchunk_extend (chunk, (const void *) &size, sizeof (uint64_t));
        // data
        zchunk_extend (chunk, (const void *) buffer, size);

        free (buffer); buffer = NULL;

        asset = (bios_proto_t *) zhashx_next (self->assets);
    }

    if (zchunk_write (chunk, zfile_handle (file)) == -1) {
        log_error ("zchunk_write () failed.");
    }

    zchunk_destroy (&chunk);
    zfile_close (file);
    zfile_destroy (&file);
    return 0;
}

//  --------------------------------------------------------------------------
//  Load nut from disk
//  If 'fullpath' is NULL does nothing
//  0 - success, -1 - error

int
nut_load (nut_t *self, const char *fullpath)
{
    assert (self);
    if (!fullpath)
        return 0;

    zfile_t *file = zfile_new (NULL, fullpath);
    if (!file) {
        log_error ("zfile_new (path = NULL, file = '%s') failed.", fullpath);
        return -1;
    }
    if (!zfile_is_regular (file)) {
        log_error ("zfile_is_regular () == false");
        zfile_close (file);
        zfile_destroy (&file);
        return -1;
    }
    if (zfile_input (file) == -1) {
        zfile_close (file);
        zfile_destroy (&file);
        log_error ("zfile_input () failed; filename = '%s'", zfile_filename (file, NULL));
        return -1;
    }

    off_t cursize = zfile_cursize (file);
    if (cursize == 0) {
        log_debug ("state file '%s' is empty", zfile_filename (file, NULL));
        zfile_close (file);
        zfile_destroy (&file);
        return 0;
    }

    zchunk_t *chunk = zchunk_read (zfile_handle (file), cursize);
    assert (chunk);

    zfile_close (file);
    zfile_destroy (&file);

    off_t offset = 0;

    while (offset < cursize) {
        byte *prefix = zchunk_data (chunk) + offset;
        byte *data = zchunk_data (chunk) + offset + sizeof (uint64_t);
        offset += (uint64_t) *prefix +  sizeof (uint64_t);

        zmsg_t *zmessage = zmsg_decode (data, (size_t) *prefix);
        assert (zmessage);
        bios_proto_t *asset = bios_proto_decode (&zmessage); // zmessage destroyed
        assert (asset);

        nut_put (self, &asset);
    }
    zchunk_destroy (&chunk);
    return 0;
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
    
    zhash_t *ext = zhash_new ();
    zhash_autofree (ext);

    zhash_t *aux = zhash_new ();
    zhash_autofree (aux);


    bios_proto_t *asset = bios_proto_new (BIOS_PROTO_ASSET);
    bios_proto_set_name (asset, "%s", name);
    bios_proto_set_operation (asset, "%s", operation);
    bios_proto_set_ext (asset, &ext);
    bios_proto_set_aux (asset, &aux);
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

    // zhash_merge
    {
        zhash_t *source = zhash_new ();
        zhash_t *target = zhash_new ();

        zhash_insert (source, "a", (void *) "aaaa");
        zhash_insert (source, "b", (void *) "bbbb");
        zhash_insert (source, "ab", (void *) "aabb");

        zhash_insert (target, "ab", (void *) "cccc");
        zhash_insert (target, "d", (void *) "dddd");

        zhash_merge (source, target);

        const char *item = (const char *) zhash_lookup (target, "a");
        assert (item);
        assert (streq (item, "aaaa"));
        
        item = (const char *) zhash_lookup (target, "b");
        assert (item);
        assert (streq (item, "bbbb"));

        item = (const char *) zhash_lookup (target, "ab");
        assert (item);
        assert (streq (item, "aabb"));

        item = (const char *) zhash_lookup (target, "d");
        assert (item);
        assert (streq (item, "dddd"));

        item = (const char *) zhash_lookup (source, "a");
        assert (item);
        assert (streq (item, "aaaa"));

        item = (const char *) zhash_lookup (source, "b");
        assert (item);
        assert (streq (item, "bbbb"));

        item = (const char *) zhash_lookup (source, "ab");
        assert (item);
        assert (streq (item, "aabb"));

        assert (zhash_size (target) == 4);
        assert (zhash_size (source) == 3);

        zhash_destroy (&source);
        zhash_destroy (&target);
    }
    {
        zhash_t *source = zhash_new ();
        zhash_t *target = zhash_new ();

        zhash_insert (source, "a", (void *) "aaaa");
        zhash_insert (source, "b", (void *) "bbbb");
        zhash_insert (source, "ab", (void *) "aabb");

        zhash_merge (source, target);

        const char *item = (const char *) zhash_lookup (target, "a");
        assert (item);
        assert (streq (item, "aaaa"));
        
        item = (const char *) zhash_lookup (target, "b");
        assert (item);
        assert (streq (item, "bbbb"));

        item = (const char *) zhash_lookup (target, "ab");
        assert (item);
        assert (streq (item, "aabb"));

        item = (const char *) zhash_lookup (source, "a");
        assert (item);
        assert (streq (item, "aaaa"));

        item = (const char *) zhash_lookup (source, "b");
        assert (item);
        assert (streq (item, "bbbb"));

        item = (const char *) zhash_lookup (source, "ab");
        assert (item);
        assert (streq (item, "aabb"));

        assert (zhash_size (target) == 3);
        assert (zhash_size (source) == 3);

        zhash_destroy (&source);
        zhash_destroy (&target);
    }
    {
        zhash_t *source = zhash_new ();
        zhash_t *target = zhash_new ();

        zhash_insert (target, "a", (void *) "aaaa");
        zhash_insert (target, "b", (void *) "bbbb");
        zhash_insert (target, "ab", (void *) "aabb");

        zhash_merge (source, target);

        const char *item = (const char *) zhash_lookup (target, "a");
        assert (item);
        assert (streq (item, "aaaa"));
        
        item = (const char *) zhash_lookup (target, "b");
        assert (item);
        assert (streq (item, "bbbb"));

        item = (const char *) zhash_lookup (target, "ab");
        assert (item);
        assert (streq (item, "aabb"));

        assert (zhash_size (target) == 3);
        assert (zhash_size (source) == 0);

        zhash_destroy (&source);
        zhash_destroy (&target);
    }   
    //  Test methods 
    self = nut_new ();

    bios_proto_t *asset =  test_asset_new ("ups", BIOS_PROTO_ASSET_OP_CREATE);
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "ups");
    bios_proto_ext_insert (asset, "abc.d", "%s", " ups string 1");
    nut_put (self, &asset);

    asset =  test_asset_new ("epdu", BIOS_PROTO_ASSET_OP_CREATE);
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "epdu");
    nut_put (self, &asset);

    asset =  test_asset_new ("ROZ.UPS33", BIOS_PROTO_ASSET_OP_CREATE);
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "ups");
    bios_proto_ext_insert (asset, "d.ef", "%s", "roz.ups33 string 1");
    nut_put (self, &asset);

    asset =  test_asset_new ("MBT.EPDU4", BIOS_PROTO_ASSET_OP_CREATE);
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "epdu");
    bios_proto_ext_insert (asset, "hhh", "%s", "mbt.epdu4 string 1");
    bios_proto_ext_insert (asset, "ip.1", "%s", "4.3.2.1");
    bios_proto_ext_insert (asset, "daisychain", "%s", "3");
    nut_put (self, &asset);

    asset =  test_asset_new ("MBT.EPDU5", BIOS_PROTO_ASSET_OP_CREATE);
    nut_put (self, &asset);

    // save/load
    
    int rv = nut_save (self, "./test_state_file");
    assert (rv == 0);
    nut_destroy (&self);
    self = nut_new ();
    rv = nut_load (self, "./test_state_file");
    assert (rv == 0);

    // nut_get_assets
    {
        zlistx_t *list = nut_get_assets (self);
        assert (list);
        
        const char *asset_name = (const char *) zlistx_first (list);
        assert (asset_name);
        assert (streq (asset_name, "epdu"));
        
        asset_name = (const char *) zlistx_next (list);
        printf ("%s\n", asset_name);
        assert (asset_name);
        assert (streq (asset_name, "ROZ.UPS33"));
             
        asset_name = (const char *) zlistx_next (list);
        assert (asset_name);
        assert (streq (asset_name, "MBT.EPDU4"));

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

        assert (streq (nut_asset_ip (self, "MBT.EPDU4"), "4.3.2.1"));
        assert (streq (nut_asset_daisychain (self, "MBT.EPDU4"), "3"));
    }

    asset = test_asset_new ("epdu", BIOS_PROTO_ASSET_OP_UPDATE);
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "epdu");
    bios_proto_ext_insert (asset, "xxx", "%s", "epdu string 1");
    bios_proto_ext_insert (asset, "ip.1", "%s", "121.120.199.198");
    bios_proto_ext_insert (asset, "daisychain", "%s", "14");
    nut_put (self, &asset);

    asset =  test_asset_new ("ROZ.UPS33", BIOS_PROTO_ASSET_OP_UPDATE);
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "ups");
    bios_proto_ext_insert (asset, "d.ef", "%s", "roz.ups33 string 1");
    bios_proto_ext_insert (asset, "ip.1", "%s", "1.1.2.3");
    bios_proto_ext_insert (asset, "daisychain", "%s", "5");
    nut_put (self, &asset);

    asset =  test_asset_new ("ups", BIOS_PROTO_ASSET_OP_RETIRE);
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "ups");
    bios_proto_ext_insert (asset, "d.ef", "%s", "roz.ups33 string 1");
    bios_proto_ext_insert (asset, "ip.1", "%s", "127.0.0.1");
    bios_proto_ext_insert (asset, "daisychain", "%s", "16");
    nut_put (self, &asset);

    asset =  test_asset_new ("MBT.EPDU4", BIOS_PROTO_ASSET_OP_UPDATE);
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "epdu");
    bios_proto_ext_insert (asset, "ip.1", "%s", "10.130.38.52");
    bios_proto_ext_insert (asset, "daisychain", "%s", "44");
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
        assert (streq (asset_name, "ROZ.UPS33"));
    
        asset_name = (const char *) zlistx_next (list);
        assert (asset_name);
        assert (streq (asset_name, "MBT.EPDU4"));

        asset_name = (const char *) zlistx_next (list);
        assert (asset_name == NULL);
        zlistx_destroy (&list);
    }

    // nut_asset_daisychain, nut_asset_ip
    {
        assert (nut_asset_ip (self, "non-existing-asset") == NULL);
        assert (nut_asset_daisychain (self, "non-existing-asset") == NULL);

        assert (nut_asset_ip (self, "ups") == NULL);
        assert (nut_asset_daisychain (self, "ups") == NULL);

        assert (streq (nut_asset_ip (self, "epdu"), "121.120.199.198"));
        assert (streq (nut_asset_daisychain (self, "epdu"), "14"));

        assert (streq (nut_asset_ip (self, "ROZ.UPS33"), "1.1.2.3"));
        assert (streq (nut_asset_daisychain (self, "ROZ.UPS33"), "5"));

        assert (streq (nut_asset_ip (self, "MBT.EPDU4"), "10.130.38.52"));
        assert (streq (nut_asset_daisychain (self, "MBT.EPDU4"), "44"));
    }

    asset =  test_asset_new ("MBT.EPDU4", BIOS_PROTO_ASSET_OP_UPDATE);
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "epdu");
    bios_proto_ext_insert (asset, "ip.1", "%s", "50.50.50.50");
    nut_put (self, &asset);

    // nut_asset_daisychain, nut_asset_ip
    {
        assert (nut_asset_ip (self, "non-existing-asset") == NULL);
        assert (nut_asset_daisychain (self, "non-existing-asset") == NULL);

        assert (nut_asset_ip (self, "ups") == NULL);
        assert (nut_asset_daisychain (self, "ups") == NULL);

        assert (streq (nut_asset_ip (self, "epdu"), "121.120.199.198"));
        assert (streq (nut_asset_daisychain (self, "epdu"), "14"));

        assert (streq (nut_asset_ip (self, "ROZ.UPS33"), "1.1.2.3"));
        assert (streq (nut_asset_daisychain (self, "ROZ.UPS33"), "5"));

        assert (streq (nut_asset_ip (self, "MBT.EPDU4"), "50.50.50.50"));
        assert (streq (nut_asset_daisychain (self, "MBT.EPDU4"), "44"));
    }

    asset =  test_asset_new ("MBT.EPDU4", BIOS_PROTO_ASSET_OP_UPDATE);
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "epdu");
    bios_proto_ext_insert (asset, "daisychain", "%s", "77");
    nut_put (self, &asset);

    // nut_asset_daisychain, nut_asset_ip
    {
        assert (nut_asset_ip (self, "non-existing-asset") == NULL);
        assert (nut_asset_daisychain (self, "non-existing-asset") == NULL);

        assert (nut_asset_ip (self, "ups") == NULL);
        assert (nut_asset_daisychain (self, "ups") == NULL);

        assert (streq (nut_asset_ip (self, "epdu"), "121.120.199.198"));
        assert (streq (nut_asset_daisychain (self, "epdu"), "14"));

        assert (streq (nut_asset_ip (self, "ROZ.UPS33"), "1.1.2.3"));
        assert (streq (nut_asset_daisychain (self, "ROZ.UPS33"), "5"));

        assert (streq (nut_asset_ip (self, "MBT.EPDU4"), "50.50.50.50"));
        assert (streq (nut_asset_daisychain (self, "MBT.EPDU4"), "77"));
    }

    asset =  test_asset_new ("MBT.EPDU4", BIOS_PROTO_ASSET_OP_CREATE);
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "epdu");
    bios_proto_ext_insert (asset, "daisychain", "%s", "43");
    nut_put (self, &asset);

    // nut_asset_daisychain, nut_asset_ip
    {
        assert (nut_asset_ip (self, "non-existing-asset") == NULL);
        assert (nut_asset_daisychain (self, "non-existing-asset") == NULL);

        assert (nut_asset_ip (self, "ups") == NULL);
        assert (nut_asset_daisychain (self, "ups") == NULL);

        assert (streq (nut_asset_ip (self, "epdu"), "121.120.199.198"));
        assert (streq (nut_asset_daisychain (self, "epdu"), "14"));

        assert (streq (nut_asset_ip (self, "ROZ.UPS33"), "1.1.2.3"));
        assert (streq (nut_asset_daisychain (self, "ROZ.UPS33"), "5"));

        assert (streq (nut_asset_ip (self, "MBT.EPDU4"), ""));
        assert (streq (nut_asset_daisychain (self, "MBT.EPDU4"), "43"));
    }

    asset =  test_asset_new ("ROZ.UPS33", BIOS_PROTO_ASSET_OP_DELETE);
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "ups");
    nut_put (self, &asset);

    asset =  test_asset_new ("epdu", BIOS_PROTO_ASSET_OP_DELETE);
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "epdu");
    nut_put (self, &asset);

    // nut_get_assets
    {
        zlistx_t *list = nut_get_assets (self);
        assert (list);

        const char *asset_name = (const char *) zlistx_first (list);
        assert (asset_name);
        assert (streq (asset_name, "MBT.EPDU4"));

        asset_name = (const char *) zlistx_next (list);
        assert (asset_name == NULL);
        zlistx_destroy (&list);
    }

    // nut_asset_daisychain, nut_asset_ip
    {
        assert (nut_asset_ip (self, "ups") == NULL);
        assert (nut_asset_daisychain (self, "ups") == NULL);

        assert (nut_asset_ip (self, "epdu") == NULL);
        assert (nut_asset_daisychain (self, "epdu") == NULL);

        assert (nut_asset_ip (self, "ROZ.UPS33") == NULL);
        assert (nut_asset_daisychain (self, "ROZ.UPS33") == NULL);

        assert (streq (nut_asset_ip (self, "MBT.EPDU4"), ""));
        assert (streq (nut_asset_daisychain (self, "MBT.EPDU4"), "43"));
    }

    nut_destroy (&self);
    assert (self == NULL);

    //  @end
    printf ("OK\n");
}
