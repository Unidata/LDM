/*
 * See file ../COPYRIGHT for copying and redistribution conditions.
 *
 * This module implements the runtime database.  It is thread-compatible
 * but not thread-safe.
 */
#include <config.h>

#undef NDEBUG
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rdblib.h"
#include "backend.h"
#include <log.h>

static struct {
    Backend*    backend;
    int         isOpen;
    int         readonly;
}       rdb;

/*******************************************************************************
 * Utility functions:
 ******************************************************************************/

/*
 * Copies a string.  Logs a message if an error occurs.
 *
 * ARGUMENTS:
 *      string          The string to copy.  Shall not be NULL.
 * RETURNS:
 *      NULL            System error.  "log_start()" called.
 *      else            Pointer to a copy of the string.  The client should free
 *                      the returned string when it is no longer needed.
 */
static char*
copyString(const char* const string)
{
    char*       copy;

    assert(NULL != string);

    copy = strdup(string);

    if (NULL == copy)
        log_serror("Couldn't copy %lu-byte string \"%s\"",
            (unsigned long)strlen(string), string);

    return copy;
}

/*
 * Shortens a key by removing the last component of the path.
 *
 * ARGUMENTS:
 *      key             The key to be shortened.
 */
static void
shortenKey(char* const key)
{
    char*       lastSep = strrchr(key, RDB_PATH_SEPARATOR);

    if (NULL == lastSep)
        lastSep = key;

    *lastSep = 0;
}

/*******************************************************************************
 * Public API:
 ******************************************************************************/

/*
 * Opens a runtime database.
 *
 * ARGUMENTS:
 *      path            Pathname of the database directory.  Shall not be NULL.
 *                      The directory must already exist and be writable.
 *      forWriting      Open the database for writing? 0 <=> no
 * RETURNS:
 *      0               The database was successfuly opened.
 *      RDB_SYSERR      System error.  "log_start()" called.
 *      RDB_DBERR       Backend database error.  "log_start()" called.
 */
RdbStatus
rdbOpen(
    const char* const   path,
    const int           forWriting)
{
    RdbStatus   status;

    assert(NULL != path);

    if (!rdb.isOpen) {
        Backend*        backend;
        
        status = beOpen(&backend, path, forWriting);

        if (0 == status) {
            rdb.readonly = !forWriting;
            rdb.backend = backend;
            rdb.isOpen = 1;
        }                               /* "backend" allocated */
    }                                   /* "rdb" not open */

    return status;
}

/*
 * Closes a runtime database.
 *
 * RETURNS:
 *      0               Success.
 *      RDB_DBERR       Backend database error.  "log_start()" called.
 */
RdbStatus
rdbClose(void)
{
    RdbStatus   status;

    if (rdb.isOpen) {
        status = beClose(rdb.backend);
        rdb.isOpen = 0;
    }

    return status;
}

/*
 * Removes a runtime database.  This method should not be called while any
 * process has an active rdbOpen().
 *
 * ARGUMENTS
 *      path            Pathname of the database directory.  Shall not be NULL.
 * RETURNS:
 *      0               The runtime database was successfuly removed.
 *      RDB_SYSERR      System error.  "log_start()" called.
 *      RDB_DBERR       Backend database error.  "log_start()" called.
 */
RdbStatus
rdbRemove(
    const char* const   path)
{
    RdbStatus   status;

    assert(NULL != path);

    return beRemove(path);
}

/*
 * Puts a key/string pair into the database.
 *
 * ARGUMENTS:
 *      key           Pointer to the 0-terminated key.  Shall not be NULL.
 *      value         Pointer to the 0-terminated string value.  Shall not be
 *                    NULL.
 * RETURNS:
 *      0             The key/value pair was successfully added to the
 *                    database.
 *      RDB_NOENTRY   The given key is invalid for the database.
 *      RDB_WONGMODE  The database is not open for writing.
 *      RDB_SYSERR    System error.  "log_start()" called.
 */
RdbStatus
rdbPutString(
    const char* const   key,
    const char* const   value)
{
    assert(rdb.isOpen);
    assert(NULL != key);
    assert(NULL != value);

    return bePut(rdb.backend, key, value);
}

