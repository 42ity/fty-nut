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
    bool changed;
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
    self->changed = false;
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

//  Helper function
//  0 - OK, 1 - throw it away
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

//  Helper function
//  Remove uninteresting entries from 'ext' field
static void
clear_ext (zhash_t *hash)
{
    if (!hash)
        return;
    zlistx_t *to_delete = zlistx_new ();
    zlistx_set_destructor (to_delete, (czmq_destructor *) zstr_free);
    zlistx_set_duplicator (to_delete, (czmq_duplicator *) strdup);

    const char *item = (const char *) zhash_first (hash);
    while (item) {
        if (!streq (zhash_cursor (hash), "ip.1") &&
            !streq (zhash_cursor (hash), "upsconf_block") &&
            !streq (zhash_cursor (hash), "daisy_chain")) {
            zlistx_add_end (to_delete, (void *) zhash_cursor (hash));
        }
        item = (const char *) zhash_next (hash);
    }

    item = (const char *) zlistx_first (to_delete);
    while (item) {
        zhash_delete (hash, item);
        item = (const char *) zlistx_next (to_delete);
    }

    zlistx_destroy (&to_delete);
}

//  --------------------------------------------------------------------------
//  Helper function
//  compare 'ext' field in two messages
int nut_ext_value_is_the_same (bios_proto_t *m1, bios_proto_t *m2, const char *attr)
{
    if (!m1 || !m2 || !attr) return 0;
    
    const char *a1 = bios_proto_ext_string (m1, attr, "");
    const char *a2 = bios_proto_ext_string (m2, attr, "");
    return streq (a1, a2);
}

