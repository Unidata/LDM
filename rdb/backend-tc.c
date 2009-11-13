/*
 * See file ../COPYRIGHT for copying and redistribution conditions.
 *
 * This module implements the runtime database backend database API via the
 * Tokyo Cabinet API.
 */
#include <ldmconfig.h>

#undef NDEBUG
#include <assert.h>

#ifndef bool
#  define bool int
#endif
#ifndef true
#  define true 1
#endif 
#ifndef false
#  define false 0
#endif
#include <tcutil.h>
#include <tcbdb.h>
#include <stdint.h>

#include "backend.h"
#include "rdblib.h"

struct backend {
    TCBDB*      bdb;
};

/*
 * Opens the backend database.
 *
 * ARGUMENTS:
 *      backend         Pointer to pointer to backend structure.  Upon
 *                      successful return, "*backend" will be set.  The client
 *                      should call "beClose(*backend)" when the backend is no
 *                      longer needed.
 *      path            Pathname of the database file.  Shall not be NULL.
 *      forWriting      Open the database for writing? 0 <=> no
 * RETURNS:
 *      0               Success.  "*backend" is set.
 *      RDB_SYSERR      System error.  See "errno".
 *      RDB_DBERR       Backend database error.
 */
RdbStatus
beOpen(
    Backend** const     backend,
    const char* const   path,
    int                 forWriting)
{
    RdbStatus   status;
    Backend*    back = (Backend*)malloc(sizeof(Backend));

    if (back == NULL) {
        status = RDB_SYSERR;
    }
    else {
        TCBDB*      bdb = tcbdbnew();

        assert(path != NULL);
        assert(backend != NULL);

        if (bdb == NULL) {
            status = RDB_SYSERR;
        }
        else {
            if (!tcbdbopen(bdb, path,
                    forWriting ? BDBOWRITER | BDBOCREAT : BDBOREADER)) {
                status = RDB_DBERR;
            }
            else {
                back->bdb = bdb;
                *backend = back;
                status = 0;
            }

            if (status)
                tcbdbdel(bdb);
        }                               /* "bdb" allocated */

        if (status)
            free(back);
    }                                   /* "back" allocated */

    return status;
}

/*
 * Maps a key to a string.
 *
 * ARGUMENTS:
 *      backend         Pointer to the database.  Must have been returned by
 *                      "beOpen()".
 *      key             Pointer to the 0-terminated key.
 *      value           Pointer to the string value.
 * RETURNS:
 *      0               Success.
 *      RDB_DBERR       Backend database error.
 */
RdbStatus
bePut(
    Backend*            backend,
    const char* const   key,
    const char* const   value)
{
    assert(backend != NULL);
    assert(backend->bdb != NULL);
    assert(key != NULL);
    assert(value != NULL);

    return tcbdbput2(backend->bdb, key, value)
        ? 0
        : RDB_DBERR;
}
