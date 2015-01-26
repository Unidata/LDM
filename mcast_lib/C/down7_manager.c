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

#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct elt {
    struct elt*  next;
    ServiceAddr* ul7;
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
 * @param[in] ft    Feedtype to subscribe to.
 * @param[in] ul7   Upstream LDM-7 to which to subscribe.
 * @retval    NULL  Failure. `log_start()` called.
 * @return          Point to new element.
 */
static Elt*
elt_new(
        const feedtypet    ft,
        ServiceAddr* const ul7)
{
    Elt* elt = LOG_MALLOC(sizeof(Elt), "downstream LDM-7 element");

    if (elt) {
        elt->ul7 = sa_clone(ul7);

        if (NULL == elt->ul7) {
            free(elt);
            elt = NULL;
        }
        else {
            elt->ft = ft;
            elt->pid = -1;
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
        status = down7_run(elt->ul7, elt->ft, getQueuePath());

        if (status == LDM7_SHUTDOWN) {
            log_log(LOG_NOTICE);
            exit(0);
        }

        log_log(LOG_ERR);
        exit(status);
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
        const feedtypet    ft,
        ServiceAddr* const ul7)
{
    int  status;
    Elt* elt = elt_new(ft, ul7);

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
    int  status;

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
