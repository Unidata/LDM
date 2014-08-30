/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: file_id_map.c
 * @author: Steven R. Emmerson
 *
 * This file implements a singleton mapping from VCMTP file identifiers to LDM
 * data-product signatures (i.e., MD5 checksums). The same mapping is accessible
 * from multiple processes and persists between LDM sessions.
 *
 * The functions in this module are thread-compatible but not thread-safe.
 */

#include "config.h"

#include "ldm.h"
#include "log.h"
#include "file_id_map.h"

#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef _XOPEN_PATH_MAX
/* For some reason, the following isn't defined by gcc(1) 4.8.3 on Fedora 19 */
#   define _XOPEN_PATH_MAX 1024
#endif

/**
 * Description of the memory-mapped object:
 */
static const char*  MMO_DESC = "file-identifier map";
/**
 * Pathname of the file containing the memory-mapped object:
 */
static char         pathname[_XOPEN_PATH_MAX];
/**
 * File descriptor for file containing the memory-mapped object:
 */
static int          fd;
/**
 * Structure of the memory-mapped object:
 */
typedef struct {
} Header;
typedef struct {
    /*
     * Keep consonant with `fileSizeFromNumSigs()` and `numSigsFromFileSize()`
     */
    size_t      numSigs; ///< Number of signatures
    size_t      oldSig;  ///< Offset of oldest signature
    McastFileId fileId0; ///< File-Id of oldest signature
    signaturet  sigs[1]; ///< Data-products signatures
} Mmo;
static Mmo*         mmo;
/**
 * Locking structure:
 */
static struct flock lock;
/**
 * Size, in bytes, of a data-product signature:
 */
static const size_t SIG_SIZE = sizeof(signaturet);
/**
 * Signal mask for blocking most signals:
 */
static sigset_t     mostSignals;
/**
 * Maximum number of signatures in the map (i.e., capacity of the circular
 * buffer):
 */
static size_t       maxSigs;
/**
 * Size of the file in bytes.
 */
static size_t       fileSize;
/**
 * Signal mask for saved signals.
 */
static sigset_t     saveSet;

/**
 * Initializes some static members of this module.
 */
static void
initModule(void)
{
    (void)sigfillset(&mostSignals);
    (void)sigdelset(&mostSignals, SIGABRT);
    (void)sigdelset(&mostSignals, SIGFPE);
    (void)sigdelset(&mostSignals, SIGILL);
    (void)sigdelset(&mostSignals, SIGSEGV);
    (void)sigdelset(&mostSignals, SIGBUS);

    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = sizeof(*mmo);
}

/**
 * Block all but the most severe signals. This is typically done because a
 * critical section is about to be entered. A critical section is one that needs
 * to complete in order to avoid corruption. Should not be called again without
 * an intervening call to `restoreSigs()`.
 */
static inline void
blockSigs(void)
{
    (void)pthread_sigmask(SIG_BLOCK, &mostSignals, &saveSet);
}

/**
 * Restore the signal mask to what it was when `block()` was called. Typically,
 * this would be called upon exiting a critical section.
 *
 * @pre {`saveSet` has been set by a previous call to `blockSigs()`}
 */
static inline void
restoreSigs(void)
{
    (void)pthread_sigmask(SIG_SETMASK, &saveSet, NULL);
}

/**
 * Locks the file-identifier map. Blocks until the lock is acquired.
 *
 * @pre                 {`fd` is open and `pathname` is set}
 * @param  exclusive    Whether or not the lock should be exclusive.
 * @retval 0            Success.
 * @retval LDM7_SYSTEM  System error. `log_add()` called.
 */
static Ldm7Status
lockMap(
        const bool exclusive)
{
    lock.l_type = exclusive ? F_WRLCK : F_RDLCK;

    if (-1 == fcntl(fd, F_SETLKW, &lock)) {
        LOG_SERROR3("Couldn't lock %s (%s) for %s", MMO_DESC, pathname,
                exclusive ? "writing" : "reading");
        return LDM7_SYSTEM;
    }

    return 0;
}

/**
 * Unlocks the file-identifier map.
 *
 * @pre                    {`lockMap()` was previous called}
 * @retval    0            Success.
 * @retval    LDM7_SYSTEM  System failure. `log_add()` called.
 */
