/*
 * See file ../COPYRIGHT for copying and redistribution conditions.
 *
 * This module hides the decision on what database system to use.
 *
 * This module implements the runtime database backend database API via the
 * Berkeley DB API.
 */
#include <config.h>

#undef NDEBUG
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <db.h>
#include "backend.h"
#include "registry.h"
#include <log.h>

struct backend {
    DB*         db;
};

#define DB_FILENAME    "registry.db"

/*
 * Decorator of the Berkeley cursor structure for use by this backend module.
 */
struct cursor {
    DBT         key;
    DBT         value;
    Backend*    backend;
    DBC*        dbCursor;
};

/*
 * Callback function for logging error-messages from the Berkeley DB.
 *
 * ARGUMENTS:
 *      env             Pointer to the Berkeley DB environment.
 *      prefix          Pointer to the message-prefix.
 *      msg             Pointer to the error-message.
 */
static void
logDbError(
    const DB_ENV*       env,
    const char*         prefix,
    const char*         msg)
{
    /*
     * An error-message from the database starts a sequence of log-messages.
     */
    log_start("Berkeley DB: %s", msg);
}

/*
 * Forcebly removes the database environment.
 *
 * ARGUMENTS:
 *      path            Pathname of the database directory.  Shall not be NULL.
 *                      The client can free it upon return.
 * RETURNS:
 *      0               Success.
 *      ENOMEM          System error.  "log_start()" called.
 *      EIO             Backend database error.  "log_start()" called.
 */
static RegStatus
removeEnvironment(
    const char* const   path)
{
    RegStatus   status = EIO;           /* failure */
    DB_ENV*     env;

    assert(NULL != path);

    if (status = db_env_create(&env, 0)) {
        log_start("Couldn't create database environment: %s",
            db_strerror(status));
    }
    else {
        env->set_errcall(env, logDbError);

        if (status = env->remove(env, path, DB_FORCE)) {
            log_add("Couldn't remove database environment");
        }
        else {
            status = 0;                 /* success */
        }
    }                                   /* "env" allocated */

    return status;
}

/*
 * Sets a cursor.
 *
 * Arguments:
 *      cursor          Pointer to the cursor.  Shall not be NULL.
 *      mode            The mode for setting the cursor.  One of DB_SET_RANGE
 *                      or DB_NEXT.
 * Returns:
 *      0               Success.  "*cursor" is set.
 *      ENOENT          No such entry.
 *      EIO             Backend database error.  "log_start()" called.
 */
static RegStatus setCursor(
    Cursor* const       cursor,
    int                 mode)
{
    DBC*        dbCursor = cursor->dbCursor;
    DBT*        keyDbt = &cursor->key;
    DBT*        valueDbt = &cursor->value;
    int         status = dbCursor->get(dbCursor, keyDbt, valueDbt, mode);

    if (DB_NOTFOUND == status) {
        status = ENOENT;
    }
    else if (0 != status) {
        status = EIO;
    }

    return status;
}

/*
 * Returns the path name of the Berkeley database.
 *
 * Arguments:
 *      db              Pointer to the database.  Shall not be NULL.
 *
 * Returns
 *      Pointer to the path name of the database.
 */
static const char* getPath(DB* db)
{
    DB_ENV* const       env = db->get_env(db);
    const char*         path;

    (void)env->get_home(env, &path);

    return path;
}

/******************************************************************************
 * Public API:
 ******************************************************************************/

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
 *      ENOMEM          System error.  "log_start()" called.
 *      EIO             Backend database error.  "log_start()" called.
 */
