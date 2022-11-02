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
#include <db.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

#include "backend.h"
#include "log.h"
#include "registry.h"
#include "string_buf.h"

typedef struct cursor        Cursor;

/*
 * Decorator of the Berkeley cursor structure for use by this backend module.
 */
struct cursor {
    DBT         key;
    DBT         value;
    DBC*        dbCursor;
};

struct backend {
    DB*         db;
    Cursor      cursor;
};

static const char     DB_DIRNAME[] = "registry";
static const char     DB_FILENAME[] = "registry.db";

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
    log_add("Berkeley DB: %s", msg);
}

/*
 * The "is_alive" function for checking the database via DB_ENV->failchk().
 *
 * Arguments:
 *      env             Pointer to the database environment.  Shall not be NULL.
 *      pid             Process ID of the process to check.
 *      tid             Thread ID of the thread to check.  Ignored because all
 *                      such processes are single-threaded.
 *      flags           Flags.  One of
 *                          0                       Ignored.
 *                          DB_MUTEX_PROCESS_ONLY   Return only if the process
 *                                                  is alive.  Ignore the 
 *                                                  thread ID.
 * Returns:
 *      0               The specified thread of control doesn't exist.
 *      else            The specified thread of control exists.
 */
static int
is_alive(
    DB_ENV*             env,
    pid_t               pid,
    db_threadid_t       tid,
    u_int32_t           flags)
{
    return kill(pid, 0) == 0 ? 1 : 0;
}

/*
 * Creates a database environment handle
 *
 * ARGUMENTS:
 *      path            Pathname of the database directory.  Shall not be NULL.
 *                      The client can free it upon return.
 *      dbEnv           Pointer to a pointer to the database environment.  Shall
 *                      not be NULL.  "*dbEnv" is set upon successful return.
 * RETURNS:
 *      0               Success.  "*dbEnv" is set.
 *      ENOMEM          System error.  "log_add()" called.
 *      EIO             Backend database error.  "log_add()" called.
 */
static RegStatus
createEnvHandle(
    const char* const   path,
    DB_ENV** const      dbEnv)
{
    RegStatus   status;
    DB_ENV*     env;

    assert(NULL != path);

    log_list_clear();

    if (status = db_env_create(&env, 0)) {
        log_add_syserr("Couldn't create environment handle for database: %s",
            db_strerror(status));
        log_flush_error();
        status = ENOMEM;
    }
    else {
        env->set_errcall(env, logDbError);

        if (status = env->set_isalive(env, is_alive)) {
            log_add("Couldn't register \"is_alive()\" function for "
                "database \"%s\"", path);
            status = EIO;
        }
        else {
            static const unsigned      threadCount = 256;

            if (status = env->set_thread_count(env, threadCount)) {
                log_add("Couldn't set thread count to %u for database \"%s\"",
                    threadCount, path);
                status = EIO;
            }
            else {
                *dbEnv = env;
            }
        }

        if (status)
            (void)env->close(env, 0);
    }                                   /* "env" allocated */

    return status;
}

/*
 * Opens the database environment.
 *
 * ARGUMENTS:
 *      path            Pathname of the database directory.  Shall not be NULL.
 *                      The client can free it upon return.
 *      dbEnv           Pointer to a pointer to the database environment.  Shall
 *                      not be NULL.  "*dbEnv" is set upon successful return.
 * RETURNS:
 *      0               Success.  "*dbEnv" is set.
 *      ENOMEM          System error.  "log_add()" called.
 *      EIO             Backend database error.  "log_add()" called.
 */
static RegStatus
openEnvironment(
    const char* const   path,
    DB_ENV** const      dbEnv)
{
    RegStatus   status;
    DB_ENV*     env;

    assert(NULL != path);

    log_list_clear();

    if (0 == (status = createEnvHandle(path, &env))) {
        /*
         * The database is configured for "concurrent data store"
         * access rather than for transactional access because the
         * former is faster and sufficient.
         */
        status = env->open(env, path, DB_CREATE | DB_INIT_CDB | DB_INIT_MPOOL,
            0);

        if (status) {
            log_add("Couldn't open environment for database \"%s\"", path);
            status = EIO;
        }
        else {
            *dbEnv = env;
        }

        if (status)
            (void)env->close(env, 0);
    }                                   /* "env" allocated */

    return status;
}

