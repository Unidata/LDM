/*
 *   See file COPYRIGHT for copying and redistribution conditions.
 */

#ifndef _PQ_H
#define _PQ_H

#include "ldm.h"        /* prod_class_t */
#include "prod_class.h"

#include <sys/types.h>	/* off_t, mode_t */
#include <stdbool.h>
#include <stddef.h>	/* size_t */


/**
 * The functions below return ENOERR upon success.
 * Upon failure, the return something else :-).
 * (Usually, that something else will be the a system
 * error (errno.h), don't count on it making sense.)
 */
#ifndef ENOERR
#define ENOERR 0
#endif /*!ENOERR */

#define PQ_END      -1	// at end of product-queue
#define PQ_DUP      -2 	// attempt to insert duplicate product
#define PQ_BIG      -3 	// attempt to insert product that's too large
#define PQ_SYSTEM   -4	// system error
#define PQ_LOCKED   -5  // data-product is locked by another process
#define PQ_CORRUPT  -6	// the product-queue is corrupt
#define PQ_NOTFOUND -7	// no such data-product
#define PQ_INVAL    -8  // Invalid argument

typedef struct pqueue pqueue; /* private, implemented in pq.c */
extern struct pqueue *pq;

typedef struct pqe_index pqe_index;

/* prototype for 4th arg to pq_sequence() */
typedef int pq_seqfunc(const prod_info *infop, const void *datap,
	void *xprod, size_t len,
	void *otherargs);

/**
 * Which direction the cursor moves in pq_sequence().
 */
typedef enum {
	TV_LT = -1,
	TV_EQ =  0,
	TV_GT =  1
} pq_match;

struct pqe_index {
	off_t      offset;
	signaturet signature;
	bool       sig_is_set;
};

typedef struct {
    prod_info info;
    void*     data;
    void*     encoded;
    size_t    size;
} prod_par_t;

typedef struct {
    timestampt inserted;
    off_t      offset;
    bool       early_cursor;
    bool       is_full;
    bool       is_locked;
} queue_par_t;

typedef void pq_next_func(
        const prod_par_t* restrict  prod_par,
        const queue_par_t* restrict queue_par,
        void* restrict              app_par);

/**
 * pflags arg to pq_open() and pq_create()
 */
#define PQ_DEFAULT	0x00
#define PQ_NOCLOBBER	0x01	/* Don't destroy existing file on create */
#define PQ_READONLY	0x02	/* Default is read/write */
#define PQ_NOLOCK	0x04	/* Disable locking (DANGER!) */
#define PQ_PRIVATE	0x08	/* mmap() the file MAP_PRIVATE, default MAP_SHARED */
#define PQ_NOGROW	0x10	/* If pq_create(), must have intialsz */
#define PQ_NOMAP	0x20	/* Use malloc/read/write/free instead of mmap() */
#define PQ_MAPRGNS	0x40	/* Map region by region, default whole file */
#define PQ_SPARSE       0x80    /* Created as sparse file, zero blocks unallocated */
#define PQ_THREADSAFE   0x100   /* Make the queue access functions thread-safe */
/* N.B.: bits 0x1000 (and above) in use internally */

#define pqeOffset(pqe) ((pqe).offset)
#define pqeEqual(left, rght) (pqeOffset(left) == pqeOffset(rght))

#define PQE_NONE (_pqenone)
#define pqeIsNone(pqe) (pqeEqual(pqe, PQE_NONE))
#define PQUEUE_DUP PQ_DUP	// attempt to insert duplicate product
#define PQUEUE_BIG PQ_BIG	// attempt to insert product that's too large
#define PQUEUE_END PQ_END	// return value indicating end of queue