RegStatus
beOpen(
    Backend** const     backend,
    const char* const   path,
    int                 forWriting)
{
    RegStatus   status;
    Backend*    back = (Backend*)malloc(sizeof(Backend));

    assert(NULL != path);

    if (NULL == back) {
        log_serror("Couldn't allocate %lu bytes", (long)sizeof(Backend));
        status = ENOMEM;
    }
    else {
        DB_ENV* env;

        if (status = db_env_create(&env, 0)) {
            log_start("Couldn't create database environment handle: %s",
                db_strerror(status));
            status = EIO;
        }
        else {
            env->set_errcall(env, logDbError);

            /*
             * The database is configured for "concurrent data store" access
             * rather than for transactional access because the former is
             * faster and sufficient.
             */
            if (status = env->open(env, path,
                    DB_CREATE | DB_INIT_CDB | DB_INIT_MPOOL, 0)) {
                log_add("Couldn't open or create database environment in "
                    "\"%s\": %s", path, strerror(status));
                status = EIO;
            }
            else {
                DB*     db;

                if (status = db_create(&db, env, 0)) {
                    log_add("Couldn't create database handle");
                    status = EIO;
                }
                else {
                    db->set_errcall(db, logDbError);

                    if (status = db->open(db, NULL, DB_FILENAME, NULL,
                            DB_BTREE, forWriting ? DB_CREATE : DB_RDONLY, 0)) {
                        log_add("Couldn't open database \"%s\" in \"%s\" "
                            "for %s", DB_FILENAME, path,
                            forWriting ? "writing" : "reading");
                        status = EIO;
                    }
                    else {
                        back->db = db;
                        *backend = back;
                        status = 0;     /* success */
                    }                   /* "db" opened */

                    /*
                     * According to the documentation on DB->open(), if that
                     * call fails, then DB->close() must be called to discard
                     * the DB handle, so DB->close() is the termination
                     * counterpart of db_create() rather than of DB->open().
                     */
                    if (status)
                        (void)db->close(db, 0);
                }                       /* "db" allocated */
            }                           /* "env" opened */

            /*
             * According to the documentation on DB_ENV->open(), if that
             * call fails, then DB_ENV->close() must be called to discard
             * the DB_ENV handle, so DB_ENV->close() is the termination
             * counterpart of db_env_create() rather than of DB_ENV->open().
             */
            if (status)
                (void)env->close(env, 0);
        }                               /* "env" allocated */

        if (status)
            free(back);
    }                                   /* "back" allocated */

    return status;
}

/*
 * Closes the backend database.
 *
 * ARGUMENTS:
 *      backend         Pointer to the database.  Shall have been set by
 *                      "beOpen()".  May be NULL.  Upon return, "backend"
 *                      shall not be used again.
 * RETURNS:
 *      0               Success.
 *      EIO             Backend database error.  "log_start()" called.
 */
RegStatus
beClose(
    Backend* const      backend)
{
    RegStatus   status;

    if (NULL == backend) {
        status = 0;                     /* success */
    }
    else {
        DB* const       db = backend->db;
        const char*     path = getPath(db);
        DB_ENV* const   env = db->get_env(db);

        if (db->close(db, 0)) {
            log_add("Couldn't close backend database \"%s\"", path);
            status = EIO;
        }
        else {
            backend->db = NULL;

            if (env->close(env, 0)) {
                log_add("Couldn't close environment of backend database \"%s\"",
                    path);
                status = EIO;
            }
            else {
                free(backend);
                status = 0;             /* success */
            }
        }
    }

    return status;
}

/*
 * Resets the backend database.  This function shall be called only when
 * nothing holds the database open.
 *
 * ARGUMENTS:
 *      path            Pathname of the database directory.  Shall not be NULL.
 *                      The client can free it upon return.
 * RETURNS:
 *      0               Success.
 *      ENOMEM          System error.  "log_start()" called.
 *      EIO             Backend database error.  "log_start()" called.
 */
RegStatus
beReset(
    const char* const   path)
{
    return removeEnvironment(path);
}

/*
 * Removes the backend database.  This function shall be called only when
 * nothing holds the database open.
 *
 * ARGUMENTS:
 *      path            Pathname of the database directory.  Shall not be NULL.
 *                      The client can free it upon return.
 * RETURNS:
 *      0               Success.
 *      ENOMEM          System error.  "log_start()" called.
 *      EIO             Backend database error.  "log_start()" called.
 */
RegStatus
beRemove(
    const char* const   path)
{
    RegStatus   status;
    DB_ENV*     env;

    assert(NULL != path);

    /*
     * First, remove the database.
     */
    if (status = db_env_create(&env, 0)) {
        log_start("Couldn't create database environment in \"%s\": %s",
            path, db_strerror(status));
        status = EIO;
    }
    else {
        env->set_errcall(env, logDbError);

        if (status = env->open(env, path, 0, 0)) {
            log_add("Couldn't open database environment in \"%s\"", path);
            status = EIO;
        }
        else {
            if (status = env->dbremove(env, NULL, DB_FILENAME, NULL, 0)) {
                log_add("Couldn't remove database file \"%s\" in \"%s\"",
                    DB_FILENAME, path);
                status = EIO;
            }
        }                               /* "env" opened */

        /*
         * According to the documentation on DB_ENV->open(), if that call
         * fails, then DB_ENV->close() must be called to discard the DB_ENV
         * handle, so DB_ENV->close() is the termination counterpart of
         * db_env_create() rather than of DB_ENV->open().
         */
        (void)env->close(env, 0);
    }                                   /* "env" allocated */

    /*
     * Then, remove the database environment.
     */
    if (0 == status)
        status = removeEnvironment(path);

    return status;
}

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
 *      EIO             Backend database error.  "log_start()" called.
 */
