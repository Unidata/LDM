/*
 *   See file ../COPYRIGHT for copying and redistribution conditions.
 */

#ifndef REQUESTER6_H
#define REQUESTER6_H

#include <signal.h>

#include "error.h"
#include "pq.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    REQ6_SUCCESS = 0,
    REQ6_TIMED_OUT,
    REQ6_UNKNOWN_HOST,
    REQ6_BAD_VERSION,
    REQ6_BAD_PATTERN,
    REQ6_NOT_ALLOWED,
    REQ6_BAD_RECLASS,
    REQ6_NO_CONNECT,
    REQ6_DISCONNECT,
    REQ6_SYSTEM_ERROR
} req6_error;

ErrorObj*
req6_new(
    const char* const                   upName,
    const unsigned                      port,
    const prod_class_t* const           request,
    const int                           inactiveTimeout,
    const char* const                   pqPathname,
    pqueue* const                       pq,
    const int                           isPrimary);


/*
 * Closes the socket.
 */
void
req6_close();

#ifdef __cplusplus
}
#endif

#endif
