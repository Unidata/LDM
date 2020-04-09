/**
 * Copyright 2015 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: down7_manager.c
 * @author: Steven R. Emmerson
 *
 * This file implements the manager of downstream LDM-7s.
 */

#include "config.h"

#include "down7.h"
#include "down7_manager.h"
#include "globals.h"
#include "ldm.h"
#include "ldmfork.h"
#include "ldmprint.h"
#include "log.h"
#include "pq.h"
#include "VirtualCircuit.h"

#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**
 * Executes a downstream LDM-7. Doesn't return until an error occurs or a
 * termination signal is received.
 *
 * @param[in] ldmSrvr        Address of LDM7 server from which to obtain
 *                           multicast information, backlog products, and
 *                           products missed by the FMTP layer. Caller may free
 *                           upon return.
 * @param[in] feed           Feedtype of multicast group to receive.
 * @param[in] fmtpIface      Name of virtual interface to be created and used by
 *                           FMTP layer or "dummy", in which case no such
 *                           interface will be created and `vcEnd` must be
 *                           invalid. Caller may free.
 * @param[in] vcEnd          Local AL2S virtual-circuit endpoint. If the
 *                           endpoint isn't valid, then the AL2S virtual circuit
 *                           will not be created. Caller may free.
 * @param[in] pqPathname     Pathname of the product-queue. Caller may free.
 * @retval    0              Success
 * @retval    LDM7_INVAL     `fmtpIface` and `vcEnd` are inconsistent
 * @retval    LDM7_MCAST     Multicast layer failure. `log_add()` called.
 * @retval    LDM7_SYSTEM    System error occurred. `log_add()` called.
 */
static int
executeDown7(
    InetSockAddr* const restrict      ldmSrvr,
    const feedtypet                   feed,
    const char* const restrict        fmtpIface,
    const VcEndPoint* const restrict  vcEnd,
    const char* const restrict        pqPathname)
{
    pqueue* pq;
    int     status = pq_open(pqPathname, PQ_THREADSAFE, &pq);
    if (status) {
        log_add("Couldn't open product-queue \"%s\"", pqPathname);
        status = LDM7_SYSTEM;
    }
    else {
        McastReceiverMemory* const mrm = mrm_open(ldmSrvr, feed);

        if (mrm == NULL) {
            log_add("Couldn't open multicast receiver memory");
            status = LDM7_SYSTEM;
        }
        else {
            status = down7_init(ldmSrvr, feed, fmtpIface, vcEnd, pq, mrm);

            if (status) {
                log_add("Couldn't initialize downstream LDM7");
            }
            else {
                // Blocks until error or termination requested
                status = down7_run();

                if (status) {
                	log_add("Error executing downstream LDM7");
                	log_flush_error();
                }

                down7_destroy();
            } // Downstream LDM7 initialized

            (void)mrm_close(mrm);
        } // `mrm` open

        (void)pq_close(pq);
    } // product-queue open

    return status;
}

typedef struct elt {
    struct elt*    next;
    InetSockAddr*  ldmSrvr;
    char*          fmtpIface;
    /// Local virtual-circuit endpoint
    VcEndPoint     vcEnd;
    feedtypet      feed;
    pid_t          pid;
} Elt;

/**
 * Top of the stack.
 */
static Elt* top;

/**
 * Returns a new element.
 *
 * @param[in] feed        Feedtype to subscribe to.
 * @param[in] ldmSrvr     Upstream LDM-7 to which to subscribe. Caller may free.
 * @param[in] fmtpIface   Name of virtual interface to be created and used by
 *                        FMTP layer. May be `NULL`. Caller may free.
 * @param[in] vcEnd       Local AL2S virtual-circuit endpoint. Caller may free.
 * @retval    NULL        Failure. `log_add()` called.
 * @return                Pointer to new element.
 */
static Elt*
elt_new(
        const feedtypet                    feed,
        const InetSockAddr* const restrict ldmSrvr,
        const char* const restrict         fmtpIface,
        const VcEndPoint* const restrict   vcEnd)
{
    bool failure = true;
    Elt* elt = log_malloc(sizeof(Elt), "downstream LDM-7 element");

    if (elt) {
        elt->ldmSrvr = isa_clone(ldmSrvr);

        if (elt->ldmSrvr == NULL) {
            log_syserr("isa_clone() failure");
        }
        else {
            elt->fmtpIface = strdup(fmtpIface ? fmtpIface : "dummy");

            if (elt->fmtpIface == NULL) {
                log_add_syserr("Couldn't duplicate interface specification");
            }
            else {
                if (!vcEndPoint_copy(&elt->vcEnd, vcEnd)) {
                    log_add_syserr("Couldn't copy virtual-circuit endpoint");
                }
                else {
                    elt->feed = feed;
                    elt->pid = 0;
                    failure = false;
                } // `elt->vcEnd` initialized

                if (failure)
                    free(elt->fmtpIface);
            } // `elt->iface` allocated

            if (failure)
                isa_free(elt->ldmSrvr);
        } // `elt->ul7` allocated

        if (failure) {
            free(elt);
            elt = NULL;
        }
    } // `elt` allocated

    return elt;
}