static Ldm7Status
unlockMap(void)
{
    lock.l_type = F_UNLCK;

    if (-1 == fcntl(fd, F_SETLKW, &lock)) {
        LOG_SERROR2("Couldn't unlock %s (%s)", MMO_DESC, pathname);
        return LDM7_SYSTEM;
    }

    return 0;
}

/**
 * Locks the file-identifier map for writing and blocks most signals. Calls
 * `blockSigs()`.
 *
 * @pre                 {`fd` is open and `pathname` is set}
 * @retval 0            Success.
 * @retval LDM7_SYSTEM  System error. `log_add()` called.
 */
static Ldm7Status
lockMapAndBlockSignals(void)
{
    int status = lockMap(true);

    if (0 == status)
        blockSigs();

    return status;
}

/**
 * Restores the signal mask to what it was when `blockSigs()` was called and
 * unblocks the file-identifier map.
 *
 * @pre                 {`lockMapAndBlockSigs` was previously called}
 * @retval 0            Success.
 * @retval LDM7_SYSTEM  System error. `log_add()` called.
 */
static inline Ldm7Status
restoreSignalsAndUnlockMap(void)
{
    restoreSigs();

    return unlockMap();
}

/**
 * Returns the minimum size of the file given the number of data-product
 * signatures.
 *
 * @param[in] numSigs  Number of signatures.
 * @return             Minimum size of the file in bytes.
 */
static inline size_t
fileSizeFromNumSigs(
        const size_t numSigs)
{
    /* The following accommodates `numSigs == 0` */
    return sizeof(*mmo) + SIG_SIZE*numSigs - SIG_SIZE;
}

/**
 * Returns the maximum number of data-product signatures that can be stored in
 * a file of a given size.
 *
 * @param[in] size  Size of the file in bytes.
 * @return          Maximum number of data-product signatures that can be stored
 *                  in the file.
 */
static inline size_t
numSigsFromFileSize(
        const size_t size)
{
    return (size < sizeof(*mmo))
            ? 0
            : 1 + (size - sizeof(*mmo)) / SIG_SIZE;
}

/**
 * Sets the size, in bytes, of the open file.
 *
 * @pre                     {`fd` is open and `pathname` is set}
 * @retval     0            Success. `fileSize` is set.
 * @retval     LDM7_SYSTEM  System error. `log_add()` called.
 */
static Ldm7Status
fileSizeFromFile(void)
{
    struct stat  statBuf;
    int          status = fstat(fd, &statBuf);

    if (status) {
        LOG_SERROR2("Couldn't get size of %s (\"%s\")", MMO_DESC, pathname);
        return LDM7_SYSTEM;
    }

    fileSize = statBuf.st_size;

    return 0;
}

/**
 * Memory-maps the file containing the file-identifier map.
 *
 * @pre                 {`fd` is open and `pathname` is set}
 * @retval 0            Success.
 * @retval LDM7_SYSTEM  System error. `log_add()` called.
 */
static Ldm7Status
mapMap(void)
{
    void* const ptr = mmap(NULL, fileSize, PROT_READ|PROT_WRITE, MAP_SHARED,
            fd, 0);

    if (MAP_FAILED == ptr) {
        LOG_SERROR2("Couldn't memory-map %s (\"%s\")", MMO_DESC, pathname);
        return LDM7_SYSTEM;
    }

    mmo = ptr;

    return 0;
}

/**
 * Un-memory-maps the file containing the file-identifier map.
 *
 * @pre                 {`fd` is open and `pathname` is set}
 * @retval 0            Success.
 * @retval LDM7_SYSTEM  System error. `log_add()` called.
 * @return
 */
static Ldm7Status
unmapMap(void)
{
    if (munmap(mmo, fileSize)) {
        LOG_SERROR2("Couldn't un-memory-map %s (\"%s\")", MMO_DESC, pathname);
        return LDM7_SYSTEM;
    }

    return 0;
}

/**
 * Sets the size of the file containing the file-identifier map.
 *
 * @param[in] size         The new size for the file in bytes.
 * @retval    0            Success.
 * @retval    LDM7_SYSTEM  System error. `log_add()` called.
 */