RegStatus
bePut(
    Backend*            backend,
    const char* const   key,
    const char* const   value)
{
    RegStatus   status;
    DBT         keyDbt;
    DBT         valueDbt;

    assert(NULL != key);
    assert(NULL != value);

    (void)memset(&keyDbt, 0, sizeof(DBT));
    (void)memset(&valueDbt, 0, sizeof(DBT));

    keyDbt.data = (void*)key;
    keyDbt.size = strlen(key) + 1;
    valueDbt.data = (void*)value;
    valueDbt.size = strlen(value) + 1;

    if (status = backend->db->put(backend->db, NULL, &keyDbt, &valueDbt,
            0)) {
        log_add("Couldn't map key \"%s\" to value \"%s\"", key, value);
        status = EIO;
    }
    else {
        status = 0;
    }

    return status;
}

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
 *      ENOENT          The given key doesn't match any entry.
 *      EIO             Backend database error.  "log_start()" called.
 */
RegStatus
beGet(
    Backend*            backend,
    const char* const   key,
    char** const        value)
{
    RegStatus   status;
    DBT         keyDbt;
    DBT         valueDbt;
    DB*         db;

    assert(NULL != key);
    assert(NULL != value);

    (void)memset(&keyDbt, 0, sizeof(DBT));
    (void)memset(&valueDbt, 0, sizeof(DBT));

    keyDbt.data = (void*)key;
    keyDbt.size = strlen(key) + 1;
    valueDbt.flags = DB_DBT_MALLOC;
    db = backend->db;

    status = db->get(db, NULL, &keyDbt, &valueDbt, 0);

    if (0 == status) {
        *value = (char*)valueDbt.data;
    }
    else if (DB_NOTFOUND == status) {
        status = ENOENT;
    }
    else {
        log_add("Couldn't get value for key \"%s\"", key);
        status = EIO;
    }

    return status;
}

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
    const char* const   key)
{
    RegStatus   status;
    DBT         keyDbt;

    assert(NULL != backend);
    assert(NULL != backend->db);
    assert(NULL != key);

    (void)memset(&keyDbt, 0, sizeof(DBT));

    keyDbt.data = (void*)key;
    keyDbt.size = strlen(key) + 1;

    status = backend->db->del(backend->db, NULL, &keyDbt, 0);

    if (DB_NOTFOUND == status) {
        status = 0;
    }
    else if (status) {
        log_add("Couldn't delete entry for key \"%s\"", key);
        status = EIO;
    }

    return status;
}

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
    Backend* const      backend)
{
    RegStatus   status;

    assert(NULL != backend);
    assert(NULL != backend->db);

    if (status = backend->db->sync(backend->db, 0)) {
        log_add("Couldn't sync() database");
        status = EIO;
    }
    else {
        status = 0;                     /* success */
    }

    return status;
}

/*
 * Creates a new cursor structure.
 *
 * ARGUMENTS:
 *      backend         Pointer to the backend database.  Shall have been
 *                      set by beOpen().  Shall not be NULL.
 *      cursor          Pointer to a pointer to a cursor structure.  Shall
 *                      not be NULL.  Upon successful return, "*cursor" will
 *                      be set.  The client should call "beFreeCursor()" when
 *                      the cursor is no longer needed.
 * RETURNS
 *      0               Success.  "*cursor" is set.
 *      EIO             Backend database error.  "log_start()" called.
 *      ENOMEM          System error.  "log_start()" called.
 */
RegStatus
beNewCursor(
    Backend* const      backend,
    Cursor** const      cursor)
{
    RegStatus   status;
    Cursor*     cur = (Cursor*)malloc(sizeof(Cursor));

    assert(NULL != backend);
    assert(NULL != backend->db);
    assert(NULL != cursor);

    if (NULL == cur) {
        log_serror("Couldn't allocate %lu bytes", (long)sizeof(Cursor));
        status = ENOMEM;
    }
    else {
        DB*         db = backend->db;
        DBC*        dbCursor;
        /*
         * Because this function is only used for reading, the Berkeley
         * cursor needn't be transactionally protected.
         */
        if (status = db->cursor(db, NULL, &dbCursor, 0)) {
            log_add("Couldn't create cursor for database \"%s\"", getPath(db));
            status = EIO;
        }
        else {
            (void)memset(&cur->key, 0, sizeof(DBT));
            (void)memset(&cur->value, 0, sizeof(DBT));

            cur->key.data = NULL;
            cur->value.data = NULL;
            cur->key.flags |= DB_DBT_REALLOC;
            cur->value.flags |= DB_DBT_REALLOC;
            cur->dbCursor = dbCursor;
            cur->backend = backend;
            *cursor = cur;              /* success */
        }                               /* "dbCursor" allocated */

        if (status)
            free(cur);
    }                                   /* "cur" allocated */

    return status;
}

