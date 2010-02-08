/*
 *   Copyright 2005, University Corporation for Atmospheric Research
 *   See ../COPYRIGHT file for copying and redistribution conditions.
 */
/* $Id: globals.c,v 1.1.2.3 2007/02/12 20:38:54 steve Exp $ */

#include "config.h"

#include <rpc/rpc.h>  /* svc_req */
#include <stddef.h>
#include <signal.h>   /* sig_atomic_t */
#include <string.h>

#include "ldm.h"
#include "globals.h"
#include "remote.h"
#include "ldm.h"
#include "log.h"
#include "pq.h"
#include "registry.h"

volatile sig_atomic_t   done = 0;
const char*             logfname = 0;
pqueue*                 pq = NULL;

static int              cleanupRegistered = 0;
static char*            queuePath;
static char*            pqactConfigPath;
static char*            pqsurfConfigPath;
static char*            ldmdConfigPath;
static char*            pqactDataDirPath;
static char*            pqsurfDataDirPath;
static char*            surfQueuePath;

/*
 * Timeout for rpc calls:
 */
unsigned int            rpctimeo = DEFAULT_RPCTIMEO;

/*
 * Time to sleep in pq_suspend() and before retrying connects.
 */
unsigned int            interval = 30;       

/*
 * Shut down a service connection that has been idle this long.
 * The keepalive timeout (for the other end) is
 * inactive_timeo/2 - 2 * interval;
 */
const int               inactive_timeo = 720;  /* 12 m */

/*
 * In requests, set 'from' to 'toffset' ago, and it may get
 * adjusted by  pq_clss_setfrom();
 */
int                     max_latency = DEFAULT_OLDEST;
int                     toffset = TOFFSET_NONE;

/*
 * Terminates this module.  This function shall only be called by exit().
 */
static void terminate(void)
{
    free(queuePath);
    free(pqactConfigPath);
    free(pqsurfConfigPath);
    free(ldmdConfigPath);
    free(pqactDataDirPath);
    free(pqsurfDataDirPath);
    free(surfQueuePath);
}

/*
 * Registers the cleanup handler.
 */
static void registerCleanup()
{
    if (!cleanupRegistered) {
        if (atexit(terminate)) {
            log_serror("globals.c: Couldn't register cleanup routine");
            log_log(LOG_ERR);
        }
        else {
            cleanupRegistered = 1;
        }
    }
}

/*
 * Sets the path name of a file for the duration of the process.
 *
 * Arguments:
 *      path            Pointer to the path name.  Shall not be NULL.  Can be
 *                      absolute or relative to the current working directory.
 *      var             Pointer to the pointer to the path name to be set.
 */
static void setPath(
    const char* const   path,
    char** const        var)
{
    char*       newPath = strdup(path);

    if (NULL == newPath) {
        log_serror("Couldn't duplicate string \"%s\"", path);
        log_log(LOG_ERR);
    }
    else {
        free(*var);
        *var = newPath;
    }
}

/*
 * Returns the path name of a file.
 *
 * Arguments:
 *      name            The name of the registry parameter that contains the
 *                      desired pathname.
 *      var             Pointer to the variable to be set to point to the
 *                      desired pathname.
 *      desc            Pointer to a description of the file.
 * Returns:
 *      NULL            Error.  "log_start()" called.
 *      else            Pointer to the pathname of the file.  Might be absolute
 *                      or relative to the current working directory.
 */
static const char* getPath(
    const char* const   name,
    char** const        var,
    const char* const   desc)
{
    if (NULL == *var) {
        if (reg_getString(name, var)) {
            log_add("Couldn't get pathname of %s", desc);
        }
        else {
            registerCleanup();
        }
    }

    return *var;
}

/******************************************************************************
 * Public API:
 ******************************************************************************/

/*
 * Calls exit() if the "done" global variable is set; othewise, returns 1 so
 * that it can be easily used in programming loops.
 *
 * Arguments:
 *      status  Exit status for the call to exit().
 * Returns:
 *      1
 */
int exitIfDone(
    const int   status)
{
    if (done)
        exit(status);

    return 1;
}

/*
 * Sets the path name of the product-queue.
 *
 * Arguments:
 *      path            Pointer to the path name of the product-queue.
 *                      Shall not be NULL.  Can be absolute or relative to the
 *                      current working directory.
 */
void setQueuePath(
    const char* const   path)
{
    setPath(path, &queuePath);
}

/*
 * Returns the path name of the product-queue.
 *
 * Returns:
 *      NULL            Error.  "log_start()" called.
 *      else            Pointer to the pathname of the product-queue.
 *                      Might be absolute or relative to the current working
 *                      directory.
 */
const char* getQueuePath()
{
    return getPath(REG_QUEUE_PATH, &queuePath, "product-queue");
}

