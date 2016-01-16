/*
 * Copyright 2012 University Corporation for Atmospheric Research. All rights
 * reserved.
 *
 * See file COPYRIGHT in the top-level source-directory for legal conditions.
 */

/**
 * @file semRWLock.c
 *
 * A semaphore-based read/write lock. The implementation is thread-compatible
 * but not thread-safe.
 */

#include <config.h>

#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <log.h>

#include "semRWLock.h"

/**
 * The individual semaphores within the semaphore set.
 */
enum {
    SI_LOCK = 0, SI_NUM_READERS, SI_NUM_SEMS
};

/**
 * This module's opaque type:
 */
struct srwl_Lock {
    const char* isValid;
    int semId;
    pid_t pid; /* the process that owns this structure */
    unsigned numReadLocks;
    unsigned numWriteLocks;
};

/**
 * Static semaphore-set operations
 */
static struct sembuf writeLockOps[SI_NUM_SEMS];
static struct sembuf readLockOps[SI_NUM_SEMS];
static struct sembuf shareOps[1];
static struct sembuf writeUnlockOps[1];
static struct sembuf readUnlockOps[1];
static int isInitialized;

/**
 * Valid string.
 */
static const char* const VALID_STRING = __FILE__;

/**
 * Protection modes for the semaphore.
 */
static mode_t read_write;

/**
 * Vets a lock structure.
 *
 * @retval RWL_SUCCESS  The lock structure is valid
 * @retval RWL_INVALID  The lock structure is invalid. log_add() called.
 */
static srwl_Status vet(
        srwl_Lock* const lock /**< [in/out] the lock structure to vet */)
{
    int status;

    if (lock->isValid != VALID_STRING) {
        log_add("Invalid lock structure");

        status = RWL_INVALID;
    }
    else {
        const pid_t pid = getpid();

        if (pid != lock->pid) {
            /*
             * This process must be a child process. Reset the lock. NB: A
             * fork() zeroes the "semadj" value of semaphores in the child
             * process.
             */
            lock->numReadLocks = 0;
            lock->numWriteLocks = 0;
            lock->pid = pid;
        }

        status = RWL_SUCCESS;
    }

    return status;
}

/**
 * Deletes a semaphore set.
 *
 * @param[in] semId     Semaphore set identifier
 * @retval RWL_SUCCESS  Success.
 * @retval RWL_SYSTEM   System error. See "errno". log_add() called.
 */
static srwl_Status deleteSemSet(
        const int semId)
{
    if (semctl(semId, 0, IPC_RMID) == 0)
        return RWL_SUCCESS;

    log_add("Couldn't delete semaphore set: semId=%d", semId);
    return RWL_SYSTEM;
}

/**
 * Creates a read/write lock based on creating a new semaphore set. Any previous
 * semaphore set will be deleted.
 *
 * @retval RWL_SUCCESS  Success
 * @retval RWL_SYSTEM   System error. See "errno". log_add() called.
 */
static srwl_Status createLock(
        const key_t key /**< [in] the key associated with the semaphore */,
        int* const semId /**< [out] pointer to the semaphore identifier */)
{
    srwl_Status status;
    int id;

    (void)deleteSemSet(semget(key, 0, read_write));
    log_clear();

    id = semget(key, SI_NUM_SEMS, IPC_CREAT | IPC_EXCL | read_write);

    if (-1 == id) {
        log_add_syserr("Couldn't create semaphore set");
        status = RWL_SYSTEM;
    }
    else {
        unsigned short semVal[SI_NUM_SEMS];
        union semun {
            int val; /* Value for SETVAL */
            struct semid_ds *buf; /* Buffer for IPC_STAT, IPC_SET */
            unsigned short *array; /* Array for GETALL, SETALL */
        } arg;

        semVal[SI_LOCK] = 1;
        semVal[SI_NUM_READERS] = 0;
        arg.array = semVal;

        if (semctl(id, 0, SETALL, arg) == -1) {
            log_add_syserr("Couldn't initialize semaphore set: semId=%d", id);
            (void) deleteSemSet(id);
            status = RWL_SYSTEM;
        }
        else {
            *semId = id;
            status = RWL_SUCCESS;
        }
    }

    return status;
}

