/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: mldm_sender_map.c
 * @author: Steven R. Emmerson
 *
 * This file implements a singleton mapping between feed-types and process-IDs
 * of multicast LDM senders. The same mapping is accessible from multiple
 * processes and exists for the duration of the LDM session.
 *
 * The functions in this module are thread-compatible but not thread-safe.
 */

#include "config.h"

#include "ldm.h"
#include "ldmprint.h"
#include "log.h"
#include "mldm_sender_map.h"

#include <fcntl.h>
#include <limits.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

/**
 * filename of the shared memory object:
 */
static const char* SMO_FILENAME = "/mldmSenderMap";
/**
 * Number of feed-types:
 */
static const size_t NUM_FEEDTYPES = sizeof(feedtypet)*CHAR_BIT;
/**
 * Number of PID-s:
 */
static const size_t NUM_PIDS = sizeof(feedtypet)*CHAR_BIT;
/**
 * File descriptor for shared memory object for multicast LDM sender map:
 */
static int          fileDes;
/**
 * Array of process identifiers indexed by feed-type bit-index.
 */
static pid_t*       pids;
/**
 * Locking structure for concurrent access to the shared PID array:
 */
static struct flock lock;

/**
 * Opens a shared memory object. Creates it if it doesn't exist. The resulting
 * shared memory object will have zero size.
 *
 * @retval 0            Success.
 * @retval LDM7_SYSTEM  System error. `log_add()` called.
 */
Ldm7Status
smo_open(
        const char* const restrict pathname,
        int* const restrict        fileDes)
{
    int status;
    int fd = shm_open(pathname, O_RDWR|O_CREAT|O_EXCL, 0666);

    if (-1 < fd) {
        *fileDes = fd;
        status = 0;
    }
    else {
        /* Shared memory object already exists. */
        fd = shm_open(pathname, O_RDWR|O_CREAT|O_TRUNC, 0666);

        if (-1 < fd) {
            *fileDes = fd;
            status = 0;
        }
        else {
            LOG_SERROR1("Couldn't open shared memory object %s", pathname);
            status = LDM7_SYSTEM;
        }
    }

    return status;
}

/**
 * Closes a shared memory object by closing the associated file descriptor and
 * unlinking the pathname.
 *
 * @param[in] fd        The file-descriptor associated with the shared memory
 *                      object.
 * @param[in] pathname  The pathname of the shared memory object.
 */
static void
smo_close(
        const int fd,
        const char* const pathname)
{
    (void)close(fd);
    (void)shm_unlink(pathname);
}

/**
 * Clears a shared PID array by setting all its elements to zero.
 *
 * @param[in] pids     The shared PID array.
 * @param[in] numPids  The number of elements in the array.
 */
static void
spa_clear(
        pid_t* const pids,
        const size_t numPids)
{
    (void)memset(pids, 0, sizeof(pid_t)*numPids);
}

/**
 * Initializes a shared PID array from a shared memory object. All elements of
 * the array will be zero.
 *
 * @param[in]  fd           File descriptor of the shared memory object.
 * @param[in]  numPids      Number of PID-s in the array.
 * @param[out] pids         The array of pids.
 * @retval     0            Success. `*pids` is set.
 * @retval     LDM7_SYSTEM  System error. `log_add()` called.
 */
Ldm7Status
spa_init(
        const int     fd,
        const size_t  numPids,
        pid_t** const pids)
{
    const size_t SIZE = sizeof(pid_t)*numPids;
    int status = ftruncate(fd, SIZE);

    if (status) {
        LOG_SERROR0("Couldn't set size of shared PID array");
        status = LDM7_SYSTEM;
    }
    else {
        void* const addr = mmap(NULL, SIZE, PROT_READ|PROT_WRITE, MAP_SHARED,
                fd, 0);

        if (MAP_FAILED == addr) {
            LOG_SERROR0("Couldn't memory-map shared PID array");
            status = LDM7_SYSTEM;
        }
        else {
            spa_clear(addr, numPids);
            *pids = addr;
            status = 0;
        }
    }

    return status;
}

/**
 * Initializes this module. Shall be called only once per LDM session.
 *
 * @retval 0            Success.
 * @retval LDM7_SYSTEM  System error. `log_add()` called.
 */
Ldm7Status
msm_init(void)
{
    int fd;
    int status = smo_open(SMO_FILENAME, &fd);

    if (0 == status) {
        status = spa_init(fd, NUM_PIDS, &pids);

        if (status) {
            smo_close(fd, SMO_FILENAME);
        }
        else {
            fileDes = fd;
            lock.l_whence = SEEK_SET;
            lock.l_start = 0;
            lock.l_len = sizeof(pid_t); // locking first entry is sufficient
            status = 0;
        } // shared PID array initialized
    } // `fd` is open

    return status;
}