/*
 * Checks the database environment.
 *
 * ARGUMENTS:
 *      path            Pathname of the database directory.  Shall not be NULL.
 *                      The client can free it upon return.
 * RETURNS:
 *      0               Success.
 *      ENOMEM          System error.  "log_add()" called.
 *      EIO             Backend database error.  "log_add()" called.
 *      ECANCELED       The database environment must be recovered.
 *                      "log_add()" called.
 */
static RegStatus
verifyEnvironment(
    const char* const   path)
{
    RegStatus   status;
    DB_ENV*     env;

    assert(NULL != path);

    if (0 == (status = openEnvironment(path, &env))) {
        if (0 != (status = env->failchk(env, 0))) {
            log_add("The environment of database \"%s\" must be recovered",
                path);
            status = ECANCELED;
        }

        (void)env->close(env, 0);
    }                                   /* "env" allocated */

    return status;
}

/*
 * Forcebly removes the database environment.
 *
 * ARGUMENTS:
 *      path            Pathname of the database directory.  Shall not be NULL.
 *                      The client can free it upon return.
 * RETURNS:
 *      0               Success.
 *      ENOMEM          System error.  "log_add()" called.
 *      EIO             Backend database error.  "log_add()" called.
 */
static RegStatus
removeEnvironment(
    const char* const   path)
{
    RegStatus   status;
    DB_ENV*     env;

    assert(NULL != path);

    if (0 == (status = createEnvHandle(path, &env))) {
        if (status = env->remove(env, path, DB_FORCE)) {
            log_add("Couldn't remove environment for database \"%s\"", path);
            status = EIO;
        }
    }                                   /* "env" allocated */

    return status;
}

/*
 * Creates a database handle.
 *
 * ARGUMENTS:
 *      path            Pathname of the database directory.  Shall not be NULL.
 *                      The client can free it upon return.
 *      envHandle       Pointer to a pointer to the database environment
 *                      handle.  Shall not be NULL.  "*envHandle" is set upon
 *                      successful return.
 *      dbHandle        Pointer to a pointer to the database handle.  
 *                      "*dbHandle" is set upon successful return.
 * RETURNS:
 *      0               Success.  "*envHandle" and "*dbHandle" are set.
 *      ENOMEM          System error.  "log_add()" called.
 *      EIO             Backend database error.  "log_add()" called.
 */
static RegStatus
createDbHandle(
    const char* const   path,
    DB_ENV** const      envHandle,
    DB** const          dbHandle)
{
    RegStatus   status;
    DB_ENV*     env;

    assert(NULL != envHandle);
    assert(NULL != dbHandle);

    if (0 == (status = openEnvironment(path, &env))) {
        DB*     db;

        if (status = db_create(&db, env, 0)) {
            log_add("Couldn't create database handle");
            status = ENOMEM;
        }
        else {
            db->set_errcall(db, logDbError);
            *envHandle = env;
            *dbHandle = db;
        }

        if (status)
            (void)env->close(env, 0);
    }                                   /* "env" allocated */

    return status;
}

/*
 * Verifies the database.
 *
 * ARGUMENTS:
 *      path            Pathname of the database directory.  Shall not be NULL.
 *                      The client can free it upon return.
 * RETURNS:
 *      0               Success.
 *      ENOMEM          System error.  "log_add()" called.
 *      EIO             Backend database error.  "log_add()" called.
 */
static RegStatus
verifyDatabase(
    const char* const   path)
{
    RegStatus   status;
    DB_ENV*     env;
    DB*         db;

    assert(NULL != path);

    if (0 == (status = createDbHandle(path, &env, &db))) {
        if (status = db->verify(db, DB_FILENAME, NULL, NULL, 0)) {
            log_add("Couldn't verify file \"%s\" of database "
                "\"%s\"", DB_FILENAME, path);
            status = EIO;
        }

        (void)env->close(env, 0);
    }                                   /* "env" allocated */

    return status;
}

/*
 * Verifies the backend: both environment and database.
 *
 * Arguments:
 *      path            Pathname of the database directory.  Shall not be NULL.
 *                      The client can free it upon return.
 * Returns:
 *      0               Success.
 *      ENOMEM          System error.  "log_add()" called.
 *      EIO             Backend database error.  "log_add()" called.
 *      ECANCELED       The backend must be recovered.  "log_add()" called.
 */
static RegStatus
verifyBackend(
    const char* const   path)
{
    RegStatus   status = verifyEnvironment(path);

    if (0 == status)
        status = verifyDatabase(path);

    return status;
}

