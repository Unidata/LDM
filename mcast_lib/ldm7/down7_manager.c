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
 * @param[in] servAddr       Pointer to the address of the server from which to
 *                           obtain multicast information, backlog products, and
 *                           products missed by the FMTP layer. Caller may free
 *                           upon return.
 * @param[in] feedtype       Feedtype of multicast group to receive.
 * @param[in] iface          Name of interface to use to receive multicast
 *                           packets or "dummy". Caller may free.
 * @param[in] vcEnd          Local virtual-circuit endpoint
 * @param[in] pqPathname     Pathname of the product-queue.
 * @retval    0              Success
 * @retval    LDM7_MCAST     Multicast layer failure. `log_add()` called.
 * @retval    LDM7_SYSTEM    System error occurred. `log_add()` called.
 */
static int
executeDown7(
    const ServiceAddr* const restrict servAddr,
    const feedtypet                   feedtype,
    const char* const restrict        iface,
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
        McastReceiverMemory* const mrm = mrm_open(servAddr, feedtype);

        if (mrm == NULL) {
            log_add("Couldn't open multicast receiver memory");
            status = LDM7_SYSTEM;
        }
        else {
            Down7* down7 = down7_new(servAddr, feedtype, iface, vcEnd, pq, mrm);

            if (NULL == down7) {
                log_add("Couldn't create new downstream LDM 7");
                status = LDM7_SYSTEM;
            }
            else {
                status = down7_run(down7); // Blocks until error

                if (status == LDM7_INTR)
                    status = 0; // Success

                down7_free(down7);
            } // `down7` allocated

            (void)mrm_close(mrm);
        } // `mrm` open

        (void)pq_close(pq);
    } // product-queue open

    return status;
}

typedef struct elt {
    struct elt*    next;
    ServiceAddr*   ul7;
    char*          iface;
    /// Local virtual-circuit endpoint
    VcEndPoint     vcEnd;
    feedtypet      ft;
    pid_t          pid;
} Elt;

/**
 * Top of the stack.
 */
static Elt* top;

/**
 * Returns a new element.
 *
 * @param[in] ft          Feedtype to subscribe to.
 * @param[in] ul7         Upstream LDM-7 to which to subscribe.
 * @param[in] iface       IP address of FMTP interface. Caller may free upon
 *                        return.
 * @param[in] vcEnd       Local virtual-circuit endpoint. Caller may free.
 * @retval    NULL        Failure. `log_add()` called.
 * @return                Pointer to new element.
 */
static Elt*
elt_new(
        const feedtypet                  ft,
        ServiceAddr* const restrict      ul7,
        const char* const restrict       iface,
        const VcEndPoint* const restrict vcEnd)
{
    bool failure = true;
    Elt* elt = log_malloc(sizeof(Elt), "downstream LDM-7 element");

    if (elt) {
        elt->ul7 = sa_clone(ul7);

        if (elt->ul7) {
            elt->iface = strdup(iface);

            if (elt->iface == NULL) {
                log_add_syserr("Couldn't duplicate interface specification");
            }
            else {
                if (!vcEndPoint_copy(&elt->vcEnd, vcEnd)) {
                    log_add_syserr("Couldn't copy virtual-circuit endpoint");
                }
                else {
                    elt->ft = ft;
                    elt->pid = -1;
                    failure = false;
                } // `elt->vcEnd` initialized

                if (failure)
                    free(elt->iface);
            } // `elt->iface` allocated

            if (failure)
                sa_free(elt->ul7);
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
        sa_free(elt->ul7);
        free(elt->iface);
        free(elt);
    }
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
        status = executeDown7(elt->ul7, elt->ft, elt->iface, &elt->vcEnd,
                getQueuePath());

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
        elt->pid = -1;
    }
}

/*******************************************************************************
 * Public API:
 ******************************************************************************/

/**
 * Adds a potential downstream LDM-7.
 *
 * @param[in] ft           Feedtype to subscribe to.
 * @param[in] ul7          Upstream LDM-7 to which to subscribe. Caller may free.
 * @param[in] iface        IP address of FMTP interface. Caller may free upon
 *                         return. "0.0.0.0" obtains the system's default
 *                         interface.
 * @param[in] vcEnd        Local virtual-circuit endpoint. Caller may free.
 * @retval    0            Success.
 * @retval    LDM7_SYSTEM  System failure. `log_add()` called.
 */
Ldm7Status
d7mgr_add(
        const feedtypet                  ft,
        ServiceAddr* const restrict      ul7,
        const char* const restrict       iface,
        const VcEndPoint* const restrict vcEnd)
{
    int  status;
    Elt* elt = elt_new(ft, ul7, iface, vcEnd);

    if (NULL == elt) {
        status = LDM7_SYSTEM;
    }
    else {
        elt->next = top;
        top = elt;
        status = 0;
    }

    return status;
}

/**
 * Frees the downstream LDM-7 manager.
 */
void
d7mgr_free(void)
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