/**
 * Locks the map. Idempotent. Blocks until the lock is acquired or an error
 * occurs.
 *
 * @param[in] exclusive    Lock for exclusive access?
 * @retval    0            Success.
 * @retval    LDM7_SYSTEM  System failure. `log_add()` called.
 */
Ldm7Status
msm_lock(
        const bool exclusive)
{
    lock.l_type = exclusive ? F_RDLCK : F_WRLCK;

    if (-1 == fcntl(fileDes, F_SETLKW, &lock)) {
        LOG_SERROR0("Couldn't lock shared PID array");
        return LDM7_SYSTEM;
    }

    return 0;
}

/**
 * Adds a mapping between a feed-type and a multicast LDM sender process-ID.
 *
 * @param[in] feedtype     Feed-type.
 * @param[in] pid          Multicast LDM sender process-ID.
 * @retval    0            Success.
 * @retval    LDM7_DUP     Process identifier duplicates existing entry.
 *                         `log_add()` called.
 * @retval    LDM7_DUP     Feed-type overlaps with feed-type being sent by
 *                         another process. `log_add()` called.
 */
Ldm7Status
msm_put(
        const feedtypet feedtype,
        const pid_t     pid)
{
    unsigned  ibit;
    feedtypet mask;

    for (ibit = 0, mask = 1; ibit < NUM_FEEDTYPES; mask <<= 1, ibit++) {
        if ((feedtype & mask) && pids[ibit]) {
            LOG_START2("Feed-type %s is already being sent by process %ld",
                    s_feedtypet(mask), (long)pids[ibit]);
            return LDM7_DUP;
        }
        if (pid == pids[ibit]) {
            LOG_START2("Process %ld is already sending feed-type %s",
                    (long)pid, s_feedtypet(mask));
            return LDM7_DUP;
        }
    }

    for (ibit = 0, mask = 1; ibit < NUM_FEEDTYPES; mask <<= 1, ibit++)
        if (feedtype & mask)
            pids[ibit] = pid;

    return 0;
}

/**
 * Returns the process-ID associated with a feed-type.
 *
 * @param[in]  feedtype     Feed-type.
 * @param[out] pid          Associated process-ID.
 * @retval     0            Success. `*pid` is set.
 * @retval     LDM7_NOENT   No PID associated with feed-type.
 */
Ldm7Status
msm_getPid(
        const feedtypet feedtype,
        pid_t* const    pid)
{
    unsigned  ibit;
    feedtypet mask;

    for (ibit = 0, mask = 1; ibit < NUM_FEEDTYPES; mask <<= 1, ibit++) {
        if ((mask & feedtype) && pids[ibit]) {
            *pid = pids[ibit];
            return 0;
        }
    }

    return LDM7_NOENT;
}

/**
 * Unlocks the map.
 *
 * @retval    0            Success.
 * @retval    LDM7_SYSTEM  System failure. `log_add()` called.
 */
Ldm7Status
msm_unlock(void)
{
    lock.l_type = F_UNLCK;

    if (-1 == fcntl(fileDes, F_SETLKW, &lock)) {
        LOG_SERROR0("Couldn't unlock shared PID array");
        return LDM7_SYSTEM;
    }

    return 0;
}

/**
 * Removes the entry corresponding to a process identifier.
 *
 * @param[in] pid          Process identifier.
 * @retval    0            Success. `msm_getPid()` for the associated feed-type
 *                         will return LDM7_NOENT.
 * @retval    LDM7_NOENT   No entry corresponding to given process identifier.
 *                         Database is unchanged.
 */
Ldm7Status
msm_removePid(
        const pid_t pid)
{
    int       status = LDM7_NOENT;
    unsigned  ibit;

    for (ibit = 0; ibit < NUM_FEEDTYPES; ibit++) {
        if (pid == pids[ibit]) {
            pids[ibit] = 0;
            status = 0;
        }
    }

    return status;
}

/**
 * Clears all entries.
 */
void
msm_clear(void)
{
    spa_clear(pids, NUM_PIDS);
}

/**
 * Destroys this module. Should be called only once per LDM session.
 *
 * @retval 0            Success.
 * @retval LDM7_SYSTEM  System error. `log_add()` called.
 */
Ldm7Status
msm_destroy(void)
{
    smo_close(fileDes, SMO_FILENAME);
    return 0;
}
