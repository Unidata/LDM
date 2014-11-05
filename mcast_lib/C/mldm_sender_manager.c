/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: mldm_sender_manager.c
 * @author: Steven R. Emmerson
 *
 * This file implements the API to the manager of separate multicast LDM sender
 * processes.
 *
 * The functions in this module are thread-compatible but not thread-safe.
 */

#include "config.h"

#include "globals.h"
#include "log.h"
#include "mcast.h"
#include "mcast_info.h"
#include "mldm_sender_manager.h"
#include "mldm_sender_map.h"
#include "ldmprint.h"
#include "pq.h"
#include "StrBuf.h"

#include <errno.h>
#include <signal.h>
#include <search.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

static void* mcastInfoSet;

/**
 * Indicates if a particular multicast LDM sender is running.
 *
 * @pre                    {Multicast LDM sender PID map is locked for writing}.
 * @param[in] feedtype     Feed-type of multicast group.
 * @retval    0            The multicast LDM sender associated with the given
 *                         multicast group is running.
 * @retval    LDM7_NOENT   No such process.
 * @retval    LDM7_SYSTEM  System error. `log_start()` called.
 */
static Ldm7Status
mlsm_isRunning(
        const feedtypet feedtype)
{
    pid_t pid;
    int   status = msm_getPid(feedtype, &pid);

    if (status == 0) {
        if (kill(pid, 0) == 0) {
            /* Can signal the process */
            status = 0;
        }
        else {
            /* Can't signal the process */
            uwarn("According to my information, the PID of the multicast LDM "
                    "sender associated with feed-type %s is %d -- but that "
                    "process can't be signaled by this process. I'll assume "
                    "the relevant multicast LDM sender is not running.",
                    s_feedtypet(feedtype), pid);
            (void)msm_removePid(pid);   // don't care if it exists or not
            status = LDM7_NOENT;
        }
    }

    return status;
}

/**
 * Executes the process image of the multicast LDM sender program. If this
 * function returns, then an error occurred and `log_start()` was called. The
 * multicast LDM sender process inherits the following from this process:
 *     - The LDM log;
 *     - The logging level; and
 *     - The LDM product-queue;
 *
 * @param[in] info  Information on the multicast group.
 */