/*
 * Puts a key/integer pair into the database.
 *
 * ARGUMENTS:
 *      key           Pointer to the 0-terminated key.  Shall not be NULL.
 *      value         The integer value.
 * RETURNS:
 *      0             The key/value pair was successfully added to the
 *                    database.
 *      RDB_NOENTRY   The given key is invalid for the database.
 *      RDB_WONGMODE  The database is not open for writing.
 *      RDB_SYSERR    System error.  "log_start()" called.
 */
RdbStatus
rdbPutInt(
    const char* const   key,
    const int           value)
{
    char    buffer[80];

    assert(rdb.isOpen);
    assert(NULL != key);

    (void)snprintf(buffer, sizeof(buffer), "%d", value);

    return bePut(rdb.backend, key, buffer);
}

/*
 * Gets the string-value to which a key maps.  If no entry is found for the
 * given key, then the key is shortened by removing its last path component and
 * the lookup is retried.  This continues until an entry is found or the
 * search-key becomes the empty-string.
 *
 * ARGUMENTS:
 *      key           Pointer to the 0-terminated key.  Shall not be NULL.
 *      value         Pointer to the pointer to the value.  Upon successful
 *                    return, "*value" will point to the 0-terminated string to
 *                    which the key maps.  The client should call
 *                    "free(*value)" when the value is no longer needed.
 *      defaultValue  Pointer to the 0-terminated string-value to be returned
 *                    if no entry is found.  May be NULL.  Copied if not
 *                    NULL.
 * RETURNS:
 *      0             The value associated with the key was successfully
 *                    retrieved. "*value" will point to the value.
 *      RDB_WRONGTYPE The value associated with the key is not a string.
 *      RDB_SYSERR    System error.  "log_start()" called.
 */
RdbStatus
rdbGetString(
    const char* const   key,
    char** const        value,
    char* const         defaultValue)
{
    RdbStatus   status;
    char*       keyBuf;

    assert(rdb.isOpen);
    assert(NULL != key);
    assert(NULL != value);

    keyBuf = copyString(key);

    if (NULL == keyBuf) {
        status = RDB_SYSERR;
    }
    else {
        for (status = RDB_NOENTRY; 0 != *keyBuf; shortenKey(keyBuf)) {
            status = beGet(rdb.backend, keyBuf, value);

            if (RDB_NOENTRY != status)
                break;
        }                               /* key loop */

        if (RDB_NOENTRY == status) {
            if (NULL == defaultValue) {
                *value = NULL;
                status = 0;
            }
            else {
                char*   clone = copyString(defaultValue);

                if (NULL == clone) {
                    status = RDB_SYSERR;
                }
                else {
                    *value = clone;
                    status = 0;
                }
            }
        }

        free(keyBuf);
    }                                   /* "keyBuf" allocated */

    return status;
}

/*
 * Gets the integer-value to which a key maps.  If no entry is found for the
 * given key, then the key is shortened by removing its last path component and
 * the lookup is retried.  This continues until an entry is found or the
 * search-key becomes the empty-string.
 *
 * ARGUMENTS:
 *      key           Pointer to the 0-terminated key.  Shall not be NULL.
 *      value         Pointer to the value.  Upon successful return, "*value"
 *                    will be the integer associated with the key.
 *      defaultValue  The value to be returned if no entry is found.
 * RETURNS:
 *      0             The value associated with the key was successfully
 *                    retrieved. "*value" will be the value.
 *      RDB_WRONGTYPE The value associated with the key is not an integer.
 *      RDB_SYSERR    System error.  "log_start()" called.
 */
