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

#include "ldmprint.h"
#include "log.h"
#include "mldm_sender_map.h"

#include <pthread.h>
#include <sys/shm.h>

/// Number of feed-types:
static const size_t NUM_FEEDTYPES = (sizeof(feedtypet)*CHAR_BIT);

/// Information on a multicast LDM process:
typedef struct {
    /// Process identifier of multicast LDM sender:
    pid_t          pid;
    /// Port number of multicast LDM sender's FMTP server in host byte order:
    unsigned short fmtpPort;
    /// Port number of multicast LDM sender's RPC server in host byte order:
    unsigned short mldmSrvrPort;
} ProcInfo;

/// Shared memory object that maps from LDM feed to multicast LDM information:
typedef struct {
    pthread_rwlock_t rwLock;
    ProcInfo         procInfos[1];
} Smo;
static Smo*         smo;

/// ID of the shared-memory object:
static int          shmId;

/// Whether or not this module is initialized:
static bool         initialized = false;

static char*
smo_mldmStr(
        const feedtypet feed,
        const pid_t     pid,
        const in_port_t fmtpPort,
        const in_port_t mldmSrvrPort)
{
    return ldm_format(128, "{feed=%s, pid=%ld, fmtpPort=%hu, "
            "mldmSrvrPort=%hu}", s_feedtypet(feed), (long)pid, fmtpPort,
            mldmSrvrPort);
}

/**
 * Initializes the shared-memory object that contains the mapping from LDM feed
 * to information on the associated multicast LDM process. Should only be called
 * by the top-level LDM server and only once per LDM session.
 *
 * @retval 0            Success
 * @retval LDM7_SYSTEM  Failure. `log_add()` called.
 * @threadsafety        Compatible but not safe
 */
static Ldm7Status
smo_init(void)
{
    /*
     * The X/Open system interfaces (XSI) shared-memory API is used instead of
     * the X/OPEN Shared Memory Objects (SHM) REALTIME API because
     *   - `IPC_PRIVATE` can be used, obviating the need for a unique pathname
     *   - An SHM pathname can't be portably stat()ed for diagnostic purposes
     *     should shared-memory creation fail.
     */

    int          status;
    const size_t nbytes = sizeof(Smo)+(NUM_FEEDTYPES-1)*sizeof(ProcInfo);

    shmId = shmget(IPC_PRIVATE, nbytes, 0600);

    if (shmId == -1) {
        log_add_syserr("shmget() failure");
        status = LDM7_SYSTEM;
    }
    else {
        // Shared memory segment is initialized with all zero values
        log_debug("Allocated %zu-byte shared memory", nbytes);

        smo = shmat(shmId, NULL, 0);

        if (smo == (void*)-1) {
            log_add_syserr("shmat() failure");
            status = LDM7_SYSTEM;
        }
        else {
            pthread_rwlockattr_t rwLockAttr;

            status = pthread_rwlockattr_init(&rwLockAttr);

            if (status) {
                log_add_errno(status, "pthread_rwlockattr_init() failure");
                status = LDM7_SYSTEM;
            }
            else {
                status = pthread_rwlockattr_setpshared(&rwLockAttr,
                        PTHREAD_PROCESS_SHARED);

                if (status) {
                    log_add_errno(status, "pthread_rwlockattr_setpshared() "
                            "failure");
                    status = LDM7_SYSTEM;
                }
                else {
                    status = pthread_rwlock_init(&smo->rwLock, &rwLockAttr);

                    if (status) {
                        log_add_errno(status, "pthread_rwlock_init() failure");
                        status = LDM7_SYSTEM;
                    }
                } // Read/write lock attributes configured

                (void)pthread_rwlockattr_destroy(&rwLockAttr);
            } // Read/write lock attributes initialized

            if (status) {
                (void)shmdt(smo);
                smo = NULL;
            }
        } // Shared-memory object attached

        if (status)
            (void)shmctl(shmId, IPC_RMID, NULL);
    } // Shared-memory object created

    return status;
}

/**
 * Destroys the shared-memory object.
 *
 * @param final   Should the shared-memory object be deleted?
 * @threadsafety  Compatible but not safe
 */
static void
smo_destroy(const bool final)
{
    if (smo) {
        if (final)
            (void)pthread_rwlock_destroy(&smo->rwLock);

        (void)shmdt(smo);
        smo = NULL;

        if (final) {
            (void)shmctl(shmId, IPC_RMID, NULL);
            shmId = -1;
        }
    }
}

