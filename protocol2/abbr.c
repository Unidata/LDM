/*
 *   See file ../COPYRIGHT for copying and redistribution conditions.
#undef NDEBUG
#undef NDEBUG
 */

#undef NDEBUG

#include "config.h"

#include "abbr.h"
#include "ldm.h"
#include "mylog.h"

#include <ctype.h>
#include <pthread.h>
#include <regex.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static pthread_once_t once_control = PTHREAD_ONCE_INIT;
static regex_t        regex;

static void regex_fini(void)
{
    regfree(&regex);
}

static void regex_init(void)
{
    int status = regcomp(&regex, "feed", REG_ICASE | REG_NOSUB);
    mylog_assert(status == 0);
    atexit(regex_fini);
}

/**
 * Sets the logging identifier based on a remote-host identifier and an optional
 * suffix.
 *
 * @param remote    Pointer to identifier of remote host.  May be a hostname or
 *                  a dotted-quad IP address. Client may free upon return.
 * @param suffix    Pointer to suffix to be added to ulog(3) identifier (e.g.,
 *                  "(feed)") or NULL.
 */
void
set_abbr_ident(
    const char* const   remote,
    const char* const   suffix)
{
    (void)pthread_once(&once_control, regex_init);
    bool isFeeder = suffix
        ? regexec(&regex, suffix, 0, NULL, 0) == 0
        : false;
    (void)mylog_set_upstream_id(remote, isFeeder);
}
