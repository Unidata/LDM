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
 * Decorator for the Berkeley cursor structure for use by this backend module.
 */
typedef struct {
    DBT         key;
    DBT         value;
    Backend*    backend;
    DBC*        dbCursor;
}       BackCursor;

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
 * Copies a key/value pair from a backend cursor structure to an RDB cursor
 * structure.
 *
 * ARGUMENTS:
 *      rdbCursor       Pointer to the RDB cursor structure.
 *      backend         Pointer to the backend cursor structure.
 * RETURNS:
 *      0               Success.  The backend cursor structure is set.
 *      ENOMEM   System error.  The backend cursor structure is
 *                      unmodified.
 */
static RegStatus
copyEntry(
    RdbCursor*          rdbCursor,
    BackCursor*         backCursor)
{
    RegStatus   status;
    char*       key = strdup(backCursor->key.data);

    if (NULL == key) {
        status = ENOMEM;
    }
    else {
        char*       value = strdup(backCursor->value.data);

        if (NULL == value) {
            free(key);
            status = ENOMEM;
        }
        else {
            free(rdbCursor->key);
            free(rdbCursor->value);

            rdbCursor->key = key;
            rdbCursor->value = value;
            status = 0;
        }
    }                                   /* "key" allocated */

    return status;
}

/*
 * Resets the backend database.
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
reset(
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
                log_add("Couldn't open database environment in \"%s\"", path);
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

                    if (status)
                        db->close(db, 0);
                }                       /* "db" allocated */

                if (status) {
                    env->close(env, 0);
                    env = NULL;
                }
            }                       /* "env" opened */

            /*
             * DB_ENV->close() is called here (if it hasn't been called
             * already) instead of calling DB_ENV->remove()) because calling
             * DB_ENV->remove() after DB_ENV->close() results in a SIGSEGV and
             * calling DB_ENV->remove() with no preceeding call to env->close()
             * results in a memory-leak.
             */
            if (status && NULL != env)
                env->close(env, 0);
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
        const char* path;
        DB*         db = backend->db;
        DB_ENV*     env = db->get_env(db);

        if (db->close(db, 0)) {
            (void)env->get_home(env, &path);
            log_add("Couldn't close backend database \"%s\"", path);
            status = EIO;
        }
        else {
            backend->db = NULL;

            if (env->close(env, 0)) {
                (void)env->get_home(env, &path);
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
 * Resets the backend database.
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
    return reset(path);
}

/*
 * Removes the backend database.
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

    if (status = db_env_create(&env, 0)) {
        log_start("Couldn't create database environment: %s",
            db_strerror(status));
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
            else {
                /*
                 * The database environment is closed and then recreated rather
                 * than creating another DB_ENV and removing that because
                 * calling DB_ENV->remove() while another DB_ENV exists results
                 * in a SIGSEGV.
                 */
                if (status = env->close(env, 0)) {
                    log_add("Couldn't close database environment in \"%s\"",
                        path);
                    env = NULL;
                }
                else {
                    status = reset(path);
                }                       /* "env" closed */
            }                           /* database removed */
        }                               /* "env" opened */

        if (status && NULL != env)
            env->close(env, 0);
    }                                   /* "env" allocated */

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
 *      EIO             Backend database error.  "log_start()" called.
 *      ENOMEM          System error.  "log_start()" called.
 */
RegStatus
beInitCursor(
    Backend* const      backend,
    RdbCursor* const    rdbCursor)
{
    RegStatus   status;
    DBC*        dbCursor;
    DB_ENV*     env;
    DB*         db;

    assert(NULL != backend);
    assert(NULL != backend->db);
    assert(NULL != rdbCursor);

    db = backend->db;
    env = db->get_env(db);

    /*
     * Because this function is only used for reading, the Berkeley cursor
     * needn't be transactionally protected.
     */
    status = db->cursor(db, NULL, &dbCursor, 0);

    if (status) {
        const char*     path;

        (void)env->get_home(env, &path);
        log_add("Couldn't create cursor for database \"%s\"", path);
        status = EIO;
    }
    else {
        BackCursor*  backCursor = (BackCursor*)malloc(sizeof(BackCursor));

        if (NULL == backCursor) {
            log_serror("Couldn't allocate %lu bytes", (long)sizeof(BackCursor));
            status = ENOMEM;
        }
        else {
            (void)memset(&backCursor->key, 0, sizeof(DBT));
            (void)memset(&backCursor->value, 0, sizeof(DBT));

            backCursor->key.data = NULL;
            backCursor->value.data = NULL;
            backCursor->key.flags |= DB_DBT_REALLOC;
            backCursor->value.flags |= DB_DBT_REALLOC;
            backCursor->dbCursor = dbCursor;
            backCursor->backend = backend;
            rdbCursor->key = NULL;
            rdbCursor->value = NULL;
            rdbCursor->private = backCursor;
            status = 0;
        }                               /* "backCursor" allocated */
    }                                   /* "dbCursor" allocated */

    return status;
}

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
 *      ENOENT          The database is empty.  "*rdbCursor" is unmodified.
 *      EIO             Backend database error.  "*rdbCursor" is unmodified.
 *                      "log_start()" called.
 *      ENOMEM          System error.  "log_start()" called.
 */
