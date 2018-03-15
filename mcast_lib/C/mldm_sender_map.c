/**
 * Copyright 2018 University Corporation for Atmospheric Research. All rights
 * reserved. See the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: mldm_sender_map.c
 * @author: Steven R. Emmerson
 *
 * This file implements a singleton mapping between feed-types and information
 * on multicast LDM sender processes. The same mapping is accessible from
 * multiple processes and exists for the duration of the LDM session.
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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

/**
 * Pathname of the shared memory object:
 */
static char*        smo_pathname;
/**
 * Number of feed-types:
 */
static const size_t NUM_FEEDTYPES = sizeof(feedtypet)*CHAR_BIT;
/**
 * File descriptor for shared memory object for multicast LDM sender map:
 */
static int          fileDes;
/**
 * Multicast LDM process information structure.
 */
typedef struct {
    /// Process identifier of multicast LDM sender
    pid_t          pid;
    /// Port number of multicast LDM sender's FMTP TCP server in host byte order
    unsigned short port;
    /// Port number of multicast LDM sender's RPC server in host byte order
    unsigned short mldmSrvrPort;
} ProcInfo;
/**
 * Array of process information structures indexed by feed-type bit-index.
 */
static ProcInfo*  procInfos;
/**
 * Locking structure for concurrent access to the shared process-information
 * array:
 */
static struct flock lock;

/**
 * Opens a shared memory object. Creates it if it doesn't exist. The resulting
 * shared memory object will have zero size.
 *
 * @param[in]  pathname     Pathname of the shared memory object.
 * @param[out] fd           File descriptor of the shared memory object.
 * @retval     0            Success. `*fd` is set.
 * @retval     LDM7_SYSTEM  System error. `log_add()` called.
 */