RdbStatus
rdbGetInt(
    const char* const   key,
    int* const          value,
    int                 defaultValue)
{
    RdbStatus   status;
    char*       keyBuf;

    assert(rdb.isOpen);
    assert(NULL != key);
    assert(NULL != value);

    keyBuf = copyString(key);

    if (NULL == keyBuf) {
        status = RDB_SYSERR;
    }
    else {
        char*   stringValue;

        for (status = RDB_NOENTRY; 0 != *keyBuf; shortenKey(keyBuf)) {
            status = beGet(rdb.backend, keyBuf, &stringValue);

            if (RDB_NOENTRY != status)
                break;
        }                               /* key loop */

        if (RDB_NOENTRY == status) {
            *value = defaultValue;
            status = 0;
        }
        else if (0 == status) {
            char*       end;
            int         val;

            errno = 0;
            val = strtol(stringValue, &end, 0);

            if (0 != *end || (0 == val && 0 != errno)) {
                status = RDB_WRONGTYPE;
            }
            else {
                *value = val;
            }

            free(stringValue);
        }                               /* "stringValue" allocated */

        free(keyBuf);
    }                                   /* "keyBuf" allocated */

    return status;
}

/*
 * Deletes an entry in the database.
 *
 * ARGUMENTS:
 *      key             Pointer to the 0-terminated key.
 * RETURNS:
 *      0               Success.  The entry associated with the key was deleted.
 *      RDB_NOENTRY     The given key doesn't match any entry.
 *      RDB_DBERR       Backend database error.  "log_start()" called.
 */
RdbStatus
rdbDelete(
    const char* const   key)
{
    assert(rdb.isOpen);
    assert(NULL != key);

    return beDelete(rdb.backend, key);
}

/*******************************************************************************
 * Cursor API:
 ******************************************************************************/

/*
 * The following API is used this way:
 *
        #
        # Returning status; not keeping last-op state.  Has 6 functions and 3
        # calls per iteration.
        #
        RdbCursor*       cursor;
        RdbStatus        status = rdbNewCursor(&cursor);

        if (status) {
            ...
        }
        else {
            for (status = rdbFirstEntry(cursor); status == 0;
                    status = rdbNextEntry(cursor)) {
                rdbCursorKey(cursor)
                rdbCursorStringValue(cursor)
            }
            if (RDB_NOENTRY != status) {
                ...
            }
            rdbFreeCursor(cursor);
        }                               // "cursor" allocated

        ########################################################################
        # Rejected alternatives:
        ########################################################################

        #
        # Returning status value; no last-op state.  Has 5 functions and 3
        # calls per iteration.
        #
        RdbCursor*       cursor;
        RdbStatus        status = rdbNewCursor(&cursor);

        for (; 0 == status; status = rdbNextEntry(cursor)) {
            rdbCursorKey(cursor)
            rdbCursorStringValue(cursor)
        }
        if (RDB_NOENTRY != status) {
            ...
        }
        rdbFreeCursor(cursor);

        #
        # Returning object; not keeping last-op state.  Has 6 functions and 3
        # calls per iteration.
        #
        RdbCursor*       cursor = rdbNewCursor();

        if (NULL == cursor) {
            ...
        }
        else {
            RdbStatus        status;

            for (status = rdbFirstEntry(cursor); status == 0;
                    status = rdbNextEntry(cursor)) {
                rdbCursorKey(cursor)
                rdbCursorStringValue(cursor)
            }
            if (RDB_NOENTRY != status) {
                ...
            }
            rdbFreeCursor(cursor);
        }

        #
        # Returning actual object; keeping last-op state.  Has 6 functions
        # and 4 calls per iteration.
        #
        RdbCursor*       cursor = rdbNewCursor();

        for (; rdbCursorStatus(cursor) == 0; rdbNextEntry(cursor)) {
            rdbCursorKey(cursor)
            rdbCursorStringValue(cursor)
        }
        if (rdbCursorStatus(cursor) != RDB_NOENTRY) {
            ...
        }
        rdbFreeCursor(cursor);

        #
        # Returning success indicator; keeping last-op state.  Has 6
        # functions and 4 calls per iteration.
        #
        RdbCursor*       cursor;

        for (rdbInitEntry(&cursor); rdbNextEntry(cursor); ) {
            rdbCursorKey(cursor)
            rdbCursorStringValue(cursor)
        }
        if (rdbCursorStatus(cursor) != RDB_NOENTRY) {
            ...
        }
        rdbFreeCursor(cursor);

        #
        # Exposing internals of "RdbCursor".  Has 3 functions; 1 call per
        # iteration; and tighter coupling with client through data-structure.
        #
        for (cursor = rdbNewCursor(); cursor.status == 0;
                rdbNextEntry(cursor)) {
            cursor.key
            cursor.valueType
            cursor.value
            cursor.stringValue?
        }
        if (RDB_NOENTRY != cursor.status) {
            ...
        }
        rdbFreeCursor(cursor);
 */

