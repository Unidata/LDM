/*
 *   See file ../COPYRIGHT for copying and redistribution conditions.
 *
 *   This header-file defines the API for the backend database.
 */

#ifndef LDM_BACKEND_H
#define LDM_BACKEND_H

typedef struct backend  Backend;

#include "registry.h"
#include "stringBuf.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char*       key;            /* responsibility of backend database */
    char*       value;          /* responsibility of backend database */
    void*       private;        /* responsibility of backend database */
}       RdbCursor;

/*
 * Opens the backend database.
 *
 * ARGUMENTS:
 *      backend         Pointer to pointer to backend structure.  Shall not be
 *                      NULL.  Upon successful return, "*backend" will be set.
 *                      The client should call "beClose(*backend)" when the
 *                      backend is no longer needed.
 *      path            Pathname of the database directory.  Shall not be NULL.
 *                      The client can free it upon return.
 *      forWriting      Open the database for writing? 0 <=> no
 * RETURNS:
 *      0               Success.  "*backend" is set.
 *      ENOMEM   System error.  "log_start()" called.
 *      EIO    Backend database error.  "log_start()" called.
 */
RegStatus
beOpen(
    Backend** const     backend,
    const char* const   path,
    int                 forWriting);

/*
 * Closes the backend database.
 *
 * ARGUMENTS:
 *      backend         Pointer to the database.  Shall have been set by
 *                      "beOpen()".  Shall not be NULL.  Upon return, "backend"
 *                      shall not be used again.
 * RETURNS:
 *      0               Success.
 *      EIO    Backend database error.  "log_start()" called.
 */
RegStatus
beClose(
    Backend* const      backend);

/*
 * Removes the backend database.
 *
 * ARGUMENTS:
 *      path            Pathname of the database directory.  Shall not be NULL.
 *                      The client can free it upon return.
 * RETURNS:
 *      0               Success.
 *      ENOMEM   System error.  "log_start()" called.
 *      EIO    Backend database error.  "log_start()" called.
 */
RegStatus
beRemove(
    const char* const   path);

/*
 * Maps a key to a string.  Overwrites any pre-existing entry.
 *
 * ARGUMENTS:
 *      backend         Pointer to the database.  Shall have been set by
 *                      "beOpen()".  Shall not be NULL.
 *      key             Pointer to the 0-terminated key.  Shall not be NULL.
 *      value           Pointer to the string value.  Shall not be NULL.
 * RETURNS:
 *      0               Success.
 *      EIO    Backend database error.  "log_start()" called.
 */
RegStatus
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
 *      key             Pointer to the 0-terminated key.  Shall not be NULL.
 *      value           Pointer to a pointer to the string value.  Shall not be
 *                      NULL.  "*value" shall point to the 0-terminated string
 *                      value upon successful return.  The client should call
 *                      "free(*value)" when the value is no longer needed.
 * RETURNS:
 *      0               Success.  "*value" points to the string value.
 *      ENOENT     The given key doesn't match any entry.
 *      EIO    Backend database error.  "log_start()" called.
 */
RegStatus
beGet(
    Backend*            backend,
    const char* const   key,
    char** const        value);

/*
 * Deletes an entry in the database.
 *
 * ARGUMENTS:
 *      backend         Pointer to the database.  Shall have been set by
 *                      "beOpen()".  Shall not be NULL.
 *      key             Pointer to the 0-terminated key.
 * RETURNS:
 *      0               Success.  The entry associated with the key was deleted.
 *      EIO             Backend database error.  "log_start()" called.
 */
RegStatus
beDelete(
    Backend* const      backend,
    const char* const   key);

/*
 * Synchronizes the database (i.e., flushes any cached data to disk).
 *
 * ARGUMENTS:
 *      backend         Pointer to the database.  Shall have been set by
 *                      "beOpen()".  Shall not be NULL.
 * RETURNS:
 *      0               Success.
 *      EIO    Backend database error.  "log_start()" called.
 */
RegStatus
beSync(
    Backend* const      backend);

/*
 * Initializes an RDB cursor structure.
 *
 * ARGUMENTS:
 *      backend         Pointer to the backend database.  Shall have been
 *                      set by beOpen().  Shall not be NULL.
 *      rdbCursor       Pointer to an RDB cursor structure.  Shall not be NULL.
 *                      Upon successful return, "*rdbCursor" will be set.  The
 *                      client should call "beCloseCursor()" when the cursor
 *                      is no longer needed.
 * RETURNS
 *      0               Success.  "*rdbCursor" is set.
 *      EIO    Backend database error.  "log_start()" called.
 *      ENOMEM   System error.  "log_start()" called.
 */
RegStatus
beInitCursor(
    Backend* const      backend,
    RdbCursor* const    rdbCursor);

/*
 * Sets an RDB cursor structure to reference the first entry in the backend
 * database whose key is greater than or equal to a given key.
 *
 * ARGUMENTS:
 *      rdbCursor       Pointer to the RDB cursor structure.  Shall not be NULL.
 *                      Shall have been set by "beInitCursor()".  Upon
 *                      successful return, "*rdbCursor" will be set.  The
 *                      client shall not modify "rdbCursor->key" or
 *                      "rdbCursor->value" or the strings to which they point.
 *      key             Pointer to the starting key.  Shall not be NULL.  The
 *                      empty string obtains the first entry in the database,
 *                      if it exists.
 * RETURNS
 *      0               Success.  "*rdbCursor" is set.
 *      ENOENT     The database is empty.  "*rdbCursor" is unmodified.
 *      EIO    Backend database error.  "*rdbCursor" is unmodified.
 *                      "log_start()" called.
 *      ENOMEM   System error.  "log_start()" called.
 */
RegStatus
beFirstEntry(
    RdbCursor* const    rdbCursor,
    const char* const   key);

/*
 * Advances a cursor to the next entry.
 *
 * ARGUMENTS:
 *      rdbCursor       Pointer to the RDB cursor structure.  Shall not be NULL.
 *                      Shall have been set by "beFirstCursor()".  Upon
 *                      successful return, "*rdbCursor" will be set.  The client
 *                      shall not modify "rdbCursor->key" or "rdbCursor->value"
 *                      or the strings to which they point.  This function can
 *                      change "rdbCursor->key", "rdbCursor->value", or the
 *                      strings to which they point.
 * RETURNS
 *      0               Success.  "*rdbCursor" is set.
 *      ENOENT     The database is empty.  "*rdbCursor" is unmodified.
 *      EIO    Backend database error.  "*rdbCursor" is unmodified.
 *                      "log_start()" called.
 */
RegStatus
beNextEntry(
    RdbCursor* const    rdbCursor);

/*
 * Closes an RDB cursor.
 *
 * ARGUMENTS:
 *      rdbCursor       Pointer to the RDB cursor structure.
 * RETURNS:
 *      0               Success.  The client shall not use the cursor again.
 *      EIO    Backend database error.  "log_start()" called.
 */
RegStatus
beCloseCursor(RdbCursor* rdbCursor);

#ifdef __cplusplus
}
#endif

#endif