/**
 * Frees an element.
 *
 * @param[in] elt  Element to be freed.
 */
static void
elt_free(
        Elt* const elt)
{
    if (elt) {
        isa_free(elt->ldmSrvr);
        free(elt->fmtpIface);
        free(elt);
    }
}

/**
 * Returns the string representation of an element.
 *
 * @param[in] elt     Element
 * @retval    `NULL`  Failure. `log_add()` called.
 * @return            String representation of `elt`. Caller should free.
 */
static char*
elt_toString(const Elt* const elt)
{
    char* const vcEndStr = vcEndPoint_format(&elt->vcEnd);
    char* const string = ldm_format(128, "{feed=%s, ldmSrvr=%s, fmtpIface=%s, "
            "vcEnd=%s, pid=%lu}", s_feedtypet(elt->feed), isa_toString(elt->ldmSrvr),
            elt->fmtpIface, vcEndStr, (unsigned long)elt->pid);

    free(vcEndStr);

    return string;
}

/**
 * Starts the downstream LDM-7 referenced by an element as a child process of
 * the current process.
 *
 * @param[in] elt          Element whose downstream LDM-7 is to be started.
 * @retval    0            Success.
 * @retval    LDM7_SYSTEM  System error. `log_add()` called.
 */
static Ldm7Status
elt_start(
        Elt* const elt)
{
    int   status;
    pid_t pid = ldmfork();

    if (-1 == pid) {
        /* System error */
        log_add("Couldn't fork downstream LDM-7 child process");
        status = LDM7_SYSTEM;
    }
    else if (0 == pid) {
        /* Child process */
        status = executeDown7(elt->ldmSrvr, elt->feed, elt->fmtpIface,
                &elt->vcEnd, getQueuePath());

        if (status) {
            log_add("executeDown7() failure: status=%d", status);
            log_flush_error();
            log_free();
            exit(1); // Should never happen
        }

        log_flush_notice();
        log_free();
        exit(0);
    }
    else {
        /* Parent process */
        elt->pid = pid;
        if (log_is_enabled_debug) {
            char* const eltStr = elt_toString(elt);
            log_debug("Started multicast receiver %s", eltStr);
            free(eltStr);
        }
        status = 0;
    }

    return status;
}

/**
 * Stops a downstream LDM-7 child process by sending it a SIGTERM. Idempotent.
 *
 * @param[in] elt          Element whose downstream LDM-7 is to be stopped.
 */
static void
elt_stop(
        Elt* const elt)
{
    if (0 < elt->pid) {
        (void)kill(elt->pid, SIGTERM);
        elt->pid = 0;
    }
}

/*******************************************************************************
 * Public API:
 ******************************************************************************/

/**
 * Adds a potential downstream LDM-7.
 *
 * @param[in] feed         Feedtype to subscribe to.
 * @param[in] ldmSrvr      Upstream LDM-7 to which to subscribe. Caller may free.
 * @param[in] fmtpIface    Name of virtual interface to be created and used by
 *                         FMTP layer. May be `NULL`. Caller may free.
 * @param[in] vcEnd        Local AL2S virtual-circuit endpoint. Caller may free.
 * @retval    0            Success.
 * @retval    LDM7_SYSTEM  System failure. `log_add()` called.
 */
Ldm7Status
d7mgr_add(
        const feedtypet                    feed,
        const InetSockAddr* const restrict ldmSrvr,
        const char* const restrict         fmtpIface,
        const VcEndPoint* const restrict   vcEnd)
{
    int  status;
    Elt* elt = elt_new(feed, ldmSrvr, fmtpIface, vcEnd);

    if (NULL == elt) {
        status = LDM7_SYSTEM;
    }
    else {
        elt->next = top;
        top = elt;
        if (log_is_enabled_debug) {
            char* eltStr = elt_toString(elt);
            log_debug("Added multicast receiver %s", eltStr);
            free(eltStr);
        }
        status = 0;
    }

    return status;
}

/**
 * Destroys the downstream LDM-7 manager.
 */
void
d7mgr_destroy(void)
{
    if (top) {
        for (Elt* elt = top; elt != NULL; ) {
            Elt* next = elt->next;
            elt_free(elt);
            elt = next;
        }

        top = NULL;
    }
}

/**
 * Starts all multicast-receiving LDM-7s as individual child processes of the
 * current process.
 *
 * @retval 0            Success.
 * @retval LDM7_SYSTEM  System error. `log_add()` called. All started
 *                      multicast LDM receivers were stopped.
 */
int
d7mgr_startAll(void)
{
    int  status = 0; // Support no entries

    for (Elt* elt = top; elt != NULL; elt = elt->next) {
        status = elt_start(elt);

        if (status) {
            for (Elt* elt2 = top; elt2 != elt; elt2 = elt2->next)
                elt_stop(elt2);
            break;
        }
    }

    return status;
}
