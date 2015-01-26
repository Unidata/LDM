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
 * Name of the shared memory object:
 */
static const char* SMO_NAME = "multicast LDM sender shared memory object";
/**
 * filename of the shared memory object:
 */
static const char* SMO_FILENAME = "/mldmSenderMap";
/**
 * Number of feed-types:
 */
static const size_t NUM_FEEDTYPES = sizeof(feedtypet)*CHAR_BIT;
/**
 * File descriptor for shared memory object for multicast LDM sender map:
 */
static int          fileDes;
/**
 * Array of process identifiers indexed by feed-type bit-index.
 */
static pid_t*       pids;
/**
 * Locking structure:
 */
static struct flock lock;

/**
 * Initializes this module. Shall be called only once per LDM session.
 *
 * @retval 0            Success.
 * @retval LDM7_SYSTEM  System error. `log_add()` called.
 */
Ldm7Status
msm_init(void)
{
    int status;
    int fd = shm_open(SMO_FILENAME, O_RDWR|O_CREAT|O_TRUNC, 0666);

    if (-1 == fd) {
        LOG_SERROR1("Couldn't open %s", SMO_NAME);
        status = LDM7_SYSTEM;
    }
    else {
        const size_t SIZE = sizeof(pid_t)*NUM_FEEDTYPES;

        if (0 != ftruncate(fd, SIZE)) {
            LOG_SERROR1("Couldn't set size of %s", SMO_NAME);
            status = LDM7_SYSTEM;
        }
        else {
            void* const addr = mmap(NULL, SIZE, PROT_READ|PROT_WRITE,
                    MAP_SHARED, fd, 0);

            if (MAP_FAILED == addr) {
                LOG_SERROR1("Couldn't memory-map %s", SMO_NAME);
                status = LDM7_SYSTEM;
            }
            else {
                (void)memset(addr, 0, SIZE);
                lock.l_whence = SEEK_SET;
                lock.l_start = 0;
                lock.l_len = sizeof(pid_t); // locking first entry is sufficient
                fileDes = fd;
                pids = addr;
                status = 0;
            } // shared memory object memory-mapped
        } // size of shared memory object set

        if (status)
            (void)close(fd);
    } // `fd` open

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
        LOG_SERROR1("Couldn't lock %s", SMO_NAME);
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
                    (long)pids[ibit], s_feedtypet(mask));
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
        LOG_SERROR1("Couldn't unlock %s", SMO_NAME);
        return LDM7_SYSTEM;
    }

    return 0;
}

/**
 * Removes the entry corresponding to a process identifier.
 *
 * @param[in] pid          Process identifier.
 * @retval    0            Success. `msp_getPid()` for the associated feed-type
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
    feedtypet mask;

    for (ibit = 0, mask = 1; ibit < NUM_FEEDTYPES; mask <<= 1, ibit++) {
        if (pid == pids[ibit]) {
            pids[ibit] = 0;
            status = 0;
        }
    }

    return status;
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
    if (close(fileDes)) {
        LOG_SERROR1("Couldn't close %s", SMO_NAME);
        return LDM7_SYSTEM;
    }

    (void)shm_unlink(SMO_FILENAME);

    return 0;
}