static void
execMldmSender(
    const McastInfo* const restrict info)
{
    char* args[14]; // Keep sufficiently capacious (search for `i\+\+`)
    int   i = 0;

    args[i++] = "mldm_sender";
    args[i++] = "-I";
    args[i++] = info->server.inetId;
    args[i++] = "-l";
    char* arg = (char*)getulogpath(); // safe cast
    if (arg == NULL)
        arg = "";
    args[i++] = arg;
    args[i++] = "-P";
    char* serverPortOptArg = ldm_format(12, "%hu", info->server.port);
    if (serverPortOptArg == NULL) {
        LOG_ADD0("Couldn't create server-port option-argument");
    }
    else {
        args[i++] = serverPortOptArg;
        if (ulogIsVerbose())
            args[i++] = "-v";
        if (ulogIsDebug())
            args[i++] = "-x";
        args[i++] = "-q";
        args[i++] = (char*)getQueuePath(); // safe cast
        char feedtypeBuf[256];
        (void)sprint_feedtypet(feedtypeBuf, sizeof(feedtypeBuf), info->feed);
        args[i++] = feedtypeBuf; // multicast group identifier
        char* mcastGroupOperand = ldm_format(128, "%s:%hu", info->group.inetId, info->group.port);
        if (mcastGroupOperand == NULL) {
            LOG_ADD0("Couldn't create multicast group operand");
        }
        else {
            args[i++] = mcastGroupOperand;
            args[i++] = NULL;
            execvp(args[0], args);
            LOG_SERROR1("Couldn't execvp() multicast LDM sender \"%s\"",
                    args[0]);
            free(mcastGroupOperand);
        }

        free(serverPortOptArg);
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
mlsm_spawn(
    const McastInfo* const restrict info,
    pid_t* const restrict           pid)
{
    int   status;
    pid_t child = fork();

    if (child == -1) {
        char* const id = mi_format(info);

        LOG_SERROR("Couldn't fork() multicast LDM sender for \"%s\"", id);
        free(id);
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
 * Starts executing the multicast LDM sender process that's responsible for a
 * particular multicast group. Doesn't block.
 *
 * @pre                    {Multicast LDM sender PID map is locked.}
 * @pre                    {Relevant multicast LDM sender isn't running.}
 * @param[in] info         Information on the multicast group.
 * @retval    0            Success. Multicast LDM sender spawned.
 * @retval    LDM7_SYSTEM  System error. `log_start()` called.
 */
static Ldm7Status
mlsm_execute(
    const McastInfo* const  info)
{
    const feedtypet feedtype = mi_getFeedtype(info);
    pid_t           pid;
    int             status = mlsm_spawn(info, &pid);

    if (0 == status) {
        if ((status = msm_put(feedtype, pid)) != 0) {
            char* const id = mi_asFilename(info);

            LOG_ADD("Terminating just-started multicast LDM sender for "
                    "\"%s\"", id);
            free(id);
            (void)kill(pid, SIGTERM);
        }
    }

    return status;
}

/**
 * Indicates if two multicast information objects conflict (e.g., have
 * feed-types that overlap, specify the same TCP server IP address and port
 * number, etc.).
 *
 * @param[in] info1  First multicast information object.
 * @param[in] info2  Second multicast information object.
 * @retval    true   The multicast information objects do conflict.
 * @retval    false  The multicast information objects do not conflict.
 */
static bool
doConflict(
        const McastInfo* const info1,
        const McastInfo* const info2)
{
    return (mi_getFeedtype(info1) & mi_getFeedtype(info2)) // feeds overlap
            || (0 == mi_compareServers(info1, info2)) // same TCP server
            || (0 == mi_compareGroups(info1, info2)); // same multicast group
}

/**
 * Compares two multicast information objects and returns a value less than,
 * equal to, or greater than zero as the first object is considered less than,
 * equal to, or greater than the second object, respectively. Only the
 * feed-types are compared.
 *
 * @param[in] o1  First multicast information object.
 * @param[in] o2  Second multicast information object.
 * @retval    -1  First object is less than second object.
 * @retval     0  First object equals second object.
 * @retval    +1  First object is greater than second object.
 */
static int
findMcastInfos(
        const void* o1,
        const void* o2)
{
    const feedtypet f1 = mi_getFeedtype((McastInfo*)o1);
    const feedtypet f2 = mi_getFeedtype((McastInfo*)o2);

    return (f1 < f2)
              ? -1
              : (f1 == f2)
                ? 0
                : 1;
}

/**
 * Compares two multicast information objects and returns a value less than,
 * equal to, or greater than zero as the first object is considered less than,
 * equal to, or greater than the second object, respectively. The objects are
 * considered equal if they conflict (e.g., have feed-types that overlap,
 * specify the same TCP server IP address and port number, etc.).
 *
 * @param[in] o1  First multicast information object.
 * @param[in] o2  Second multicast information object.
 * @retval    -1  First object is less than second object.
 * @retval     0  First object equals second object or the objects conflict.
 * @retval    +1  First object is greater than second object.
 */
static int
searchMcastInfos(
        const void* o1,
        const void* o2)
{
    const McastInfo* const i1 = o1;
    const McastInfo* const i2 = o2;

    if (doConflict(i1, i2))
        return 0;

    return findMcastInfos(o1, o2);
}

/******************************************************************************
 * Public API:
 ******************************************************************************/

/**
 * Adds a potential multicast LDM sender. The sender is not started. This
 * function should be called for all potential senders before any child
 * process is forked so that all child processes will have this information.
 *
 * @param[in] info         Information on the multicast group. Caller may free.
 * @retval    0            Success.
 * @retval    LDM7_DUP     Multicast group information conflicts with earlier
 *                         addition. Manager not modified. `log_add()` called.
 * @retval    LDM7_SYSTEM  System failure. `log_add()` called.
 */
Ldm7Status
mlsm_addPotentialSender(
    const McastInfo* const restrict   info)
{
    int              status;
    McastInfo* const elt = mi_clone(info);

    if (NULL == elt) {
        status = LDM7_SYSTEM;
    }
    else {
        const void* const node = tsearch(elt, &mcastInfoSet, searchMcastInfos);

        if (NULL == node) {
            LOG_SERROR0("Couldn't add to multicast information set");
            status = LDM7_SYSTEM;
            mi_free(elt);
        }
        else if (*(McastInfo**)node != elt) {
            char* const id = mi_asFilename(info);
            LOG_START1("Multicast information conflicts with earlier addition: "
                    "%s", id);
            free(id);
            status = LDM7_DUP;
            mi_free(elt);
        }
        else {
            status = 0;
        }
    } // `elt` allocated

    return status;
}

/**
 * Ensures that the multicast LDM sender process that's responsible for a
 * particular multicast group is running. Doesn't block.
 *
 * @param[in]  feedtype     Multicast group feed-type.
 * @param[out] mcastInfo    Information on corresponding multicast group.
 * @retval     0            Success. The group is being multicast and
 *                          `*mcastInfo` is set.
 * @retval     LDM7_NOENT   No corresponding potential sender was added via
 *                          `mlsm_addPotentialSender()`. `log_start() called`.
 * @retval     LDM7_SYSTEM  System error. `log_start()` called.
 */
Ldm7Status
mlsm_ensureRunning(
        const feedtypet   feedtype,
        McastInfo** const mcastInfo)
{
    McastInfo key;
    int       status;

    key.feed = feedtype;

    const void* const node = tfind(&key, &mcastInfoSet, findMcastInfos);

    if (NULL == node) {
        LOG_START1("No multicast LDM sender is associated with feed-type %s",
                s_feedtypet(feedtype));
        status = LDM7_NOENT;
    }
    else {
        *mcastInfo = *(McastInfo**)node;

        if (0 == (status = msm_lock(true))) {
            status = mlsm_isRunning(feedtype);

            if (status == LDM7_NOENT)
                status = mlsm_execute(*(McastInfo**)node);

            (void)msm_unlock();
        } // multicast LDM sender PID map is locked
    } // feedtype maps to potential multicast LDM sender

    return status;
}

/**
 * Handles the termination of a multicast LDM sender process. This function
 * should be called by the top-level LDM server when it notices that a child
 * process has terminated.
 *
 * @param[in] pid          Process-ID of the terminated multicast LDM sender
 *                         process.
 * @retval    0            Success.
 * @retval    LDM7_NOENT   PID doesn't correspond to known process.
 * @retval    LDM7_SYSTEM  System error. `log_add()` called.
 */
Ldm7Status
mlsm_terminated(
        const pid_t pid)
{
    int status = msm_lock(true);

    if (0 == status) {
        status = msm_removePid(pid);
        (void)msm_unlock();
    }

    return status;
}
