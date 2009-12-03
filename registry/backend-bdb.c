/*
 * See file ../COPYRIGHT for copying and redistribution conditions.
 *
 * This module implements the runtime database backend database API via the
 * Berkeley DB API.
 */
#include <config.h>

#undef NDEBUG
#include <assert.h>

#include <db.h>
#include "backend.h"
#include <stdlib.h>
#include <string.h>
#include <log.h>

struct backend {
    DB_ENV*     env;
    DB*         db;
};

#define DB_FILENAME    "ldm-runtime.db"

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
 *      REG_SYS_ERROR   System error.  The backend cursor structure is
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
        status = REG_SYS_ERROR;
    }
    else {
        char*       value = strdup(backCursor->value.data);

        if (NULL == value) {
            free(key);
            status = REG_SYS_ERROR;
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
 *      REG_SYS_ERROR   System error.  "log_start()" called.
 *      REG_DB_ERROR    Backend database error.  "log_start()" called.
 */
RegStatus
beOpen(
    Backend** const     backend,
    const char* const   path,
    int                 forWriting)
{
    RegStatus   status;
    Backend*    back = (Backend*)malloc(sizeof(Backend));

    assert(NULL != backend);
    assert(NULL != path);

    if (NULL == back) {
        log_serror("Couldn't allocate %lu bytes", (long)sizeof(Backend));
        status = REG_SYS_ERROR;
    }
    else {
        DB_ENV* env;

        if (status = db_env_create(&env, 0)) {
            log_start("Couldn't create database environment: %s",
                db_strerror(status));
            status = REG_DB_ERROR;
        }
        else {
            env->set_errcall(env, logDbError);

            /*
             * The database is configured for concurrent access rather than for
             * transactional access because the former is faster and sufficient.
             */
            if (status = env->open(env, path,
                    DB_CREATE | DB_INIT_CDB | DB_INIT_MPOOL, 0)) {
                log_add("Couldn't open database environment in \"%s\"", path);
                status = REG_DB_ERROR;
            }
            else {
                DB*     db;

                if (status = db_create(&db, env, 0)) {
                    log_add("Couldn't create database");
                    status = REG_DB_ERROR;
                }
                else {
                    db->set_errcall(db, logDbError);

                    if (status = db->open(db, NULL, DB_FILENAME, NULL, DB_BTREE,
                            forWriting ? DB_CREATE : DB_RDONLY, 0)) {
                        log_add("Couldn't open database \"%s\" in \"%s\" for "
                            "%s", DB_FILENAME, path,
                            forWriting ? "writing" : "reading");
                        status = REG_DB_ERROR;
                    }
                    else {
                        back->env = env;
                        back->db = db;
                        *backend = back;
                        status = 0;     /* success */
                    }                   /* "db" opened */

                    if (status && NULL != db)
                        db->remove(db, DB_FILENAME, NULL, 0);
                }                       /* "db" allocated */

                if (status) {
                    env->close(env, 0);
                    env = NULL;
                }
            }                       /* "env" opened */

            /*
             * env->close() is called here (if it hasn't been called already)
             * instead of calling env->remove()) because calling env->remove()
             * after env->close() results in a SIGSEGV and calling
             * env->remove() with no preceeding call to env->close() results in
             * a memory-leak.
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
 *                      "beOpen()".  Shall not be NULL.  Upon return, "backend"
 *                      shall not be used again.
 * RETURNS:
 *      0               Success.
 *      REG_DB_ERROR    Backend database error.  "log_start()" called.
 */
RegStatus
beClose(
    Backend* const      backend)
{
    RegStatus   status;
    const char* path;
    DB_ENV*     env;
    DB*         db;

    assert(NULL != backend);
    assert(NULL != backend->db);
    assert(NULL != backend->env);

    env = backend->env;
    db = backend->db;
    status = db->close(db, 0);

    if (status) {
        (void)env->get_home(env, &path);
        log_add("Couldn't close backend database \"%s\"", path);
        status = REG_DB_ERROR;
    }
    else {
        backend->db = NULL;

        if (env->close(env, 0)) {
            (void)env->get_home(env, &path);
            log_add("Couldn't close environment of backend database \"%s\"",
                path);
            status = REG_DB_ERROR;
        }
        else {
            backend->env = NULL;
            free(backend);
            status = 0;                 /* success */
        }
    }

    return status;
}

/*
 * Removes the backend database.
 *
 * ARGUMENTS:
 *      path            Pathname of the database directory.  Shall not be NULL.
 *                      The client can free it upon return.
 * RETURNS:
 *      0               Success.
 *      REG_SYS_ERROR   System error.  "log_start()" called.
 *      REG_DB_ERROR    Backend database error.  "log_start()" called.
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
        status = REG_DB_ERROR;
    }
    else {
        env->set_errcall(env, logDbError);

        if (status = env->open(env, path, 0, 0)) {
            log_add("Couldn't open database environment in \"%s\"", path);
            status = REG_DB_ERROR;
        }
        else {
            if (status = env->dbremove(env, NULL, DB_FILENAME, NULL, 0)) {
                log_add("Couldn't open database environment in \"%s\"", path);
                status = REG_DB_ERROR;
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
                }
                else {
                    if (status = db_env_create(&env, 0)) {
                        log_start("Couldn't create database environment: %s",
                            db_strerror(status));
                        status = REG_DB_ERROR;
                    }
                    else {
                        env->set_errcall(env, logDbError);

                        if (status = env->remove(env, path, 0)) {
                            log_add("Couldn't remove database environment");
                            status = REG_DB_ERROR;
                        }
                        else {
                            env = NULL;
                            status = 0; /* success */
                        }
                    }                   /* "env" allocated */
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
 *      key             Pointer to the 0-terminated key.
 *      value           Pointer to the string value.
 * RETURNS:
 *      0               Success.
 *      REG_DB_ERROR    Backend database error.  "log_start()" called.
 */
RegStatus
bePut(
    Backend*                    backend,
    const char* const           key,
    const StringBuf* const      value)
{
    RegStatus   status;
    DBT         keyDbt;
    DBT         valueDbt;

    assert(NULL != backend);
    assert(NULL != backend->db);
    assert(NULL != key);
    assert(NULL != value);

    (void)memset(&keyDbt, 0, sizeof(DBT));
    (void)memset(&valueDbt, 0, sizeof(DBT));

    keyDbt.data = (void*)key;
    keyDbt.size = strlen(key) + 1;
    valueDbt.data = (void*)sb_string(value);
    valueDbt.size = sb_len(value) + 1;

    if (status = backend->db->put(backend->db, NULL, &keyDbt, &valueDbt,
            0)) {
        log_add("Couldn't map key \"%s\" to value \"%s\"", key, value);
        status = REG_DB_ERROR;
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
 *      key             Pointer to the 0-terminated key.
 *      value           Pointer to a pointer to the string value.  Shall not be
 *                      NULL.  "*value" shall point to the 0-terminated string
 *                      value upon successful return.  The client should call
 *                      "free(*value)" when the value is no longer needed.
 * RETURNS:
 *      0               Success.  "*value" points to the string value.
 *      REG_NO_NODE     The given key doesn't match any entry.
 *      REG_DB_ERROR    Backend database error.  "log_start()" called.
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

    assert(NULL != backend);
    assert(NULL != backend->db);
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
        status = REG_NO_NODE;
    }
    else {
        log_add("Couldn't get value for key \"%s\"", key);
        status = REG_DB_ERROR;
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
 *      REG_NO_NODE     The given key doesn't match any entry.
 *      REG_DB_ERROR    Backend database error.  "log_start()" called.
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
        status = REG_NO_NODE;
    }
    else if (status) {
        log_add("Couldn't delete entry for key \"%s\"", key);
        status = REG_DB_ERROR;
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
 *      REG_DB_ERROR    Backend database error.  "log_start()" called.
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
        status = REG_DB_ERROR;
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
 *      REG_DB_ERROR    Backend database error.  "log_start()" called.
 *      REG_SYS_ERROR   System error.  "log_start()" called.
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
    assert(NULL != backend->env);
    assert(NULL != backend->db);
    assert(NULL != rdbCursor);

    env = backend->env;
    db = backend->db;

    /*
     * Because this function is only used for reading, the Berkeley cursor
     * needn't be transactionally protected.
     */
    status = db->cursor(db, NULL, &dbCursor, DB_READ_COMMITTED);

    if (status) {
        const char*     path;

        (void)env->get_home(env, &path);
        log_add("Couldn't create cursor for database \"%s\"", path);
        status = REG_DB_ERROR;
    }
    else {
        BackCursor*  backCursor = (BackCursor*)malloc(sizeof(BackCursor));

        if (NULL == backCursor) {
            log_serror("Couldn't allocate %lu bytes", (long)sizeof(BackCursor));
            status = REG_SYS_ERROR;
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
 *      REG_DB_ERROR    Backend database error.  "*rdbCursor" is unmodified.
 *                      "log_start()" called.
 *      REG_NO_NODE     The database is empty.  "*rdbCursor" is unmodified.
 *      REG_SYS_ERROR   System error.  "log_start()" called.
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
        status = REG_SYS_ERROR;
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
            status = REG_NO_NODE;
        }
        else {
            const char* path;
            DB_ENV*     env = backCursor->backend->env;

            (void)env->get_home(env, &path);
            log_add("Couldn't set cursor for database \"%s\" to first entry on "
                "or after key \"%s\"", path, key);
            status = REG_DB_ERROR;
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
 *      REG_DB_ERROR    Backend database error.  "*rdbCursor" is unmodified.
 *                      "log_start()" called.
 *      REG_NO_NODE     The database is empty.  "*rdbCursor" is unmodified.
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
        status = REG_NO_NODE;
    }
    else {
        DB_ENV*         env = backCursor->backend->env;
        const char*     path;

        (void)env->get_home(env, &path);
        log_add("Couldn't advance cursor for database \"%s\" to next entry "
            "after key \"%s\"", path, rdbCursor->key);
        status = REG_DB_ERROR;
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
 *      REG_DB_ERROR    Backend database error.  "log_start()" called.
 */
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
        DB_ENV*         env = backCursor->backend->env;
        const char*     path;

        (void)env->get_home(env, &path);
        log_add("Couldn't close cursor for database \"%s\"", path);
        status = REG_DB_ERROR;
    }

    free(backCursor->key.data);
    free(backCursor->value.data);
    free(rdbCursor->key);
    free(rdbCursor->value);
    free(rdbCursor->private);

    backCursor->key.data = NULL;
    backCursor->value.data = NULL;
    rdbCursor->key = NULL;
    rdbCursor->value = NULL;
    rdbCursor->private = NULL;

    return status;
}
