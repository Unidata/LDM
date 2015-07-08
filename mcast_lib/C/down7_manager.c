/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
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

#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**
 * The set of termination signals.
 */
static sigset_t termSigSet;

/**
 * Initializes the set of termination signals.
 */
static void
setTermSigSet(void)
{
    (void)sigemptyset(&termSigSet);
    (void)sigaddset(&termSigSet, SIGINT);
    (void)sigaddset(&termSigSet, SIGTERM);
}

/**
 * Returns the set of termination signals.
 *
 * @return Pointer to the set of termination signals.
 */
static sigset_t*
getTermSigSet()
{
    static pthread_once_t initialized = PTHREAD_ONCE_INIT;
    (void)pthread_once(&initialized, setTermSigSet);
    return &termSigSet;
}

/**
 * Adds termination signals to the set of blocked signals.
 */
static inline void
blockTermSigs(void)
{
    (void)pthread_sigmask(SIG_BLOCK, getTermSigSet(), NULL);
}

/**
 * Waits for a termination signal and then stops a downstream LDM-7.
 *
 * @param[in] arg  Pointer to the downstream LDM-7 to be stopped upon receiving
 *                 a termination signal.
 */
static void*
waitForTermSig(
        void* const arg)
{
    int sig;
    (void)sigwait(getTermSigSet(), &sig);
    if (down7_stop((Down7*)arg))
        log_log(LOG_ERR);
    log_free();
    return NULL;
}

/**
 * Executes a downstream LDM-7. Doesn't return until an error occurs or a
 * termination signal is received.
 *
 * @param[in] servAddr       Pointer to the address of the server from which to
 *                           obtain multicast information, backlog products, and
 *                           products missed by the VCMTP layer. Caller may free
 *                           upon return.
 * @param[in] feedtype       Feedtype of multicast group to receive.
 * @param[in] mcastIface     IP address of interface to use for receiving
 *                           packets.
 * @param[in] pqPathname     Pathname of the product-queue.
 * @retval    LDM7_MCAST     Multicast layer failure. `log_add()` called.
 * @retval    LDM7_SHUTDOWN  Process termination requested.
 * @retval    LDM7_SYSTEM    System error occurred. `log_start()` called.
 */
static int
executeDown7(
    const ServiceAddr* const restrict servAddr,
    const feedtypet                   feedtype,
    const char* const restrict        mcastIface,
    const char* const restrict        pqPathname)
{
    Down7* down7 = down7_new(servAddr, feedtype, mcastIface, pqPathname);
    int    status;

    if (NULL == down7) {
        status = LDM7_SYSTEM;
    }
    else {
        pthread_t termWaitThread;
        status = pthread_create(&termWaitThread, NULL, waitForTermSig, down7);
        if (status) {
            LOG_ERRNUM0(status, "Couldn't create termination-waiting thread");
            status = LDM7_SYSTEM;
        }
        else {
            (void)pthread_detach(termWaitThread);
            blockTermSigs();
            status = down7_start(down7);
        }
        down7_free(down7);
    } // `down7` allocated

    return status;
}

typedef struct elt {
    struct elt*  next;
    ServiceAddr* ul7;
    char*        mcastIface;
    feedtypet    ft;
    pid_t        pid;
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
 * @param[in] mcastIface  IP address of interface to use for incoming packets.
 *                        Caller may free upon return.
 * @retval    NULL        Failure. `log_start()` called.
 * @return                Pointer to new element.
 */
static Elt*
elt_new(
        const feedtypet             ft,
        ServiceAddr* const restrict ul7,
        const char* const restrict  mcastIface)
{
    Elt* elt = LOG_MALLOC(sizeof(Elt), "downstream LDM-7 element");

    if (elt) {
        elt->ul7 = sa_clone(ul7);

        if (NULL == elt->ul7) {
            free(elt);
            elt = NULL;
        }
        else {
            elt->mcastIface = strdup(mcastIface);
            if (elt->mcastIface == NULL) {
                LOG_SERROR0("Couldn't duplicate interface specification");
                sa_free(elt->ul7);
                free(elt);
                elt = NULL;
            }
            else {
                elt->ft = ft;
                elt->pid = -1;
            }
        }
    }

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
        free(elt->mcastIface);
        free(elt);
    }
}

/**
 * Starts the downstream LDM-7 referenced by an element as a child process of
 * the current process.
 *
 * @param[in] elt          Element whose downstream LDM-7 is to be started.
 * @retval    0            Success.
 * @retval    LDM7_SYSTEM  System error. `log_start()` called.
 */
static Ldm7Status
elt_start(
        Elt* const elt)
{
    int   status;
    pid_t pid = ldmfork();

    if (-1 == pid) {
        /* System error */
        LOG_ADD0("Couldn't fork downstream LDM-7 child process");
        status = LDM7_SYSTEM;
    }
    else if (0 == pid) {
        /* Child process */
        status = executeDown7(elt->ul7, elt->ft, elt->mcastIface, getQueuePath());

        if (status == LDM7_SHUTDOWN) {
            log_log(LOG_NOTICE);
            log_free();
            exit(0);
        }

        log_log(LOG_ERR);
        log_free();
        abort(); // Should never happen
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
 * @retval    0            Success.
 * @retval    LDM7_SYSTEM  System failure. `log_start()` called.
 */
Ldm7Status
d7mgr_add(
        const feedtypet             ft,
        ServiceAddr* const restrict ul7)
{
    int  status;
    Elt* elt = elt_new(ft, ul7, "0.0.0.0"); // all interfaces for now

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
 * @retval LDM7_SYSTEM  System error. `log_start()` called. All started
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
