/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: prod_index_map.c
 * @author: Steven R. Emmerson
 *
 * This file implements a singleton mapping from product indexes to LDM
 * data-product signatures (i.e., MD5 checksums). The same mapping is accessible
 * from multiple processes and persists between LDM sessions.
 *
 * The functions in this module are thread-compatible but not thread-safe.
 */

#include "config.h"

#include "ldm.h"
#include "ldmprint.h"
#include "log.h"
#include "prod_index_map.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef _XOPEN_PATH_MAX
/* For some reason, the following isn't defined by gcc(1) 4.8.3 on Fedora 19 */
#   define _XOPEN_PATH_MAX 1024 // value mandated by XPG6; includes NUL
#endif

#define MIN(a,b) ((a) <= (b) ? (a) : (b))

/**
 * Description of the memory-mapped object:
 */
static const char*  MMO_DESC = "product-index map";
/**
 * Pathname of the file containing the memory-mapped object:
 */
static char         pathname[PATH_MAX];
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
    size_t        numSigs;  ///< Number of signatures
    size_t        oldSig;   ///< Offset of oldest signature
    FmtpProdIndex oldIProd; ///< Product-index of oldest signature
    signaturet    sigs[1];  ///< Data-product signatures
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
 * Whether or not the product-index map is open.
 */
static volatile bool isOpen;
/**
 * Whether or not the product-index map is open for writing:
 */
static volatile bool forWriting;

/**
 * Ensures that the product-index map is in the correct open state.
 *
 * @param[in] shouldBeOpen  Whether or not the product-index map should be open.
 * @retval    0             The product-index map is in the correct state.
 * @retval    LDM7_LOGIC    The product-index map is in the incorrect state.
 *                          `log_add()` called.
 */
static int
ensureProperState(const bool shouldBeOpen)
{
    int status;

    if (shouldBeOpen == isOpen) {
        status = 0;
    }
    else {
        log_add("Product-index map is %s", isOpen ? "open" : "not open");
        status = LDM7_LOGIC;
    }

    return status;
}

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
 * Returns the pathname of the product-index map for a given feedtype.
 *
 * @param[out] buf       The buffer into which to format the pathname.
 * @paramin]   size      The size of the buffer in bytes.
 * @param[in]  dirname   Pathname of the parent directory or NULL, in which case
 *                       the current working directory is used.
 * @param[in]  feedtype  The feedtype.
 * @retval    -1         Error. `log_add()` called.
 * @return               The number of bytes that would be written to the buffer
 *                       had it been sufficiently large excluding the
 *                       terminating null byte. If greater than or equal to
 *                       `size`, then the buffer is not NUL-terminated.
 */
static int
pim_getPathname(
        char*             buf,
        size_t            size,
        const char* const dirname,
        const feedtypet   feedtype)
{
    char  feedStr[256];
    int   status = sprint_feedtypet(feedStr, sizeof(feedStr), feedtype);
    if (status < 0) {
        log_add("Couldn't format feedtype %#lx", (unsigned long)feedtype);
    }
    else {
        status = snprintf(buf, size, "%s/%s.pim", dirname ? dirname : ".",
                feedStr);
        if (status < 0)
            log_add("Couldn't construct pathname of product-index map: "
                    "bufsize=%zu, dirname=\"%s\", feedtype=%#lx", size,
                    dirname, (unsigned long)feedtype);
    }

    return status;
}

/**
 * Locks the product-index map. Blocks until the lock is acquired.
 *
 * @pre                 {`fd` is open and `pathname` is set}
 * @param  exclusive    Whether or not the lock should be exclusive.
 * @retval 0            Success.
 * @retval LDM7_SYSTEM  System error. `log_add()` called.
 */
static Ldm7Status
lockMap(const bool exclusive)
{
    lock.l_type = exclusive ? F_WRLCK : F_RDLCK;

    if (-1 == fcntl(fd, F_SETLKW, &lock)) {
        log_add_syserr("Couldn't lock %s (%s) for %s", MMO_DESC, pathname,
                exclusive ? "writing" : "reading");
        return LDM7_SYSTEM;
    }

    return 0;
}