static Ldm7Status
smo_open(
        const char* const restrict pathname,
        int* const restrict        fd)
{
    int status;
    int myFd = shm_open(pathname, O_RDWR|O_CREAT|O_EXCL, 0666);

    if (-1 < myFd) {
        *fd = myFd;
        status = 0;
    }
    else if (errno != EEXIST) {
        log_add_syserr("Couldn't create shared memory object \"%s\"", pathname);
        status = LDM7_SYSTEM;
    }
    else {
        log_info("Shared memory object \"%s\" already exists");
        myFd = shm_open(pathname, O_RDWR|O_CREAT|O_TRUNC, 0666);

        if (-1 < myFd) {
            *fd = myFd;
            status = 0;
        }
        else {
            log_add_syserr("Couldn't open shared memory object \"%s\"",
                    pathname);
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
        const int         fd,
        const char* const pathname)
{
    (void)close(fd);
    (void)shm_unlink(pathname);
}

/**
 * Initializes a shared memory object. All elements of the array will be zero.
 *
 * @param[in]  fd           File descriptor of the shared memory object.
 * @param[in]  size         Size of the shared memory object in bytes.
 * @param[out] smo          The shared memory object.
 * @retval     0            Success. `*smo` is set.
 * @retval     LDM7_SYSTEM  System error. `log_add()` called.
 */
static Ldm7Status
smo_init(
        const int    fd,
        const size_t size,
        void** const smo)
{
    int          status = ftruncate(fd, size);

    if (status) {
        log_add_syserr("Couldn't set size of shared memory object");
        status = LDM7_SYSTEM;
    }
    else {
        void* const addr = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED,
                fd, 0);

        if (MAP_FAILED == addr) {
            log_add_syserr("Couldn't memory-map shared memory object");
            status = LDM7_SYSTEM;
        }
        else {
            (void)memset(addr, 0, size);
            *smo = addr;
            status = 0;
        }
    }

    return status;
}

/**
 * Sets the pathname of the shared-memory object. The name is unique to the
 * user.
 *
 * @retval 0            Success
 * @retval LDM7_SYSTEM  System error. `log_add()` called.
 */
static Ldm7Status
msm_setSmoPathname(void)
{
    static const char format[] = "/mldmSenderMap-%s";
    int               status;
    const char*       userName = getenv("LOGNAME");
    if (userName == NULL) {
        userName = getenv("USER");
        if (userName == NULL) {
            log_add("Couldn't get value of environment variables "
                    "\"LOGNAME\" or \"USER\"");
            status = LDM7_SYSTEM;
        }
    }
    if (userName) {
        int nbytes = snprintf(NULL, 0, format, userName);
        if (nbytes < 0) {
            log_add_syserr("Couldn't get size of pathname of shared-memory "
                    "object");
            status = LDM7_SYSTEM;
        }
        else {
            nbytes += 1; // for NUL-terminator
            smo_pathname = log_malloc(nbytes,
                    "pathname of shared-memory object");
            if (smo_pathname == NULL) {
                status = LDM7_SYSTEM;
            }
            else {
                (void)snprintf(smo_pathname, nbytes, format, userName);
                status = 0;
            }
        }
    }
    return status;
}

/**
 * Initializes this module. Shall be called only once per LDM session.
 *
 * @retval 0            Success.
 * @retval LDM7_INVAL   This module is already initialized. `log_add()` called.
 * @retval LDM7_SYSTEM  System error. `log_add()` called.
 */
Ldm7Status
msm_init(void)
{
    log_debug("Entered");
    int status;
    if (smo_pathname) {
        log_add("Multicast sender map is already initialized");
        status = LDM7_INVAL;
    }
    else {
        status = msm_setSmoPathname();
        if (status) {
            log_add("Couldn't initialize pathname of shared-memory object");
        }
        else {
            int fd;
            status = smo_open(smo_pathname, &fd);
            if (0 == status) {
                void* addr;
                status = smo_init(fd, NUM_FEEDTYPES, &addr);
                if (status) {
                    log_add("Couldn't initialize shared-memory object \"%s\"",
                            smo_pathname);
                    smo_close(fd, smo_pathname);
                }
                else {
                    procInfos = addr;
                    fileDes = fd;
                    lock.l_whence = SEEK_SET;
                    lock.l_start = 0;
                    lock.l_len = 0; // entire object
                } // shared PID array initialized
            } // `fd` is open
        } // `smo_pathname` set
    } // module not initialized

    log_debug("Returning");
    return status;
}

/**
 * Locks the map. Idempotent. Blocks until the lock is acquired or an error
 * occurs. Locking the map is explicit because the map is shared by multiple
 * processes and a transaction might require several function calls.
 *
 * @param[in] exclusive    Lock for exclusive access?
 * @retval    0            Success.
 * @retval    LDM7_SYSTEM  System failure. `log_add()` called.
 */
Ldm7Status
msm_lock(const bool exclusive)
{
    lock.l_type = exclusive ? F_RDLCK : F_WRLCK;

    if (-1 == fcntl(fileDes, F_SETLKW, &lock)) {
        log_add_syserr("Couldn't lock shared process-information array: "
                "fileDes=%d", fileDes);
        return LDM7_SYSTEM;
    }

    return 0;
}

/**
 * Adds a mapping between a feed-type and a multicast LDM sender process.
 *
 * @param[in] feedtype      Feed-type.
 * @param[in] pid           Multicast LDM sender process-ID.
 * @param[in] port          Port number of sender's FMTP TCP server in host byte
 *                          order
 * @param[in] mldmSrvrPort  Port number of multicast LDM sender's RPC server in
 *                          host byte order
 * @retval    0             Success.
 * @retval    LDM7_DUP      Process identifier duplicates existing entry.
 *                          `log_add()` called.
 * @retval    LDM7_DUP      Feed-type overlaps with feed-type being sent by
 *                          another process. `log_add()` called.
 */
Ldm7Status
msm_put(
        const feedtypet      feedtype,
        const pid_t          pid,
        const unsigned short port,
        const unsigned short mldmSrvrPort)
{
    unsigned  ibit;
    feedtypet mask;

    for (ibit = 0, mask = 1; ibit < NUM_FEEDTYPES; mask <<= 1, ibit++) {
        const pid_t infoPid = procInfos[ibit].pid;
        if ((feedtype & mask) && infoPid) {
            log_add("Feed-type %s is already being sent by process %ld",
                    s_feedtypet(mask), (long)pid);
            return LDM7_DUP;
        }
        if (pid == infoPid) {
            log_add("Process-information array already contains entry for PID "
                    "%ld", (long)pid);
            return LDM7_DUP;
        }
    }

    for (ibit = 0, mask = 1; ibit < NUM_FEEDTYPES; mask <<= 1, ibit++) {
        if (feedtype & mask) {
            ProcInfo* const procInfo = procInfos + ibit;
            procInfo->pid = pid;
            procInfo->port = port;
            procInfo->mldmSrvrPort = mldmSrvrPort;
        }
    }

    return 0;
}

/**
 * Returns process-information associated with a feed-type.
 *
 * @param[in]  feedtype      Feed-type.
 * @param[out] pid           Associated process-ID.
 * @param[out] port          Port number of multicast LDM sender's FMTP TCP
 *                           server
 * @param[out] mldmSrvrPort  Port number of multicast LDM sender's RPC server
 * @retval     0             Success. `*pid`, `*port`, and `mldmSrvrPort` are
 *                           set
 * @retval     LDM7_NOENT    No process associated with feed-type.
 */
Ldm7Status
msm_get(
        const feedtypet                feedtype,
        pid_t* const restrict          pid,
        unsigned short* const restrict port,
        unsigned short* const restrict mldmSrvrPort)
{
    unsigned  ibit;
    feedtypet mask;

    for (ibit = 0, mask = 1; ibit < NUM_FEEDTYPES; mask <<= 1, ++ibit) {
        const ProcInfo* procInfo = procInfos + ibit;
        const pid_t     infoPid = procInfo->pid;
        if ((mask & feedtype) && infoPid) {
            *pid = infoPid;
            *port = procInfo->port;
            *mldmSrvrPort = procInfo->mldmSrvrPort;
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
        log_add_syserr("Couldn't unlock shared process-information array: "
                "fileDes=%d", fileDes);
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
msm_remove(const pid_t pid)
{
    int       status = LDM7_NOENT;
    unsigned  ibit;

    for (ibit = 0; ibit < NUM_FEEDTYPES; ibit++) {
        ProcInfo* const procInfo = procInfos + ibit;
        if (pid == procInfo->pid) {
            (void)memset(procInfo, 0, sizeof(*procInfo));
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
    if (smo_pathname)
        (void)memset(procInfos, 0, sizeof(ProcInfo)*NUM_FEEDTYPES);
}

/**
 * Destroys this module. Should be called only once per LDM session.
 *
 * @retval 0            Success.
 */
void
msm_destroy(void)
{
    log_debug("Entered");
    if (smo_pathname) {
        smo_close(fileDes, smo_pathname);
        free(smo_pathname);
        smo_pathname = NULL;
    }
    log_debug("Returning");
}