/**
 * Obtains a read-write lock based on an existing semaphore set.
 *
 * @retval RWL_SUCCESS  Success.
 * @retval RWL_EXIST    The lock doesn't exist.
 * @retval RWL_SYSTEM   System error. See "errno". log_add() called.
 */
static srwl_Status getLock(
        const key_t key /**< [in] the key associated with the semaphore */,
        int* const semId /**< [out] pointer to the semaphore identifier */)
{
    srwl_Status status;
    int id = semget(key, SI_NUM_SEMS, read_write);

    if (-1 == id) {
        log_add_syserr("Couldn't get existing semaphore set");
        status = RWL_EXIST;
    }
    else {
        *semId = id;
        status = RWL_SUCCESS;
    }

    return status;
}

/**
 * Initializes a lock.
 *
 * @retval RWL_SUCCESS  Success.
 * @retval RWL_EXIST    "create" is false and the semaphore set doesn't exist.
 *                      log_add() called.
 * @retval RWL_SYSTEM   System error. log_add() called.
 */
static srwl_Status initLock(
        const int create /**< [in] Whether to create the lock. If true, then
         any previous lock will be deleted. */,
        int key /**< [in] IPC key for the semaphore */,
        srwl_Lock** const lock /**< [out] address of pointer to lock */)
{
    srwl_Status status;
    srwl_Lock* lck;

    if (!isInitialized) {
        struct sembuf acquireLock;
        struct sembuf releaseLock;

        acquireLock.sem_num = SI_LOCK;
        acquireLock.sem_op = -1;
        acquireLock.sem_flg = SEM_UNDO; /* release lock upon exit */

        releaseLock.sem_num = SI_LOCK;
        releaseLock.sem_op = 1;
        releaseLock.sem_flg = SEM_UNDO; /* undo acquireLock undo */

        writeLockOps[0] = acquireLock;
        writeLockOps[1].sem_num = SI_NUM_READERS;
        writeLockOps[1].sem_op = 0;
        writeLockOps[1].sem_flg = 0;

        readLockOps[0] = acquireLock;
        readLockOps[1].sem_num = SI_NUM_READERS;
        readLockOps[1].sem_op = 1;
        readLockOps[1].sem_flg = SEM_UNDO; /* decrement #readers upon exit */

        shareOps[0] = releaseLock;

        writeUnlockOps[0] = releaseLock;

        readUnlockOps[0].sem_num = SI_NUM_READERS;
        readUnlockOps[0].sem_op = -1;
        readUnlockOps[0].sem_flg = SEM_UNDO; /* undo readLockOps[1] undo */

        {
            mode_t um = umask(0);

            umask(um);

            read_write = 0666 & ~um;
        }

        isInitialized = 1;
    }

    lck = (srwl_Lock*) malloc(sizeof(srwl_Lock));

    if (NULL == lck) {
        log_add_syserr("Couldn't allocate %lu bytes for lock",
                (unsigned long) sizeof(srwl_Lock));
        status = RWL_SYSTEM;
    }
    else {
        int semId;

        status = create ? createLock(key, &semId) : getLock(key, &semId);

        if (RWL_SUCCESS != status) {
            free(lck);
        }
        else {
            lck->semId = semId;
            lck->pid = getpid();
            lck->isValid = VALID_STRING;
            lck->numReadLocks = 0;
            lck->numWriteLocks = 0;
            *lock = lck;
        }
    } /* "lck" allocated */

    return status;
}

/**
 * Creates a semaphore-based, read/write lock. Any previous lock is deleted.
 *
 * @param key           [in] IPC key for the semaphore set
 * @param lock          [out] address of pointer to lock
 * @retval RWL_SUCCESS  Success
 * @retval RWL_SYSTEM   System error. See "errno". log_add() called.
 */
srwl_Status srwl_create(
        int key,
        srwl_Lock** const lock)
{
    return initLock(1, key, lock);
}