static Ldm7Status
truncateMap(
        const size_t size)
{
    if (ftruncate(fd, size)) {
        LOG_SERROR3("Couldn't set size of %s (\"%s\") to %lu bytes",
                MMO_DESC, pathname, (unsigned long)size);
        return LDM7_SYSTEM;
    }

    fileSize = size;

    return 0;
}

/**
 * Consolidates the contents of the file-identifier map in the unadjusted file
 * into one contiguous segment.
 *
 * @param[in] max          Maximum number of signatures in the unadjusted file.
 * @retval    0            Success.
 * @retval    LDM7_SYSTEM  System error. `log_add()` called.
 */
static Ldm7Status
consolidateMap(
        const size_t max)
{
    /*
     * In general, the signatures in the (unadjusted) circular buffer will be in
     * two contiguous segments: a "new" segment and an "old" segment. The "new"
     * segment comprises the more recent signatures from the beginning of the
     * buffer to just before `oldSeg`; the "old" segment comprises the older
     * signatures from `oldSeg` to the end of the (unadjusted) buffer. The goal
     * of this function is to consolidate the two segments into one contiguous
     * segment with the oldest signature at the beginning of the buffer.
     */
    int          status;
    const size_t newCount = mmo->oldSig;
    const size_t oldCount = mmo->numSigs < max
            ? mmo->numSigs // buffer isn't full
            : mmo->numSigs - mmo->oldSig;
    size_t       smallBytes, bigBytes;
    void         *smallSeg, *bigSeg, *newBigSeg, *newSmallSeg;

    if (newCount >= oldCount) {
        bigBytes = newCount * SIG_SIZE;
        smallBytes = oldCount * SIG_SIZE;
        smallSeg = mmo->sigs + newCount;
        bigSeg = mmo->sigs;
        newBigSeg = mmo->sigs + oldCount;
        newSmallSeg = mmo->sigs;
    }
    else {
        bigBytes = oldCount * SIG_SIZE;
        smallBytes = newCount * SIG_SIZE;
        smallSeg = mmo->sigs;
        bigSeg = mmo->sigs + newCount;
        newBigSeg = mmo->sigs;
        newSmallSeg = mmo->sigs + oldCount;
    }

    signaturet* tmpSigs = LOG_MALLOC(smallBytes, "temporary signatures buffer");

    if (NULL == tmpSigs) {
        status = LDM7_SYSTEM;
    }
    else {
        sigset_t saveSet;

        (void)memcpy(tmpSigs, smallSeg, smallBytes); // copy segment
        (void)memmove(newBigSeg, bigSeg, bigBytes); // shift other segment
        (void)memcpy(newSmallSeg, tmpSigs, smallBytes); // restore segment
        mmo->oldSig = 0;

        free(tmpSigs);
        maxSigs = max;
        status = 0;
    }

    return status;
}

/**
 * Shifts the signatures in a (consolidated) file-identifier map towards lower
 * offsets, reducing the number of signatures to a given maximum -- adjusting
 * the map parameters as necessary. Does nothing if the given maximum is greater
 * than or equal to the actual number of signatures.
 *
 * @param[in] max      Maximum number of signatures.
 * @retval    0        Success.
 */
static Ldm7Status
shiftMapDown(
        const size_t max)
{
    if (max < mmo->numSigs) {
        const size_t delta = mmo->numSigs - max;
        sigset_t     saveSet;

        (void)memmove(mmo->sigs, mmo->sigs + delta, max*SIG_SIZE);
        mmo->numSigs = max;
        mmo->fileId0 += delta;
    }

    maxSigs = max;

    return 0;
}

/**
 * Expands the size of the file containing the file-identifier map and
 * memory-maps it.
 *
 * @param[in] newSize      New size for the file in bytes. Must be greater than
 *                         the current size of the file.
 * @retval    0            Success.
 * @retval    LDM7_SYSTEM  System error. `log_add()` called. The state of the
 *                         file is unspecified.
 */
static Ldm7Status
expandMapAndMap(
        const size_t newSize)
{
    const size_t oldSize = fileSize;
    int          status = truncateMap(newSize);

    if (0 == status) {
        if (0 == (status = mapMap())) {
            consolidateMap(numSigsFromFileSize(oldSize));
        } // file is memory-mapped
    } // file size was increased

    return status;
}