/**
 * Unlocks the product-index map.
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
        log_add_syserr("Couldn't unlock %s (%s)", MMO_DESC, pathname);
        return LDM7_SYSTEM;
    }

    return 0;
}

/**
 * Locks the product-index map for writing and blocks most signals. Calls
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
 * unblocks the product-index map.
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
maxSigsFromFileSize(
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
        log_add_syserr("Couldn't get size of %s (\"%s\")", MMO_DESC, pathname);
        return LDM7_SYSTEM;
    }

    fileSize = statBuf.st_size;

    return 0;
}

/**
 * Memory-maps the file containing the product-index map.
 *
 * @pre                 {`forWriting`, `fd`, and `pathname` are set}
 * @retval 0            Success.
 * @retval LDM7_SYSTEM  System error. `log_add()` called.
 */
static Ldm7Status
mapMap(void)
{
    void* const ptr = mmap(NULL, fileSize,
            forWriting ? PROT_READ|PROT_WRITE : PROT_READ, MAP_SHARED, fd, 0);

    if (MAP_FAILED == ptr) {
        log_add_syserr("Couldn't memory-map %s (\"%s\")", MMO_DESC, pathname);
        return LDM7_SYSTEM;
    }

    mmo = ptr;

    return 0;
}

/**
 * Un-memory-maps the file containing the product-index map.
 *
 * @pre                 {`fd` and `pathname` are set}
 * @retval 0            Success.
 * @retval LDM7_SYSTEM  System error. `log_add()` called.
 * @return
 */
static Ldm7Status
unmapMap(void)
{
    if (munmap(mmo, fileSize)) {
        log_add_syserr("Couldn't un-memory-map %s (\"%s\")", MMO_DESC, pathname);
        return LDM7_SYSTEM;
    }

    return 0;
}

/**
 * Sets the size of the file containing the product-index map.
 *
 * @param[in] size         The new size for the file in bytes.
 * @retval    0            Success.
 * @retval    LDM7_SYSTEM  System error. `log_add()` called.
 */
static Ldm7Status
truncateMap(const size_t size)
{
    if (ftruncate(fd, size)) {
        log_add_syserr("Couldn't set size of %s (\"%s\") to %lu bytes",
                MMO_DESC, pathname, (unsigned long)size);
        return LDM7_SYSTEM;
    }

    fileSize = size;

    return 0;
}

/**
 * Consolidates the contents of the product-index map into one contiguous
 * segment.
 *
 * @param[in] max          Maximum number of signatures in the file.
 * @retval    0            Success.
 * @retval    LDM7_SYSTEM  System error. `log_add()` called.
 */