/*
 * Sets an cursor structure to reference the first entry in the backend
 * database whose key is greater than or equal to a given key.
 *
 * ARGUMENTS:
 *      cursor          Pointer to the cursor structure.  Shall not be NULL.
 *                      Shall have been set by "beInitCursor()".  Upon
 *                      successful return, "*cursor" will be set.
 *      key             Pointer to the starting key.  Shall not be NULL.  The
 *                      empty string obtains the first entry in the database,
 *                      if it exists.
 * RETURNS
 *      0               Success.  "*cursor" is set.
 *      ENOENT          The database is empty.  "*cursor" is unmodified.
 *      EIO             Backend database error.  "*cursor" is unmodified.
 *                      "log_start()" called.
 *      ENOMEM          System error.  "log_start()" called.
 */
RegStatus
beFirstEntry(
    Cursor* const       cursor,
    const char* const   key)
{
    RegStatus   status;
    char*       dupKey;

    assert(NULL != cursor);
    assert(NULL != key);

    dupKey = strdup(key);

    if (NULL == dupKey) {
        log_serror("Couldn't allocate %lu bytes", (long)strlen(key));
        status = ENOMEM;
    }
    else {
        cursor->key.data = dupKey;
        cursor->key.size = strlen(dupKey) + 1;

        if (EIO == (status = setCursor(cursor, DB_SET_RANGE))) {
            log_add("Couldn't set cursor for database \"%s\" to first entry "
                "on or after key \"%s\"", getPath(cursor->backend->db), key);
        }
    }                               /* "dupKey" allocated */

    return status;
}

/*
 * Advances a cursor to the next entry.
 *
 * ARGUMENTS:
 *      cursor          Pointer to the cursor structure.  Shall not be NULL.
 *                      Shall have been set by "beFirstCursor()".  Upon
 *                      successful return, "*cursor" will be set.
 * RETURNS
 *      0               Success.  "*cursor" is set.
 *      ENOENT          The database is empty.  "*cursor" is unmodified.
 *      EIO             Backend database error.  "*cursor" is unmodified.
 *                      "log_start()" called.
 */
RegStatus
beNextEntry(
    Cursor* const       cursor)
{
    RegStatus   status;

    assert(NULL != cursor);

    if (EIO == (status = setCursor(cursor, DB_NEXT))) {
        log_add("Couldn't advance cursor for database \"%s\" to next entry "
            "after key \"%s\"", getPath(cursor->backend->db), cursor->key.data);
    }

    return status;
}

/*
 * Frees a cursor.  Should be called after every successful call to
 * beNewCursor().
 *
 * ARGUMENTS:
 *      cursor          Pointer to the cursor structure.  Shall not be NULL.
 * RETURNS:
 *      0               Success.  The client shall not use the cursor again.
 *      EIO             Backend database error.  "log_start()" called.
 */
RegStatus
beFreeCursor(Cursor* cursor)
{
    RegStatus           status;
    DBC*                dbCursor;

    assert(NULL != cursor);

    dbCursor = cursor->dbCursor;

    if (status = dbCursor->close(dbCursor)) {
        log_start("Couldn't close cursor for database \"%s\"",
            getPath(cursor->backend->db));
        status = EIO;
    }
    else {
        free(cursor->key.data);
        free(cursor->value.data);
        free(cursor);
    }

    return status;
}

/*
 * Returns the key of a cursor.
 *
 * Arguments:
 *      cursor          Pointer to the cursor whose key is to be returned.
 *                      Shall not be NULL.
 * Returns:
 *      Pointer to the key.  Shall not be NULL if beFirstEntry() or 
 *      beNextEntry() was successful.
 */
const char* beGetKey(const Cursor* cursor)
{
    assert(NULL != cursor);

    return cursor->key.data;
}

/*
 * Returns the value of a cursor.
 *
 * Arguments:
 *      cursor          Pointer to the cursor whose value is to be returned.
 *                      Shall not be NULL.
 * Returns:
 *      Pointer to the value.  Shall not be NULL if beFirstEntry() or 
 *      beNextEntry() was successful.
 */
const char* beGetValue(const Cursor* cursor)
{
    assert(NULL != cursor);

    return cursor->value.data;
}
