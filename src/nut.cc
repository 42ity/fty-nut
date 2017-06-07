/*  =========================================================================
    nut - agent nut structure

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
    nut - agent nut structure
@discuss
@end
*/

#include "fty_nut_classes.h"

//  Structure of our class

struct _nut_t {
    zhashx_t *assets;      // hash of messages ("device name", fty_proto_t*)
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
    zhashx_set_destructor (self->assets, (zhashx_destructor_fn *) fty_proto_destroy);
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
filter_message (fty_proto_t *message)
{
    assert (message);
    const char *type = fty_proto_aux_string (message, "type", NULL);
    const char *subtype = fty_proto_aux_string (message, "subtype", NULL);
    if (!type || !subtype)
        return 1;
    if (streq (type, "device") &&
        (streq (subtype, "epdu") || streq (subtype, "ups") || streq (subtype, "sts") || streq(subtype, "sensor") ))
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
            !streq (zhash_cursor (hash), "daisy_chain") &&
            !streq (zhash_cursor (hash), "port") &&
            !streq (zhash_cursor (hash), "subtype") &&
            !streq (zhash_cursor (hash), "parent_name.1") &&
            !streq (zhash_cursor (hash), "logical_asset") &&
            !streq (zhash_cursor (hash), "max_current") &&
            !streq (zhash_cursor (hash), "max_power") )
        {
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
int nut_ext_value_is_the_same (fty_proto_t *m1, fty_proto_t *m2, const char *attr)
{
    if (!m1 || !m2 || !attr) return 0;

    const char *a1 = fty_proto_ext_string (m1, attr, "");
    const char *a2 = fty_proto_ext_string (m2, attr, "");
    return streq (a1, a2);
}
//  --------------------------------------------------------------------------
//  Helper function
//  compare 'aux' field in two messages
int nut_aux_value_is_the_same (fty_proto_t *m1, fty_proto_t *m2, const char *attr)
{
    if (!m1 || !m2 || !attr) return 0;

    const char *a1 = fty_proto_aux_string (m1, attr, "");
    const char *a2 = fty_proto_aux_string (m2, attr, "");
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
//  Store fty_proto_t message transfering ownership

void
nut_put (nut_t *self, fty_proto_t **message_p)
{
    assert (self);
    assert (message_p);

    fty_proto_t *message = *message_p;

    if (!message)
        return;

    if (filter_message (message)) {
        fty_proto_destroy (message_p);
        return;
    }
    // copy from aux to ext what we need
    const char *tmp = fty_proto_aux_string (message,"subtype", NULL);
    if (tmp) fty_proto_ext_insert (message, "subtype", "%s", tmp);
    tmp = fty_proto_aux_string (message,"parent_name.1", NULL);
    if (tmp) fty_proto_ext_insert (message, "parent_name.1", "%s", tmp);

    fty_proto_t *asset = (fty_proto_t *) zhashx_lookup (self->assets, fty_proto_name (message));
    if (!asset) {
        clear_ext (fty_proto_ext (message));
        zhash_t *aux = fty_proto_get_aux (message);
        zhash_destroy (&aux);
        int rv = zhashx_insert (self->assets, fty_proto_name (message), message);
        assert (rv == 0);
        *message_p = NULL;
        self->changed = true;
        return;
    }

    if (streq (fty_proto_operation (message), FTY_PROTO_ASSET_OP_CREATE) ||
        streq (fty_proto_operation (message), FTY_PROTO_ASSET_OP_UPDATE)) {

        fty_proto_set_operation (asset, "%s", fty_proto_operation (message));
        if (!nut_ext_value_is_the_same (asset, message, "ip.1")) {
            self->changed = true;
            fty_proto_ext_insert (asset, "ip.1", "%s", fty_proto_ext_string (message, "ip.1", ""));
        }
        if (!nut_ext_value_is_the_same (asset, message, "upsconf_block")) {
            self->changed = true;
            fty_proto_ext_insert (asset, "upsconf_block", "%s", fty_proto_ext_string (message, "upsconf_block", ""));
        }
        if (!nut_ext_value_is_the_same (asset, message, "daisy_chain")) {
            self->changed = true;
            fty_proto_ext_insert (asset, "daisy_chain", "%s", fty_proto_ext_string (message, "daisy_chain",""));
        }
        if (!nut_ext_value_is_the_same (asset, message, "type")) {
            self->changed = true;
            fty_proto_ext_insert (asset, "type", "%s", fty_proto_ext_string (message, "type",""));
        }
        if (!nut_ext_value_is_the_same (asset, message, "subtype")) {
            self->changed = true;
            fty_proto_ext_insert (asset, "subtype", "%s", fty_proto_ext_string (message, "subtype",""));
        }
        if (!nut_ext_value_is_the_same (asset, message, "port")) {
            self->changed = true;
            fty_proto_ext_insert (asset, "port", "%s", fty_proto_ext_string (message, "port",""));
        }
        if (!nut_ext_value_is_the_same (asset, message, "logical_asset")) {
            self->changed = true;
            fty_proto_ext_insert (asset, "logical_asset", "%s", fty_proto_ext_string (message, "logical_asset",""));
        }

        if (!nut_ext_value_is_the_same (asset, message, "parent_name.1")) {
            self->changed = true;
            fty_proto_ext_insert (asset, "parent_name.1", "%s", fty_proto_ext_string (message, "parent_name.1",""));
        }

        if (!nut_ext_value_is_the_same (asset, message, "subtype")) {
            self->changed = true;
            fty_proto_ext_insert (asset, "subtype", "%s", fty_proto_ext_string (message, "subtype",""));
        }

        if (!nut_ext_value_is_the_same (asset, message, "max_current")) {
            self->changed = true;
            fty_proto_ext_insert (asset, "max_current", "%s", fty_proto_ext_string (message, "max_current",""));
        }

        if (!nut_ext_value_is_the_same (asset, message, "max_power")) {
            self->changed = true;
            fty_proto_ext_insert (asset, "max_power", "%s", fty_proto_ext_string (message, "max_power",""));
        }

        fty_proto_destroy (message_p);
    }
    else
    if (streq (fty_proto_operation (message), FTY_PROTO_ASSET_OP_DELETE) ||
        streq (fty_proto_operation (message), FTY_PROTO_ASSET_OP_RETIRE)) {

        zhashx_delete (self->assets, fty_proto_name (message));
        self->changed = true;
        fty_proto_destroy (message_p);
    }
    else {
        log_error (
                "unknown asset operation '%s'. Skipping.",
                fty_proto_operation (message) ? fty_proto_operation (message) : "(null)");
        fty_proto_destroy (message_p);
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

//  --------------------------------------------------------------------------
// Get list of sensors names
zlist_t *
nut_get_sensors (nut_t *self)
{
    assert (self);
    zlistx_t *list = zhashx_keys (self->assets);
    zlistx_set_comparator (list, (czmq_comparator *) strcmp);
    zlist_t *result = zlist_new();
    zlist_autofree (result);
    zlist_comparefn (result, (zlist_compare_fn *) strcmp);
    char * name = (char *)zlistx_first (list);
    while (name) {
        const char *subtype = nut_asset_subtype(self, name);
        if (subtype && streq (subtype, "sensor")) {
            zlist_append (result, name);
        }
        name = (char *)zlistx_next (list);
    }
    zlistx_destroy (&list);
    return result;
}

//  --------------------------------------------------------------------------
// Get list of names of all UPS and PDU
zlist_t *
nut_get_powerdevices (nut_t *self)
{
    assert (self);
    zlistx_t *list = zhashx_keys (self->assets);
    zlistx_set_comparator (list, (czmq_comparator *) strcmp);
    zlist_t *result = zlist_new();
    zlist_autofree (result);
    zlist_comparefn (result, (zlist_compare_fn *) strcmp);
    char * name = (char *) zlistx_first (list);
    while (name) {
        const char *subtype = nut_asset_subtype(self, name);
        if (subtype && (streq (subtype, "ups") || streq (subtype, "pdu") || streq (subtype, "epdu") || streq (subtype, "sts"))) {
            zlist_append (result, name);
        }
        name = (char *)zlistx_next (list);
    }
    zlistx_destroy (&list);
    return result;
}

// Helper function for nut_asset_XXX
const char *
nut_asset_get_string (nut_t *self, const char *asset_name, const char *ext_key)
{
    assert (self);
    assert (asset_name);
    assert (ext_key);

    fty_proto_t *asset = (fty_proto_t *) zhashx_lookup (self->assets, asset_name);
    if (!asset) {
        return NULL;
    }
    static const char *default_value = "";
    return fty_proto_ext_string (asset, ext_key, default_value);
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
// or "" (empty string) when given asset does not have daisychain number specified
const char *
nut_asset_daisychain (nut_t *self, const char *asset_name)
{
    return nut_asset_get_string (self, asset_name, "daisy_chain");
}

// ---------------------------------------------------------------------------
// return port string of given asset
// or NULL when asset_name does not exist
// or "" (empty string) when given asset does not have port specified
const char *
nut_asset_port (nut_t *self, const char *asset_name)
{
    return nut_asset_get_string (self, asset_name, "port");
}

// ---------------------------------------------------------------------------
// return asset subtype string of given asset
// or NULL when asset_name does not exist
// or "" (empty string) when given asset does not have asset subtype specified
const char *
nut_asset_subtype (nut_t *self, const char *asset_name)
{
    return nut_asset_get_string (self, asset_name, "subtype");
}

// ---------------------------------------------------------------------------
// return asset location (aka parent_name.1) string of given asset
// or NULL when asset_name does not exist
// or "" (empty string) when given asset does not have parent_name.1 specified
const char *
nut_asset_location (nut_t *self, const char *asset_name)
{
    return nut_asset_get_string (self, asset_name, "parent_name.1");
}

// ---------------------------------------------------------------------------
// return asset max_current (defined by user) of given asset
// or NULL when asset_name does not exist
// or "" (empty string) when given asset does not have max_current specified
const char *
nut_asset_max_current (nut_t *self, const char *asset_name)
{
    return nut_asset_get_string (self, asset_name, "max_current");
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

    fty_proto_t *asset = (fty_proto_t *) zhashx_first (self->assets);
    while (asset) {
        uint64_t size = 0;  // Note: the zmsg_encode() and zframe_size()
                            // below return a platform-dependent size_t,
                            // but in protocol we use fixed uint64_t
        assert ( sizeof(size_t) <= sizeof(uint64_t) );
        zframe_t *frame = NULL;
        fty_proto_t *duplicate = fty_proto_dup (asset);
        assert (duplicate);
        zmsg_t *zmessage = fty_proto_encode (&duplicate); // duplicate destroyed here
        assert (zmessage);

/* Note: the CZMQ_VERSION_MAJOR comparison below actually assumes versions
 * we know and care about - v3.0.2 (our legacy default, already obsoleted
 * by upstream), and v4.x that is in current upstream master. If the API
 * evolves later (incompatibly), these macros will need to be amended.
 */
#if CZMQ_VERSION_MAJOR == 3
        {
            byte *buffer = NULL;
            size = zmsg_encode (zmessage, &buffer);

            assert (buffer);
            assert (size > 0);
            frame = zframe_new (buffer, size);
            free (buffer);
            buffer = NULL;
        }
#else
        frame = zmsg_encode (zmessage);
        size = zframe_size (frame);
#endif
        zmsg_destroy (&zmessage);
        assert (frame);
        assert (size > 0);

        // prefix
// FIXME: originally this was for uint64_t, should it be sizeof (size) instead?
// Also is usage of uint64_t here really warranted (e.g. dictated by protocol)?
        zchunk_extend (chunk, (const void *) &size, sizeof (uint64_t));
        // data
        zchunk_extend (chunk, (const void *) zframe_data (frame), zframe_size (frame));

        zframe_destroy (&frame);
        asset = (fty_proto_t *) zhashx_next (self->assets);
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
    zframe_t *frame = zframe_new (zchunk_data (chunk), zchunk_size (chunk));
    assert (frame);
    zchunk_destroy (&chunk);

    zfile_close (file);
    zfile_destroy (&file);

/* Note: Protocol data uses 8-byte sized words, and zmsg_XXcode and file
 * functions deal with platform-dependent unsigned size_t and signed off_t.
 * The off_t is a difficult one to print portably, SO suggests casting to
 * the intmax type and printing that :)
 * https://stackoverflow.com/questions/586928/how-should-i-print-types-like-off-t-and-size-t
 */
    off_t offset = 0;
    log_debug ("zfile_cursize == %jd", (intmax_t)cursize);

    while (offset < cursize) {
        byte *prefix = zframe_data (frame) + offset;
        byte *data = zframe_data (frame) + offset + sizeof (uint64_t);
        offset += (uint64_t) *prefix +  sizeof (uint64_t);
        log_debug ("prefix == %" PRIu64 "; offset = %" PRIu64 " ", (uint64_t ) *prefix, offset);

/* Note: the CZMQ_VERSION_MAJOR comparison below actually assumes versions
 * we know and care about - v3.0.2 (our legacy default, already obsoleted
 * by upstream), and v4.x that is in current upstream master. If the API
 * evolves later (incompatibly), these macros will need to be amended.
 */
        zmsg_t *zmessage = NULL;
#if CZMQ_VERSION_MAJOR == 3
        zmessage = zmsg_decode (data, (size_t) *prefix);
#else
        {
            zframe_t *fr = zframe_new (data, (size_t) *prefix);
            zmessage = zmsg_decode (fr);
            zframe_destroy (&fr);
        }
#endif
        assert (zmessage);
        fty_proto_t *asset = fty_proto_decode (&zmessage); // zmessage destroyed
        assert (asset);

        // nut_put (self, &asset);
        int rv = zhashx_insert (self->assets, fty_proto_name (asset), asset);
        assert (rv == 0);
    }
    zframe_destroy (&frame);
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
    fty_proto_t *asset = (fty_proto_t *) zhashx_first (self->assets);
    while (asset) {
        log_debug ("%s", (const char *) zhashx_cursor (self->assets));
        log_debug (
                "\t%s %s",
                fty_proto_name (asset),
                fty_proto_operation (asset));
        log_debug ("\tEXT:");
        zhash_t *ext = fty_proto_ext (asset);
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
        zhash_t *aux = fty_proto_aux (asset);
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
        asset = (fty_proto_t *) zhashx_next (self->assets);
    }
}

static void
nut_print_zsys (nut_t *self)
{
    // Note: no "if (verbose)" checks in this dedicated routine
    assert (self);
    fty_proto_t *asset = (fty_proto_t *) zhashx_first (self->assets);
    while (asset) {
        zsys_debug ("%s", (const char *) zhashx_cursor (self->assets));
        zsys_debug (
                "\t%s %s",
                fty_proto_name (asset),
                fty_proto_operation (asset));
        zsys_debug ("\tEXT:");
        zhash_t *ext = fty_proto_ext (asset);
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
        zhash_t *aux = fty_proto_aux (asset);
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
        asset = (fty_proto_t *) zhashx_next (self->assets);
    }
}
//  --------------------------------------------------------------------------
//  Self test of this class

//  Helper test function
//  create new ASSET message of type fty_proto_t
static fty_proto_t *
test_asset_new (const char *name, const char *operation
        )
{
    assert (name);
    assert (operation);

    fty_proto_t *asset = fty_proto_new (FTY_PROTO_ASSET);
    fty_proto_set_name (asset, "%s", name);
    fty_proto_set_operation (asset, "%s", operation);
    return asset;
}

//  Helper test function
//  0 - same, 1 - different
static int
test_zlistx_compare (zlistx_t *expected, zlistx_t **received_p, int print = 0)
{
    // Note: no "if (self->verbose)" checks in this dedicated routine for tests
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
    nut_t *self = nut_new();
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

    fty_proto_t *asset =  test_asset_new ("ups", FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert (asset, "type", "%s", "device");
    fty_proto_aux_insert (asset, "subtype", "%s", "ups");
    fty_proto_ext_insert (asset, "abc.d", "%s", " ups string 1");
    nut_put (self, &asset);
    assert (nut_changed(self) == true);
    zlistx_add_end (expected, (void *) "ups");

    asset =  test_asset_new ("sensor", FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert (asset, "type", "%s", "device");
    fty_proto_aux_insert (asset, "subtype", "%s", "sensor");
    fty_proto_ext_insert (asset, "port", "%s", "port01");
    fty_proto_aux_insert (asset, "parent_name.1", "%s", "ups");
    fty_proto_ext_insert (asset, "logical_asset", "%s", "room01");
    nut_put (self, &asset);
    zlistx_add_end (expected, (void *) "sensor");

    asset =  test_asset_new ("epdu", FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert (asset, "type", "%s", "device");
    fty_proto_aux_insert (asset, "subtype", "%s", "epdu");
    nut_put (self, &asset);
    zlistx_add_end (expected, (void *) "epdu");

    asset =  test_asset_new ("ROZ.UPS33", FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_ext_insert (asset, "d.ef", "%s", "roz.ups33 string 1");
    fty_proto_ext_insert (asset, "description" , "%s",  "UPS1 9PX 5000i");
    fty_proto_ext_insert (asset, "device.location" , "%s",  "New IT Power LAB");
    fty_proto_ext_insert (asset, "location_u_pos" , "%s",  "1");
    fty_proto_ext_insert (asset, "u_size" , "%s",  "3");
    fty_proto_ext_insert (asset, "device.contact" , "%s",  "Gabriel Szabo");
    fty_proto_ext_insert (asset, "hostname.1" , "%s",  "ups33.roz53.lab.etn.com");
    fty_proto_ext_insert (asset, "battery.type" , "%s",  "PbAc");
    fty_proto_ext_insert (asset, "phases.output" , "%s",  "1");
    fty_proto_ext_insert (asset,  "device.type" , "%s",  "ups");
    fty_proto_ext_insert (asset, "business_critical" , "%s",  "yes");
    fty_proto_ext_insert (asset, "status.outlet.2" , "%s",  "on");
    fty_proto_ext_insert (asset, "status.outlet.1" , "%s",  "on");
    fty_proto_ext_insert (asset, "serial_no" , "%s",  "G202D51129");
    fty_proto_ext_insert (asset, "ups.serial" , "%s",  "G202D51129");
    fty_proto_ext_insert (asset, "installation_date" , "%s",  "2015-01-05");
    fty_proto_ext_insert (asset, "model" , "%s",  "Eaton 9PX");
    fty_proto_ext_insert (asset, "phases.input" , "%s",  "1");
    fty_proto_ext_insert (asset, "ip.1" , "%s",  "10.130.53.33");
    fty_proto_ext_insert (asset, "ups.alarm" , "%s",  "Automatic bypass mode!");
    fty_proto_ext_insert (asset, "manufacturer" , "%s",  "EATON");

    fty_proto_aux_insert (asset, "type", "%s", "device");
    fty_proto_aux_insert (asset, "subtype", "%s", "ups");
    fty_proto_aux_insert (asset, "parent", "%s", "4");
    fty_proto_aux_insert (asset, "status", "%s", "active");
    fty_proto_aux_insert (asset, "priority", "%s", "2");

    nut_put (self, &asset);
    zlistx_add_end (expected, (void *) "ROZ.UPS33");

    asset =  test_asset_new ("DUMMY.EPDU42", FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert (asset, "type", "%s", "device");
    fty_proto_aux_insert (asset, "subtype", "%s", "epdu");
    fty_proto_ext_insert (asset, "ip.1", "%s", "127.0.0.1");
    fty_proto_ext_insert (asset, "upsconf_block", "%s", "|[DUMMY.EPDU42]|\tdriver=dummy-ups|\tport=/tmp/DUMMY.EPDU42.dev||");
    nut_put (self, &asset);
    zlistx_add_end (expected, (void *) "DUMMY.EPDU42");

    asset =  test_asset_new ("DUMMY.UPS42", FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert (asset, "type", "%s", "device");
    fty_proto_aux_insert (asset, "subtype", "%s", "ups");
    fty_proto_ext_insert (asset, "ip.1", "%s", "127.0.0.1");
    fty_proto_ext_insert (asset, "upsconf_block", "%s", ",driver=dummy-ups,port=/tmp/DUMMY.UPS42.dev");
    nut_put (self, &asset);
    zlistx_add_end (expected, (void *) "DUMMY.UPS42");

    asset =  test_asset_new ("MBT.EPDU4", FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert (asset, "type", "%s", "device");
    fty_proto_aux_insert (asset, "subtype", "%s", "epdu");
    fty_proto_ext_insert (asset, "hhh", "%s", "mbt.epdu4 string 1");
    fty_proto_ext_insert (asset, "ip.1", "%s", "10.130.53.33");
    fty_proto_ext_insert (asset, "daisy_chain", "%s", "3");
    nut_put (self, &asset);
    zlistx_add_end (expected, (void *) "MBT.EPDU4");

    asset =  test_asset_new ("ROZ.ePDU14", FTY_PROTO_ASSET_OP_CREATE);

    fty_proto_ext_insert (asset, "status.outlet.13", "%s", "on");
    fty_proto_ext_insert (asset, "status.outlet.10", "%s", "on");
    fty_proto_ext_insert (asset, "status.outlet.11", "%s", "on");
    fty_proto_ext_insert (asset, "description" , "%s",  "ePDU14 G3");
    fty_proto_ext_insert (asset, "hostname.1" , "%s",  "epdu14.roz53.lab.etn.com");
    fty_proto_ext_insert (asset, "phases.output" , "%s",  "1");
    fty_proto_ext_insert (asset, "daisy_chain" , "%s",  "2");
    fty_proto_ext_insert (asset, "outlet.count" , "%s",  "16");
    fty_proto_ext_insert (asset, "device.type" , "%s",  "epdu");
    fty_proto_ext_insert (asset, "location_w_pos" , "%s",  "left");
    fty_proto_ext_insert (asset, "status.outlet.7" , "%s",  "on");
    fty_proto_ext_insert (asset, "status.outlet.6" , "%s",  "on");
    fty_proto_ext_insert (asset, "outlet.group.count" , "%s",  "1");
    fty_proto_ext_insert (asset, "status.outlet.5" , "%s",  "on");
    fty_proto_ext_insert (asset, "status.outlet.4" , "%s",  "on");
    fty_proto_ext_insert (asset, "business_critical" , "%s",  "yes");
    fty_proto_ext_insert (asset, "status.outlet.3" , "%s",  "on");
    fty_proto_ext_insert (asset, "status.outlet.2" , "%s",  "on");
    fty_proto_ext_insert (asset, "status.outlet.1" , "%s",  "on");
    fty_proto_ext_insert (asset, "status.outlet.9" , "%s",  "on");
    fty_proto_ext_insert (asset, "status.outlet.8" , "%s",  "on");
    fty_proto_ext_insert (asset, "serial_no" , "%s",  "H734F27009");
    fty_proto_ext_insert (asset, "ups.serial" , "%s",  "H734F27009");
    fty_proto_ext_insert (asset, "installation_date" , "%s",  "2015-01-05");
    fty_proto_ext_insert (asset, "http_link.1" , "%s",  "http://epdu14.roz53.lab.etn.com/");
    fty_proto_ext_insert (asset, "model" , "%s",  "EPDU MA 0U (C14 10A 1P)16XC13");
    fty_proto_ext_insert (asset, "phases.input" , "%s",  "1");
    fty_proto_ext_insert (asset, "ip.1" , "%s",  "10.130.53.33");
    fty_proto_ext_insert (asset, "device.part" , "%s",  "EMAB03");
    fty_proto_ext_insert (asset, "manufacturer" , "%s",  "EATON");
    fty_proto_ext_insert (asset, "status.outlet.16" , "%s",  "on");
    fty_proto_ext_insert (asset, "status.outlet.14" , "%s",  "on");
    fty_proto_ext_insert (asset, "status.outlet.15" , "%s",  "on");
    fty_proto_ext_insert (asset, "status.outlet.12" , "%s",  "on");

    fty_proto_aux_insert (asset, "type", "%s", "device");
    fty_proto_aux_insert (asset, "subtype", "%s", "epdu");
    fty_proto_aux_insert (asset, "parent", "%s", "4");
    fty_proto_aux_insert (asset, "status", "%s", "active");
    fty_proto_aux_insert (asset, "priority", "%s", "1");

    nut_put (self, &asset);
    zlistx_add_end (expected, (void *) "ROZ.ePDU14");

    asset =  test_asset_new ("MBT.EPDU5", FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert (asset, "type", "%s", "datacenter");
    nut_put (self, &asset);

    asset =  test_asset_new ("abcde", FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert (asset, "type", "%s", "device");
    fty_proto_aux_insert (asset, "subtype", "%s", "server");
    nut_put (self, &asset);

    asset =  test_asset_new ("", FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert (asset, "type", "%s", "device");
    fty_proto_aux_insert (asset, "subtype", "%s", "server");
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

        assert (streq (nut_asset_port (self, "sensor"), "port01"));
        assert (streq (nut_asset_location (self, "sensor"), "ups"));
    }

    asset = test_asset_new ("epdu", FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert (asset, "type", "%s", "device");
    fty_proto_aux_insert (asset, "subtype", "%s", "epdu");
    fty_proto_ext_insert (asset, "ip.1", "%s", "121.120.199.198");
    fty_proto_ext_insert (asset, "daisy_chain", "%s", "14");
    nut_put (self, &asset);

    asset =  test_asset_new ("ROZ.UPS33", FTY_PROTO_ASSET_OP_UPDATE);
    fty_proto_aux_insert (asset, "type", "%s", "device");
    fty_proto_aux_insert (asset, "subtype", "%s", "ups");
    fty_proto_ext_insert (asset, "ip.1", "%s", "1.1.2.3");
    fty_proto_ext_insert (asset, "daisy_chain", "%s", "5");
    nut_put (self, &asset);

    asset =  test_asset_new ("ups", FTY_PROTO_ASSET_OP_RETIRE);
    fty_proto_aux_insert (asset, "type", "%s", "device");
    fty_proto_aux_insert (asset, "subtype", "%s", "ups");
    fty_proto_ext_insert (asset, "d.ef", "%s", "roz.ups33 string 1");
    fty_proto_ext_insert (asset, "ip.1", "%s", "127.0.0.1");
    fty_proto_ext_insert (asset, "daisy_chain", "%s", "16");
    nut_put (self, &asset);
    void *handle = zlistx_find (expected, (void *) "ups");
    zlistx_delete (expected, handle);

    asset =  test_asset_new ("MBT.EPDU4", FTY_PROTO_ASSET_OP_UPDATE);
    fty_proto_aux_insert (asset, "type", "%s", "device");
    fty_proto_aux_insert (asset, "subtype", "%s", "epdu");
    fty_proto_ext_insert (asset, "ip.1", "%s", "10.130.38.52"); // ip changed
    // daisy chane is missing intentionally
    nut_put (self, &asset);

    asset =  test_asset_new ("sensor", FTY_PROTO_ASSET_OP_UPDATE);
    fty_proto_aux_insert (asset, "type", "%s", "device");
    fty_proto_aux_insert (asset, "subtype", "%s", "sensor");
    fty_proto_ext_insert (asset, "port", "%s", "port00");
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
        assert (streq (nut_asset_daisychain (self, "MBT.EPDU4"), ""));

        assert (streq (nut_asset_ip (self, "ROZ.ePDU14"), "10.130.53.33"));
        assert (streq (nut_asset_daisychain (self, "ROZ.ePDU14"), "2"));

        assert (streq (nut_asset_port (self, "sensor"), "port00"));

        zlist_t *sensors = nut_get_sensors (self);
        assert (zlist_size (sensors) == 1);
        assert (streq ((char *)zlist_first (sensors), "sensor"));
        zlist_destroy (&sensors);
    }

    asset =  test_asset_new ("MBT.EPDU4", FTY_PROTO_ASSET_OP_UPDATE);
    fty_proto_aux_insert (asset, "type", "%s", "device");
    fty_proto_aux_insert (asset, "subtype", "%s", "epdu");
    fty_proto_ext_insert (asset, "ip.1", "%s", "10.130.38.52"); // ip NOT changed
    fty_proto_ext_insert (asset, "daisy_chain", "%s", "44"); // daisychain is added
    nut_put (self, &asset);

    asset =  test_asset_new ("sensor", FTY_PROTO_ASSET_OP_DELETE);
    fty_proto_aux_insert (asset, "type", "%s", "device");
    fty_proto_aux_insert (asset, "subtype", "%s", "sensor");
    fty_proto_ext_insert (asset, "port", "%s", "port00");
    nut_put (self, &asset);
    handle = zlistx_find (expected, (void *) "sensor");
    zlistx_delete (expected, handle);

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

        assert (nut_asset_port (self, "sensor") == NULL);

    }

    asset =  test_asset_new ("MBT.EPDU4", FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert (asset, "type", "%s", "device");
    fty_proto_aux_insert (asset, "subtype", "%s", "epdu");
    fty_proto_ext_insert (asset, "daisy_chain", "%s", "44");
    fty_proto_ext_insert (asset, "ip.1", "%s", "10.130.38.52");
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

    asset =  test_asset_new ("ROZ.UPS33", FTY_PROTO_ASSET_OP_DELETE);
    fty_proto_aux_insert (asset, "type", "%s", "device");
    fty_proto_aux_insert (asset, "subtype", "%s", "ups");
    nut_put (self, &asset);
    handle = zlistx_find (expected, (void *) "ROZ.UPS33");
    zlistx_delete (expected, handle);

    asset =  test_asset_new ("epdu", FTY_PROTO_ASSET_OP_DELETE);
    fty_proto_aux_insert (asset, "type", "%s", "device");
    fty_proto_aux_insert (asset, "subtype", "%s", "epdu");
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