/*
 * Constructs a path name for the database.
 *
 * Arguments:
 *      path            Pathname of the database directory.  Shall not be NULL.
 *                      The client can free it upon return.
 *      ext             Extension for the database path name.
 * Returns:
 *      NULL            System error.  "log_add()" called.
 *      else            Pointer to the path name of the database.  The
 *                      client should call "free()" when the path name is no
 *                      longer needed.
 */
static char*
makeDatabasePath(
    const char* const   path,
    const char* const   ext)
{
    static const size_t lenFilename = sizeof(DB_FILENAME) - 1;
    size_t              lenPath = strlen(path);
    size_t              len = strlen(path) + 1 + lenFilename + strlen(ext) + 1;
    char*               buf = (char*)malloc(len);

    if (NULL == buf) {
        log_syserr("Couldn't allocate %lu bytes", (unsigned long)len);
    }
    else {
        (void)strcpy(strcpy(strcpy(strcpy(buf, path) + lenPath, "/") + 1, 
            DB_FILENAME) + lenFilename, ext);
    }

    return buf;
}

/*
 * Copies the database.
 *
 * Arguments:
 *      path            Pointer to the pathname of the database directory.
 *                      Shall not be NULL.  The client can free it upon return.
 *      fromExt         Pointer to the "from" extension.  Shall not be NULL.
 *      toExt           Pointer to the "to" extension.  Shall not be NULL.
 * Returns:
 *      0               Success.
 *      ENOMEM          System error.  "log_add()" called.
 *      EIO             Backend database error.  "log_add()" called.
 */
