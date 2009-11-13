/*
 *   See file ../COPYRIGHT for copying and redistribution conditions.
 *
 *   This header-file defines the API for the backend database.
 */

#ifndef BACKEND_H
#define BACKEND_H

#include "rdblib.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct backend  Backend;

typedef struct {
    Rdb*        rdb;            /* responsibility of "rdblib" */
    char*       key;            /* responsibility of backend database */
    char*       value;          /* responsibility of backend database */
    void*       private;        /* responsibility of backend database */
}       RdbCursor;

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
    int                 forWriting);

/*
 * Maps a key to a string.  Overwrites any pre-existing entry.
 *
 * ARGUMENTS:
 *      backend         Pointer to the database.  Shall have been set by
 *                      "beOpen()".  Shall not be NULL.
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
    const char* const   value);

/*
 * Returns the string to which a key maps.
 *
 * ARGUMENTS:
 *      backend         Pointer to the database.  Shall have been set by
 *                      "beOpen()".  Shall not be NULL.
 *      key             Pointer to the 0-terminated key.
 *      value           Pointer to a pointer to the string value.  Shall not be
 *                      NULL.  "*value" shall point to the 0-terminated string
 *                      value upon successful return.  The client should call
 *                      "free(*value)" when the value is no longer needed.
 * RETURNS:
 *      0               Success.  "*value" points to the string value.
 *      RDB_NOENTRY     The given key doesn't match any entry.
 *      RDB_DBERR       Backend database error.
 */
RdbStatus
beGet(
    Backend*            backend,
    const char* const   key,
    char** const        value);

/*
 * Closes the backend database.
 *
 * ARGUMENTS:
 *      backend         Pointer to the database.  Shall have been set by
 *                      "beOpen()".  Shall not be NULL.  Upon return, "backend"
 *                      shall not be used again.
 * RETURNS:
 *      0               Success.
 *      RDB_DBERR       Backend database error.
 */
RdbStatus
beClose(
    Backend* const      backend);

#ifdef __cplusplus
}
#endif

#endif
