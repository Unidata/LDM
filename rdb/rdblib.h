/*
 *   See file ../COPYRIGHT for copying and redistribution conditions.
 *
 *   This header-file defines the API for the runtime configuration database.
 */

#ifndef RCDLIB_H
#define RCDLIB_H

#ifdef __cplusplus
extern "C" {
#endif

#define RDB_PATH_SEPARATOR      '/'

typedef enum {
    RDB_BADARG = 1,
    RDB_WRONGMODE,
    RDB_NOENTRY,
    RDB_WRONGTYPE,
    RDB_SYSERR,
    RDB_DBERR
}       RdbStatus;

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
    const int           forWriting);

/*
 * Closes a runtime database.
 *
 * RETURNS:
 *      0               Success.
 *      RDB_DBERR       Backend database error.  "log_start()" called.
 */
RdbStatus
rdbClose(void);

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
    const char* const   path);

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
    const char* const   value);

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
    const int           value);

/*
 * Gets the string-value to which a key maps.
 *
 * ARGUMENTS:
 *      key           Pointer to the 0-terminated key.  Shall not be NULL.
 *      value         Pointer to the pointer to the value.  Upon successful
 *                    return, "*value" will point to the 0-terminated string to
 *                    which the key maps.  The client should call
 *                    "free(*value)" when the value is no longer needed.
 *      defaultValue  Pointer to the 0-terminated string-value to be returned
 *                    if no such key exists.  May be NULL.  Copied if not
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
    char* const         defaultValue);

#ifdef __cplusplus
}
#endif

#endif