/*
 * Returns a new cursor object.
 *
 * ARGUMENTS:
 *      cursor          Pointer to a pointer to cursor structure.  Shall not be
 *                      NULL.  Upon successful return, "*cursor" will be set.
 *                      The client should call "rdbFreeCursor(*cursor)" when
 *                      the cursor is no longer needed.
 * RETURNS:
 *      0               Success.  "*cursor" is set.
 *      RDB_SYSERR      System error.  "log_start()" called.
 *      RDB_DBERR       Database error.  "log_start()" called.
 */
RdbStatus
rdbNewCursor(
    RdbCursor** const   cursor)
{
    RdbStatus   status;
    RdbCursor*  curs = (RdbCursor*)malloc(sizeof(RdbCursor));

    assert(rdb.isOpen);
    assert(NULL != cursor);

    if (NULL == curs) {
        log_serror("Couldn't allocate %d bytes for cursor", sizeof(RdbCursor));
        status = RDB_SYSERR;
    }
    else {
        status = beInitCursor(rdb.backend, curs, "");

        if (0 == status) {
            *cursor = curs;
        }
        else {
            free(curs);
        }
    }                                   /* "curs" allocated */

    return status;
}

/*
 * Set a cursor to reference the first entry in its database.
 *
 * ARGUMENTS:
 *      cursor          Pointer to cursor.  Shall not be NULL.  Shall have been
 *                      set by "rdbNewCursor()".  Upon successful return,
 *                      "*cursor" will reference the first entry of the
 *                      database.
 * RETURNS:
 *      0               Success.  "*cursor" is set.
 *      RDB_SYSERR      System error.  "log_start()" called.
 *      RDB_DBERR       Database error.  "log_start()" called.
 *      RDB_NOENTRY     The database is empty.
 */
RdbStatus
rdbFirstEntry(
    RdbCursor* const   cursor)
{
    assert(NULL != cursor);

    return beFirstEntry(cursor);
}

/*
 * Modifies a cursor structure to reference the next entry in the database.
 *
 * ARGUMENTS:
 *      cursor          Pointer to the cursor structure.  Shall not be NULL.
 *                      Will be modified to reference the next entry.
 * RETURNS:
 *      0               Success.  "*cursor" has been modified.
 *      RDB_NOENTRY     No more entries.
 */
RdbStatus
rdbNextEntry(
    RdbCursor* const     cursor)
{
    assert(NULL != cursor);

    return beNextEntry(cursor);
}

/*
 * Returns a cursor's key.
 *
 * ARGUMENTS:
 *      cursor          Pointer to the cursor.  Shall not be NULL.
 * RETURNS:
 *      The cursor's key.  The client shall not free the returned pointer.
 */
const char*
rdbCursorKey(
    const RdbCursor* const       cursor)
{
    assert(NULL != cursor);

    return cursor->key;
}

/*
 * Returns a cursor's value as a string.
 *
 * ARGUMENTS:
 *      cursor           Pointer to the cursor.  Shall not be NULL.
 * RETURNS:
 *      The cursor's value as a string. The client shall not free the returned
 *      pointer.
 */
const char*
rdbCursorValueString(
    const RdbCursor* const       cursor)
{
    assert(NULL != cursor);

    return cursor->value;
}

/*
 * Frees the resources of a cursor.
 *
 * ARGUMENTS:
 *      cursor          Pointer to the cursor.  Shall not be NULL.  Shall have
 *                      been set by "rdbNewCursor()".  Upon return, "cursor"
 *                      shall not be used again.
 * RETURNS:
 *      0               Success.
 *      RDB_DBERR       Backend database error.  "log_start()" called.
 */
RdbStatus
RdbFreeCursor(RdbCursor* const cursor)
{
    RdbStatus   status;

    assert(NULL != cursor);

    status = beCloseCursor(cursor);

    free(cursor);

    return status;
}
