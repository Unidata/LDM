/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: mldm_sender.c
 * @author: Steven R. Emmerson
 *
 * This file implements the multicast LDM sender, which is a program for
 * multicasting a single multicast group.
 */

#include "config.h"

#include "globals.h"
#include "log.h"
#include "mcast.h"
#include "mcast_info.h"
#include "mldm_sender_memory.h"
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
 * @param[in] muf          The multicast upstream file object associated with
 *                         the multicast group. Must be locked.
 * @retval    0            The multicast upstream LDM associated with the given
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
            uwarn("According to the persistent multicast upstream LDM memory, "
                    "the PID of the relevant multicast upstream LDM is %d -- "
                    "but that process can't be signaled by this process. "
                    "I'll assume the relevant multicast upstream LDM is not "
                    "running.", pid);
            status = LDM7_NOENT;
        }
    }

    return status;
}

/**
 * Creates a multicast LDM sender.
 *
 * @param[in]  info         Information on the multicast group.
 * @param[out] sender       The multicast LDM sender.
 * @retval     0            Success. `*sender` is set.
 * @retval     LDM7_INVAL   An Internet address couldn't be converted into a
 *                          binary IP address. `log_start()` called.
 * @retval     LDM7_SYSTEM  System failure. `log_start()` called.
 */
static Ldm7Status
mls_createMulticastSender(
    const McastInfo* const restrict info,
    McastSender** const restrict    sender)
{
    McastSender* sndr;
    int          status = mcastSender_new(&sndr, info->server.addr,
            info->server.port, info->mcast.addr, info->mcast.port);

    if (status == EINVAL)
        return LDM7_INVAL;
    if (status)
        return LDM7_SYSTEM;

    *sender = sndr;

    return 0;
}

/**
 * Sends data-products to a multicast group.
 *
 * @param[in]  pq           The product-queue from which data-products are read.
 * @param[out] sender       The multicast LDM sender.
 * @retval     0            Termination was requested.
 * @retval     LDM7_SYSTEM  Failure. `log_start()` called.
 */
static Ldm7Status
mls_multicastProducts(
    pqueue* const restrict      pq,
    McastSender* const restrict sender)
{
    LOG_START0("Unimplemented");
    return LDM7_SYSTEM;
}

/**
 * Destroys a sender of data to a multicast group.
 *
 * @param[in] sender  The sender of data to a multicast group.
 */
static void
mls_destroyMulticastSender(
    McastSender* const restrict sender)
{
    // TODO
}
/**
 * Executes a multicast upstream LDM. Blocks until termination is requested or
 * an error occurs.
 *
 * This method is public so that it can be tested.
 *
 * @param[in]  info         Information on the multicast group.
 * @param[in]  pq           The product-queue from which data-products are
 *                          multicast.
 * @retval     0            Success. Termination was requested.
 * @retval     LDM7_SYSTEM  System failure. `log_start()` called.
 */
Ldm7Status
mls_execute(
    const McastInfo* const restrict info,
    pqueue* const restrict               pq)
{
    McastSender* sender;
    int          status = mls_createMulticastSender(info, &sender);

    if (status == 0) {
        status = mls_multicastProducts(pq, sender);
        mls_destroyMulticastSender(sender);
    }

    return status;
}

/**
 * Forks a multicast upstream LDM process that sends data-products to a
 * multicast group. Doesn't block.
 *
 * @param[in]  info         Information on the multicast group.
 * @param[in]  pq           The product-queue from which data-products are read.
 * @param[out] pid          The identifier of the multicast upstream LDM
 *                          process.
 * @retval     0            Success. `*pid` is set.
 * @retval     LDM7_SYSTEM  System error. `log_start()` called.
 */
static Ldm7Status
mls_run(
    const McastInfo* const restrict info,
    pqueue* const restrict               pq,
    pid_t* const restrict                pid)
{
    int   status;
    pid_t child = fork();

    if (child == -1) {
        const char* const id = mi_asFilename(info);

        LOG_SERROR("Couldn't fork() multicast upstream LDM for \"%s\"", id);
        status = LDM7_SYSTEM;
    }
    else if (child == 0) {
        /* Child process */
        if ((status = mls_execute(info, pq)) != 0)
            log_log(LOG_ERR);
        exit(status);
    }
    else {
        /* Parent process */
        *pid = child;
        status = 0;
    }

    return status;
}

/**
 * Ensures that the multicast upstream LDM process that's responsible for a
 * particular multicast group is running. Doesn't block.
 *
 * @param[in] info         Information on the multicast group.
 * @param[in] muf          The multicast upstream file object associated with
 *                         the multicast group. Must be locked.
 * @param[in] pq           The product-queue from which data-products are
 *                         multicast.
 * @retval    0            Success. The group is being multicast.
 * @retval    LDM7_SYSTEM  System error. `log_start()` called.
 */
static Ldm7Status
mls_ensure(
    const McastInfo* const info,
    MldmSenderMemory* const          muf,
    pqueue* const restrict      pq)
{
    int status = mls_isRunning(muf);

    if (status == LDM7_NOENT) {
        pid_t pid;

        if ((status = mls_run(info, pq, &pid)) == 0) {
            if ((status = msm_setPid(muf, pid)) != 0) {
                const char* const id = mi_asFilename(info);

                LOG_ADD("Terminating just-started multicast upstream LDM for "
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
 * Ensures that the multicast upstream LDM process that's responsible for a
 * particular multicast group is running. Doesn't block.
 *
 * @param[in] info         Information on the multicast group.
 * @retval    0            Success. The group is being multicast.
 * @retval    LDM7_SYSTEM  System error. `log_start()` called.
 */
Ldm7Status
mls_ensureRunning(
    const McastInfo* const restrict info,
    pqueue* const restrict               pq)
{
    int                status;
    MldmSenderMemory* const muf = msm_new(info);

    if (muf == NULL) {
        status = LDM7_SYSTEM;
    }
    else {
        if ((status = msm_lock(muf)) == 0) {
            status = mls_ensure(info, muf, pq);
            (void)msm_unlock(muf);
        }

        msm_free(muf);
    } // `muf` allocated

    return status;
}