/**
 * Gets an existing semaphore-based, read/write lock.
 *
 * @param key   [in] IPC key for the lock
 * @param lock  [out] Address of the pointer to the lock
 * @retval RWL_SUCCESS  Success
 * @retval RWL_EXIST    The semaphore set doesn't exist. log_add() called.
 * @retval RWL_SYSTEM   System error. log_add() called.
 */
srwl_Status srwl_get(
        const key_t key,
        srwl_Lock** const lock)
{
    return initLock(0, key, lock);
}

/**
 * Unconditionally deletes a read/write lock -- including the semaphore on
 * which the lock is based. Lock can no longer be used after this function
 * returns.
 *
 * @retval RWL_SUCCESS  Success
 * @retval RWL_INVALID  The lock is invalid. log_add() called.
 * @retval RWL_SYSTEM   System error. log_add() called. The resulting state
 *                      of "*lock" is unspecified.
 */
srwl_Status srwl_delete(
        srwl_Lock* const lock /**< [in] pointer to the lock or NULL */)
{
    srwl_Status status = vet(lock);

    if (RWL_SUCCESS == status) {
        status = deleteSemSet(lock->semId);

        if (RWL_SUCCESS == status) {
            lock->isValid = NULL;
            free(lock);
        }
    }

    return status;
}

/**
 * Unconditionally deletes a read/write lock by IPC key. The Semaphore on
 * which the lock is based is deleted.
 *
 * @param key           The IPC key
 * @retval RWL_SUCCESS  Success
 * @retval RWL_EXIST    The key has no associated read/write lock
 * @retval RWL_SYSTEM   System error. log_add() called.
 */
srwl_Status srwl_deleteByKey(
        const key_t key)
{
    int status = semget(key, 0, read_write);

    if (-1 == status) {
        log_add_syserr("Couldn't get semaphore set");
        status = (ENOENT == errno) ? RWL_EXIST : RWL_SYSTEM;
    }
    else if (semctl(status, 0, IPC_RMID)) {
        log_add_syserr("Couldn't delete existing semaphore set %d", status);
        status = RWL_SYSTEM;
    }
    else {
        status = RWL_SUCCESS;
    }

    return status;
}

/**
 * Frees resources associated with a read/write lock. Does not delete the
 * semaphore on which the lock is based. The lock can not be used after this
 * function returns.
 *
 * @retval RWL_SUCCESS  Success or "lock" was NULL
 * @retval RWL_EXIST    The lock is locked. log_add() called.
 * @retval RWL_SYSTEM   System error. See "errno". log_add() called. The state
 *                      of "*lock" is unspecified.
 */
srwl_Status srwl_free(
        srwl_Lock* const lock /**< [in] pointer to the lock or NULL */)
{
    srwl_Status status = RWL_SUCCESS;

    if (NULL == lock) {
        status = RWL_SUCCESS;
    }
    else {
        status = vet(lock);

        if (RWL_SUCCESS == status) {
            if (0 != lock->numWriteLocks || 0 != lock->numReadLocks) {
                log_add("Lock is locked: semId=%d, numReadLocks=%u, "
                "numWriteLocks=%u", lock->semId, lock->numReadLocks,
                        lock->numWriteLocks);
                status = RWL_EXIST;
            }

            if (RWL_SUCCESS == status) {
                lock->isValid = NULL;
                free(lock);
            }
        }
    }

    return status;
}

/**
 * Locks a read/write lock for writing. Waits until the lock is available.
 * Reentrant.
 *
 * @retval RWL_SUCCESS  Success
 * @retval RWL_INVALID  Lock structure is invalid. log_add() called.
 * @retval RWL_EXIST    Lock is locked for reading and the current process is
 *                      the one that created it. log_add() called.
 * @retval RWL_SYSTEM   System error. See "errno". log_add() called. Resulting
 *                      state of the lock is unspecified.
 */