#ifdef __cplusplus
extern "C" {
#endif

extern const pqe_index _pqenone;

/**
 * Creates a product-queue. On success, the writer-counter of the created
 * product-queue will be one.
 *
 * @param[in] path        Pathname of the product-queue.
 * @param[in] mode        File mode. Bitwise OR of
 *                        - S_IRWXU    Read, write, execute/search by owner.
 *                            + S_IRUSR  Read permission, owner.
 *                            + S_IWUSR  Write permission, owner.
 *                        - S_IRWXG    Read, write, execute/search by group.
 *                            + S_IRGRP  Read permission, group.
 *                            + S_IWGRP  Write permission, group.
 *                        - S_IRWXO    Read, write, execute/search by others.
 *                            + S_IROTH  Read permission, others.
 *                            + S_IWOTH  Write permission, others.
 *                        This value is bitwise ANDed with the complement of the
 *                        process' file mode creation mask.
 * @param[in]  pflags     Product-queue flags. Bitwise OR of
 *                          PQ_DEFAULT	  Default attributes (0).
 *                          PQ_MAPRGNS    Map region by region. Default is whole
 *                                        file.
 *                          PQ_NOCLOBBER  Don't replace an existing
 *                                        product-queue.
 *                          PQ_NOLOCK     Disable locking (DANGER!)
 *                          PQ_NOMAP      Use `malloc/read/write/free` instead
 *                                        of `mmap()`
 *                          PQ_READONLY   Only allow reading from the queue
 *                          PQ_PRIVATE    `mmap()` the file `MAP_PRIVATE`.
 *                                        Default is `MAP_SHARED`.
 *                          PQ_THREADSAFE Ensure thread-safe access
 * @param[in]  align      Alignment parameter for file components or 0.
 * @param[in]  initialsz  Size, in bytes, of the data portion of the product-
 *                        queue.
 * @param[in]  nproducts  Number of product slots to create.
 * @param[out] pqp        Product-queue.
 * @retval     0          Success. `*pqp` is set.
 */
int
pq_create(const char *path, mode_t mode,
        int pflags,
        size_t align,
        off_t initialsz, /* initial allocation available */
        size_t nproducts, /* initial rl->nalloc, ... */
        pqueue **pqp);

/**
 * Opens an existing product-queue.
 *
 * @param[in] path         Pathname of product-queue.
 * @param[in] pflags       File-open flags. Bitwise OR of
 *                           PQ_MAPRGNS    Map region by region, default whole
 *                                         file if possible; otherwise by region
 *                           PQ_NOLOCK     Disable locking (DANGER!)
 *                           PQ_NOMAP      Use `malloc/read/write/free` instead
 *                                         of `mmap()`
 *                           PQ_PRIVATE    `mmap()` the file `MAP_PRIVATE`.
 *                                         Default is `MAP_SHARED`
 *                           PQ_READONLY   Default is read-write.
 * @param[out] pqp         Memory location to receive pointer to product-queue
 *                         structure.
 * @retval     0           Success. *pqp set.
 * @retval     EACCES      Permission denied. pflags doesn't contain PQ_READONLY
 *                         and the product-queue is already open by the maximum
 *                         number of writers.
 * @retval     PQ_CORRUPT  The  product-queue is internally inconsistent.
 * @return                 Other <errno.h> error-code.
 */
int
pq_open(
    const char* const path,
    int               pflags,
    pqueue** const    pqp);

/**
 * Returns the flags used to open or create a product-queue.
 *
 * @param[in] pq  The product-queue.
 * @return        The flags: a bitwise OR of
 *                    PQ_MAPRGNS     Map region by region, default whole file.
 *                    PQ_NOCLOBBER   Don't replace an existing product-queue.
 *                    PQ_NOLOCK      Disable locking.
 *                    PQ_NOMAP       Use `malloc/read/write/free` instead of
 *                                   `mmap()`
 *                    PQ_PRIVATE     `mmap()` the file `MAP_PRIVATE`. Default is
 *                                   `MAP_SHARED`
 *                    PQ_READONLY    Default is read/write.
 *                    PQ_THREADSAFE  Product-queue access is thread-safe
 */
int
pq_getFlags(
        pqueue* const pq);

/*
 * On success, if the product-queue was open for writing, then its
 * writer-counter will be decremented.
 *
 * Returns:
 *      0               Success.
 *      EOVERFLOW       Write-count of product queue was prematurely zero.
 *      !0              Other <errno.h> code.
 */
int
pq_close(pqueue *pq);

/**
 * Returns the pathname of a product-queue as given to `pq_create()` or
 * `pq_open()`.
 *
 * @param[in] pq  The product-queue.
 * @return        The pathname of the product-queue as given to `pq_create()` or
 *                `pq_open()`.
 */
const char* pq_getPathname(
        pqueue* pq);

/*
 * Let the user find out the pagesize.
 */
int
pq_pagesize(pqueue *pq);

/**
 * Returns the size of the data portion of a product-queue.
 *
 * @param[in] pq  Pointer to the product-queue object.
 * @return        The size, in bytes, of the data portion of the product-queue.
 */
size_t
pq_getDataSize(
    pqueue* const       pq);

/**
 * Inserts a data-product at the tail-end of the product-queue without signaling
 * the process group.
 *
 * @param[in] pq           The product-queue.
 * @param[in] prod         The data-product.
 * @retval    ENOERR       Success.
 * @retval    EACCES       Couldn't make room: no unlocked products left to delete
 * @retval    EACCES       Queue is read-only
 * @retval    EINVAL       Invalid argument.
 * @retval    PQ_DUP       Product already exists in the queue.
 * @retval    PQ_BIG       Product is too large to insert in the queue.
 */
int
pq_insertNoSig(pqueue *pq, const product *prod);

/**
 * Insert at rear of queue, send SIGCONT to process group
 *
 * @param[in,out]  pq    Product queue
 * @param[in]      prod  Data product
 * @retval 0             Success.
 * @retval EACCES        Couldn't make room: no unlocked products left to delete
 * @retval EINVAL        Invalid argument.
 * @retval PQ_DUP        Product already exists in the queue.
 * @retval PQ_BIG        Product is too large to insert in the queue.
 * @retval PQ_SYSTEM     System failure. log_error() called.
 */
int
pq_insert(pqueue *pq, const product *prod);

/**
 * Returns some useful, "highwater" statistics of a product-queue.  The
 * statistics are since the queue was created.
 *
 * @param[in]  pq            Pointer to the product-queue.  Shall not be NULL.
 * @param[out] highwaterp    Pointer to the maxium number of bytes used in the data portion of the
 *                           product-queue.  Shall not be NULL. Set upon successful return.
 * @param[out] maxproductsp  Pointer to the maximum number of data-products that the product-queue
 *                           has held since it was created.  Shall not be NULL.  Set upon successful
 *                           return.
 * @retval 0                 Success.  "*highwaterp" and "*maxproductsp" are set.
 * @return                   <errno.h> error code.
 */
int
pq_highwater(pqueue *pq, off_t *highwaterp, size_t *maxproductsp);

/*
 * Indicates if the product-queue is full (i.e., if a data-product has been
 * deleted in order to make room for another data-product).
 *
 * Arguments:
 *      pq              Pointer to the product-queue structure.  Shall not be
 *                      NULL.
 *      isFull          Pointer to the indicator of whether or not the queue
 *                      is full.  Shall not be NULL.  Set upon successful
 *                      return.  "*isfull" will be non-zero if and only if the
 *                      product-queue is full.
 * Returns:
 *      0               Success.  "*isFull" is set.
 *      else            <errno.h> error code.
 */
int pq_isFull(
    pqueue* const       pq,
    int* const          isFull);

/*
 * Returns the time of the most-recent insertion of a data-product.
 *
 * Arguments:
 *      pq              Pointer to the product-queue structure.  Shall not be
 *                      NULL.
 *      mostRecent      Pointer to the time of the most-recent insertion of a
 *                      data-product.  Upon successful return, "*mostRecent"
 *                      shall be TS_NONE if such a time doesn't exist (because
 *                      the queue is empty, for example).
 * Returns:
 *      0               Success.  "*mostRecent" is set.
 *      else            <errno.h> error code.
 */
int pq_getMostRecent(
    pqueue* const       pq,
    timestampt* const   mostRecent);

/*
 * Returns metrics associated with the minimum virtual residence time of
 * data-products in the queue since the queue was created or the metrics reset.
 * The virtual residence time of a data-product is the time that the product
 * was removed from the queue minus the time that the product was created.  The
 * minimum virtual residence time is the minimum of the virtual residence times
 * over all applicable products.
 *
 * Arguments:
 *      pq              Pointer to the product-queue structure.  Shall not be
 *                      NULL.
 *      minVirtResTime  Pointer to the minimum virtual residence time of the
 *                      queue since the queue was created.  Shall not be NULL.
 *                      "*minVirtResTime" is set upon successful return.  If
 *                      such a time doesn't exist (because no products have
 *                      been deleted from the queue, for example), then
 *                      "*minVirtResTime" shall be TS_NONE upon successful
 *                      return.
 *      size            Pointer to the amount of data used, in bytes, when the
 *                      minimum virtual residence time was set. Shall not be
 *                      NULL. Set upon successful return. If this parameter
 *                      doesn't exist, then "*size" shall be set to -1.
 *      slots           Pointer to the number of slots used when the minimum
 *                      virtual residence time was set. Shall not be NULL. Set
 *                      upon successful return. If this parameter doesn't exist,
 *                      the "*slots" shall be set to 0.
 * Returns:
 *      0               Success.  All the outout metrics are set.
 *      else            <errno.h> error code.
 */
int pq_getMinVirtResTimeMetrics(
    pqueue* const       pq,
    timestampt* const   minVirtResTime,
    off_t* const        size,
    size_t* const       slots);

/*
 * Clears the metrics associated with the minimum virtual residence time of
 * data-products in the queue.  After this function, the minimum virtual
 * residence time metrics will be recomputed as products are deleted from the
 * queue.
 *
 * Arguments:
 *      pq              Pointer to the product-queue structure.  Shall not be
 *                      NULL.  Must be open for writing.
 * Returns:
 *      0               Success.  The minimum virtual residence time metrics are
 *                      cleared.
 *      else            <errno.h> error code.
 */
int pq_clearMinVirtResTimeMetrics(
    pqueue* const       pq);

/**
 * Get some detailed product queue statistics.  These may be useful for
 * monitoring the internal state of the product queue:
 *
 * @param[out] nprodsp      The current number of products in the queue.  May be
 *                          NULL.
 * @param[out] nfreep       The current number of free regions.  This should be
 *                          small and it's OK if it's zero, since new free
 *                          regions are created as needed by deleting oldest
 *                          products.  If this gets large, insertion and
 *                          deletion take longer.  May be NULL.
 * @param[out] nemptyp      The number of product slots left.  This may
 *                          decrease, but should eventually stay above some
 *                          positive value unless too few product slots were
 *                          allocated when the queue was created.  New product
 *                          slots get created when adjacent free regions are
 *                          consolidated, and product slots get consumed when
 *                          larger free regions are split into smaller free
 *                          regions.  May be NULL.
 * @param[out] nbytesp      The current number of bytes in the queue used for
 *                          data products.  May be NULL.
 * @param[out] maxprodsp    The maximum number of products in the queue, so far.
 *                          May be NULL.
 * @param[out] maxfreep     The maximum number of free regions, so far.  May be
 *                          NULL.
 * @param[out] minemptyp    The minimum number of empty product slots, so far.
 *                          May be NULL.
 * @param[out] maxbytesp    The maximum number of bytes used for data, so far.
 *                          May be NULL.
 * @param[out] age_oldestp  The age in seconds of the oldest product in the
 *                          queue.  May be NULL.
 * @param[out] maxextentp   Extent of largest free region  May be NULL.
 * @retval 0          Success.
 * @retval EACCESS or EAGAIN
 *                    The region is already locked by another process.
 * @retval EINVAL     The product-queue file doesn't support locking.
 * @retval ENOLCK     The number of locked regions would exceed a system-imposed
 *                    limit.
 * @retval EDEADLK    A deadlock in region locking has been detected.
 * @retval EBADF      The file descriptor of the product-queue is invalid.
 * @retval EIO        An I/O error occurred while reading from the file system.
 * @retval EOVERFLOW  The file size in bytes or the number of blocks allocated
 *                    to the file or the file serial number cannot be
 *                    represented correctly.
 * @retval EINTR      A signal was caught during execution.
 * @retval EFBIG or EINVAL
 *                    The size of the product-queue is greater than the maximum
 *                    file size.
 * @retval EROFS      The named file resides on a read-only file system.
 * @retval EAGAIN     The mapping could not be locked in memory, if required by
 *                    mlockall(), due to a lack of resources.
 * @retval EINVAL     The product-queue object wants to map the product-queue to
 *                    a fixed memory location that is not a multiple of the page
 *                    size as returned by sysconf(), or is considered invalid
 *                    by the O/S.
 * @retval EMFILE     The number of mapped regions would exceed an O/S-dependent
 *                    limit (per process or per system).
 * @retval ENODEV     The file descriptor of the product-queue object refers to
 *                    a file whose type is not supported by mmap().
 * @retval ENOMEM     The size of the product-queue exceeds that allowed for the
 *                    address space of a process.
 * @retval ENOMEM     The mapping could not be locked in memory, if required by
 *                    mlockall(), because it would require more space than the
 *                    system is able to supply.
 * @retval ENOTSUP    The O/S does not support the combination of accesses
 *                    requested.
 * @retval ENXIO      The size of the product-queue is invalid for the object
 *                    specified by its file descriptor.
 *
 * Note: the fixed number of slots allocated for products when the
 * queue was created is nalloc = (nprods + nfree + nempty).
 */
int
pq_stats(
		pqueue* const pq,
        size_t* const nprodsp,
        size_t* const nfreep,
        size_t* const nemptyp,
        size_t* const nbytesp,
        size_t* const maxprodsp,
        size_t* const maxfreep,
        size_t* const minemptyp,
        size_t* const maxbytesp,
        double* const age_oldestp,
        size_t* const maxextentp);

/*
 * Returns the number of slots in a product-queue.
 *
 * Arguments:
 *      pq              Pointer to the product-queue object.
 * Returns:
 *      The number of slots in the product-queue.
 */
size_t
pq_getSlotCount(
    pqueue* const       pq);

/*
 * Returns the insertion-timestamp of the oldest data-product in the
 * product-queue.
 *
 * Arguments:
 *      oldestCursor    Pointer to structure to received the insertion-time
 *                      of the oldest data-product.
 * Returns:
 *      ENOERR          Success.
 *      else            Failure.
 */
int
pq_getOldestCursor(
    pqueue*             pq,
    timestampt* const   oldestCursor);

/*
 * Returns the number of pq_open()s for writing outstanding on an existing
 * product queue.  If a writing process terminates without calling pq_close(),
 * then the actual number will be less than this number.  This function opens
 * the product-queue read-only, so if there are no outstanding product-queue
 * writers, then the returned count will be zero.
 *
 * Arguments:
 *      path    The pathname of the product-queue.
 *      count   The memory to receive the number of writers.
 * Returns:
 *      0           Success.  *count will be the number of writers.
 *      EINVAL      path is NULL or count is NULL.  *count untouched.
 *      ENOSYS      Function not supported because product-queue doesn't support
 *                  writer-counting.
 *      PQ_CORRUPT  The  product-queue is internally inconsistent.
 *      else        <errno.h> error-code.  *count untouched.
 */
int
pq_get_write_count(
    const char* const   path,
    unsigned* const     count);

/*
 * Sets to zero the number of pq_open()s for writing outstanding on the
 * product-queue.  This is a dangerous function and should only be used when
 * it is known that there are no outstanding pq_open()s for writing on the
 * product-queue.
 *
 * Arguments:
 *      path    The pathname of the product-queue.
 * Returns:
 *      0           Success.
 *      EINVAL      path is NULL.
 *      PQ_CORRUPT  The  product-queue is internally inconsistent.
 *      else        <errno.h> error-code.
 */
int
pq_clear_write_count(const char* const path);

/**
 * For debugging: dump extents of regions on free list, in order by extent.
 */
int
pq_fext_dump(pqueue *const pq);

/**
 * Set cursor used by pq_sequence() or pq_seqdel().
 */
void
pq_cset(pqueue *pq, const timestampt *tvp);

/**
 * Set cursor_offset used by pq_sequence() to disambiguate among
 * multiple products with identical queue insertion times.
 */
void
pq_coffset(pqueue *pq, off_t c_offset);

/**
 * Get current cursor value used by pq_sequence() or pq_seqdel().
 */
void
pq_ctimestamp(pqueue *pq, timestampt *tvp);

/**
 * Figure out the direction of scan of clssp, and set *mtp to it.
 * Set the cursor to include all of clssp time range in the queue.
 * (N.B.: For "reverse" scans, this range may not include all
 * the arrival times.)
 */
int
pq_cClassSet(pqueue *pq,  pq_match *mtp, const prod_class_t *clssp);

/*
 * Set the cursor based on the insertion-time of the product with the given
 * signature if and only if the associated data-product is found in the 
 * product-queue.
 *
 * Arguments:
 *      pq              Pointer to the product-queue.
 *      signature       The given signature.
 * Returns:
 *      0       Success.  The cursor is set to reference the data-product with
 *              the same signature as the given one.
 *      EACCES  "pq->fd" is not open for read or "pq->fd" is not open for write
 *              and PROT_WRITE was specified for a MAP_SHARED type mapping.
 *      EAGAIN  The mapping could not be locked in memory, if required by
 *              mlockall(), due to a lack of resources.
 *      EBADF   "pq->fd" is not a valid file descriptor open for reading.
 *      EDEADLK The necessary lock is blocked by some lock from another process
 *              and putting the calling process to sleep, waiting for that lock
 *              to become free would cause a deadlock.
 *      EFBIG or EINVAL
 *              The extent of the region is greater than the maximum file size.
 *      EFBIG   The file is a regular file and the extent of the region is
 *              greater than the offset maximum established in the open file
 *              description associated with "pq->fd".
 *      EINTR   A signal was caught during execution.
 *      EINVAL  The region's offset or extent is not valid, or "pq->fd" refers
 *              to a file that does not support locking.
 *      EINVAL  The region's offset is not a multiple of the page size as 
 *              returned by sysconf(), or is considered invalid by the 
 *              implementation.
 *      EIO     An I/O error occurred while reading from the file system.
 *      EIO     The metadata of a data-product in the product-queue could not be
 *              decoded.
 *      EMFILE  The number of mapped regions would exceed an
 *              implementation-dependent limit (per process or per system).
 *      ENODEV  "pq->fd" refers to a file whose type is not supported by mmap().
 *      ENOLCK  Satisfying the request would result in the number of locked
 *              regions in the system exceeding a system-imposed limit.
 *      ENOMEM  There is insufficient room in the address space to effect the
 *              necessary mapping.
 *      ENOMEM  The region's mapping could not be locked in memory, if required
 *              by mlockall(), because it would require more space than the 
 *              system is able to supply.
 *      ENOMEM  Insufficient memory is available.
 *      ENOTSUP The implementation does not support the access requested in
 *              "pq->pflags".
 *      ENXIO   The region's location is invalid for the object specified by
 *              "pq->fd".
 *      EOVERFLOW
 *              The smallest or, if the region's extent is non-zero, the
 *              largest offset of any byte in the requested segment cannot be
 *              represented correctly in an object of type off_t.
 *      EOVERFLOW
 *              The file size in bytes or the number of blocks allocated to the
 *              file or the file serial number cannot be represented correctly.
 *      EOVERFLOW
 *              The file is a regular file and the region's offset plus 
 *              extent exceeds the offset maximum established in the open file
 *              description associated with "fd". 
 *      EROFS   The file resides on a read-only file system.
 *
 *      PQ_CORRUPT
 *              The product-queue is corrupt.
 *      PQ_NOTFOUND
 *              A data-product with the given signature was not found in the
 *              product-queue.
 */
int
pq_setCursorFromSignature(
    pqueue* const       pq,
    const signaturet    signature);

/**
 * Process the data-product with a given signature.
 *
 * @param[in] pq           Product-queue.
 * @param[in] sig          Signature of data-product to process.
 * @param[in] func         Function to process data-product.
 * @param[in] optArg       Optional `func` argument.
 * @retval    PQ_SYSTEM    System error. `log_error()` called.
 * @retval    PQ_CORRUPT   The product-queue is corrupt. `log_error()` called.
 * @retval    PQ_NOTFOUND  A data-product with the given signature was not found
 *                         in the product-queue.
 * @return                 Return-code of `func`. All the above error-codes are
 *                         negative.
 */
int
pq_processProduct(
        pqueue* const     pq,
        signaturet  const sig,
        pq_seqfunc* const func,
        void* const       optArg);

/**
 * Step thru the time sorted inventory according to 'mt',
 * and the current cursor value.
 *
 * If(mt == TV_LT), pq_sequence() will get a product
 * whose queue insertion timestamp is strictly less than
 * the current cursor value.
 *
 * If(mt == TV_GT), pq_sequence() will get a product
 * whose queue insertion timestamp is strictly greater than
 * the current cursor value.
 *
 * If(mt == TV_EQ), pq_sequence() will get a product
 * whose queue insertion timestamp is equal to
 * the current cursor value.
 *
 * If no product is in the inventory which which meets the
 * above spec, return PQUEUE_END.
 *
 * Otherwise, if the product info matches class,
 * execute ifMatch(xprod, len, otherargs) and return the
 * return value from ifMatch().
 *
 * @param[in] pq          Product-queue.
 * @param[in] mt          Direction from current position in queue to start
 *                        matching.
 * @param[in] clss        Class of data-products to match.
 * @param[in] ifMatch     Function to call for matching products.
 * @param[in] otherargs   Optional argument to `ifMatch`.
 * @retval    PQ_CORRUPT  Product-queue is corrupt (NB: <0). `log_add()` called.
 * @retval    PQ_END      No next product (NB: <0)
 * @retval    PQ_INVAL    Invalid argument (NB: <0). `log_add()` called.
 * @retval    PQ_SYSTEM   System error (NB: <0). `log_add()` called.
 * @retval    0           Product didn't match. `log_add()` called.
 * @return                Return-value of `ifMatch()`. `log_add()` called.
 */
int
pq_sequence(
        pqueue* const             pq,
        pq_match                  mt,
        const prod_class_t* const clss,
        pq_seqfunc* const         ifMatch,
        void* const               otherargs);

/**
 * Step thru the time sorted inventory according to 'mt',
 * and the current cursor value.
 *
 * If(mt == TV_LT), pq_sequence() will get a product
 * whose queue insertion timestamp is strictly less than
 * the current cursor value.
 *
 * If(mt == TV_GT), pq_sequence() will get a product
 * whose queue insertion timestamp is strictly greater than
 * the current cursor value.
 *
 * If(mt == TV_EQ), pq_sequence() will get a product
 * whose queue insertion timestamp is equal to
 * the current cursor value.
 *
 * If no product is in the inventory which which meets the
 * above spec, return PQUEUE_END.
 *
 * Otherwise, if the product info matches class,
 * execute ifMatch(xprod, len, otherargs) and return the
 * return value from ifMatch().
 *
 * @param[in]  pq         Product-queue.
 * @param[in]  mt         Direction from current position in queue to start
 *                        matching.
 * @param[in]  clss       Class of data-products to match.
 * @param[in]  ifMatch    Function to call for matching products.
 * @param[in]  otherargs  Optional argument to `ifMatch`.
 * @param[out] off        Offset to the region in the product-queue or NULL. If
 *                        NULL, then upon return the product is unlocked and may
 *                        be deleted by another process to make room for a new
 *                        product. If non-NULL, then this variable is set before
 *                        `ifMatch()` is called and the product's state upon
 *                        return from this function depends on that function's
 *                        return-value:
 *                          - 0     The product is locked against deletion and
 *                                  the caller should call `pq_release()` when
 *                                  the product may be deleted
 *                          - else  The product is unlocked and may be deleted
 * @retval     PQ_CORRUPT Product-queue is corrupt (NB: <0)
 * @retval     PQ_END     No next product (NB: <0)
 * @retval     PQ_INVAL   Invalid argument (NB: <0)
 * @retval     PQ_SYSTEM  System error (NB: <0)
 * @return                Return-value of `ifMatch()`. Should be >0.
 */
int
pq_sequenceLock(
        pqueue* const restrict             pq,
        pq_match                           mt,
        const prod_class_t* const restrict clss,
        pq_seqfunc* const                  ifMatch,
        void* const                        otherargs,
        off_t* const                       offset);

/**
 * Step thru the time-sorted inventory from the current time-cursor.
 *
 * @param[in,out] pq           Product queue
 * @param[in]     reverse      Whether to match in reverse direction (i.e.,
 *                             towards earlier times).
 * @param[in]     clss         Product matching criteria
 * @param[in]     func         Function to call for matching products
 * @param[in]     keep_locked  Whether or not product should be locked (i.e.,
 *                             kept unavailable for deletion) upon return. If
 *                             `true`, then caller must call
 *                             `pq_release(queue_par->offset)`, where
 *                             `queue_par` is the queue-parameters argument to
 *                             `func`.
 * @param[in,out] app_par      Application-supplied parameters or `NULL`
 * @retval        0            Success. `func()` was called.
 * @retval        PQ_END       End of time-queue hit
 * @retval        PQ_INVAL     Invalid argument. log_error() called.
 * @retval        PQ_SYSTEM    System failure. log_error() called.
 */
int
pq_next(
        pqueue* const restrict             pq,
        const bool                         reverse,
        const prod_class_t* const restrict clss,
        pq_next_func* const                func,
        const bool                         keep_locked,
        void* const restrict               app_par);

/**
 * Releases a data-product that was locked by `pq_sequenceLock()` so that it can
 * be deleted to make room for another product.
 *
 * @retval 0            Success.
 * @retval PQ_CORRUPT   Product-queue is corrupt. `log_error()` called.
 * @retval PQ_INVAL     Product-queue is closed. `log_error()` called.
 * @retval PQ_NOTFOUND  `offset` doesn't refer to a locked product. `log_error()`
 *                      called.
 */
int
pq_release(
        pqueue* const pq,
        const off_t   offset);

/**
 * Boolean function to check that the cursor time is in the time range specified
 * by clssp. Returns non-zero if this is the case, zero if not.
 */
int
pq_ctimeck(pqueue *pq, pq_match mt, const prod_class_t *clssp,
        const timestampt *maxlatencyp);

/**
 * Like pq_sequence(), but the ifmatch action is to remove the product from
 * inventory. If wait is nonzero, then wait for locks.
 *
 * TODO: add class filter
 */
/*ARGSUSED*/
int
pq_seqdel(
        pqueue* const       pq,
        pq_match            mt,
        const prod_class_t* clss,
        const int           wait,
        size_t* const       extentp,
        timestampt* const   timestampp);

/**
 * Deletes the data-product with the given signature from a product-queue.
 *
 * @param[in] pq           The product-queue
 * @param[in] sig          The signature of the data-product to be deleted.
 * @retval    0            Success. The data-product was found and deleted.
 * @retval    PQ_CORRUPT   The product-queue is corrupt.
 * @retval    PQ_LOCKED    The data-product was found but is locked by another
 *                         process.
 * @retval    PQ_NOTFOUND  The data-product wasn't found.
 * @retval    PQ_SYSTEM    System error. Error message logged.
 */
int
pq_deleteBySignature(
        pqueue* const restrict pq,
        const signaturet       sig);

/*
 * Returns the creation-time of the data-product in the product-queue whose
 * insertion-time is closest-to but less-than the "to" time of a class
 * specification.  Sets the cursor of the product-queue to the insertion-
 * time of the data-product, if found.
 *
 * Arguments:
 *      pq      Pointer to product-queue open for reading.
 *      clssp   Pointer to selection-criteria.
 *      tsp     Pointer to timestamp.  Set to creation-time of first, matching
 *              data-product; otherwise, unmodified.
 * Returns:
 *      0       Success (maybe).  *tsp is modified if and only if a matching 
 *              data-product was found.
 *      else    Failure.  <errno.h> error-code.
 */
int
pq_last(pqueue* const             pq,
        const prod_class_t* const clssp,
        timestampt* const         tsp);

/*
 * Modifies a data-product class-specification according to the most recent
 * data-product in the product-queue that matches the specification.
 *
 * The product-queue cursor is unconditionally cleared.
 *
 * Arguments:
 *      pq              Pointer to the product-queue.
 *      clssp           Pointer to the data-product class-specification.
 *                      Modified on and only on success.
 * Returns:
 *      0               Success.  "clssp" is modified.
 *      PQUEUE_END      There's no matching data-product in the product-queue.
 *      else            <errno.h> error-code.
 */
int
pq_clss_setfrom(
        pqueue* const       pq,
        prod_class_t* const clssp);

/**
 * Suspends execution until
 *   - A signal is delivered whose action is to execute a signal-catching
 *     function;
 *   - SIGCONT is received, indicating another data-product is available; or
 *   - The given amount of time elapses.
 * Upon return, the signal mask is what it was on entry.
 *
 * @param[in] maxsleep     Number of seconds to suspend or 0 for an indefinite
 *                         suspension.
 * @param[in] unblockSigs  Additional signals to unblock during suspension.
 *                         Ignored if `numSigs == 0`.
 * @param[in] numSigs      Number of additional signals to unblock. May be `0`.
 * @return                 Requested amount of suspension-time minus the amount
 *                         of time actually suspended.
 */
unsigned
pq_suspendAndUnblock(
        const unsigned int maxsleep,
        const int* const   unblockSigs,
        const int          numSigs);

/**
 * Suspends execution until
 *   - A signal is delivered whose action is to execute a signal-catching
 *     function;
 *   - SIGCONT is received, indicating another data-product is available; or
 *   - The given amount of time elapses.
 * Upon return, the signal mask is what it was on entry.
 *
 * @param[in] maxsleep  Number of seconds to suspend or 0 for an indefinite
 *                      suspension.
 * @return              Requested amount of suspension-time minus the amount of
 *                      time actually suspended.
 */
unsigned
pq_suspend(unsigned int maxsleep);

/*
 * Returns an appropriate error-message given a product-queue and error-code.
 *
 * Arguments:
 *      pq      Pointer to the product-queue.
 *      error   The error-code.
 * Returns:
 *      Pointer to appropriate NUL-terminated error-message.
 */
/*ARGSUSED*/
const char*
pq_strerror(
    pqueue*   pq,
    const int error);

/**
 * Returns an allocated region into which to write a data-product based on
 * data-product metadata.
 *
 * @param[in,out] pq      Pointer to the product-queue object.
 * @param[in]     infop   Pointer to the data-product metadata object.
 * @param[out]    ptrp    Pointer to the pointer to the region into which to
 *                        write the data-product.  Set upon successful return.
 * @param[out]    indexp  Pointer to the handle for the region.  Set upon
 *                        successful return. The client must call `pqe_insert()`
 *                        when all the data has been written or `pqe_discard()`
 *                        to abort the writing and release the region.
 * @retval        0       Success.  "*ptrp" and "*indexp" are set.
 * @return                <errno.h> error code.
 * @see `pqe_insert()`
 */
int
pqe_new(pqueue *pq,
        const prod_info *infop,
        void **ptrp, pqe_index *indexp);

/**
 * Returns an allocated region into which to write an XDR-encoded data-product.
 *
 * @param[in]  pq         Pointer to the product-queue.
 * @param[in]  size       Size of the XDR-encoded data-product in bytes --
 *                        including the data-product metadata.
 * @param[in]  signature  The data-product's MD5 checksum.
 * @param[out] ptrp       Pointer to the pointer to the region into which to
 *                        write the XDR-encoded data-product -- starting with
 *                        the data-product metadata. The caller must begin
 *                        writing at `*ptrp` and not write more than `size`
 *                        bytes of data.
 * @param[out] indexp     Pointer to the handle for the region.
 * @retval     0          Success.  `*ptrp` and `*indexp` are set. The caller
 *                        must eventually call either `pqe_insert()` when all
 *                        the data has been written or `pqe_discard()` to abort
 *                        the writing and release the region.
 * @retval     EINVAL     `pq == NULL || ptrp == NULL || indexp == NULL`.
 *                        `log_error()` called.
   @retval     EACCES     Product-queue is read-only. `log_error()` called.
 * @retval     PQ_BIG     Data-product is too large for product-queue.
 *                        `log_error()` called.
 * @retval     PQ_DUP     If a data-product with the same signature already
 *                        exists in the product-queue.
 * @return                `<errno.h>` error code. `log_error()` called.
 * @see `pqe_insert()`
 */
int
pqe_newDirect(
    pqueue* const restrict    pq,
    const size_t              size,
    const signaturet          signature,
    void** const restrict     ptrp,
    pqe_index* const restrict indexp);

/**
 * Discards a region obtained from `pqe_new()` or `pqe_newDirect()`.
 *
 * @param[in] pq         Pointer to the product-queue.  Shall not be NULL.
 * @param[in] pqe_index  Pointer to the region-index set by `pqe_new()` or
 *                       `pqe_newDirect()`.  Shall not be NULL.
 * @retval 0             Success.
 * @return               <errno.h> error code. `log_error()` called.
 */
int
pqe_discard(
        pqueue* const restrict          pq,
        const pqe_index* const restrict index);

/**
 * LDM 4 convenience funct.
 * Change signature, Insert at rear of queue, send SIGCONT to process group
 */
int
pqe_xinsert(pqueue *pq, pqe_index index, const signaturet realsignature);

/**
 * Finalizes insertion of the data-product reserved by a prior call to
 * `pqe_new()` or `pqe_newDirect()` and sends a SIGCONT to the process group on
 * success. If the reference to the data-product is valid and an error occurs,
 * then the product is not inserted: its data-region and signature are freed.
 *
 * @param[in] pq           The product-queue.
 * @param[in] index        The data-product reference returned by `pqe_new()` or
 *                         `pqe_newDirect()`.
 * @retval    0            Success. `SIGCONT` sent to process-group.
 * @retval    PQ_BIG       According to its metadata, the data-product is larger
 *                         than the space allocated for it by `pqe_new()` or
 *                         `pqe_newDirect()`. `pqe_discard()` called.
 *                         `log_error()` called.
 * @retval    PQ_CORRUPT   The metadata of the data-product referenced by
 *                         `index` couldn't be deserialized. `pqe_discard()`
 *                         called. `log_error()` called.
 * @retval    PQ_NOTFOUND  The data-product referenced by `index` wasn't found.
 *                         `log_error()` called.
 * @retval    PQ_SYSTEM    System failure. `pq_discard()` called.
 *                         `log_error()` called.
 */
int
pqe_insert(
        pqueue* const restrict          pq,
        const pqe_index* const restrict index);

/**
 * Returns the number of outstanding product reservations (i.e., the number of
 * times `pqe_new()` and `pqe_newDirect()` have been called minus the number of
 * times `pqe_insert()` and `pqe_discard()` have been called.
 *
 * @param[in] pq  Product queue
 * @return        Number of outstanding product reservations
 */
long pqe_get_count(
        pqueue* const pq);

/**
 * Returns the magic number of a product-queue.
 *
 * @param[in]  pq     Product-queue
 * @retval     0      `pq == NULL`
 * @retval     -1     `pq->base == NULL`
 * @return            The magic number of `pq`
 */
size_t pq_getMagic(const pqueue* const pq);

/**
 * Logs a warning if the oldest product in the queue was acted upon.
 * @param[in] queue_par  Product-queue parameters
 * @param[in] prod_par   Product parameters
 * @param[in] prefix     Beginning of the sentence " oldest product in full queue"
 */
#define PQ_WARN_IF_OLDEST(queue_par, prod_par, prefix) \
    do { \
        if (queue_par->is_full && queue_par->early_cursor) { \
            char        buf[LDM_INFO_MAX]; \
            timestampt  now; \
            (void)set_timestamp(&now); \
            log_warning(prefix " oldest product in full queue: age=%g s, prod=%s", \
                d_diff_timestamp(&now, &queue_par->inserted), \
                s_prod_info(buf, sizeof(buf), &prod_par->info, log_is_enabled_debug)); \
            log_warning("Products might be deleted before being acted upon! " \
                    "Queue too small? System overloaded?"); \
         } \
    } while (0) // Ensures use of terminating semicolon

#ifdef __cplusplus
}
#endif

#endif /* !_PQ_H */
