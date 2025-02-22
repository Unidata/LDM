/* DO NOT EDIT THIS FILE. It was created by extractDecls */
/*
 * Copyright 2012 University Corporation for Atmospheric Research. All rights
 * reserved.
 *
 * See file COPYRIGHT in the top-level source-directory for legal conditions.
 */

#ifndef SEM_READ_WRITE_LOCK
#define SEM_READ_WRITE_LOCK

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Opaque type for this module.
 */
typedef struct srwl_Lock srwl_Lock;

/**
 * Return codes
 */
typedef enum {
    RWL_SUCCESS = 0,   /**< Success. Will always be zero. */
    RWL_INVALID,        /**< Lock structure is invalid */
    RWL_EXIST,          /**< Something exists that shouldn't or vice versa */
    RWL_SYSTEM          /**< System error. See "errno". */
} srwl_Status;

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
        srwl_Lock** const lock);

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
        srwl_Lock** const lock);

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
        srwl_Lock* const lock /**< [in] pointer to the lock or NULL */);

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
        const key_t key);

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
        srwl_Lock* const lock /**< [in] pointer to the lock or NULL */);

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
        srwl_Lock* const lock /**< [in/out] the lock to be locked */);

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
        srwl_Lock* const lock /**< [in/out] the lock to be locked */);

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
        srwl_Lock* const lock /**< [in/out] the lock to be unlocked */);

#ifdef __cplusplus
}
#endif

#endif