/*
 * Sets the path name of the default pqact(1) configuration-file for the
 * duration of the process.
 *
 * Arguments:
 *      path            Pointer to the path name.  Shall not be NULL.  Can be
 *                      absolute or relative to the current working directory.
 */
void setPqactConfigPath(
    const char* const   path)
{
    setPath(path, &pqactConfigPath);
}

/*
 * Returns the path name of the default pqact(1) configuration-file.
 *
 * Returns:
 *      NULL            Error.  "log_start()" called.
 *      else            Pointer to the pathname.  Might be absolute or relative
 *                      to the current working directory.
 */
const char* getPqactConfigPath()
{
    return getPath(REG_PQACT_CONFIG_PATH, &pqactConfigPath,
        "default pqact(1) configuration-file");
}

/*
 * Sets the path name of the ldmd(1) configuration-file for the
 * duration of the process.
 *
 * Arguments:
 *      path            Pointer to the path name.  Shall not be NULL.  Can be
 *                      absolute or relative to the current working directory.
 */
void setLdmdConfigPath(
    const char* const   path)
{
    setPath(path, &ldmdConfigPath);
}

/*
 * Returns the path name of the ldmd(1) configuration-file.
 *
 * Returns:
 *      NULL            Error.  "log_start()" called.
 *      else            Pointer to the pathname.  Might be absolute or relative
 *                      to the current working directory.
 */
const char* getLdmdConfigPath()
{
    return getPath(REG_LDMD_CONFIG_PATH, &ldmdConfigPath,
        "ldmd(1) configuration-file");
}

/*
 * Sets the path name of the default pqact(1) data-directory for the
 * duration of the process.
 *
 * Arguments:
 *      path            Pointer to the path name.  Shall not be NULL.  Can be
 *                      absolute or relative to the current working directory.
 */
void setPqactDataDirPath(
    const char* const   path)
{
    setPath(path, &pqactDataDirPath);
}

/*
 * Returns the path name of the default pqact(1) data-directory.
 *
 * Returns:
 *      NULL            Error.  "log_start()" called.
 *      else            Pointer to the pathname.  Might be absolute or relative
 *                      to the current working directory.
 */
const char* getPqactDataDirPath()
{
    return getPath(REG_PQACT_DATADIR_PATH, &pqactDataDirPath,
        "default pqact(1) data-directory");
}

/*
 * Sets the path name of the default pqsurf(1) data-directory for the
 * duration of the process.
 *
 * Arguments:
 *      path            Pointer to the path name.  Shall not be NULL.  Can be
 *                      absolute or relative to the current working directory.
 */
void setPqsurfDataDirPath(
    const char* const   path)
{
    setPath(path, &pqsurfDataDirPath);
}

/*
 * Returns the path name of the default pqsurf(1) data-directory.
 *
 * Returns:
 *      NULL            Error.  "log_start()" called.
 *      else            Pointer to the pathname.  Might be absolute or relative
 *                      to the current working directory.
 */
const char* getPqsurfDataDirPath()
{
    return getPath(REG_PQSURF_DATADIR_PATH, &pqsurfDataDirPath,
        "default pqsurf(1) data-directory");
}

/*
 * Sets the path name of the default pqsurf(1) output product-queue for the
 * duration of the process.
 *
 * Arguments:
 *      path            Pointer to the path name.  Shall not be NULL.  Can be
 *                      absolute or relative to the current working directory.
 */
void setSurfQueuePath(
    const char* const   path)
{
    setPath(path, &surfQueuePath);
}

/*
 * Returns the path name of the default pqsurf(1) output product-queue
 *
 * Returns:
 *      NULL            Error.  "log_start()" called.
 *      else            Pointer to the pathname.  Might be absolute or relative
 *                      to the current working directory.
 */
const char* getSurfQueuePath()
{
    return getPath(REG_SURFQUEUE_PATH, &surfQueuePath,
        "default pqsurf(1) output product-queue");
}

/*
 * Sets the path name of the default pqsurf(1) configuration-file for the
 * duration of the process.
 *
 * Arguments:
 *      path            Pointer to the path name.  Shall not be NULL.  Can be
 *                      absolute or relative to the current working directory.
 */
void setPqsurfConfigPath(
    const char* const   path)
{
    setPath(path, &pqsurfConfigPath);
}

/*
 * Returns the path name of the default pqsurf(1) configuration-file.
 *
 * Returns:
 *      NULL            Error.  "log_start()" called.
 *      else            Pointer to the pathname.  Might be absolute or relative
 *                      to the current working directory.
 */
const char* getPqsurfConfigPath()
{
    return getPath(REG_PQSURF_CONFIG_PATH, &pqsurfConfigPath,
        "default pqsurf(1) configuration-file");
}