RegStatus
beFirstEntry(
    RdbCursor* const    rdbCursor,
    const char* const   key)
{
    RegStatus           status;

    assert(NULL != rdbCursor);
    assert(NULL != rdbCursor->private);
    assert(NULL != key);

    char*   dupKey = strdup(key);

    if (NULL == dupKey) {
        log_serror("Couldn't allocate %lu bytes", (long)strlen(key));
        status = ENOMEM;
    }
    else {
        BackCursor*         backCursor = (BackCursor*)rdbCursor->private;
        DBC*                dbCursor = backCursor->dbCursor;
        DBT*                keyDbt = &backCursor->key;
        DBT*                valueDbt = &backCursor->value;

        keyDbt->data = dupKey;
        keyDbt->size = strlen(dupKey) + 1;
        status = dbCursor->get(dbCursor, keyDbt, valueDbt, DB_SET_RANGE);

        if (0 == status) {
            status = copyEntry(rdbCursor, backCursor);
        }
        else if (DB_NOTFOUND == status) {
            status = ENOENT;
        }
        else {
            const char* path;
            DB_ENV*     env =
                backCursor->backend->db->get_env(backCursor->backend->db);

            (void)env->get_home(env, &path);
            log_add("Couldn't set cursor for database \"%s\" to first entry on "
                "or after key \"%s\"", path, key);
            status = EIO;
        }
    }                               /* "dupKey" allocated */

    return status;
}

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
    RdbCursor* const    rdbCursor)
{
    RegStatus   status;
    BackCursor* backCursor;
    DBC*        dbCursor;
    DBT*        keyDbt;
    DBT*        valueDbt;

    assert(NULL != rdbCursor);
    assert(NULL != rdbCursor->private);

    backCursor = (BackCursor*)rdbCursor->private;
    dbCursor = backCursor->dbCursor;
    keyDbt = &backCursor->key;
    valueDbt = &backCursor->value;

    status = dbCursor->get(dbCursor, keyDbt, valueDbt, DB_NEXT);

    if (0 == status) {
        status = copyEntry(rdbCursor, backCursor);
    }
    else if (DB_NOTFOUND == status) {
        status = ENOENT;
    }
    else {
        DB_ENV*         env =
            backCursor->backend->db->get_env(backCursor->backend->db);
        const char*     path;

        (void)env->get_home(env, &path);
        log_add("Couldn't advance cursor for database \"%s\" to next entry "
            "after key \"%s\"", path, rdbCursor->key);
        status = EIO;
    }

    return status;
}

/*
 * Closes an RDB cursor.
 *
 * ARGUMENTS:
 *      rdbCursor       Pointer to the RDB cursor structure.
 * RETURNS:
 *      0               Success.  The client shall not use the cursor again.
 *      EIO             Backend database error.  "log_start()" called.
 */
RegStatus
beCloseCursor(RdbCursor* rdbCursor)
{
    RegStatus           status;
    BackCursor*         backCursor;
    DBC*                dbCursor;

    assert(NULL != rdbCursor);
    assert(NULL != rdbCursor->private);

    backCursor = (BackCursor*)rdbCursor->private;
    dbCursor = backCursor->dbCursor;
    status = dbCursor->close(dbCursor);

    if (status) {
        DB_ENV*         env =
            backCursor->backend->db->get_env(backCursor->backend->db);
        const char*     path;

        (void)env->get_home(env, &path);
        log_start("Couldn't close cursor for database \"%s\"", path);
        status = EIO;
    }
    else {
        free(backCursor->key.data);
        free(backCursor->value.data);
        free(rdbCursor->key);
        free(rdbCursor->value);
        free(rdbCursor->private);

        rdbCursor->key = NULL;
        rdbCursor->value = NULL;
        rdbCursor->private = NULL;
    }

    return status;
}
