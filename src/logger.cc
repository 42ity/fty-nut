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

/*
@header
    logger - logging api
@discuss
@end
*/

#include <cerrno>

#include "fty_nut_classes.h"

#define ASSERT_LEVEL \
    assert(level == LOG_DEBUG   || \
           level == LOG_INFO    || \
           level == LOG_WARNING || \
           level == LOG_ERR     || \
           level == LOG_CRIT    || \
           level == LOG_NOOP)

static int log_stderr_level = LOG_WARNING;
static FILE* log_file = NULL;

//extern int errno;

/*XXX: gcc-specific!, see http://stackoverflow.com/questions/7623735/error-initializer-element-is-not-constant */
static void init_log_file(void) __attribute__((constructor));
static void init_log_file(void) {
    log_file = stderr;
}

void
log_set_level (int level)
{
    ASSERT_LEVEL;
    log_stderr_level = level;
}

int
log_get_level ()
{
    return log_stderr_level;
}

static int do_logv(
        int level,
        const char* file,
        int line,
        const char* func,
        const char* format,
        va_list args) {

    const char *prefix;
    char *fmt;
    char *buffer;

    int r;

    if (level > log_get_level()) {
        //no-op if logging disabled
        return 0;
    }

    switch (level) {
        case LOG_DEBUG:
            prefix = "DEBUG"; break;
        case LOG_INFO:
            prefix = "INFO"; break;
        case LOG_WARNING:
            prefix = "WARNING"; break;
        case LOG_ERR:
            prefix = "ERROR"; break;
        case LOG_CRIT:
            prefix = "CRITICAL"; break;
        default:
            fprintf (log_file, "[ERROR]: %s:%d (%s) invalid log level %d", __FILE__, __LINE__, __func__, level);
            return -1;
    };

    r = asprintf (&fmt, "[%s]: %s:%d (%s) %s", prefix, file, line, func, format);
    if (r == -1) {
        fprintf (log_file, "[ERROR]: %s:%d (%s) can't allocate enough memory for format string: %s", __FILE__, __LINE__, __func__, strerror (errno));
        return r;
    }

    r = vasprintf (&buffer, fmt, args);
    free (fmt);   // we don't need it in any case
    if (r == -1) {
        fprintf (log_file, "[ERROR]: %s:%d (%s) can't allocate enough memory for message string: %s", __FILE__, __LINE__, __func__, strerror (errno));
        return r;
    }

    if (level <= log_stderr_level) {
        fputs (buffer, log_file);
        fputc ('\n', log_file);
    }
    free (buffer);

    return 0;

}

int
log_do (
    int level,
    const char* file,
    int line,
    const char* func,
    const char* format,
    ...) {

    int r;
    int saved_errno = errno;
    va_list args;

    va_start (args, format);
    r = do_logv (level, file, line, func, format, args);
    va_end (args);

    errno = saved_errno;
    return r;
}

//  --------------------------------------------------------------------------
//  Self test of this class

void
logger_test (bool verbose)
{
    printf (" * logger: ");

    //  @selftest

    //  @end
    printf ("Empty test - OK\n");
}
