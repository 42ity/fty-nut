/*  =========================================================================
    fsutils - filesystem utils

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

#ifndef FSUTILS_H_INCLUDED
#define FSUTILS_H_INCLUDED

// Taken from 'core' repo, src/shared/log.(h|c)
// Original author: Tomas Halman

#include <sys/stat.h>
#include <vector>
#include <string>

namespace shared {

// returns "/"
FTY_NUT_EXPORT const char*
    path_separator ();

// get the file mode
FTY_NUT_EXPORT mode_t
   file_mode (const std::string& path);

// return true if path exists and it is a regular file
FTY_NUT_EXPORT bool
    is_file (const std::string& path);

// return true if path exists and it is a directory
FTY_NUT_EXPORT bool
    is_dir (const std::string& path);

// get list of all items in directory
FTY_NUT_EXPORT bool
    items_in_directory (
            const std::string& path,
            std::vector <std::string>& items
            );

// get list of all regular files in directory
FTY_NUT_EXPORT bool
    files_in_directory (
            const std::string& path,
            std::vector <std::string>& files
            );

/**
 * \brief create directory (if not exists yet)
 * \param path to the newly created directory
 * \param mode (rights)
 * \param create parent directories if needed
 *
 * In case of failure also errno is set, see "man 3 mkdir" for details.
 */
FTY_NUT_EXPORT bool
    mkdir_if_needed (
            const char *path,
            mode_t mode = 0755, /* OCTAL, not HEX! */
            bool create_parent=true);

// return basename - the component of path following the final path_separator()
FTY_NUT_EXPORT std::string
    basename (const std::string& path);

} // namespace shared

//  Self test of this class
FTY_NUT_EXPORT void
    fsutils_test (bool verbose);
//  @end

#endif
