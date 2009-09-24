#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/shm.h>

#include "error.h"
#include "SharedCounter.h"

struct SharedCounter {
    int                 shmid;          /* shared-memory identifier */
    volatile unsigned*  counter;        /* shared-counter */
};


/*
 * Opens a shared-counter.
 *
 * Arguments:
 *      path    Pathname of the stat()able file to be associated with 
 *              the shared-counter.
 *      *sc     Pointer to shared-counter.  Set on and only on success.
 *
 * Returns:
 *      NULL    Success
 *      !NULL   Failure:
 *                      SC_SYSTEM
 */
ErrorObj*
sc_open(
    const char*                 path,
    SharedCounter** const       sc)
{
    ErrorObj*           error;
    size_t              nbytes = sizeof(SharedCounter);
    SharedCounter*      ptr = malloc(nbytes);

    if (NULL == ptr) {
        error = ERR_NEW2(SC_SYSTEM, NULL,
            "Couldn't allocate %lu bytes: %s",
            (unsigned long)nbytes, strerror(errno));
    }
    else {
        key_t   key = ftok(path, 0);

        if ((key_t)-1 == key) {
            error = ERR_NEW1(SC_SYSTEM, NULL,
                "Couldn't create key for shared-memory segment: %s",
                strerror(errno));
        }
        else {
            int shmid = shmget(key, sizeof(unsigned), 0600 | IPC_CREAT);

            if (-1 == shmid) {
                error = ERR_NEW1(SC_SYSTEM, NULL,
                    "Couldn't get shared-memory segment: %s", strerror(errno));
            }
            else {
                void* const     counter = shmat(shmid, NULL, 0);

                if ((void*)-1 == counter) {
                    error = ERR_NEW1(SC_SYSTEM, NULL,
                        "Couldn't attach shared-memory segment: %s",
                        strerror(errno));
                }
                else {
                    ptr->shmid = shmid;
                    ptr->counter = (unsigned*)counter;
                    *sc = ptr;
                    error = NULL;       /* success */
                }                       /* shared-memory segment attached */

                if (error)
                    (void)shmctl(shmid, IPC_RMID, NULL);
            }                           /* got shared-memory segment */
        }                               /* got key for shared memory segment */

        if (error)
            free(ptr);
    }                                   /* ptr allocated */

    return error;
}


/*
 * Increments a shared-counter and returns the previous value.
 *
 * Arguments:
 *      sc      Pointer to shared-counter.
 *      *prev   Previous value.  Set on and only on success.
 *
 * Returns:
 *      NULL    Success.
 *      !NULL   Failure:
 */
ErrorObj*
sc_increment(
    SharedCounter* const        sc,
    unsigned* const             prev)
{
    ErrorObj*           error = NULL;           /* success */

    *prev = sc->counter++;

    return error;
}
    

/*
 * Closes a shared-counter.
 *
 * Arguments:
 *      sc      Pointer to shared-counter.  May be NULL.
 *
 * Returns:
 *      NULL    Success.
 *      !NULL   Failure:
 *                      SC_SYSTEM
 */
ErrorObj*
sc_close(
    SharedCounter* const        sc)
{
    ErrorObj*   error = NULL;           /* success */

    if (NULL != sc) {
        if (NULL != sc->counter) {
            shmid_ds    stat;

            if (-1 == shmdt(sc->counter)) {
                error = ERR_NEW1(SC_ERROR, NULL,
                    "Couldn't detatch shared-memory segment: %s",
                    strerror(errno));
            }
            else {
                if (-1 == shmctl(sc->shmid, IPC_STAT, &stat)) {
                    error = ERR_NEW1(SC_ERROR, NULL,
                        "Couldn't get status of shared-memory segment: %s",
                        strerror(errno));
                }
                else {
                    if (0 == stat->shm_nattch) {
                        if (-1 == shmctl(sc->shmid, IPC_RMID, NULL)) {
                            error = ERR_NEW1(SC_ERROR, NULL,
                                "Couldn't destroy shared-memory segment: %s",
                                strerror(errno));
                        }
                    }

                    sc->counter = NULL;
                    sc->shmid = -1;
                }
            }
        }

        free(sc);
    }

    return error;
}