static RegStatus
copyDatabase(
    const char* const   path,
    const char* const   fromExt,
    const char* const   toExt)
{
    RegStatus           status;
    char* const         fromPath = makeDatabasePath(path, fromExt);

    if (NULL == fromPath) {
        status = ENOMEM;
    }
    else {
        char* const toPath = makeDatabasePath(path, toExt);

        if (NULL == toPath) {
            status = ENOMEM;
        }
        else {
            size_t      lenFromPath = strlen(fromPath);
            size_t      lenToPath = strlen(toPath);
            char*       cmd = (char*)malloc(3 + lenFromPath + 1 +
                lenToPath + 1);

            (void)strcpy(strcpy(strcpy(strcpy(cmd, "cp ") + 3, fromPath)
                + lenFromPath, " ") + 1, toPath);

            if (system(cmd)) {
                log_add("Couldn't execute command \"%s\"", cmd);
                status = ENOMEM;
            }
            else {
                status = 0;             /* success */
            }

            free(toPath);
        }                               /* "toPath" allocated */

        free(fromPath);
    }                                   /* "databasePath" allocated */

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
 *      EIO             Backend database error.  "log_add()" called.
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
 * Closes the cursor.
 *
 * Arguments:
 *      backend         Pointer to the backend database.  Shall not be NULL.
 * Returns:
 *      0               Success.
 *      EIO             Backend database error.  "log_add()" called.
 */
static RegStatus closeCursor(
    Backend* const       backend)
{
    RegStatus           status = 0;     /* success */
    Cursor* const       cursor = &backend->cursor;
    DBC* const          dbCursor = cursor->dbCursor;

    if (dbCursor) {
        log_list_clear();

        if (status = dbCursor->close(dbCursor)) {
            log_add("Couldn't close cursor for database \"%s\"",
                getPath(backend->db));
            status = EIO;
        }
        else {
            free(cursor->key.data);
            free(cursor->value.data);
            cursor->key.data = NULL;
            cursor->value.data = NULL;
            cursor->dbCursor = NULL;
        }
    }

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
 *      dir             Pathname of the parent directory of the database.
 *                      Shall not be NULL.  The client can free it upon return.
 *      forWriting      Open the database for writing? 0 <=> no
 * RETURNS:
 *      0               Success.  "*backend" is set.
 *      ENOMEM          System error.  "log_add()" called.
 *      EIO             Backend database error.  "log_add()" called.
 */
RegStatus
beOpen(
    Backend** const     backend,
    const char* const   dir,
    int                 forWriting)
{
    RegStatus   status;
    Backend*    back = (Backend*)malloc(sizeof(Backend));

    assert(NULL != dir);

    if (NULL == back) {
        log_syserr("Couldn't allocate %lu bytes", (long)sizeof(Backend));
        status = ENOMEM;
    }
    else {
        DB_ENV*         env;
        DB*             db;
        StringBuf*      path;

        if (0 == (status = sb_new(&path, PATH_MAX))) {
            if (0 == (status = sb_cat(path, dir, "/", DB_DIRNAME))) {
                if (0 == (status = createDbHandle(path, &env, &db))) {
                    if (status = db->open(db, NULL, DB_FILENAME, NULL,
                            DB_BTREE, forWriting ? DB_CREATE : DB_RDONLY, 0)) {
                        log_add("Couldn't open database \"%s\" in \"%s\" "
                            "for %s", DB_FILENAME, path,
                            forWriting ? "writing" : "reading");
                        status = EIO;
                    }
                    else {
                        back->db = db;
                        back->cursor.dbCursor = NULL;
                        *backend = back;    /* success */
                    }                       /* "db" opened */

                    /*
                     * According to the documentation on DB->open(), if that
                     * call fails, then DB->close() must be called to discard
                     * the DB handle, so DB->close() is the termination
                     * counterpart of db_create() rather than of DB->open().
                     */
                    if (status) {
                        (void)db->close(db, 0);
                        (void)env->close(env, 0);
                    }
                }                       /* "env" allocated */
            }                           /* DB directory pathname created */

            sb_free(path);
        }                               /* "path" allocated */

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
 *                      "beOpen()" or may be NULL.  Upon return, "backend"
 *                      shall not be used again.
 * RETURNS:
 *      0               Success.
 *      EIO             Backend database error.  "log_add()" called.
 */
RegStatus
beClose(
    Backend* const      backend)
{
    RegStatus   status = 0;             /* success */

    if (NULL != backend) {
        DB* const       db = backend->db;
        char*           path = strdup(getPath(db));
        DB_ENV* const   env = db->get_env(db);

        if (closeCursor(backend))
            status = EIO;

        if (db->close(db, 0)) {
            log_add("Couldn't close backend database \"%s\"", path);
            status = EIO;
        }
        else {
            backend->db = NULL;
        }

        if (env->close(env, 0)) {
            log_add("Couldn't close environment of backend database \"%s\"",
                path);
            status = EIO;
        }

        if (0 == status)
            free(backend);

        free(path);
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
 *      EIO             Backend database error.  "log_add()" called.
 */
RegStatus
beReset(
    const char* const   path)
{
    RegStatus   status;

    assert(NULL != path);

    if (0 == (status = verifyBackend(path))) {
        /*
         * The database is OK.  Make a backup copy.
         */
        status = copyDatabase(path, "", ".bck");
    }
    else if (ECANCELED == status) {
        /*
         * The backend database needs to be restored.
         */
        log_notice_q("Restoring from backup");

        if (0 == (status = removeEnvironment(path))) {
            status = copyDatabase(path, ".bck", "");
        }
    }

    return status;
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
 *      ENOMEM          System error.  "log_add()" called.
 *      EIO             Backend database error.  "log_add()" called.
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
    if (0 == (status = openEnvironment(path, &env))) {
        if (status = env->dbremove(env, NULL, DB_FILENAME, NULL, 0)) {
            log_add("Couldn't remove database file \"%s\" in \"%s\"",
                DB_FILENAME, path);
            status = EIO;
        }

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
 *      EIO             Backend database error.  "log_add()" called.
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
 *      EIO             Backend database error.  "log_add()" called.
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
 *      EIO             Backend database error.  "log_add()" called.
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
 *      EIO    Backend database error.  "log_add()" called.
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
 * Initializes the cursor.
 *
 * ARGUMENTS:
 *      backend         Pointer to the backend database.  Shall have been
 *                      set by beOpen().  Shall not be NULL.
 * RETURNS
 *      0               Success.
 *      EINVAL          The backend database already has an active cursor.
 *      EIO             Backend database error.  "log_add()" called.
 *      ENOMEM          System error.  "log_add()" called.
 */
RegStatus
beInitCursor(
    Backend* const      backend)
{
    RegStatus   status;

    assert(NULL != backend);
    assert(NULL != backend->db);

    if (backend->cursor.dbCursor) {
        log_add("Cursor already active for backend database \"%s\"",
            getPath(backend->db));
        status = EINVAL;
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
            (void)memset(&backend->cursor.key, 0, sizeof(DBT));
            (void)memset(&backend->cursor.value, 0, sizeof(DBT));

            backend->cursor.key.data = NULL;
            backend->cursor.value.data = NULL;
            backend->cursor.key.flags |= DB_DBT_REALLOC;
            backend->cursor.value.flags |= DB_DBT_REALLOC;
            backend->cursor.dbCursor = dbCursor;
        }                               /* "dbCursor" allocated */
    }

    return status;
}

/*
 * Sets the cursor to reference the first entry in the backend
 * database whose key is greater than or equal to a given key.
 *
 * ARGUMENTS:
 *      backend         Pointer to the backend database.  Shall have been
 *                      set by beOpen().  Shall not be NULL.
 *      key             Pointer to the starting key.  Shall not be NULL.  The
 *                      empty string obtains the first entry in the database,
 *                      if it exists.
 * RETURNS
 *      0               Success.
 *      EINVAL          The cursor is not initialized.
 *      ENOENT          The database is empty.
 *      EIO             Backend database error.  "log_add()" called.
 *      ENOMEM          System error.  "log_add()" called.
 */
RegStatus
beFirstEntry(
    Backend* const      backend,
    const char* const   key)
{
    RegStatus   status;
    Cursor*     cursor;

    assert(NULL != backend);
    assert(NULL != key);

    cursor = &backend->cursor;

    if (!cursor->dbCursor) {
        log_add("Cursor for backend database \"%s\" not initialized",
            getPath(backend->db));
        status = EINVAL;
    }
    else {
        char* const dupKey = strdup(key);

        if (NULL == dupKey) {
            log_syserr("Couldn't allocate %lu bytes", (long)strlen(key));
            status = ENOMEM;
        }
        else {
            backend->cursor.key.data = dupKey;
            backend->cursor.key.size = strlen(dupKey) + 1;

            if (EIO == (status = setCursor(&backend->cursor, DB_SET_RANGE))) {
                log_add("Couldn't set cursor for database \"%s\" to first "
                    "entry on or after key \"%s\"", getPath(backend->db), key);
            }
        }                               /* "dupKey" allocated */
    }

    return status;
}

/*
 * Advances the cursor to the next entry.
 *
 * ARGUMENTS:
 *      backend         Pointer to the backend database.  Shall have been
 *                      set by beOpen().  Shall not be NULL.
 * RETURNS
 *      0               Success.
 *      ENOENT          The database is empty.
 *      EIO             Backend database error.  "log_add()" called.
 */
RegStatus
beNextEntry(
    Backend* const      backend)
{
    RegStatus   status;
    Cursor*     cursor;

    assert(NULL != backend);

    cursor = &backend->cursor;

    if (!cursor->dbCursor) {
        log_add("Cursor for backend database \"%s\" not initialized",
            getPath(backend->db));
        status = EINVAL;
    }
    else {
        if (EIO == (status = setCursor(cursor, DB_NEXT))) {
            log_add("Couldn't advance cursor for database \"%s\" to next entry "
                "after key \"%s\"", getPath(backend->db),
                cursor->key.data);
        }
    }

    return status;
}

/*
 * Frees the cursor.  Should be called after every successful call to
 * beInitCursor().
 *
 * ARGUMENTS:
 *      backend         Pointer to the backend database.  Shall have been
 *                      set by beOpen().  Shall not be NULL.
 * RETURNS:
 *      0               Success.
 *      EIO             Backend database error.  "log_add()" called.
 */
RegStatus
beFreeCursor(
    Backend* const      backend)
{
    assert(NULL != backend);

    return closeCursor(backend);
}

/*
 * Returns the key of the cursor.
 *
 * Arguments:
 *      backend         Pointer to the backend database.  Shall have been
 *                      set by beOpen().  Shall not be NULL.
 * Returns:
 *      Pointer to the key.  Shall not be NULL if beFirstEntry() or 
 *      beNextEntry() was successful.
 */
const char* beGetKey(const Backend* const backend)
{
    assert(NULL != backend);

    return backend->cursor.key.data;
}

/*
 * Returns the value of the cursor.
 *
 * Arguments:
 *      backend         Pointer to the backend database.  Shall have been
 *                      set by beOpen().  Shall not be NULL.
 * Returns:
 *      Pointer to the value.  Shall not be NULL if beFirstEntry() or 
 *      beNextEntry() was successful.
 */
const char* beGetValue(const Backend* backend)
{
    assert(NULL != backend);

    return backend->cursor.value.data;
}