//  --------------------------------------------------------------------------
//  are there changes to be saved
bool
nut_changed(nut_t *self)
{
    return self->changed;
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

    if (filter_message (message)) {
        bios_proto_destroy (message_p);
        return;
    }

    bios_proto_t *asset = (bios_proto_t *) zhashx_lookup (self->assets, bios_proto_name (message));
    if (!asset) {
        clear_ext (bios_proto_ext (message));
        zhash_t *aux = bios_proto_get_aux (message);
        zhash_destroy (&aux);
        int rv = zhashx_insert (self->assets, bios_proto_name (message), message);
        assert (rv == 0);
        *message_p = NULL;
        self->changed = true;
        return;
    }

    if (streq (bios_proto_operation (message), BIOS_PROTO_ASSET_OP_CREATE) ||
        streq (bios_proto_operation (message), BIOS_PROTO_ASSET_OP_UPDATE)) {

        bios_proto_set_operation (asset, "%s", bios_proto_operation (message));
        if (bios_proto_ext_string (message, "ip.1", NULL)) {
            if (!nut_ext_value_is_the_same (asset, message, "ip.1")) {
                self->changed = true;
                bios_proto_ext_insert (asset, "ip.1", "%s", bios_proto_ext_string (message, "ip.1", ""));
            }
        }
        if (bios_proto_ext_string (message, "upsconf_block", NULL)) {
            if (!nut_ext_value_is_the_same (asset, message, "upsconf_block")) {
                self->changed = true;
                bios_proto_ext_insert (asset, "upsconf_block", "%s", bios_proto_ext_string (message, "upsconf_block", ""));
            }
        }
        if (bios_proto_ext_string (message, "daisy_chain", NULL)) {
            if (!nut_ext_value_is_the_same (asset, message, "daisy_chain")) {
                self->changed = true;
                bios_proto_ext_insert (asset, "daisy_chain", "%s", bios_proto_ext_string (message, "daisy_chain",""));
            }
        }
        bios_proto_destroy (message_p);
    }
    else
    if (streq (bios_proto_operation (message), BIOS_PROTO_ASSET_OP_DELETE) ||
        streq (bios_proto_operation (message), BIOS_PROTO_ASSET_OP_RETIRE)) {

        zhashx_delete (self->assets, bios_proto_name (message));
        self->changed = true;
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
    zlistx_t *list = zhashx_keys (self->assets);
    zlistx_set_comparator (list, (czmq_comparator *) strcmp);
    return list;
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
    self->changed = false;
    return 0;
}

//  --------------------------------------------------------------------------
//  Load nut from disk
//  If 'fullpath' is NULL does nothing
//  0 - success, -1 - error

int
nut_load (nut_t *self, const char *fullpath)
{
//    zsys_debug ("=====   LOAD ========");
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
//        bios_proto_print (asset);

        // nut_put (self, &asset);
        int rv = zhashx_insert (self->assets, bios_proto_name (asset), asset);
        assert (rv == 0);
    }
    zchunk_destroy (&chunk);
//    zsys_debug ("---------------------");
    self->changed = false;
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

static void
nut_print_zsys (nut_t *self)
{
    assert (self);
    bios_proto_t *asset = (bios_proto_t *) zhashx_first (self->assets);
    while (asset) {
        zsys_debug ("%s", (const char *) zhashx_cursor (self->assets));
        zsys_debug (
                "\t%s %s",
                bios_proto_name (asset),
                bios_proto_operation (asset));
        zsys_debug ("\tEXT:");
        zhash_t *ext = bios_proto_ext (asset);
        if (ext) {
            const char *attribute = (const char *) zhash_first (ext);
            while (attribute) {
                zsys_debug (
                        "\t\t\"%s\" = \"%s\"",
                        zhash_cursor (ext), attribute);
                attribute = (const char *) zhash_next (ext);
            }
        }
        else {
            zsys_debug ("\t\t(null)");
        }
        zsys_debug ("\tAUX:");
        zhash_t *aux = bios_proto_aux (asset);
        if (aux) {
            const char *attribute = (const char *) zhash_first (aux);
            while (attribute) {
                zsys_debug (
                        "\t\t\"%s\" = \"%s\"",
                        zhash_cursor (aux), attribute);
                attribute = (const char *) zhash_next (aux);
            }
        }
        else {
            zsys_debug ("\t\t(null)");
        }
        asset = (bios_proto_t *) zhashx_next (self->assets);
    }
}
//  --------------------------------------------------------------------------
//  Self test of this class

//  Helper test function
//  create new ASSET message of type bios_proto_t
static bios_proto_t *
test_asset_new (const char *name, const char *operation
        )
{
    assert (name);
    assert (operation);

    bios_proto_t *asset = bios_proto_new (BIOS_PROTO_ASSET);
    bios_proto_set_name (asset, "%s", name);
    bios_proto_set_operation (asset, "%s", operation);
    return asset;
}

//  Helper test function
//  0 - same, 1 - different
static int
test_zlistx_compare (zlistx_t *expected, zlistx_t **received_p, int print = 0)
{
    assert (expected);
    assert (received_p && *received_p);

    zlistx_t *received = *received_p;

    if (print) {
        zsys_debug ("received");
        const char *first = (const char *) zlistx_first (received);
        while (first) {
            zsys_debug ("\t%s", first);
            first = (const char *) zlistx_next (received);
        }
        zsys_debug ("expected");
        first = (const char *) zlistx_first (expected);
        while (first) {
            zsys_debug ("\t%s", first);
            first = (const char *) zlistx_next (expected);
        }
    }

    int rv = 1;
    const char *cursor = (const char *) zlistx_first (expected);
    while (cursor) {
        void *handle = zlistx_find (received, (void *) cursor);
        if (!handle)
            break;
        zlistx_delete (received, handle);
        cursor = (const char *) zlistx_next (expected);
    }
    if (zlistx_size (received) == 0)
        rv = 0;
    zlistx_destroy (received_p);
    *received_p = NULL;
    return rv;
}

void
nut_test (bool verbose)
{
    printf (" * nut: \n");

    //  @selftest

    //  Simple create/destroy test
    nut_t *self = nut_new ();
    assert (self);
    nut_destroy (&self);
    assert (self == NULL);

    //  Double destroy test
    nut_destroy (&self);
    nut_destroy (NULL);

    //  Test nut_ methods
    self = nut_new ();
    assert (nut_changed(self) == false);
    zlistx_t *expected = zlistx_new ();
    zlistx_set_destructor (expected, (czmq_destructor *) zstr_free);
    zlistx_set_duplicator (expected, (czmq_duplicator *) strdup);
    zlistx_set_comparator (expected, (czmq_comparator *) strcmp);

    bios_proto_t *asset =  test_asset_new ("ups", BIOS_PROTO_ASSET_OP_CREATE);
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "ups");
    bios_proto_ext_insert (asset, "abc.d", "%s", " ups string 1");
    nut_put (self, &asset);
    assert (nut_changed(self) == true);
    zlistx_add_end (expected, (void *) "ups");

    asset =  test_asset_new ("epdu", BIOS_PROTO_ASSET_OP_CREATE);
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "epdu");
    nut_put (self, &asset);
    zlistx_add_end (expected, (void *) "epdu");

    asset =  test_asset_new ("ROZ.UPS33", BIOS_PROTO_ASSET_OP_CREATE);
    bios_proto_ext_insert (asset, "d.ef", "%s", "roz.ups33 string 1");
    bios_proto_ext_insert (asset, "description" , "%s",  "UPS1 9PX 5000i");
    bios_proto_ext_insert (asset, "device.location" , "%s",  "New IT Power LAB");
    bios_proto_ext_insert (asset, "location_u_pos" , "%s",  "1");
    bios_proto_ext_insert (asset, "u_size" , "%s",  "3");
    bios_proto_ext_insert (asset, "device.contact" , "%s",  "Gabriel Szabo");
    bios_proto_ext_insert (asset, "hostname.1" , "%s",  "ups33.roz53.lab.etn.com");
    bios_proto_ext_insert (asset, "battery.type" , "%s",  "PbAc");
    bios_proto_ext_insert (asset, "phases.output" , "%s",  "1");
    bios_proto_ext_insert (asset,  "device.type" , "%s",  "ups");
    bios_proto_ext_insert (asset, "business_critical" , "%s",  "yes");
    bios_proto_ext_insert (asset, "status.outlet.2" , "%s",  "on");
    bios_proto_ext_insert (asset, "status.outlet.1" , "%s",  "on");
    bios_proto_ext_insert (asset, "serial_no" , "%s",  "G202D51129");
    bios_proto_ext_insert (asset, "ups.serial" , "%s",  "G202D51129");
    bios_proto_ext_insert (asset, "installation_date" , "%s",  "2015-01-05");
    bios_proto_ext_insert (asset, "model" , "%s",  "Eaton 9PX");
    bios_proto_ext_insert (asset, "phases.input" , "%s",  "1");
    bios_proto_ext_insert (asset, "ip.1" , "%s",  "10.130.53.33");
    bios_proto_ext_insert (asset, "ups.alarm" , "%s",  "Automatic bypass mode!");
    bios_proto_ext_insert (asset, "manufacturer" , "%s",  "EATON");

    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "ups");
    bios_proto_aux_insert (asset, "parent", "%s", "4");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "priority", "%s", "2");

    nut_put (self, &asset);
    zlistx_add_end (expected, (void *) "ROZ.UPS33");

    asset =  test_asset_new ("DUMMY.EPDU42", BIOS_PROTO_ASSET_OP_CREATE);
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "epdu");
    bios_proto_ext_insert (asset, "ip.1", "%s", "127.0.0.1");
    bios_proto_ext_insert (asset, "upsconf_block", "%s", "[DUMMY.EPDU42]\n\tdriver=dummy-ups\n\tport=/tmp/DUMMY.EPDU42.dev\n\n");
    nut_put (self, &asset);
    zlistx_add_end (expected, (void *) "DUMMY.EPDU42");

    asset =  test_asset_new ("MBT.EPDU4", BIOS_PROTO_ASSET_OP_CREATE);
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "epdu");
    bios_proto_ext_insert (asset, "hhh", "%s", "mbt.epdu4 string 1");
    bios_proto_ext_insert (asset, "ip.1", "%s", "10.130.53.33");
    bios_proto_ext_insert (asset, "daisy_chain", "%s", "3");
    nut_put (self, &asset);
    zlistx_add_end (expected, (void *) "MBT.EPDU4");

    asset =  test_asset_new ("ROZ.ePDU14", BIOS_PROTO_ASSET_OP_CREATE);

    bios_proto_ext_insert (asset, "status.outlet.13", "%s", "on");
    bios_proto_ext_insert (asset, "status.outlet.10", "%s", "on");
    bios_proto_ext_insert (asset, "status.outlet.11", "%s", "on");
    bios_proto_ext_insert (asset, "description" , "%s",  "ePDU14 G3");
    bios_proto_ext_insert (asset, "hostname.1" , "%s",  "epdu14.roz53.lab.etn.com");
    bios_proto_ext_insert (asset, "phases.output" , "%s",  "1");
    bios_proto_ext_insert (asset, "daisy_chain" , "%s",  "2");
    bios_proto_ext_insert (asset, "outlet.count" , "%s",  "16");
    bios_proto_ext_insert (asset, "device.type" , "%s",  "epdu");
    bios_proto_ext_insert (asset, "location_w_pos" , "%s",  "left");
    bios_proto_ext_insert (asset, "status.outlet.7" , "%s",  "on");
    bios_proto_ext_insert (asset, "status.outlet.6" , "%s",  "on");
    bios_proto_ext_insert (asset, "outlet.group.count" , "%s",  "1");
    bios_proto_ext_insert (asset, "status.outlet.5" , "%s",  "on");
    bios_proto_ext_insert (asset, "status.outlet.4" , "%s",  "on");
    bios_proto_ext_insert (asset, "business_critical" , "%s",  "yes");
    bios_proto_ext_insert (asset, "status.outlet.3" , "%s",  "on");
    bios_proto_ext_insert (asset, "status.outlet.2" , "%s",  "on");
    bios_proto_ext_insert (asset, "status.outlet.1" , "%s",  "on");
    bios_proto_ext_insert (asset, "status.outlet.9" , "%s",  "on");
    bios_proto_ext_insert (asset, "status.outlet.8" , "%s",  "on");
    bios_proto_ext_insert (asset, "serial_no" , "%s",  "H734F27009");
    bios_proto_ext_insert (asset, "ups.serial" , "%s",  "H734F27009");
    bios_proto_ext_insert (asset, "installation_date" , "%s",  "2015-01-05");
    bios_proto_ext_insert (asset, "http_link.1" , "%s",  "http://epdu14.roz53.lab.etn.com/");
    bios_proto_ext_insert (asset, "model" , "%s",  "EPDU MA 0U (C14 10A 1P)16XC13");
    bios_proto_ext_insert (asset, "phases.input" , "%s",  "1");
    bios_proto_ext_insert (asset, "ip.1" , "%s",  "10.130.53.33");
    bios_proto_ext_insert (asset, "device.part" , "%s",  "EMAB03");
    bios_proto_ext_insert (asset, "manufacturer" , "%s",  "EATON");
    bios_proto_ext_insert (asset, "status.outlet.16" , "%s",  "on");
    bios_proto_ext_insert (asset, "status.outlet.14" , "%s",  "on");
    bios_proto_ext_insert (asset, "status.outlet.15" , "%s",  "on");
    bios_proto_ext_insert (asset, "status.outlet.12" , "%s",  "on");

    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "epdu");
    bios_proto_aux_insert (asset, "parent", "%s", "4");
    bios_proto_aux_insert (asset, "status", "%s", "active");
    bios_proto_aux_insert (asset, "priority", "%s", "1");

    nut_put (self, &asset);
    zlistx_add_end (expected, (void *) "ROZ.ePDU14");

    asset =  test_asset_new ("MBT.EPDU5", BIOS_PROTO_ASSET_OP_CREATE);
    bios_proto_aux_insert (asset, "type", "%s", "datacenter");
    nut_put (self, &asset);

    asset =  test_asset_new ("abcde", BIOS_PROTO_ASSET_OP_CREATE);
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "server");
    nut_put (self, &asset);

    asset =  test_asset_new ("", BIOS_PROTO_ASSET_OP_CREATE);
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "server");
    nut_put (self, &asset);

    nut_print_zsys (self);

    // save/load
    int rv = nut_save (self, "./test_state_file");
    assert (rv == 0);
    assert (nut_changed (self) == false);
    nut_destroy (&self);

    self = nut_new ();
    rv = nut_load (self, "./test_state_file");
    assert (rv == 0);
    assert (nut_changed (self) == false);
    {
        zlistx_t *received = nut_get_assets (self);
        assert (received);

        rv = test_zlistx_compare (expected, &received);
        assert (rv == 0);

        assert (nut_asset_ip (self, "non-existing-asset") == NULL);
        assert (nut_asset_daisychain (self, "non-existing-asset") == NULL);

        assert (streq (nut_asset_ip (self, "ups"), ""));
        assert (streq (nut_asset_daisychain (self, "ups"), ""));

        assert (streq (nut_asset_ip (self, "epdu"), ""));
        assert (streq (nut_asset_daisychain (self, "epdu"), ""));

        assert (streq (nut_asset_ip (self, "ROZ.UPS33"), "10.130.53.33"));
        assert (streq (nut_asset_daisychain (self, "ROZ.UPS33"), ""));

        assert (streq (nut_asset_ip (self, "MBT.EPDU4"), "10.130.53.33"));
        assert (streq (nut_asset_daisychain (self, "MBT.EPDU4"), "3"));

        assert (streq (nut_asset_ip (self, "ROZ.ePDU14"), "10.130.53.33"));
        assert (streq (nut_asset_daisychain (self, "ROZ.ePDU14"), "2"));
    }

    asset = test_asset_new ("epdu", BIOS_PROTO_ASSET_OP_CREATE);
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "epdu");
    bios_proto_ext_insert (asset, "ip.1", "%s", "121.120.199.198");
    bios_proto_ext_insert (asset, "daisy_chain", "%s", "14");
    nut_put (self, &asset);

    asset =  test_asset_new ("ROZ.UPS33", BIOS_PROTO_ASSET_OP_UPDATE);
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "ups");
    bios_proto_ext_insert (asset, "ip.1", "%s", "1.1.2.3");
    bios_proto_ext_insert (asset, "daisy_chain", "%s", "5");
    nut_put (self, &asset);

    asset =  test_asset_new ("ups", BIOS_PROTO_ASSET_OP_RETIRE);
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "ups");
    bios_proto_ext_insert (asset, "d.ef", "%s", "roz.ups33 string 1");
    bios_proto_ext_insert (asset, "ip.1", "%s", "127.0.0.1");
    bios_proto_ext_insert (asset, "daisy_chain", "%s", "16");
    nut_put (self, &asset);
    void *handle = zlistx_find (expected, (void *) "ups");
    zlistx_delete (expected, handle);

    asset =  test_asset_new ("MBT.EPDU4", BIOS_PROTO_ASSET_OP_UPDATE);
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "epdu");
    bios_proto_ext_insert (asset, "ip.1", "%s", "10.130.38.52");
    nut_put (self, &asset);

    assert (nut_changed (self) == true);

    {
        zlistx_t *received = nut_get_assets (self);
        assert (received);

        rv = test_zlistx_compare (expected, &received);
        assert (rv == 0);

        assert (nut_asset_ip (self, "ups") == NULL);
        assert (nut_asset_daisychain (self, "ups") == NULL);

        assert (streq (nut_asset_ip (self, "epdu"), "121.120.199.198"));
        assert (streq (nut_asset_daisychain (self, "epdu"), "14"));

        assert (streq (nut_asset_ip (self, "ROZ.UPS33"), "1.1.2.3"));
        assert (streq (nut_asset_daisychain (self, "ROZ.UPS33"), "5"));

        assert (streq (nut_asset_ip (self, "MBT.EPDU4"), "10.130.38.52"));
        assert (streq (nut_asset_daisychain (self, "MBT.EPDU4"), "3"));

        assert (streq (nut_asset_ip (self, "ROZ.ePDU14"), "10.130.53.33"));
        assert (streq (nut_asset_daisychain (self, "ROZ.ePDU14"), "2"));
    }

    asset =  test_asset_new ("MBT.EPDU4", BIOS_PROTO_ASSET_OP_UPDATE);
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "epdu");
    bios_proto_ext_insert (asset, "daisy_chain", "%s", "44");
    nut_put (self, &asset);

    {
        zlistx_t *received = nut_get_assets (self);
        assert (received);

        rv = test_zlistx_compare (expected, &received);
        assert (rv == 0);

        assert (nut_asset_ip (self, "ups") == NULL);
        assert (nut_asset_daisychain (self, "ups") == NULL);

        assert (streq (nut_asset_ip (self, "epdu"), "121.120.199.198"));
        assert (streq (nut_asset_daisychain (self, "epdu"), "14"));

        assert (streq (nut_asset_ip (self, "ROZ.UPS33"), "1.1.2.3"));
        assert (streq (nut_asset_daisychain (self, "ROZ.UPS33"), "5"));

        assert (streq (nut_asset_ip (self, "MBT.EPDU4"), "10.130.38.52"));
        assert (streq (nut_asset_daisychain (self, "MBT.EPDU4"), "44"));

        assert (streq (nut_asset_ip (self, "ROZ.ePDU14"), "10.130.53.33"));
        assert (streq (nut_asset_daisychain (self, "ROZ.ePDU14"), "2"));
    }

    asset =  test_asset_new ("MBT.EPDU4", BIOS_PROTO_ASSET_OP_CREATE);
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "epdu");
    bios_proto_ext_insert (asset, "daisy_chain", "%s", "44");
    bios_proto_ext_insert (asset, "ip.1", "%s", "10.130.38.52");
    nut_put (self, &asset);

    {
        zlistx_t *received = nut_get_assets (self);
        assert (received);

        rv = test_zlistx_compare (expected, &received);
        assert (rv == 0);

        assert (nut_asset_ip (self, "ups") == NULL);
        assert (nut_asset_daisychain (self, "ups") == NULL);

        assert (streq (nut_asset_ip (self, "epdu"), "121.120.199.198"));
        assert (streq (nut_asset_daisychain (self, "epdu"), "14"));

        assert (streq (nut_asset_ip (self, "ROZ.UPS33"), "1.1.2.3"));
        assert (streq (nut_asset_daisychain (self, "ROZ.UPS33"), "5"));

        assert (streq (nut_asset_ip (self, "MBT.EPDU4"), "10.130.38.52"));
        assert (streq (nut_asset_daisychain (self, "MBT.EPDU4"), "44"));

        assert (streq (nut_asset_ip (self, "ROZ.ePDU14"), "10.130.53.33"));
        assert (streq (nut_asset_daisychain (self, "ROZ.ePDU14"), "2"));
    }

    asset =  test_asset_new ("ROZ.UPS33", BIOS_PROTO_ASSET_OP_DELETE);
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "ups");
    nut_put (self, &asset);
    handle = zlistx_find (expected, (void *) "ROZ.UPS33");
    zlistx_delete (expected, handle);

    asset =  test_asset_new ("epdu", BIOS_PROTO_ASSET_OP_DELETE);
    bios_proto_aux_insert (asset, "type", "%s", "device");
    bios_proto_aux_insert (asset, "subtype", "%s", "epdu");
    nut_put (self, &asset);
    handle = zlistx_find (expected, (void *) "epdu");
    zlistx_delete (expected, handle);

    {
        zlistx_t *received = nut_get_assets (self);
        assert (received);

        rv = test_zlistx_compare (expected, &received);
        assert (rv == 0);

        assert (nut_asset_ip (self, "ups") == NULL);
        assert (nut_asset_daisychain (self, "ups") == NULL);

        assert (nut_asset_ip (self, "epdu") == NULL);
        assert (nut_asset_daisychain (self, "epdu") == NULL);

        assert (nut_asset_ip (self, "ROZ.UPS33") == NULL);
        assert (nut_asset_daisychain (self, "ROZ.UPS33") == NULL);

        assert (streq (nut_asset_ip (self, "MBT.EPDU4"), "10.130.38.52"));
        assert (streq (nut_asset_daisychain (self, "MBT.EPDU4"), "44"));

        assert (streq (nut_asset_ip (self, "ROZ.ePDU14"), "10.130.53.33"));
        assert (streq (nut_asset_daisychain (self, "ROZ.ePDU14"), "2"));
    }

    zlistx_destroy (&expected);
    nut_destroy (&self);
    assert (self == NULL);

    //  @end
    printf ("OK\n");
}