srwl_Status srwl_writeLock(
        srwl_Lock* const lock /**< [in/out] the lock to be locked */)
{
    srwl_Status status = vet(lock);

    if (RWL_SUCCESS == status) {
        if (0 < lock->numReadLocks) {
            log_add("Lock is locked for reading; semId=%d", lock->semId);
            status = RWL_EXIST;
        }
        else if (0 < lock->numWriteLocks) {
            lock->numWriteLocks++;
            status = RWL_SUCCESS;
        }
        else {
            if (semop(lock->semId, writeLockOps,
                    sizeof(writeLockOps) / sizeof(writeLockOps[0])) == -1) {
                log_add_syserr("Couldn't lock for writing: semId=%d",
                        lock->semId);
                status = RWL_SYSTEM;
            }
            else {
                lock->numWriteLocks = 1;
                status = RWL_SUCCESS;
            }
        }
    }

    return status;
}

/**
 * Locks a read/write lock for reading. Waits until the lock is available.
 * Reentrant.
 *
 * @retval RWL_SUCCESS  Success
 * @retval RWL_INVALID  Lock structure is invalid. log_add() called.
 * @retval RWL_EXIST    Lock is locked for writing and the current process is
 *                      the one that created it. log_add() called.
 * @retval RWL_SYSTEM   System error. See "errno". log_add() called. Resulting
 *                      state of the lock is unspecified.
 */
srwl_Status srwl_readLock(
        srwl_Lock* const lock /**< [in/out] the lock to be locked */)
{
    srwl_Status status = vet(lock);

    if (RWL_SUCCESS == status) {
        if (0 < lock->numWriteLocks) {
            log_add("Lock is locked for writing; semId=%d", lock->semId);
            status = RWL_EXIST;
        }
        else if (0 < lock->numReadLocks) {
            lock->numReadLocks++;
            status = RWL_SUCCESS;
        }
        else {
            /*
             * A read-lock is obtained in two steps because the semop(2)
             * specification doesn't indicate that the operations array is
             * executed sequentially.
             */
            if (semop(lock->semId, readLockOps,
                    sizeof(readLockOps) / sizeof(readLockOps[0])) == -1) {
                log_add_syserr("Couldn't lock for reading: semId=%d",
                        lock->semId);
                status = RWL_SYSTEM;
            }
            else if (semop(lock->semId, shareOps,
                    sizeof(shareOps) / sizeof(shareOps[0])) == -1) {
                log_add_syserr("Couldn't share read-lock: semId=%d",
                        lock->semId);
                status = RWL_SYSTEM;
            }
            else {
                lock->numReadLocks = 1;
                status = RWL_SUCCESS;
            }
        }
    }

    return status;
}

/**
 * Unlocks a read/write lock. Must be called as many times as the lock was
 * locked before the lock will be truly unlocked.
 *
 * @retval RWL_SUCCESS  Success
 * @retval RWL_INVALID  Lock structure is invalid. log_add() called.
 * @retval RWL_SYSTEM   System error. See "errno". log_add() called. Resulting
 *                      state of the lock is unspecified.
 */
srwl_Status srwl_unlock(
        srwl_Lock* const lock /**< [in/out] the lock to be unlocked */)
{
    srwl_Status status = vet(lock);

    if (RWL_SUCCESS == status) {
        if (1 < lock->numWriteLocks) {
            lock->numWriteLocks--;
        }
        else if (1 == lock->numWriteLocks) {
            if (semop(lock->semId, writeUnlockOps,
                    sizeof(writeUnlockOps) / sizeof(writeUnlockOps[0])) == -1) {
                log_add_syserr("Couldn't unlock write-lock: semId=%d",
                        lock->semId);
                status = RWL_SYSTEM;
            }
            else {
                lock->numWriteLocks--;
            }
        }
        else if (1 < lock->numReadLocks) {
            lock->numReadLocks--;
        }
        else if (1 == lock->numReadLocks) {
            if (semop(lock->semId, readUnlockOps,
                    sizeof(readUnlockOps) / sizeof(readUnlockOps[0])) == -1) {
                log_add_syserr("Couldn't unlock read-lock: semId=%d",
                        lock->semId);
                status = RWL_SYSTEM;
            }
            else {
                lock->numReadLocks--;
            }
        }
    }

    return status;
}
