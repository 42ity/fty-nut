/*  =========================================================================
    fsutils - filesystem utils

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
    fsutils - filesystem utils
@discuss
@end
*/

#include "agent_nut_classes.h"


#include <dirent.h>
#include <string.h>
#include <errno.h>

namespace shared {

const char *
path_separator () {
    static const char *sep = "/";
    return sep;
}

mode_t
file_mode (const std::string& path) {
    struct stat st;

    if (stat (path.c_str (), &st) == -1) {
        log_error ("function `stat (pathname = '%s')` failed: %s", path.c_str (), strerror (errno));
        return 0;
    }
    return st.st_mode;
}

bool
is_file (const std::string& path) {
    return S_ISREG (file_mode (path));
}

bool
is_dir (const std::string& path) {
    return S_ISDIR (file_mode (path));
}

bool
items_in_directory (
        const std::string& path,
        std::vector <std::string>& items
        ) {

    DIR *dir = opendir (path.c_str ());
    if (!dir) {
        log_error ("function `opendir (name = '%s')` failed: %s", path.c_str (), strerror (errno));
        return false;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (streq (entry->d_name, ".") ||
            streq (entry->d_name, "..")) {
            continue;
        }
        items.push_back (entry->d_name);
    }
    closedir (dir);
    return true;
}

bool
files_in_directory (
        const std::string& path,
        std::vector <std::string>& files
        ) {
    std::string spath = path;
    spath += path_separator();

    std::vector <std::string> items;
    if (items_in_directory (spath, items) == false) {
        return false;
    }

    for (const auto& item : items) {
        if (is_file (spath + item))
            files.push_back (item);
    }
    return true;
}

bool
mkdir_if_needed (const char *path, mode_t mode, bool create_parent ) {
    if( ! path || strlen(path) == 0 ) return false;
    if( is_dir( path ) ) return true;

    if( create_parent ) {
        std::string parent = path;
        size_t i = parent.find_last_of( path_separator() );
        if( i != std::string::npos ) {
            parent = parent.substr(0,i);
            mkdir_if_needed( parent.c_str(), mode, create_parent );
        }
    }
    if (mkdir(path,mode) < 0 )
        return false;
    return is_dir(path);
}

// basename from libgen.h does not play nice with const char*
std::string basename(const std::string& path) {
    auto pos = path.rfind(path_separator());
    if (pos == std::string::npos)
        return std::string{path};

    return path.substr(pos+1);
}

} // namespace shared


//  --------------------------------------------------------------------------
//  Self test of this class

void
fsutils_test (bool verbose)
{
    printf (" * fsutils: ");

    //  @selftest

    // path_separator
    const char *separator = shared::path_separator ();
    assert (separator);
    assert (strcmp (separator, "/") == 0);

    // file_mode
    mode_t mode = shared::file_mode ("src/mapping.conf.in");
    assert ((mode & S_IFMT) == S_IFREG);
    struct stat sb;
    stat ("src/mapping.conf.in", &sb);
    assert (sb.st_mode == mode);

    // is_file
    assert (shared::is_file ("src/mapping.conf.in") == true);
    log_warning ("NOTE: We do expect the error for './mapping.conf' below...");
    assert (shared::is_file ("mapping.conf") == false);
    assert (shared::is_file ("src/fsutils.cc") == true);

    // is_dir
    assert (shared::is_dir ("src") == true);
    assert (shared::is_dir ("include") == true);
    log_warning ("NOTE: We do expect the error for './karci/' below...");
    assert (shared::is_dir ("karci") == false);

    // items_in_directory
    std::vector <std::string> items;
    // NOTE: If we do a build, the amount of files under doc/ is volatile
    //assert (shared::items_in_directory ("doc", items) == true);
    //assert (items.size () == 11);
    // This location is less subject to change over time...
    assert (shared::items_in_directory ("packaging/debian", items) == true);
    log_warning ("NOTE: If you get assertion failure for items.size() here, make sure packaging/debian/ contains the expected amount of files");
    assert (items.size () == 8);
    log_warning ("      Never mind the assertion note above, check passed :)");

    items.clear ();
    log_warning ("NOTE: We do expect the error for './non-existing-dir/' below...");
    assert (shared::items_in_directory ("non-existing-dir", items) == false);
    assert (items.size () == 0);

    assert (shared::mkdir_if_needed(".testdir") == true);
    log_warning ("NOTE: We do expect the error for './.testdir/missingsub/dir/' below...");
    assert (shared::mkdir_if_needed(".testdir/missingsub/dir", 0700, false) == false);
    log_warning ("NOTE: We do expect the errors for './.testdir/sub/dir/' and '.testdir/sub' below (if this is a first run of the test)...");
    assert (shared::mkdir_if_needed(".testdir/sub/dir", 0711, true) == true);
    log_warning ("NOTE: We do not expect errors re-ensuring that './.testdir/sub/dir/' exists below...");
    assert (shared::mkdir_if_needed(".testdir/sub/dir") == true);
    log_warning ("NOTE: We do foresee a possible error for mkdir of 'src/mapping.conf.in' below (but not an assertion fault), or maybe no error at all...");
    assert (shared::mkdir_if_needed("src/mapping.conf.in") == false);

    // TODO: the rest

    //  @end
    printf ("OK\n");
}