/**
 * Locks the shared-memory object.
 *
 * @param[in] forWriting   For writing?
 * @retval    0            Success
 * @retval    LDM7_SYSTEM  Failure. `log_add()` called.
 * @threadsafety           Compatible but not safe
 */
static Ldm7Status
smo_lock(const bool forWriting)
{
    int status = forWriting
            ? pthread_rwlock_wrlock(&smo->rwLock)
            : pthread_rwlock_rdlock(&smo->rwLock);

    if (status) {
        log_add_errno(status, "pthread_rwlock_lock() failure");
        status = LDM7_SYSTEM;
    }

    return status;
}

/**
 * Unlocks the shared-memory object.
 *
 * @retval    0            Success
 * @retval    LDM7_SYSTEM  Failure. `log_add()` called.
 * @threadsafety           Compatible but not safe
 */
static Ldm7Status
smo_unlock(void)
{
    int status = pthread_rwlock_unlock(&smo->rwLock);

    if (status) {
        log_add_errno(status, "pthread_rwlock_unlock() failure");
        status = LDM7_SYSTEM;
    }

    return status;
}

static void
smo_abortIfUnlocked(void)
{
    int status = pthread_rwlock_trywrlock(&smo->rwLock);

    log_assert(status == EBUSY);
}

/**
 * Adds a mapping between an LDM feed and a multicast LDM sender process.
 *
 * @param[in] feed          LDM feed
 * @param[in] pid           Multicast LDM sender process-ID.
 * @param[in] fmtpPort      Port number of the FMTP TCP server.
 * @param[in] mldmSrvrPort  Port number of multicast LDM sender's RPC server in
 *                          host byte order
 * @retval    0             Success.
 * @retval    LDM7_DUP      Process identifier duplicates existing entry.
 *                          `log_add()` called.
 * @retval    LDM7_DUP      Feed overlaps with feed being sent by another
 *                          process. `log_add()` called.
 * @threadsafety            Compatible but not safe
 */
static Ldm7Status
smo_put(const feedtypet      feed,
        const pid_t          pid,
        const unsigned short fmtpPort,
        const unsigned short mldmSrvrPort)
{
    smo_abortIfUnlocked();

    int status;

    if (pid == 0) {
        log_add("PID is zero");
        status = LDM7_INVAL;
    }
    else {
        status = 0;

        unsigned long mask;
        ProcInfo*     procInfo = smo->procInfos;

        /*
         * Ensure an atomic transaction by vetting before modifying. The
         * following assumes all feeds are disjoint.
         */
        for (mask = 1; mask && mask <= ANY; mask <<= 1, ++procInfo) {
            const pid_t infoPid = procInfo->pid;

            if (infoPid) {
                if (infoPid == pid) {
                    log_add("Process-information array already contains entry "
                            "for PID %ld", (long)pid);
                    status = LDM7_DUP;
                    break;
                }

                if (feed & mask) {
                    log_add("Feed %s is already being sent by process %ld",
                            s_feedtypet(mask), (long)infoPid);
                    status = LDM7_DUP;
                    break;
                }
            }
        }

        if (status == 0) {
            for (mask = 1, procInfo = smo->procInfos; mask && mask <= ANY;
                    mask <<= 1, ++procInfo) {
                if (feed & mask) {
                    procInfo->pid = pid;
                    procInfo->fmtpPort = fmtpPort;
                    procInfo->mldmSrvrPort = mldmSrvrPort;
                }
            }
        }
    }

    return status;
}

/**
 * Returns process-information associated with a feed-type.
 *
 * @param[in]  feed          LDM feed
 * @param[out] pid           Associated process-ID.
 * @param[out] fmtpPort      Port number of the associated FMTP TCP server.
 * @param[out] mldmSrvrPort  Port number of multicast LDM sender's RPC server
 * @retval     0             Success. `*pid`, `*fmtpPort`, and `*mldmSrvrPort`
 *                           are set
 * @retval     LDM7_NOENT    No process associated with feed
 * @threadsafety             Compatible but not safe
 */
static Ldm7Status
smo_get(const feedtypet                feed,
        pid_t* const restrict          pid,
        unsigned short* const restrict fmtpPort,
        unsigned short* const restrict mldmSrvrPort)
{
    smo_abortIfUnlocked();

    unsigned long   mask;
    const ProcInfo* procInfo = smo->procInfos;

    // The following assumes all feeds are disjoint
    for (mask = 1; mask && mask <= ANY; mask <<= 1, ++procInfo) {
        if (mask & feed) {
            const pid_t infoPid = procInfo->pid;

            if (infoPid) {
                *pid = infoPid;
                *fmtpPort = procInfo->fmtpPort;
                *mldmSrvrPort = procInfo->mldmSrvrPort;

                return 0;
            }
        }
    }

    return LDM7_NOENT;
}


