/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: mldm_sender_memory.c
 * @author: Steven R. Emmerson
 *
 * This file implements the persistent multicast sender memory, which contains
 * information on a multicast LDM sender process.
 */

#include "config.h"

#include "globals.h"
#include "ldm.h"
#include "ldmprint.h"
#include "log.h"
#include "mcast_info.h"
#include "mldm_sender_memory.h"
#include "StrBuf.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h>

struct MldmSenderMemory {
};

#if 0
/**
 * Returns the process-ID that's contained in a file.
 *
 * @param[in] file      The file that contains the PID.
 * @retval    -1        Error. `log_start()` called.
 * @return              The process-ID that was in the file.
 */
static pid_t
parsePid(
    FILE* const file)
{
    long pid;

    if (fscanf(file, "%ld", &pid) != 1) {
        ferror(file)
            ? LOG_SERROR0("Couldn't parse PID")
            : LOG_START0("Couldn't parse PID");
        pid = -1;
    }

    return pid;
}

/**
 * Returns the process-ID that's contained in a file.
 *
 * @param[in] pathname  The pathname of the file.
 * @retval     0        The file doesn't exist.
 * @retval    -1        Error. `log_start()` called.
 * @return              The process-ID that was in the file.
 */
static pid_t
getPidFromFile(
    const char* const pathname)
{
    long  pid;
    FILE* file = fopen(pathname, "r");

    if (file == NULL) {
        if (errno == ENOENT) {
            pid = 0;
        }
        else {
            LOG_SERROR("Couldn't open PID file \"%s\"", pathname);
            pid = -1;
        }
    }
    else {
        pid = parsePid(file);

        if (pid == -1)
            LOG_ADD1("Couldn't get PID from file \"%s\"", pathname);

        (void)fclose(file);
    } // `file` is open

    return pid;
}

/**
 * Returns the absolute pathname of the file that contains the process-ID of
 * the multicast LDM sender corresponding to a multicast group identifier.
 *
 * @param[in] info  Information on the multicast group.
 * @retval    NULL  Error. `log_start()` called.
 * @return          The absolute pathname of the PID file. The caller should
 *                  free when it's no longer needed.
 */
static char*
getPidPathname(
    const McastInfo* const info)
{
    char*             pathname;
    const char* const filename = mi_asFilename(info);

    if (filename == NULL) {
        pathname = NULL;
    }
    else {
        pathname = ldm_format(256, "%s/%s.pid", getLdmVarRunDir(), filename);
    }

    return pathname;
}

/**
 * Returns the process-ID of the multicast LDM sender corresponding to a
 * multicast group.
 *
 * @param[in] info  Information on the multicast group.
 * @retval    -1    Error. `log_add()` called.
 * @retval     0    No such process exists.
 * @return          The process-ID of the multicast LDM sender.
 */
static pid_t
getPidFromInfo(
    const McastInfo* const info)
{
    pid_t       pid;
    char* const pathname = getPidPathname(info);

    if (pathname == NULL) {
        pid = -1;
    }
    else {
        pid = getPidFromFile(pathname);
        free(pathname);
    }

    return pid;
}
#endif

/******************************************************************************
 * Public API:
 ******************************************************************************/

/**
 * Returns a new multicast sender memory object.
 *
 * @param[in] info  Information on the multicast group.
 * @retval    NULL  Failure. `log_start()` called.
 * @return          A new, initialized, multicast sender memory object.
 */
MldmSenderMemory*
msm_new(
    const McastInfo* const info)
{
    return NULL; // TODO
}

/**
 * Frees multicast sender memory object.
 *
 * @param[in] msm          The multicast sender memory object to be freed.
 */
void
msm_free(
    const MldmSenderMemory* const msm)
{
    // TODO
}

/**
 * Locks a multicast sender memory file against access by another process.
 * Blocks until the lock is acquired. The lock is not inherited by the child
 * process of a `fork()`.
 *
 * @param[in] msm          The multicast sender memory object.
 * @retval    0            Success.
 * @retval    LDM7_SYSTEM  System error. `log_start()` called.
 */
Ldm7Status
msm_lock(
    const MldmSenderMemory* const msm)
{
    return LDM7_SYSTEM; // TODO
}

/**
 * Unlocks a multicast sender memory file against access by another process.
 *
 * @param[in] msm          The multicast sender memory object.
 * @retval    0            Success.
 * @retval    LDM7_SYSTEM  System error. `log_start()` called.
 */
Ldm7Status
msm_unlock(
    const MldmSenderMemory* const msm)
{
    return LDM7_SYSTEM; // TODO
}

/**
 * Returns the process-identifier (PID) of the multicast LDM sender associated
 * with a multicast sender memory.
 *
 * @param[in]  msm          The multicast sender memory object.
 * @param[out] pid          The PID of the associated multicast LDM sender.
 * @retval     0            Success. `*pid` is set.
 * @retval     LDM7_NOENT   No such PID exists.
 * @retval     LDM7_SYSTEM  System error. `log_start()` called.
 */
Ldm7Status
msm_getPid(
    const MldmSenderMemory* const restrict msm,
    pid_t* const restrict                  pid)
{
    return LDM7_SYSTEM; // TODO
}

/**
 * Sets the process-identifier (PID) of the multicast LDM sender associated
 * with a multicast sender memory.
 *
 * @param[in] msm          The multicast sender memory object.
 * @param[in] pid          The PID of the associated multicast LDM sender.
 * @retval    0            Success.
 * @retval    LDM7_SYSTEM  System error. `log_start()` called.
 */
Ldm7Status
msm_setPid(
    const MldmSenderMemory* const msm,
    const pid_t                   pid)
{
    return LDM7_SYSTEM; // TODO
}
