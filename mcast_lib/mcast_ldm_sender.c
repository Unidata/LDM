/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: mcast_ldm_sender.c
 * @author: Steven R. Emmerson
 *
 * This file implements an API to the separate multicast LDM sender program.
 */

#include "config.h"

#include "globals.h"
#include "log.h"
#include "mcast.h"
#include "mcast_info.h"
#include "mcast_ldm_sender.h"
#include "mldm_sender_memory.h"
#include "ldmprint.h"
#include "pq.h"
#include "StrBuf.h"

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

/**
 * Indicates if a particular multicast group is being multicast.
 *
 * @param[in] muf          The multicast LDM sender file object associated with
 *                         the multicast group. Must be locked.
 * @retval    0            The multicast LDM sender LDM associated with the given
 *                         multicast group is running.
 * @retval    LDM7_NOENT   No such process.
 * @retval    LDM7_SYSTEM  System error. `log_start()` called.
 */
static Ldm7Status
mls_isRunning(
    MldmSenderMemory* const muf)
{
    pid_t pid;
    int   status = msm_getPid(muf, &pid);

    if (status == 0) {
        if (kill(pid, 0) == 0) {
            /* Can send the process a signal */
            status = 0;
        }
        else {
            /* Can't send the process a signal */
            uwarn("According to the persistent multicast LDM sender memory, "
                    "the PID of the relevant multicast LDM sender is %d -- "
                    "but that process can't be signaled by this process. "
                    "I'll assume the relevant multicast LDM sender is not "
                    "running.", pid);
            status = LDM7_NOENT;
        }
    }

    return status;
}

/**
 * Executes the process image of the multicast LDM sender program. If this
 * function returns, then an error occurred and `log_start()` was called.
 *
 * @param[in] info  Information on the multicast group.
 */
static void
execMldmSender(
    const McastInfo* const restrict info)
{
    char* args[14]; // keep sufficiently capacious for the following
    int   i = 0;

    args[i++] = "mldm_sender";
    args[i++] = "-I";
    args[i++] = info->server.inetId;
    args[i++] = "-l";
    char* arg = (char*)getulogpath(); // safe cast
    if (arg == NULL)
        arg = "";
    args[i++] = arg;
    args[i++] = "-q";
    args[i++] = (char*)getQueuePath(); // safe cast
    if (ulogIsDebug())
        args[i++] = "-x";
    if (ulogIsVerbose())
        args[i++] = "-v";
    arg = ldm_format(128, "%s:%hu", info->group.inetId, info->group.port);
    if (arg == NULL) {
        LOG_ADD0("Couldn't create multicast group argument");
    }
    else {
        args[i++] = arg;
        arg = ldm_format(12, "%hu", info->server.port);
        if (arg == NULL) {
            LOG_ADD0("Couldn't create server port argument");
        }
        else {
            args[i++] = arg;
            args[i++] = NULL;
            execvp(args[0], args);
            LOG_SERROR1("Couldn't execvp() multicast LDM sender \"%s\"",
                    args[0]);
        }
    }
}

/**
 * Spawns a multicast LDM sender process that sends data-products to a
 * multicast group. Doesn't block.
 *
 * @param[in]  info         Information on the multicast group.
 * @param[out] pid          The identifier of the multicast LDM sender process.
 * @retval     0            Success. `*pid` is set.
 * @retval     LDM7_SYSTEM  System error. `log_start()` called.
 */
static Ldm7Status
mls_spawn(
    const McastInfo* const restrict info,
    pid_t* const restrict           pid)
{
    int   status;
    pid_t child = fork();

    if (child == -1) {
        const char* const id = mi_asFilename(info);

        LOG_SERROR("Couldn't fork() multicast LDM sender for \"%s\"", id);
        status = LDM7_SYSTEM;
    }
    else if (child == 0) {
        /* Child process */
        execMldmSender(info); // shouldn't return
        log_log(LOG_ERR);
        exit(1);
    }
    else {
        /* Parent process */
        *pid = child;
        status = 0;
    }

    return status;
}

/**
 * Ensures that the multicast LDM sender process that's responsible for a
 * particular multicast group is running. Doesn't block.
 *
 * @pre                    {Multicast LDM sender memory is locked.}
 * @param[in] info         Information on the multicast group.
 * @param[in] muf          The multicast LDM sender file object associated with
 *                         the multicast group. Must be locked.
 * @retval    0            Success. The group is being multicast.
 * @retval    LDM7_SYSTEM  System error. `log_start()` called.
 */
static Ldm7Status
mls_ensure(
    const McastInfo* const  info,
    MldmSenderMemory* const muf)
{
    int status = mls_isRunning(muf);

    if (status == LDM7_NOENT) {
        pid_t pid;

        if ((status = mls_spawn(info, &pid)) == 0) {
            if ((status = msm_setPid(muf, pid)) != 0) {
                const char* const id = mi_asFilename(info);

                LOG_ADD("Terminating just-started multicast LDM sender for "
                        "\"%s\"", id);
                (void)kill(pid, SIGTERM);
            }
        }
    }

    return status;
}

/******************************************************************************
 * Public API:
 ******************************************************************************/

/**
 * Ensures that the multicast LDM sender process that's responsible for a
 * particular multicast group is running. Doesn't block.
 *
 * @param[in] info         Information on the multicast group.
 * @retval    0            Success. The group is being multicast.
 * @retval    LDM7_SYSTEM  System error. `log_start()` called.
 */
Ldm7Status
mls_ensureRunning(
    const McastInfo* const restrict info)
{
    int                status;
    MldmSenderMemory* const muf = msm_new(info);

    if (muf == NULL) {
        status = LDM7_SYSTEM;
    }
    else {
        if ((status = msm_lock(muf)) == 0) {
            status = mls_ensure(info, muf);
            (void)msm_unlock(muf);
        }

        msm_free(muf);
    } // `muf` allocated

    return status;
}
