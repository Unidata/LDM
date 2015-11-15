/**
 * Copyright 2013 University Corporation for Atmospheric Research
 * All rights reserved
 * <p>
 * See file COPYRIGHT in the top-level source-directory for copying and
 * redistribution conditions.
 * <p>
 * This module implements a set of child process identifiers -- so the LDM
 * can respond to IS_ALIVE inquiries.
 */

#include "config.h"

#include <errno.h>
#include <search.h>
#include <stdlib.h>     /* malloc(), free() */
#include <sys/types.h>

#include "ulog.h"
#include "mylog.h"
#include "child_process_set.h"


static void*    root = NULL;
static unsigned count = 0;


static int compare(
    const void *elt1,
    const void *elt2)
{
    const pid_t pid1 = *(const pid_t*)elt1;
    const pid_t pid2 = *(const pid_t*)elt2;

    return
        pid1 > pid2
            ? 1
            : pid1 == pid2
                ?  0
                : -1;
}


/**
 * Adds a PID.
 *
 * @param pid       [in] The PID to be added.
 * @retval 0        Success.
 * @retval ENOMEM   Out-of-memory.
 */
int
cps_add(
    pid_t pid)
{
    int    error;

    if (cps_contains(pid)) {
        error = 0;
    }
    else {
        pid_t *elt = malloc(sizeof(pid_t));

        if (elt == NULL) {
            error = ENOMEM;
        }
        else {
            *elt = pid;

            if (tsearch(elt, &root, compare) == NULL) {
                error = ENOMEM;
            }
            else {
                ++count;
                error = 0;
            }

            if (error)
                free(elt);
        } /* "elt" allocated */
    } /* "pid" doesn't exist */

    return error;
}


/**
 * Ensures that a PID doesn't exist.
 *
 * @param pid       [in] The PID to not exist.
 */
void
cps_remove(
    pid_t pid)
{
    void  *node = tfind(&pid, &root, compare);

    if (node != NULL) {
        pid_t *elt = *(pid_t**)node;

        (void)tdelete(&pid, &root, compare);
        free(elt);
        count--;
    }
}


/**
 * Indicates if a PID exists.
 *
 * @param pid       [in] The PID to check.
 * @retval 0        The PID doesn't exist.
 * @retval 1        The PID exists.
 */
int
cps_contains(
    pid_t pid)
{
    return tfind(&pid, &root, compare) != NULL;
}


/**
 * Returns the number of PID-s.
 *
 * @return The number of PID-s.
 */
unsigned
cps_count(void)
{
    return count;
}