/**
 * Contracts the size of the file containing the file-identifier map and
 * memory-maps it.
 *
 * @param[in] newSize      New size for the file in bytes. Must be less than
 *                         the current size of the file.
 * @retval    0            Success.
 * @retval    LDM7_SYSTEM  System error. `log_add()` called. The state of the
 *                         file is unspecified.
 */
static Ldm7Status
contractMapAndMap(
        const size_t newSize)
{
    /*
     * The file must be memory-mapped before it can be consolidated; thus, the
     * steps are: map, consolidate, shift, unmap, decrease size, and (re)map.
     */
    int status;

    (status = mapMap()) ||
    (status = consolidateMap(maxSigs)) ||
    (status = shiftMapDown(numSigsFromFileSize(newSize))) ||
    (status = unmapMap()) ||
    (status = truncateMap(newSize)) ||
    (status = mapMap());

    return status;
}

/**
 * Adjusts, if necessary, the size of the previously-existing file containing
 * the file-identifier map and memory-maps it.
 *
 * @param[in]  maxSigs      Maximum number of data-product signatures.
 * @retval     0            Success.
 * @retval     LDM7_SYSTEM  System error. `log_add()` called. The state of the
 *                          file is unspecified.
 */
static Ldm7Status
vetMapSizeAndMap(
        const size_t  maxSigs)
{
    const size_t newSize = fileSizeFromNumSigs(maxSigs);
    int          status = (newSize > fileSize)
        ? expandMapAndMap(newSize)
        : (newSize < fileSize)
            ? contractMapAndMap(newSize)
            : mapMap();

    return status;
}

/**
 * Clears the file-identifier map, which must be open for writing and locked.
 *
 * @pre {`lockMapAndBlockSigs()` has been called}
 */
static void
clearMap(void)
{
    mmo->numSigs = 0;
    mmo->oldSig = 0;
}

/**
 * Initializes and memory-maps the newly-created file that will contain the
 * file-identifier map for reading and writing.
 *
 * @param[in]  max          Maximum number of data-product signatures for the
 *                          map.
 * @retval     0            Success.
 * @retval     LDM7_SYSTEM  System error. `log_add()` called.
 */
static Ldm7Status
initNewMapAndMap(
        const size_t max)
{
    const size_t size = fileSizeFromNumSigs(max);
    int          status = truncateMap(size);

    if (0 == status) {
        fileSize = size;
        status = mapMap();

        if (0 == status) {
            clearMap();
            maxSigs = max;
        }
    }

    return status;
}

/**
 * Initializes and memory-maps the file containing the file-identifier map for
 * reading and writing.
 *
 * @param[in]  maxSigs      Maximum number of data-product signatures for the
 *                          map.
 * @param[in]  isNew        Whether or not the file was just created.
 * @retval     0            Success.
 * @retval     LDM7_SYSTEM  System error. `log_add()` called. The state of the
 *                          file is unspecified.
 */
static Ldm7Status
initMapAndMap(
        const size_t maxSigs,
        const bool   isNew)
{
    return isNew
        ? initNewMapAndMap(maxSigs)
        : vetMapSizeAndMap(maxSigs);
}

/**
 * Opens the file containing the file-identifier map for reading and writing.
 *
 * @param[in]  path         Pathname of the file. Caller may free.
 * @param[out] isNew        Whether or not the file was created.
 * @retval     0            Success. `pathname`, `fd`, and `*isNew` are set.
 * @retval     LDM7_SYSTEM  System error. `log_add()` called.
 */
static Ldm7Status
openMapForWriting(
        const char* const restrict path,
        bool* const restrict       isNew)
{
    int status;

    fd = open(path, O_RDWR|O_CREAT, 0666);

    if (-1 == fd) {
        LOG_SERROR2("Couldn't open %s (\"%s\")", MMO_DESC, path);
        status = LDM7_SYSTEM;
    }
    else {
        strncpy(pathname, path, sizeof(pathname))[sizeof(pathname)-1] = 0;

        if (0 == (status = fileSizeFromFile()))
            *isNew = 0 == fileSize;
    }

    return status;
}

