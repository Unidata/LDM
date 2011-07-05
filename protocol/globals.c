/*
 *   Copyright 2005, University Corporation for Atmospheric Research
 *   See ../COPYRIGHT file for copying and redistribution conditions.
 */
/* $Id: globals.c,v 1.1.2.3 2007/02/12 20:38:54 steve Exp $ */

#include "config.h"

#include <limits.h>
#include <rpc/rpc.h>  /* svc_req */
#include <stddef.h>
#include <signal.h>   /* sig_atomic_t */
#include <stdlib.h>
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
static char             queuePath[PATH_MAX];
static char             pqactConfigPath[PATH_MAX];
static char             pqsurfConfigPath[PATH_MAX];
static char             ldmdConfigPath[PATH_MAX];
static char             pqactDataDirPath[PATH_MAX];
static char             pqsurfDataDirPath[PATH_MAX];
static char             surfQueuePath[PATH_MAX];
static char             sysConfDirPath[PATH_MAX];

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
 * Sets a path name of a file for the duration of the process.
 *
 * Arguments:
 *      path            Pointer to the path name.  Shall not be NULL.  Can be
 *                      absolute or relative to the current working directory.
 *      buf             Buffer of length PATH_MAX into which to copy the path.
 */
static void setPath(
    const char* const   path,
    char                buf[PATH_MAX])
{
    strncpy(buf, path, PATH_MAX)[PATH_MAX-1] = 0;
}

/*
 * Returns the path name of a file.
 *
 * Arguments:
 *      name            The name of the registry parameter that contains the
 *                      desired pathname.
 *      buf             Buffer of length PATH_MAX into which to put the path.
 *      desc            Pointer to a description of the file.
 * Returns:
 *      NULL            Error.  "log_log()" called.
 *      else            Pointer to the pathname of the file.  Might be absolute
 *                      or relative to the current working directory.
 */
static const char* getPath(
    const char* const   name,
    char                buf[PATH_MAX],
    const char* const   desc)
{
    char*               var;

    if (0 == buf[0]) {
        if (reg_getString(name, &var)) {
            log_add("Couldn't get pathname of %s", desc);
            log_log(LOG_ERR);
        }
        else {
            setPath(var, buf);
        }
    }

    return buf;
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
    setPath(path, queuePath);
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
    return getPath(REG_QUEUE_PATH, queuePath, "product-queue");
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
    setPath(path, pqactConfigPath);
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
    return getPath(REG_PQACT_CONFIG_PATH, pqactConfigPath,
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
    setPath(path, ldmdConfigPath);
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
    return getPath(REG_LDMD_CONFIG_PATH, ldmdConfigPath,
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
    setPath(path, pqactDataDirPath);
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
    return getPath(REG_PQACT_DATADIR_PATH, pqactDataDirPath,
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
    setPath(path, pqsurfDataDirPath);
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
    return getPath(REG_PQSURF_DATADIR_PATH, pqsurfDataDirPath,
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
    setPath(path, surfQueuePath);
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
    return getPath(REG_SURFQUEUE_PATH, surfQueuePath,
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
    setPath(path, pqsurfConfigPath);
}

/*
 * Returns the path name of the default pqsurf(1) configuration-file.
 *
 * Returns:
 *      NULL            Error.  "log_start()" called.
 *      else            Pointer to the pathname.  Might be absolute or relative
 *                      to the current working directory.
 */
const char* getPqsurfConfigPath(void)
{
    return getPath(REG_PQSURF_CONFIG_PATH, pqsurfConfigPath,
        "default pqsurf(1) configuration-file");
}

/**
 * Returns the pathname of the home of the LDM installation.
 *
 * Returns
 *      Pointer to the pathname.  Might be absolute or relative to the current
 *      working directory.
 */
const char* getLdmHomePath(void)
{
    static const char*  ldmHomePath;

    if (NULL == ldmHomePath) {
        ldmHomePath = getenv("LDMHOME");

        if (NULL == ldmHomePath) {
            LOG_START0("LDMHOME environment variable not set. Using HOME.");
            log_log(LOG_WARNING);

            ldmHomePath = getenv("HOME");

            if (NULL == ldmHomePath) {
                LOG_START0("HOME environment variable not set");
                log_log(LOG_ERR);
                abort();
            }
        }
    }

    return ldmHomePath;
}

/**
 * Returns the pathname of the static, system-specific directory
 *
 * Returns:
 *      Pointer to the pathname.  Might be absolute or relative to the current
 *      working directory.
 */
const char* getSysConfDirPath(void)
{
    if (strlen(sysConfDirPath) == 0) {
        const char*            ldmHome = getLdmHomePath();
        static const char      subdir[] = "/etc";

        if (strlen(ldmHome) + strlen(subdir) >= sizeof(sysConfDirPath)) {
            LOG_START2("System configuration directory pathname too long: "
                    "\"%s%s\"", ldmHome, subdir);
            log_log(LOG_ERR);
            abort();
        }

        (void)strcat(strcpy(sysConfDirPath, ldmHome), subdir);
    }

    return sysConfDirPath;
}

/**
 * Returns the pathname of the registry directory.
 *
 * Returns:
 *      else            Pointer to the pathname.  Might be absolute or relative
 *                      to the current working directory.
 */
const char* getRegistryDirPath(void)
{
    return getSysConfDirPath();
}
