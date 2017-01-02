/*  =========================================================================
    logger - logging api

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

#ifndef LOGGER_H_INCLUDED
#define LOGGER_H_INCLUDED

// Taken from 'core' repo, src/shared/log.(h|c)
// Original author: Michal Vyskocil

// Trick to avoid conflict with CXXTOOLS logger, currently the BIOS code
// prefers OUR logger macros
#if defined(LOG_CXXTOOLS_H) || defined(CXXTOOLS_LOG_CXXTOOLS_H)
# undef log_error
# undef log_debug
# undef log_info
# undef log_fatal
# undef log_warn
#else
# define LOG_CXXTOOLS_H
# define CXXTOOLS_LOG_CXXTOOLS_H
#endif

#include <syslog.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOG_NOOP LOG_EMERG -1

//  @interface

// Set log level
// Setting log level to a value means: that level + all the leves that are more
// "critical" are triggered. E.g.: LOG_INFO triggers LOG_WARNING, LOG_ERR,
// LOG_CRIT but not LOG_DEBUG
FTY_NUT_EXPORT void
    log_set_level (int level);

// Get log level
FTY_NUT_EXPORT int
    log_get_level ();

//
FTY_NUT_EXPORT int
    log_do (
        int level,
        const char* file,
        int line,
        const char* func,
        const char* format,
        ...) __attribute__ ((format (printf, 5, 6)));

// Helper macro
#define log_macro(level, ...) \
    do { \
        log_do((level), __FILE__, __LINE__, __func__, __VA_ARGS__); \
    } while(0)

// Prints message with LOG_DEBUG level
#define log_debug(...) \
        log_macro(LOG_DEBUG, __VA_ARGS__)

// Prints message with LOG_INFO level
#define log_info(...) \
        log_macro(LOG_INFO, __VA_ARGS__)

// Prints message with LOG_WARNING level
#define log_warning(...) \
        log_macro(LOG_WARNING, __VA_ARGS__)

// Prints message with LOG_ERR level
#define log_error(...) \
        log_macro(LOG_ERR, __VA_ARGS__)

// Prints message with LOG_CRIT level
#define log_critical(...) \
        log_macro(LOG_CRIT, __VA_ARGS__)

//  Self test of this class.
FTY_NUT_EXPORT void
    logger_test (bool verbose);

//  @end

#ifdef __cplusplus
}
#endif

#endif