/**
 * Removes the entry corresponding to a process identifier.
 *
 * @param[in] pid          Process identifier.
 * @retval    0            Success. `msp_getPid()` for the associated feed-type
 *                         will return LDM7_NOENT.
 * @retval    LDM7_NOENT   No entry corresponding to given process identifier.
 *                         Database is unchanged.
 * @threadsafety           Compatible but not safe
 */
static Ldm7Status
smo_remove(const pid_t pid)
{
    log_debug("smo_remove() entered");
    smo_abortIfUnlocked();

    int       status = LDM7_NOENT;
    unsigned  ibit;

    for (ibit = 0; ibit < NUM_FEEDTYPES; ibit++) {
        ProcInfo* const procInfo = smo->procInfos + ibit;
        if (pid == procInfo->pid) {
            (void)memset(procInfo, 0, sizeof(*procInfo));
            status = 0;
        }
    }

    log_debug("smo_remove() returning");
    return status;
}

/**
 * Clears the map.
 *
 * @threadsafety        Compatible but not safe
 */
static void
smo_clear(void)
{
    log_debug("smo_clear() entered");
    smo_abortIfUnlocked();

    (void)memset(smo->procInfos, 0, sizeof(ProcInfo)*NUM_FEEDTYPES);

    log_debug("smo_clear() returning");
}

/******************************************************************************
 * Public API:
 ******************************************************************************/

Ldm7Status
msm_init(void)
{
    log_debug("Entered");

    int status;

    if (initialized) {
        log_add("Module is already initialized");
        status = LDM7_LOGIC;
    }
    else {
        status = smo_init();

        if (status) {
            log_add("Couldn't initialize shared-memory object");
        }
        else {
            initialized = true;
        } // `smo_pathname` set
    }

    log_debug("Returning");
    return status;
}

void
msm_destroy(const bool final)
{
    log_debug("msm_destroy(): Entered");

    if (!initialized) {
        log_add("Module is not initialized");
        log_flush_error();
    }
    else {
        smo_destroy(final);
        initialized = false;
    }

    log_debug("msm_destroy(): Returning");
}

Ldm7Status
msm_lock(const bool forWriting)
{
    int status;

    if (!initialized) {
        log_add("Module is not initialized");
        status = LDM7_LOGIC;
    }
    else {
        status = smo_lock(forWriting);

        if (status)
            log_add("Couldn't lock shared process-information object");
    }

    return status;
}

Ldm7Status
msm_unlock(void)
{
    int status;

    if (!initialized) {
        log_add("Module is not initialized");
        status = LDM7_LOGIC;
    }
    else {
        status = smo_unlock();

        if (status)
            log_add("Couldn't unlock shared process-information object");
    }

    return status;
}

Ldm7Status
msm_put(const feedtypet feed,
        const pid_t     pid,
        const in_port_t fmtpPort,
        const in_port_t mldmSrvrPort)
{
    int status;

    if (!initialized) {
        log_add("Module is not initialized");
        status = LDM7_LOGIC;
    }
    else {
        status = smo_put(feed, pid, fmtpPort, mldmSrvrPort);

        if (status) {
            log_add("Couldn't save multicast process information");
        }
        else {
            char* const mldmStr = smo_mldmStr(feed, pid, fmtpPort,
                    mldmSrvrPort);
            log_debug("Saved information on multicast process %s", mldmStr);
            free(mldmStr);
        }
    }

    return status;
}

Ldm7Status
msm_get(const feedtypet                feed,
        pid_t* const restrict          pid,
        unsigned short* const restrict fmtpPort,
        unsigned short* const restrict mldmSrvrPort)
{
    int status;

    if (!initialized) {
        log_add("Module is not initialized");
        status = LDM7_LOGIC;
    }
    else {
        status = smo_get(feed, pid, fmtpPort, mldmSrvrPort);
    }

    return status;
}

Ldm7Status
msm_remove(const pid_t pid)
{
    int status;

    if (!initialized) {
        log_add("Module is not initialized");
        status = LDM7_LOGIC;
    }
    else {
        status = smo_remove(pid);

        if (status) {
            log_add("No multicast sender corresponds to process %ld",
                    (long)pid);
        }
        else if (log_is_enabled_debug) {
            log_debug("Removed information on multicast process %ld",
                    (long)pid);
        }
    }

    return status;
}