static Ldm7Status
consolidateMap(const size_t max)
{
    /*
     * In general, the signatures in the circular buffer will be in two
     * contiguous segments: a "new" segment and an "old" segment. The "new"
     * segment comprises the more recent signatures from the beginning of the
     * buffer to just before `oldSeg`; the "old" segment comprises the older
     * signatures from `oldSeg` to the end of the buffer. This function
     * consolidates the two segments into one contiguous segment with the oldest
     * signature at the beginning of the buffer.
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

    signaturet* tmpSigs = log_malloc(smallBytes, "temporary signatures buffer");

    if (NULL == tmpSigs) {
        status = LDM7_SYSTEM;
    }
    else {
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
 * Shifts the signatures in a (consolidated) product-index map towards lower
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

        (void)memmove(mmo->sigs, mmo->sigs + delta, max*SIG_SIZE);
        mmo->numSigs = max;
        mmo->oldIProd += delta;
    }

    maxSigs = max;

    return 0;
}

/**
 * Expands the size of the file containing the product-index map and
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
            consolidateMap(maxSigsFromFileSize(oldSize));
        } // file is memory-mapped
    } // file size was increased

    return status;
}

/**
 * Contracts the size of the file containing the product-index map and
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
    int status = mapMap();

    if (status == 0) {
        status = consolidateMap(maxSigs);

        if (status == 0) {
            status = shiftMapDown(maxSigsFromFileSize(newSize));

            if (status == 0) {
                status = unmapMap();

                if (status == 0) {
                    status = truncateMap(newSize);

                    if (status == 0)
                        status = mapMap();
                }
            }
        }
    }

    return status;
}

/**
 * Adjusts, if necessary, the size of the previously-existing file containing
 * the product-index map and memory-maps it.
 *
 * @param[in]  maxNumSigs   Maximum number of data-product signatures.
 * @retval     0            Success.
 * @retval     LDM7_SYSTEM  System error. `log_add()` called. The state of the
 *                          file is unspecified.
 */
static Ldm7Status
vetMapSizeAndMap(const size_t maxNumSigs)
{
    const size_t newSize = fileSizeFromNumSigs(maxNumSigs);
    int          status = (newSize > fileSize)
        ? expandMapAndMap(newSize)
        : (newSize < fileSize)
            ? contractMapAndMap(newSize)
            : mapMap();

    maxSigs = maxNumSigs;

    return status;
}

/**
 * Clears the product-index map, which must be open for writing and locked.
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
 * product-index map for reading and writing.
 *
 * @param[in]  max          Maximum number of data-product signatures for the
 *                          map.
 * @retval     0            Success.
 * @retval     LDM7_SYSTEM  System error. `log_add()` called.
 */
static Ldm7Status
initNewMapAndMap(const size_t max)
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
 * Initializes and memory-maps the file containing the product-index map for
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
 * Opens the file associated with a product-index map.
 *
 * @param[in] dirname      Pathname of parent directory or NULL, in which case
 *                         the current working directory is used.
 * @param[in] feedtype     Feedtype of map.
 * @return    0            Success. `fd` and `pathname` are set.
 * @return    LDM7_SYSTEM  System error. `log_add()` called.
 */
static Ldm7Status
openMap(const char* const restrict dirname,
        const feedtypet            feedtype)
{
    int  status = pim_getPathname(pathname, sizeof(pathname), dirname,
            feedtype);
    if (status < 0 || status >= sizeof(pathname)) {
        status = LDM7_SYSTEM;
    }
    else {
        fd = open(pathname, forWriting ? O_RDWR|O_CREAT : O_RDONLY, 0666);
        if (-1 == fd) {
            log_add_syserr("Couldn't open %s (\"%s\")", MMO_DESC, pathname);
            status = LDM7_SYSTEM;
        }
        else {
            status = ensure_close_on_exec(fd);
            if (status) {
                log_add("Couldn't set FD_CLOEXEC flag on file \"%s\"", pathname);
                (void)close(fd);
                status = LDM7_SYSTEM;
            }
        }
    }

    return status;
}

/**
 * Opens the file containing the product-index map for reading and writing.
 *
 * @param[in]  dirname      Pathname of the parent directory or NULL, in which
 *                          case the current working directory is used. Caller
 *                          may free.
 * @param[in]  feedtype     Feedtype of the map.
 * @param[out] isNew        Whether or not the file was created.
 * @retval     0            Success. `forWriting`, `pathname`, `fd`, and
 *                          `*isNew` are set.
 * @retval     LDM7_SYSTEM  System error. `log_add()` called.
 */
static Ldm7Status
openMapForWriting(
        const char* const restrict dirname,
        const feedtypet            feedtype,
        bool* const restrict       isNew)
{
    forWriting = true;

    int status = openMap(dirname, feedtype);

    if (0 == status) {
        if (0 == (status = fileSizeFromFile()))
            *isNew = 0 == fileSize;
    }

    return status;
}

/**
 * Opens the file containing the product-index map for reading.
 *
 * @param[in] dirname      Pathname of the parent directory or NULL, in which
 *                         case the current working directory is used. Caller
 *                         may free.
 * @param[in] feedtype     Feedtype of the map.
 * @retval    0            Success. `forWriting`, `pathname`, `fd`, `fileSize`,
 *                         and `maxSigs` are set.
 * @retval    LDM7_SYSTEM  System error. `log_add()` called.
 */
static Ldm7Status
openMapForReading(
        const char* const dirname,
        const feedtypet   feedtype)
{
    forWriting = false;

    int status = openMap(dirname, feedtype);

    if (0 == status)
        if (0 == (status = fileSizeFromFile()))
            maxSigs = maxSigsFromFileSize(fileSize);

    return status;
}

/**
 * Clears the product-index map if the given product-index is not the
 * expected one.
 *
 * @pre               {`lockMapAndBlockSigs()` has been called}
 * @param[in] iProd   Product-index.
 */
static inline void
clearMapIfUnexpected(
        const FmtpProdIndex iProd)
{
    if (mmo->numSigs && (iProd != mmo->oldIProd + mmo->numSigs))
        clearMap();
}

/******************************************************************************
 * Public API:
 ******************************************************************************/

/**
 * Opens the product-index map for writing. Creates the associated file (with
 * an empty map) if it doesn't exist. A process should call this function at
 * most once. The associated file-descriptor will have the `FD_CLOEXEC` flag
 * set.
 *
 * @param[in] dirname      Pathname of the parent directory or NULL, in which
 *                         case the current working directory is used. Caller
 *                         may free.
 * @param[in] feedtype     Feedtype of the map.
 * @param[in] maxNumSigs   Maximum number of data-product signatures. Must be
 *                         positive.
 * @retval    0            Success.
 * @retval    LDM7_LOGIC   The product-index map is already open. `log_add()`
 *                         called.
 * @retval    LDM7_INVAL   Maximum number of signatures isn't positive.
 *                         `log_add()` called. The file wasn't opened or
 *                         created.
 * @retval    LDM7_SYSTEM  System error. `log_add()` called. The state of the
 *                         file is unspecified.
 */
Ldm7Status
pim_writeOpen(
        const char* const dirname,
        const feedtypet   feedtype,
        const size_t      maxNumSigs)
{
    int status = ensureProperState(false);

    if (status == 0) {
        if (0 == maxNumSigs) {
            log_add("Maximum number of signatures must be positive");
            status = LDM7_INVAL;
        }
        else {
            bool isNew;

            initModule();
            status = openMapForWriting(dirname, feedtype, &isNew);

            if (0 == status) {
                if ((status = initMapAndMap(maxNumSigs, isNew))) {
                    (void)close(fd);

                    if (isNew)
                        (void)unlink(pathname);
                }
                else {
                    isOpen = true;
					log_debug("File open: maxSigs=%zu, numSigs=%zu, "
							"oldSigOffset=%zu, oldProdIndex=%zu", maxSigs,
							mmo->numSigs, mmo->oldSig, (size_t)mmo->oldIProd);
                }
            } // `fd` open
        } // `maxSigs > 0`
    }

    return status;
}

/**
 * Opens the product-index map for reading. A process should call this
 * function at most once. The associated file-descriptor will have the
 * `FD_CLOEXEC` flag set.
 *
 * @param[in] dirname      Pathname of the parent directory or NULL, in which
 *                         case the current working directory is used. Caller
 *                         may free.
 * @param[in] feedtype     Feedtype of the map.
 * @retval    0            Success.
 * @retval    LDM7_LOGIC   The product-index map is already open. `log_add()`
 *                         called.
 * @retval    LDM7_SYSTEM  System error. `log_add()` called. The state of the
 *                         file is unspecified.
 */
Ldm7Status
pim_readOpen(
        const char* const dirname,
        const feedtypet   feedtype)
{
    int status = ensureProperState(false);

    if (status == 0) {
        initModule();
        status = openMapForReading(dirname, feedtype);

        if (0 == status) {
            if ((status = mapMap())) {
                (void)close(fd);
            }
            else {
                isOpen = true;
                log_debug("File open: maxSigs=%zu, numSigs=%zu, "
                		"oldSigOffset=%zu, oldProdIndex=%zu", maxSigs,
						mmo->numSigs, mmo->oldSig, (size_t)mmo->oldIProd);
            }
        } // `fd` open
    }

    return status;
}

/**
 * Closes the product-index map.
 *
 * @retval 0            Success.
 * @retval LDM7_LOGIC   The product-index map is not open. `log_add()` called.
 * @retval LDM7_SYSTEM  SYSTEM error. `log_add()` called. The state of the map
 *                      is unspecified.
 */
Ldm7Status
pim_close(void)
{
    int status = ensureProperState(true);

    if (status == 0) {
        status = unmapMap();

        if (0 == status) {
            if (close(fd)) {
                log_add_syserr("Couldn't close file-descriptor of %s", MMO_DESC);
                status = LDM7_SYSTEM;
            }
            else {
                isOpen = false;
            }
        }
    }

    return status;
}

/**
 * Deletes the file associated with a product-index map. Any instance of this
 * module that has the file open will continue to work until its `pim_close()`
 * is called.
 *
 * @param[in] dirname    Pathname of the parent directory or NULL, in which case
 *                       the current working directory is used.
 * @param[in] feedtype   The feedtype.
 * @retval 0             Success. The associated file doesn't exist or has been
 *                       removed.
 * @retval LDM7_SYSTEM   System error. `log_add()` called.
 */
Ldm7Status
pim_delete(
        const char* const dirname,
        const feedtypet   feedtype)
{
    char pathname[_XOPEN_PATH_MAX];
    int status = pim_getPathname(pathname, sizeof(pathname), dirname, feedtype);
    if (status < 0 || status >= sizeof(pathname)) {
        status = LDM7_SYSTEM;
    }
    else {
        status = unlink(pathname);
        if (status && errno != ENOENT) {
            log_add_syserr("Couldn't unlink file \"%s\"", pathname);
            status = LDM7_SYSTEM;
        }
        else {
            status = 0;
        }
    }
    return status;
}

/**
 * Adds a mapping from a product-index to a data-product signature to the
 * product-index map. Clears the map first if the given product-index is
 * not one greater than the previous product-index.
 *
 * @param[in] iProd        Product-index.
 * @param[in] sig          Data-product signature.
 * @retval    0            Success.
 * @retval    LDM7_LOGIC   The product-index map is not open. `log_add()`
 *                         called.
 * @retval    LDM7_SYSTEM  System error. `log_add()` called.
 */
Ldm7Status
pim_put(
        const FmtpProdIndex     iProd,
        const signaturet* const sig)
{
    int status = ensureProperState(true);

    if (status == 0) {
        status = lockMapAndBlockSignals();

        if (0 == status) {
            clearMapIfUnexpected(iProd);

            (void)memcpy(mmo->sigs + (mmo->oldSig + mmo->numSigs) % maxSigs,
                    sig, SIG_SIZE);
            char buf[2*sizeof(signaturet)+1];
            status = sprint_signaturet(buf, sizeof(buf), *sig);
            log_assert(status > 0);
            log_debug("Added: iProd=%lu, sig=%s", (unsigned long)iProd, buf);

            if (mmo->numSigs < maxSigs) {
                if (0 == mmo->numSigs++)
                    mmo->oldIProd = iProd;
            }
            else {
                mmo->oldSig = (mmo->oldSig + 1) % maxSigs;
                mmo->oldIProd++;
            }

            status = restoreSignalsAndUnlockMap();
        }
    }

    return status;
}

/**
 * Returns the data-product signature to which a product-index maps.
 *
 * @param[in]  iProd        Product index.
 * @param[out] sig          Data-product signature mapped-to by `iProd`.
 * @return     0            Success.
 * @retval     LDM7_LOGIC   The product-index map is not open. `log_add()`
 *                          called.
 * @retval     LDM7_NOENT   Product-index is unknown.
 * @retval     LDM7_SYSTEM  System error. `log_add()` called.
 */
Ldm7Status
pim_get(const FmtpProdIndex iProd,
        signaturet* const   sig)
{
    int status = ensureProperState(true);

    if (status == 0) {
        status = lockMap(false); // shared lock

        if (0 == status) {
            const FmtpProdIndex delta = iProd - mmo->oldIProd;

            if (delta >= mmo->numSigs) {
                status = LDM7_NOENT;
            }
            else {
                (void)memcpy(sig, mmo->sigs + (mmo->oldSig + delta) % maxSigs,
                        SIG_SIZE);
                status = 0;
            }

            int stat = unlockMap();
            if (stat)
                status = stat;
        } // map is locked
    }

    return status;
}

/**
 * Returns the next product-index that should be put into the product-index
 * map. The product-index will be zero if the map is empty.
 *
 * @param[out] iProd        Next product-index.
 * @retval     0            Success. `*iProd` is set.
 * @retval     LDM7_LOGIC   The product-index map is not open. `log_add()`
 *                          called.
 * @retval     LDM7_SYSTEM  System error. `log_add()` called.
 */
Ldm7Status
pim_getNextProdIndex(
        FmtpProdIndex* const iProd)
{
    int status = ensureProperState(true);

    if (status == 0) {
        status = lockMap(0); // shared lock

        if (0 == status) {
            *iProd = mmo->oldIProd + mmo->numSigs;
			status = unlockMap();
        } // map is locked
    }

    return status;
}