/**
 * Clears the file-identifier map if the given file-identifier is not the
 * expected one.
 *
 * @pre               {`lockMapAndBlockSigs()` has been called}
 * @param[in] fileId  File-identifier.
 */
static inline void
clearMapIfUnexpected(
        const McastFileId fileId)
{
    if (mmo->numSigs && (fileId != mmo->fileId0 + mmo->numSigs))
        clearMap();
}

/**
 * Initializes this module for read and write access to a  file-identifier map
 * contained in a file. Creates the file (with an empty map) if it doesn't
 * exist.
 *
 * @param[in] pathname     Pathname of the file. Caller may free.
 * @param[in] maxSigs      Maximum number of data-product signatures. Must be
 *                         positive.
 * @retval    0            Success.
 * @retval    LDM7_INVAL   Maximum number of signatures isn't positive.
 *                         `log_add()` called. The file wasn't opened or
 *                         created.
 * @retval    LDM7_SYSTEM  System error. `log_add()` called. The state of the
 *                         file is unspecified.
 */
Ldm7Status
fim_openForWriting(
        const char* const pathname,
        const size_t      maxSigs)
{
    int status;

    if (0 == maxSigs) {
        LOG_ADD0("Maximum number of signatures must be positive");
        status = LDM7_INVAL;
    }
    else {
        initModule();

        bool isNew;
        int  status = openMapForWriting(pathname, &isNew);

        if (0 == status) {
            if ((status = initMapAndMap(maxSigs, isNew))) {
                (void)close(fd);

                if (isNew)
                    (void)unlink(pathname);
            }
        } // `fd` open
    }

    return status;
}

/**
 * Closes the file-identifier map.
 *
 * @retval 0            Success.
 * @retval LDM7_SYSTEM  SYSTEM error. `log_add()` called. The state of the map
 *                      is unspecified.
 */
Ldm7Status
fim_close(void)
{
    int status = unmapMap();

    if (0 == status) {
        if (close(fd)) {
            LOG_SERROR1("Couldn't close file-descriptor of %s", MMO_DESC);
            status = LDM7_SYSTEM;
        }
    }

    return status;
}

/**
 * Adds a mapping from a file-identifier to a data-product signature to the
 * file-identifier map. Clears the map first if the given file-identifier is
 * not one greater than the previous file-identifier.
 *
 * @param[in] fileId       File-identifier.
 * @param[in] sig          Data-product signature.
 * @retval    0            Success.
 * @retval    LDM7_SYSTEM  System error. `log_add()` called.
 */
Ldm7Status
fim_put(
        const McastFileId       fileId,
        const signaturet* const sig)
{
    int status = lockMapAndBlockSignals();

    if (0 == status) {
        clearMapIfUnexpected(fileId);

        (void)memcpy(mmo->sigs + (mmo->oldSig + mmo->numSigs) % maxSigs, sig,
                SIG_SIZE);

        if (mmo->numSigs < maxSigs) {
            if (0 == mmo->numSigs++)
                mmo->fileId0 = fileId;
        }
        else {
            mmo->oldSig = (mmo->oldSig + 1) % maxSigs;
            mmo->fileId0++;
        }

        status = restoreSignalsAndUnlockMap();
    }

    return status;
}

#if 0

/**
 * Locks the map. Idempotent. Blocks until the lock is acquired or an error
 * occurs.
 *
 * @param[in] exclusive    Lock for exclusive access?
 * @retval    0            Success.
 * @retval    LDM7_SYSTEM  System failure. `log_add()` called.
 */
Ldm7Status
fim_lock(
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
fim_put(
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
 * @param[out] pid          Associated feed-type.
 * @retval     0            Success. `*pid` is set.
 * @retval     LDM7_NOENT   No PID associated with feed-type.
 */
Ldm7Status
fim_getPid(
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
fim_unlock(void)
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
fim_removePid(
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
fim_destroy(void)
{
    if (close(fileDes)) {
        LOG_SERROR1("Couldn't close %s", SMO_NAME);
        return LDM7_SYSTEM;
    }

    (void)shm_unlink(SMO_FILENAME);

    return 0;
}

#endif
