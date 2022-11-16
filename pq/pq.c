/**
 * This file implements the LDM product-queue.
 *
 * Copyright 2018 University Corporation for Atmospheric Research. All rights
 * reserved. See the file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 */

#include "config.h"

#include <inttypes.h> /* sysconf */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_MMAP
    #include <sys/mman.h>
#endif
#include <unistd.h> /* sysconf */
#include <limits.h>
#include <log.h>
#include <stdbool.h>
#include <stdio.h>  /* DEBUG */
#include <errno.h>
#include <signal.h>
#include <math.h>
#include <pthread.h>
#include <time.h>
#include <search.h>
#include <stdint.h>
#include <xdr.h>

#include "ldm.h"
#include "pq.h"
#include "fbits.h"
#include "remote.h"
#include "lcm.h"
#include "log.h"
#include "ldmfork.h"
#include "ldmprint.h"
#include "fsStats.h"
#include "ldm_xlen.h"
#include "prod_info.h"
#include "timestamp.h"

/* #define TRACE_LOCK 1 */

/*
 * The time interval, in seconds, to be subtracted from the creation-time
 * of a "signature" data-product in order to determine the initial
 * start-time for a search of the data-product in the time-queue.
 */
extern unsigned int     interval;
#define SEARCH_BACKOFF  interval

/*
 * A value which is an invalid off_t
 */
#define OFF_NONE  ((off_t)(-1))


/*
 * flags used by the region layer.
 */
#define RGN_NOLOCK      0x1     /* Don't lock region.  Used when
                                 * contention control handled
                                 * elsewhere.  */
#define RGN_NOWAIT      0x2     /* return immediate if can't lock, else wait */

#define RGN_WRITE       0x4     /* we intend to modify, else read only */

#define RGN_MODIFIED    RGN_WRITE       /* we did modify, else, discard */


/* useful for aligning memory */
#define _RNDUP(x, unit) ((((x) + (unit) - 1) / (unit)) * (unit))
#define M_RND_UNIT      (sizeof(double))
#define M_RNDUP(x)      _RNDUP(x, M_RND_UNIT)
#define M_RNDDOWN(x)    ((x) - ((x)%M_RND_UNIT))

#define MIN_RGN_SIZE    M_RND_UNIT

/* 
 * Some of the data structures and algorithms in this implementation
 * of the pq library use Skip Lists, data structures designed to
 * support the operations of search, insert, and delete in logarithmic
 * average time.  For more information on Skip Lists, see
 *
 *  William Pugh: Skip Lists: A Probabilistic Alternative to Balanced
 *  Trees. 668-676, CACM 33(6), June 1990.
 *
 * and
 *
 *  ftp://ftp.cs.umd.edu/pub/skipLists/
 */

/* Begin fb */
/*
 * This layer provides dynamic allocation of a set of "fblks" of small
 * integral sizes, intended to hold offsets.  These are used in lieu
 * of pointers in the subsequent skip list algorithm, because these may
 * all be in the shared mmap'd file where pointers would be useless.
 * So where the skip list algorithm specifies a block of pointers of
 * size k, this layer provides an fblk that can hold k offsets.  
 * 
 * The fblks must be allocated dynamically, but we know what
 * distribution of sizes is required by the skip list algorithms, so we
 * just preallocate most of the fblks and keep them on free lists for
 * each size needed.  A few extra fblks are provided for statistical
 * overflow.
 */

#define MAXLEVELS 15            /* enough for 4^15 products in the queue */

typedef size_t fblk_t;          /* index into fblks array, points to fblk */

struct fb {
#define FB_MAGIC        0x54514642
  size_t magic;                 /* TQFB, used to check alignment, endianness */
  int maxsize;                  /* maximum size of fblks */
  size_t arena_sz;              /* number of fblks, in units of fblk_t */
  size_t avail;                 /* fblks still available, of all sizes */
  size_t allocated;             /* number of fblks currently allocated */
  size_t nfree[MAXLEVELS];      /* number of free fblks for each level */
  fblk_t free[MAXLEVELS];       /* Heads of free lists.  When free,
                                   the first fblk_t in each fblk is the
                                   offset of the next free block of
                                   the same size, relative to the
                                   start of the fblks array */
#define FBLKS_NALLOC_INITIAL 2
  fblk_t fblks[FBLKS_NALLOC_INITIAL]; /* actually arena_sz long */
};
typedef struct fb fb;

/* return floor(log4(n)) */
static inline int
log4(size_t n) 
{
    log_assert(n > 0);
    return (int)(log(n + 0.5)/log(4.0));
}

typedef struct {
	int            randomsLeft;
	long           randomBits;
	unsigned short xsubi[3];
    size_t         prev_nelems;
    size_t         size;
} FbPar;

/**
 * Return random level of
 *  - 0 with probability 3/4*(1/4)^0 = .75
 *  - 1 with probability 3/4*(1/4)^1 = .1875
 *  - 2 with probability 3/4*(1/4)^2 = .046875
 *  - ...
 *  Capped at "fbp->maxsize - 1".
 *
 *  @param[in] fbp  Pointer to fblk structure
 *  @threadsafety   Compatible but not safe
 */
static int 
fb_ranlev(
        const fb* const fbp)
{
    int level;
    for (level = 0; level < fbp->maxsize - 1; level++) {
        // nrand48() returns 31 pseudo-random bits:
#       define                BITS_IN_RANDOM 31
#       define                BITS_IN_PIECE  2
#       define                PIECE_MASK     ((1 << BITS_IN_PIECE) - 1)
        static int            randomsLeft;
        static long           randomBits;
        // Randomly-generated values:
        static unsigned short xsubi[3] = {
                (unsigned short)0x473da8f190d5f1c4u,
                (unsigned short)0x440937acf01c8c4eu,
                (unsigned short)0xa8a9d686bec2da48u}; // Casts silence warnings
        if (--randomsLeft <= 0) {
            randomBits = nrand48(xsubi);
            randomsLeft = BITS_IN_RANDOM / BITS_IN_PIECE;
        }
        if (randomBits & PIECE_MASK)
            break;
        randomBits >>= BITS_IN_PIECE;
    }
    return level;
}


/* returns arena size needed for fblks for nelems products */
static size_t 
fb_arena_sz(size_t nelems) 
{
    // Keep consonant with `fb_init()` and `fb_get()`.

    int maxsize = log4(nelems) + 1;
    int numblks;                /* blks to preallocate for each level */
    int level;
    size_t total=0;
    numblks = 0.75*nelems;      /* level 0 blks to preallocate */
    for (level = 0; level < maxsize; level++) {
        int blksize = level + 1;        /* size of fblk (number of levels) */

        total += ((size_t)blksize)*numblks;
        if(numblks >= 4)
            numblks /= 4;
        else
            numblks = 1;
    }
    // Extra blocks to allow for statistical fluctuations. See `fb_init()`.
    total += 3*sqrt((double)nelems)*log4(nelems)*maxsize;
    return total;
    /*
     * I believe this function underestimates the necessary number of skip-list
     * nodes.  I believe the maximum number of nodes should be "3*nelems + 2",
     * which would correspond to the worst-case scenario in which the
     * time-queue is full and the data-products are separated from each other
     * and from the ends of the queue by the free regions in the region-list.
     * --SRE 2007-06-14
     */
}

/* return size of fb needed for nelems products */
static size_t
fb_sz(const size_t nelems)
{
    log_assert(nelems);
    static size_t prev_nelems = 0;
    static size_t size;
    if (nelems != prev_nelems) {
        prev_nelems = nelems;
        size = sizeof(fb) - sizeof(fblk_t) * FBLKS_NALLOC_INITIAL;
        size += fb_arena_sz(nelems) * sizeof(fblk_t);
    }
    return size;
}


/**
 *  Dump info about free fblks arena, for debugging.
 */
static void
fb_stats_dump(fb *fbp) 
{
    int level;
    log_assert(fbp != NULL);
    log_error("maxsize = %d", fbp->maxsize);
    log_error("arena_sz = %d", fbp->arena_sz);
    log_error("avail = %d", fbp->avail);
    log_error("allocated = %d", fbp->allocated);
    for(level = 0; level <= fbp->maxsize; level++) {
        log_error("nfree[%d]:\t%d\t%d", level, fbp->nfree[level],
               fbp->free[level]);
    }
}


static void
fb_initLevel(
        fb* const restrict     fbp,
        fblk_t* const restrict offset,
        const int              level,
        const int              blksize,
        const int              numblks)
{
    log_assert(fbp != NULL);
    log_assert(level >= 0 && level <= fbp->maxsize);
    log_assert(blksize > 0 && blksize >= level);
    log_assert(numblks > 0);

    fblk_t off = *offset;
    fbp->free[level] = off;
    for (int i = 0; i < numblks - 1; i++) { // Link each block but last to next
        fbp->fblks[off] = off + blksize;
        off += blksize;
    }
    fbp->fblks[off] = (fblk_t)OFF_NONE; // Last block
    fbp->nfree[level] = numblks;
    fbp->avail += numblks;
    *offset = (off + blksize);
}

/* initialize fblks of needed sizes in needed proportions */
static void
fb_init(fb *fbp, size_t nalloc) 
{
    // Keep consonant with `fb_arena_sz()` and `fb_get()`.
#if 1
    fbp->magic = FB_MAGIC;      /* to later check we mapped it correctly */
    log_assert(nalloc > 0);
    const int maxsize = log4(nalloc) + 1; // maxsize >= 1
    log_assert(maxsize < MAXLEVELS);
    fbp->maxsize = maxsize;
    /* free[i] is the free list for blocks of size i+1; free[maxsize] holds
       3*sqrt(nalloc)*log4(nalloc) extra blocks of max length to allow for
       random variations. */

    /* initialize arena to invalid offsets */
    size_t fblk_sz = fb_arena_sz(nalloc);
    for(int i = 0; i < fblk_sz; i++)
        fbp->fblks[i] = (fblk_t)OFF_NONE;

    fbp->allocated = 0;
    fbp->avail = 0;
    fblk_t offset = 0;

    int numblks = 0.75*nalloc;      // Level 1 blks to preallocate
    for (int level = 0; level < maxsize; level++) {
        fb_initLevel(fbp, &offset, level, level+1, numblks);
        if (numblks >= 4)
            numblks /= 4;
        else
            numblks = 1;
    }

    /*
     * Create free list of extra blocks of maximum size to allow for statistical
     * fluctuations.
     */
    numblks = 3*sqrt((double)nalloc)*log4(nalloc); // see `fb_arena_sz()`
    fb_initLevel(fbp, &offset, maxsize, maxsize, numblks);
    // resolution of `fb_arena_sz()` is `sizeof(fblk_t)` and not `maxsize` above
    log_assert(fblk_sz >= offset && fblk_sz < offset + maxsize);
    fbp->arena_sz = offset;
#else
    size_t fblk_sz;
    int i, j;
    int maxsize = log4(nalloc) + 1;
    fblk_t offset = 0;
    int level;                  /* level of skip list pointers */
    int blksize;                /* size of fblk (number of levels) */
    int numblks;                /* blks to preallocate for each level */

    fbp->magic = FB_MAGIC;      /* to later check we mapped it correctly */
    fbp->maxsize = maxsize;
    /* free[i] is the free list for blocks of size i+1; free[maxsize] holds
       3*sqrt(nalloc)*log4(nalloc) extra blocks of max length to allow for
       random variations. */

    /* initialize arena to invalid offsets */
    fblk_sz = fb_arena_sz(nalloc);
    for(i = 0; i < fblk_sz; i++)
        fbp->fblks[i] = OFF_NONE;

    fbp->allocated = 0;
    fbp->avail = 0;
    fbp->free[0] = offset;

    numblks = 0.75*nalloc;      /* level 1 blks to preallocate */
    for (level = 0; level < maxsize; level++) {
        blksize = level + 1;
        fbp->free[level] = offset;
        for (j = 0; j < numblks - 1; j++) { /* link each block to next one */
            fbp->fblks[offset] = offset + blksize;
            offset += blksize;
            fbp->avail++;
        }
        fbp->fblks[offset] = OFF_NONE;
        offset +=  blksize;
        fbp->avail++;
        fbp->nfree[level] = numblks;
        if(numblks >= 4)
            numblks /= 4;
        else
            numblks = 1;
    }

    /* create free list of extra blocks of max size */
    numblks = 3*sqrt((double)nalloc)*log4(nalloc);
    blksize = maxsize;
    fbp->free[maxsize] = offset;
    for(j = 0; j < numblks - 1; j++) {
        fbp->fblks[offset] = offset + blksize;  /* link to next */
        offset += blksize;
        fbp->avail++;
    }
    fbp->fblks[offset] = OFF_NONE; /* end of list */
    offset +=  blksize;
    fbp->avail++;
    fbp->nfree[maxsize] = numblks;
    fbp->arena_sz = offset;
#endif
}


/* 
 * Returns a free fblk of specified size to list. 
 */
static void 
fb_rel(fb *fbp, int size, fblk_t fblk)
{
    int level = size - 1;

    log_assert(fbp != NULL);
    log_assert(0 < size && size <= fbp->maxsize);
    log_assert(fblk < fbp->arena_sz);

    fbp->fblks[fblk] = fbp->free[level]; /* stick on front of list */
    fbp->free[level] = fblk;
    fbp->nfree[level]++;
    fbp->avail++;
    fbp->allocated--;
}


/**
 * Gets a free fblk of specified level (0 <= level < fbp->maxsize).
 *
 * @param[in] fbp       Pointer to fblk structure
 * @param[in] level     Level of fblk to return (origin 0).
 * @retval    OFF_NONE  if no fblk is available. `log_error()` called.
 * @return              An fblk of the given level.
 */
static fblk_t 
fb_get(fb *fbp, int level)
{
    // Keep consonant with `fb_arena_sz()` and `fb_init()`.

#if 1
    log_assert(fbp != NULL);
    log_assert(0 <= level && level < fbp->maxsize);

    for (const int wantSize = level + 1; level <= fbp->maxsize; level++) {
        if (fbp->nfree[level] > 0) {
            fblk_t fblk = fbp->free[level]; // Take it off front of list
            log_assert(fblk != (fblk_t)OFF_NONE);
            log_assert(fblk < fbp->arena_sz);
            fbp->free[level] = fbp->fblks[fblk];
            log_assert(fbp->nfree[level] > 0);
            fbp->nfree[level]--;
            log_assert(fbp->avail > 0);
            fbp->avail--;
            fbp->allocated++;

            const int gotSize = level + 1;
            // Don't split highest level intended for statistical fluctuations.
            if (level < fbp->maxsize && wantSize < gotSize) {
                // Split off remainder and release it
                fblk_t fblk2 = fblk + wantSize;
                fb_rel(fbp, gotSize - wantSize, fblk2);
                fbp->allocated++; // Restore count because fb_rel() decremented
            }

            return fblk;
        }
    }
    /*
     * All out of blocks. This means we tried to keep in the product-queue
     * significantly more products than the specified maximum number.
     */
    log_error("\"fblk\" subsystem ran out of skip-list nodes. "
            "Too many products in queue.");
    fb_stats_dump(fbp);
    return (fblk_t)OFF_NONE;
#else
    fblk_t fblk, fblk2;
    int size = level + 1;

    log_assert(fbp != NULL);
    log_assert(0 < size && size <= fbp->maxsize);

    if(fbp->nfree[level] > 0) { /* ok, available */
        fblk = fbp->free[level];        /* take it off the front of the list */
        log_assert(fblk != (fblk_t)OFF_NONE);
        fbp->free[level] = fbp->fblks[fblk];
        fbp->nfree[level]--;
        fbp->avail--;
        fbp->allocated++;
        return fblk;
    }
    /* else:  all out, have to get it from next level */
    level++;
    while (level <= fbp->maxsize) {
        if(fbp->nfree[level] > 0) {
            fblk = fbp->free[level];    /* take it off the front of the list */
            log_assert(fblk != (fblk_t)OFF_NONE);
            fbp->free[level] = fbp->fblks[fblk];
            fbp->nfree[level]--;
            fbp->avail--;
            fbp->allocated++;

            if(level < fbp->maxsize && size < level + 1) { 
                /* split off remainder and release it */
                fblk2 = fblk + size;
                fb_rel(fbp, (level+1) - size, fblk2);
                fbp->allocated++; /* fix count, since fb_rel decremented */
            }
            return fblk;
        }
        level++;
    }
    /* else: all out of extra blocks too.  This means we tried to keep
       in product queue significantly more than the specified maximum
       number of products. */
    log_error("fb layer ran out of product slots, too many products in queue");
    /* fb_stats_dump(fbp);   */
    return (fblk_t)OFF_NONE;
#endif
}
/* End fb */
/* Begin tqueue */

/*
 * The product queue is indexed by product insertion time,
 * represented by timestampt (timestamp).
 * The tqueue structure is this index. It refers to the
 * region index pq->rlp via the offset.
 */

typedef off_t tqep_t;           /* index into tqep array, points to tqelem */

#define TQ_NONE ((tqep_t) OFF_NONE)
/* For extra TQ_NIL and TQ_HEAD elements */
#define TQ_OVERHEAD_ELEMS 2

struct tqelem {
  timestampt tv;
  off_t offset;                 /* offset of region associated with
                                   product.  Also used to link up
                                   freelist of tqelems. */
  fblk_t fblk;                  /* forward "pointer" block of length lvl */
};
typedef struct tqelem tqelem;

struct tqueue
{
#define TQ_NALLOC_INITIAL       84
  size_t nalloc;                /* number of allocated product slots */
  size_t nelems;                /* current number of products in queue */
  size_t nfree;                 /* number of free tqep's left */
  tqep_t free;                  /* index of tqep free list */
  int level;                    /* Current level of skip list */
  off_t fbp_off;                /* skip list blocks */
  tqelem tqep[TQ_NALLOC_INITIAL]; /* actually nalloc long */
};
typedef struct tqueue tqueue;

typedef struct {
    size_t prev_nelems;
    size_t size;
} TqPar;

/* 
 * For a tq with the capacity to index nelems, return how much space
 * it will consume
 */
static size_t
tq_sz(const size_t nelems)
{
    log_assert(nelems);
    static size_t prev_nelems = 0;
    static size_t size;
    if (nelems != prev_nelems) {
        prev_nelems = nelems;
        size = sizeof(tqueue) - sizeof(tqelem) * TQ_NALLOC_INITIAL;
        /* TQ_OVERHEAD_ELEMS extra slots for TQ_NIL, TQ_HEADER  */
        size += (nelems + TQ_OVERHEAD_ELEMS) * sizeof(tqelem);
    }
    return size;
}

/*
 * Initialize tqueue structures
 */
static void
tq_init(tqueue *const tq, size_t const nalloc0, fb *fbp)
{
    tqelem *tqelemp;
    tqelem *const end = &tq->tqep[nalloc0 + TQ_OVERHEAD_ELEMS];
    fblk_t *fblkp;
    int i;
    int maxlevel;
    size_t nelems;
    size_t nalloc = nalloc0 + TQ_OVERHEAD_ELEMS; /* for TQ_NIL, TQ_HEADER */

    log_assert(fbp->magic == FB_MAGIC); /* sanity check */

    tq->nalloc = nalloc0;
    /* cache offset to skip list blocks, so we can find them from only tq */
    tq->fbp_off = (char *)fbp - (char *)tq;
    /* build two distinguished tqelems, TQ_NIL and TQ_HEAD */
#define TQ_NIL ((tqep_t)0)
    tqelemp = &tq->tqep[TQ_NIL];
    tqelemp->tv = TS_ENDT; /* the end of time, as we know it */
    tqelemp->offset = OFF_NONE;
    tqelemp->fblk = fb_get(fbp, 0); /* not used */
#define TQ_HEAD ((tqep_t)1)
    tqelemp = &tq->tqep[TQ_HEAD];
    tqelemp->tv = TS_NONE;              /* not used */
    tqelemp->offset = OFF_NONE; /* not used */
    maxlevel = fbp->maxsize - 1;
    tqelemp->fblk = fb_get(fbp, maxlevel);
    fblkp = &fbp->fblks[tqelemp->fblk];
    for(i=0; i < fbp->maxsize; i++) {
        fblkp[i] = TQ_NIL;              /* set all forward "pointers" of TQ_HEAD to nil */
    }

    tq->level = 0;

    tq->nelems = TQ_OVERHEAD_ELEMS;
    tq->nfree = nalloc - TQ_OVERHEAD_ELEMS;
    tq->free = tq->nelems;              /* head of list of free tqelems */
    /* Initialize rest of tqelems */
    nelems = tq->nelems;
    for(tqelemp = &tq->tqep[tq->nelems]; tqelemp < end; tqelemp++) {
        tqelemp->tv = TS_NONE;
        tqelemp->offset = ++nelems;     /* link on to free list */
        tqelemp->fblk = (fblk_t)OFF_NONE;
    }
    /* terminate free list through pointer of last element */
    tqelemp = &tq->tqep[tq->nalloc + 2-1];
    tqelemp->offset = TQ_NONE;
}

/*
 * Affirm that that another element can be added to tq
 */
static inline int
tq_HasSpace(const tqueue *const tq)
{
        log_assert(tq->nelems - TQ_OVERHEAD_ELEMS <= tq->nalloc);
        return (tq->nelems - TQ_OVERHEAD_ELEMS < tq->nalloc);
}

/* 
 * Get tqelem from free list.
 * Returns TQ_NONE if no free elements are left.
 */
static tqep_t
tq_get_tqelem(tqueue *const tq) 
{
    tqep_t result;
    tqelem *tpp;
    if(tq->nfree > 0) {
        result = tq->free;
        tpp = &tq->tqep[result];
        tq->free = tpp->offset;
        tq->nfree--;
        tq->nelems++;
        log_assert(result > TQ_HEAD && result != TQ_NONE);
        return result;
    }
    /* else */
    return TQ_NONE;
}


/*
 * Return tqelem to free list.
 * p is index of tqelem to be freed.
 */
static void
tq_rel_tqelem(tqueue *const tq, int level, tqep_t p) 
{
    tqelem *tpp = &tq->tqep[p];
    fb *fbp = (fb *)((char *)tq + tq->fbp_off);

    log_assert(fbp->magic == FB_MAGIC); /* check for sanity */
    log_assert(TQ_HEAD < p && p < tq->nalloc + TQ_OVERHEAD_ELEMS);
    tpp->tv = TS_NONE;
    tpp->offset = tq->free;
    /* free associated fblk */
    fb_rel(fbp, level + 1, tpp->fblk);
    tpp->fblk = (fblk_t)OFF_NONE;
    tq->free = p;
    tq->nfree++;
    tq->nelems--;
}


#define TV_CMP_LT(tv, uv) \
        ((tv).tv_sec < (uv).tv_sec || \
        ((tv).tv_sec == (uv).tv_sec && (tv).tv_usec < (uv).tv_usec))

#define TV_CMP_LE(tv, uv) \
        ((tv).tv_sec < (uv).tv_sec || \
        ((tv).tv_sec == (uv).tv_sec && (tv).tv_usec <= (uv).tv_usec))

#define TV_CMP_EQ(tv, uv) \
        ((tv).tv_sec == (uv).tv_sec && (tv).tv_usec == (uv).tv_usec)

/**
 * Adds an element to the time-queue.
 *
 * @param[in] tq      Pointer to time-queue.
 * @param[in] offset  Offset to data-portion of element to be added to
 *                    time-queue.
 * @retval    0       Success
 * @retval    ENOSPC  No more fblk-s: too many products in queue.
 */
static int
tq_add(
    tqueue* const       tq,
    const off_t         offset)
{
    fb*         fbp = (fb*)((char*)tq + tq->fbp_off);

    log_assert(fbp->magic == FB_MAGIC); // check for sanity
    log_assert(tq->nalloc != 0);
    log_assert(tq_HasSpace(tq));

    /*
     * Index of the time-queue element that's to be inserted. Taken from the
     * free list.
     */
    tqep_t      tpix = tq_get_tqelem(tq);
    log_assert(tpix != TQ_NONE);

    // Pointer to the i-th element in the time-queue
    #define TQE_PTR(i)              (tq->tqep + i)
    /*
     * Index of the time-queue element that's the next element after `elt` in
     * the k-level linked-list.
     */
    #define TQE_INDEX_NEXT(elt, k)  fbp->fblks[(elt)->fblk + k]
    /*
     * Pointer to the time-queue element that's the next element after `elt` in
     * the k-level linked-list.
     */
    #define TQE_GET_NEXT(elt, k)    TQE_PTR(TQE_INDEX_NEXT(elt, k))

    tqelem*     tp = TQE_PTR(tpix); // pointer to element to be inserted
    int         status = set_timestamp(&tp->tv); // set insertion-time to now

    if (status == ENOERR) {
        /*
         * Element in the time-queue that's just before the element to be
         * inserted for each linked-list level.
         */
        tqelem* tpp = TQE_PTR(TQ_HEAD);
        int     k = tq->level;
        tqelem* update[MAXLEVELS];

        // Find `tpp`
        do {
            // Level `k` element that follows `tpp`.
            tqelem* tqp = TQE_GET_NEXT(tpp, k);

            // Advance through level `k` linked-list while(q->key < key) {...}
            while (TV_CMP_LT(tqp->tv, tp->tv)) {
                tpp = tqp;
                tqp = TQE_GET_NEXT(tpp, k);
            }

            if (!TV_CMP_EQ(tqp->tv, tp->tv)) {
                // The insertion-time of the new data-product is unique.
                update[k--] = tpp;
            }
            else {
                /*
                 * A data-product with the same insertion-time as the current
                 * time already exists in the time-queue. Because keys in the
                 * time-queue must be unique, the current-time is incremented
                 * and the search is restarted from the last highest-level
                 * element. This should be safe as long as the mean interval
                 * between data-product insertions is much greater than the
                 * timestamp resolution (ASSUMPTION).
                 */
                timestamp_incr(&tp->tv);
                if (k < tq->level) {
                    k = tq->level;
                    tpp = update[k];
                }
            } // found element with same time
        } while (k >= 0); // for each skiplist level

        /*
         * Found where to put the new element (just after `tpp`). Obtain a
         * skip-list node to contain it.
         */
        k = fb_ranlev(fbp);
        /*
         * The following hack limits increments in level to 1.  This messes
         * up the theoretical distribution of random levels slightly and
         * could be left out for a "purist" implementation.
         */
        if (k > tq->level)
            k = tq->level + 1;

        fblk_t fblk = fb_get(fbp, k);      // Get new fblk of level k
        if (fblk == OFF_NONE) {
            /*
             * TODO: Return `tp` to the list of free elements; i.e., reverse the
             * effects of the earlier `tq_get_tqelem(tq)` call. Unfortunately,
             * `tq_rel_tqelem()` does too much. Fortunately, this error will
             * cause the downstream LDM to terminate, so the need isn't
             * immediate.
             */
            status = errno = ENOSPC;       // out of fblk-s
        }
        else {
            tp->fblk = fblk;
            tp->offset = offset;

            if (k > tq->level) {
                for (int i = tq->level + 1; i <= k; i++)
                    update[i] = TQE_PTR(TQ_HEAD);
                tq->level = k;
            }

            /*
             * Insert the new element by having it reference the following
             * element and having the immediately previous level-k element
             * reference the new element for all level k.
             */
            do {
                tpp = update[k];
                TQE_INDEX_NEXT(tp, k) = TQE_INDEX_NEXT(tpp, k);
                TQE_INDEX_NEXT(tpp, k) = tpix;
            } while(--k >= 0);
        }
    }                                   /* insertion-time set */

    return status;
}


/*
 * Search the tqueue 'tq' for a tqelem whose time is greatest less
 * than, equal to, or least greater than 'key', according whether 'mt'
 * is TV_LT, TV_EQ, or TV_GT.  ASSUMPTION: All keys in the time-queue are 
 * unique.
 *
 * Returns the tqelem or NULL if no match.
 */
static tqelem *
tqe_find(const tqueue *const tq, const timestampt *const key, const pq_match mt)
{
    int k;
    tqep_t p;
    tqep_t q;
    const tqelem *tpp;
    const tqelem *tqp;
    fb *fbp = (fb *)((char *)tq + tq->fbp_off);

    if(tq->nelems - TQ_OVERHEAD_ELEMS == 0) {
        return NULL;
    }
    log_assert(fbp->magic == FB_MAGIC);
    p = TQ_HEAD;                /* header of skip list */
    tpp = &tq->tqep[p];
    k = tq->level;              /* level of skip list and header */
    do {
        /* q = p->forward[k]; */
        /* same as *(fbp->fblks + tpp->fblk + k) */
        q = fbp->fblks[tpp->fblk + k];
        tqp = &tq->tqep[q];
        /* while(q->key < key) {...} */
        while(TV_CMP_LT(tqp->tv, *key)) {
            p = q;
            tpp = &tq->tqep[p];
            /* q = p->forward[k]; */
            q = fbp->fblks[tpp->fblk + k];
            tqp = &tq->tqep[q];
        }
    } while(--k >= 0);
  
    /* q is the next entry >= key.  p is < than it.  */
    switch (mt) {
    case TV_LT:
        if(p == TQ_HEAD) {
            return NULL;
        } /*else */
        return (tqelem *) tpp;
    case TV_EQ:
        if(TV_CMP_EQ(tqp->tv, *key)) {
            return (tqelem *) tqp;
        } /* else */
        return NULL;
    case TV_GT:
        if(q == TQ_NIL) {
            return NULL;
        } /* else */
        if(TV_CMP_EQ(tqp->tv, *key)) {
            q = fbp->fblks[tqp->fblk];
            tqp = &tq->tqep[q];
            if(q == TQ_NIL) {
                return NULL;
            } /* else */
            return (tqelem *) tqp;
        } /* else */
        return (tqelem *) tqp;
    }
    log_error("bad value for mt: %d", mt);
    return NULL;
}


/*
 * Return the oldest (first) element in the tqueue 'tq'
 *
 * Returns the tqelem or NULL if no match. 
 */
static tqelem *
tqe_first(const tqueue *const tq) 
{
    tqep_t p;
    tqep_t q;
    const tqelem *tpp;
    const tqelem *tqp;
    fb *fbp = (fb *)((char *)tq + tq->fbp_off);

    log_assert(fbp->magic == FB_MAGIC);
    p = TQ_HEAD;                /* header of skip list */
    tpp = &tq->tqep[p];
    /* q = p->forward[0]; */
    /* same as *(fbp->fblks + tpp->fblk) */
    q = fbp->fblks[tpp->fblk];
    if(q == TQ_NIL) {
        return NULL;
    }
    tqp = &tq->tqep[q];
    return (tqelem *) tqp;
}


/*
 * Delete elem from queue; if not found, don't do anything.
 */
static void
tq_delete(tqueue *const tq, tqelem *tqep)
{
    int k, m;
    tqep_t p;
    tqep_t q;
    const tqelem *tpp;
    const tqelem *tqp;
    tqep_t update[MAXLEVELS];
    fb *fbp = (fb *)((char *)tq + tq->fbp_off);

    log_assert(fbp->magic == FB_MAGIC); /* check for sanity */
    /* p = l->header; */
    p = TQ_HEAD;
    tpp = &tq->tqep[p];
    /* k = l->level; */
    m = tq->level;
    k = m;
    do {
        /* q = p->forward[k]; */
        /* same as *(fbp->fblks + tpp->fblk + k) */
        q = fbp->fblks[tpp->fblk + k];
        tqp = &tq->tqep[q];
        log_assert((q == TQ_NIL) || (TQ_HEAD < q && q < tq->nalloc + TQ_OVERHEAD_ELEMS));
        /* while(q->key < key) { */
        /* on fast machines distinct products may have equal timestamps */
        while(TV_CMP_LT(tqp->tv, tqep->tv) ||
              (TV_CMP_EQ(tqp->tv, tqep->tv) && tqp->offset < tqep->offset)) {
            p = q;
            tpp = tqp;
            /* q = p->forward[k]; */
            q = fbp->fblks[tpp->fblk + k];
            tqp = &tq->tqep[q];
            log_assert((q == TQ_NIL) || (TQ_HEAD < q && q < tq->nalloc + TQ_OVERHEAD_ELEMS));
        }
        update[k] = p;
    } while(--k >= 0);
    /* q may have key equal or greater than the specified key.  */
    log_assert((q == TQ_NIL) || (TQ_HEAD < q && q < tq->nalloc + TQ_OVERHEAD_ELEMS));
    /* if (q->key == key) { */
    /*
     * Given the way this function is used, the equality-test of the offsets in
     * the following line is unnecessary.  It's here as a reminder and because
     * the behavior of this function should be independent of how it's used.
     */
    if (TV_CMP_EQ(tqp->tv, tqep->tv) && tqp->offset == tqep->offset) {
        for(k = 0; k <= m; k++) {
            p = update[k];
            tpp = &tq->tqep[p];
            /* if (p->forward[k] != q) { */
            if (fbp->fblks[tpp->fblk + k] != q) {
                break;
            }
            /* p->forward[k] = q->forward[k]; */
            fbp->fblks[tpp->fblk + k] = fbp->fblks[tqp->fblk + k];
        }
        /* free(q); */
        tq_rel_tqelem(tq, k - 1, q);
    
        /* update level of list, in case we just deleted highest level */
        /* while( l->header->forward[m] == NIL && m > 0 ) { */
        p = TQ_HEAD;
        tpp = &tq->tqep[p];
        while(fbp->fblks[tpp->fblk + m] == TQ_NIL && m > 0) {
            m--;
        }
        tq->level = m;
    }
    return;
}


/*
 * Return the next element by insertion time in the time queue tq, 
 * after the one pointed to by tqep.
 */
static inline tqelem *
tq_next(const tqueue *const tq, const tqelem *const tqep) 
{
    /* get the skip list array of offsets */
    fb *fbp = (fb *)((char *)tq + tq->fbp_off);
    log_assert(fbp->magic == FB_MAGIC);

    /* use the 0-level offset block to get the next element by time */
    return (tqelem *) &tq->tqep[fbp->fblks[tqep->fblk]];
}



/* End tqueue */
/* Begin region */

/*
 * This is the basic structure used for keeping track of
 * storage allocations in the queue.  It appears on the shared region
 * list, regionl * pq->rlp.
 *
 * There are three kinds of regions: in-use, free, and empty.  In-Use
 * regions have an offset and extent and are in use for containing a
 * data product (or an internal data structure).  Free regions have an
 * offset and extent but are available for use.  Empty regions are
 * just slots for regions that don't have any meaningful offset and
 * extent currently.  There are a fixed maximum number of regions in
 * any product queue, allocated initially by the "-S nproducts" option
 * to pqcreate (or by default, assuming an average product size).
 *
 * Initially, a few overhead regions are allocated for indices, one
 * large free region is allocated for products, and all the other
 * regions are empty.
 *
 * In the steady state, there are a large number of in-use regions,
 * a small number of free regions, and however many empty slots are
 * left.  When a new product comes in, it gets a free region, if there
 * is one that's big enough.  Any extra space in the region is carved
 * off to form a new free region.  When a region is added to the list
 * of free regions, a check is made to see if it can be consolidated
 * with adjacent regions to form a larger region.  This consolidation
 * can free up empty slots that go back on the list of empty regions.
 *
 * If no free region of adequate size is available, the oldest product
 * in the queue is deleted repeatedly until a large enough region
 * becomes available, either through freeing a single large enough
 * region, or through consolidation of smaller adjacent regions.
 *
 * For empty regions, the 'next' member of the region structure forms
 * a linked list of empty regions; the 'prev' member is not used.  The
 * head of the empty list is referred to by the list of empty regions,
 * pq->rlp->empty.
 *
 * For free regions, the 'next' and 'prev' members are used instead as
 * offsets into an index to quickly access the list of free regions by
 * offset and by extent, respectively.
 *
 * Two principles are important to understanding this implementation:
 * regions are manipulated through a region table without accessing
 * the actual regions at all, and offsets rather than pointers are
 * used since the region table is shared among mulitple processes.
 * Not accessing region data except to read and write products avoids
 * paging in product space just to do region management, but also
 * means we cannot use "border tag" consolidation algorithms, for
 * example.
 *
 * Since we can't store pointers in shared data space, regions are
 * accessed by an analogous "region list index" rlix for the region
 * pq->rlp->rp[rlix].
 *
 * When in use and locked, (extent, offset) are put onto a process
 * private list of regions in use, riul* pq->riulp.
 */

struct region {
    off_t offset;
    size_t extent;
    size_t next;                /* For a free region, the skip list
                                   pointer block for freelist offset
                                   index */
    size_t prev;                /* For a free region, skip list
                                   pointer block for freelist extent
                                   index */
};
typedef struct region region;

/* sentinel value for prev and next members of regions */ 
#define RL_NONE ((size_t)(-1))

#define ISALLOC  ((unsigned)0x1)        /* extent field is or'd with ISALLOC 
                                         * when allocated */
#define set_IsAlloc(rp)         (fSet((rp)->extent, ISALLOC))
#define clear_IsAlloc(rp)       (fClr((rp)->extent, ISALLOC))
#define IsAlloc(rp)     (fIsSet((rp)->extent, ISALLOC))
#define IsFree(rp)      (!IsAlloc(rp))
#define Extent(rp)      (fMask((rp)->extent, ISALLOC))

/* End region */
/* Begin regionl */

#define RL_MAGIC        0x524c4841 /* "RLHA" to check alignment, endianness */
#define RL_NALLOC_INITIAL       5

/*
 * The list of allocations in the file.
 */
struct regionl
{
    size_t nalloc;              /* total no. of in-use + free + empty regions */
    size_t nchains;             /* number of chain slots for hashing */
    size_t empty;               /* rp-index of head of empty regions */
    /* statistics */
    size_t nelems;              /* number of in-use regions */
    size_t maxelems;            /* maximum nelems so far */
    size_t nfree;               /* number of free regions */
    size_t maxfree;             /* maximum nfree so far */
    size_t maxfextent;          /* max extent of regions currently on freelist */
    size_t nempty;              /* number of empty regions */
    size_t minempty;            /* minimum nempty so far */
    size_t nbytes;              /* number of bytes in use for data products */
    size_t maxbytes;            /* max nbytes so far */
    off_t  fbp_off;             /* skip list blocks */
                                /* skip list index for free list by offset */
    size_t level_foff;          /* current level of skip list by offset */
    size_t foff;                /* head of skip list by offset */
                                /* skip list index for free list by extent */
    size_t level_fext;          /* current level of skip list by extent */
    size_t fext;                /* head of skip list by extent */

    /* the region table, containing in-use, free, and empty regions */
    region rp[RL_NALLOC_INITIAL]; /* actually nalloc long */
};
typedef struct regionl regionl;

/* Overhead for region slots used as head and tail of free skip lists */
#define RL_FREE_OVERHEAD 4
#define RL_EMPTY_HD RL_FREE_OVERHEAD

/* 
 * Create a list of nalloc empty region slots for region list rl.
 */
static void
rp_init(regionl *const rl) 
{
    region *rep;
    region *rlrp = rl->rp;
    region *const end = rlrp + (rl->nalloc + RL_FREE_OVERHEAD);
    off_t irl = RL_EMPTY_HD + 1;

    /* Note: don't need or use prev member for empty regions. */
    for(rep = rlrp + RL_EMPTY_HD; rep < end; rep++, irl++) {
        rep->offset = OFF_NONE;
        rep->extent = 0;
        rep->next = irl;        /* link up empty region list */
    }
    rep = rlrp + (irl-2);
    rep->next = RL_NONE;        /* reset last pointer to indicate end of empty list */
}


/* 
 * Get the rp-index of an empty region slot. 
 * Returns RL_NONE if no empty slots are available.
 */
static size_t 
rp_get(regionl *rl) 
{
    size_t ix;
    region *rep;
    size_t iy;
    region *rlrp = rl->rp;

    /* Just get first empty slot on list */
    if(rl->empty == RL_NONE) {
        log_assert(rl->nempty == 0);
        return RL_NONE;
    }
    ix = rl->empty;
    rep = rlrp + ix;
    iy = rep->next;
    rl->empty = iy;
    rl->nempty--;
    if(rl->nempty < rl->minempty)
        rl->minempty = rl->nempty;
    return ix;
}


/* 
 * Return the region slot at index rlix to region list rl's empty
 * region slot list.
 */
static void 
rp_rel(regionl *rl, size_t rlix) 
{
    region *rlrp = rl->rp;
    region *rep = rlrp + rlix;
    size_t rnix = rl->empty;

    log_assert(0 < rlix && rlix < rl->nalloc + RL_FREE_OVERHEAD);

    /* Just put on front of list of empties. */
    rep->next = rnix;
    rep->offset = OFF_NONE;
    rep->extent = 0;
    rl->empty = rlix;
    rl->nempty++;
}

/* Heads of hash chain lists.  The size of this struct depends on the
 * number of products (pq->nalloc).  It is placed directly after the
 * regionl struct. */
struct rlhash {
  size_t magic;
  size_t chains[RL_NALLOC_INITIAL]; /* heads of lists of regions */
};
typedef struct rlhash rlhash;


/*
 * Returns 1 if prime, 0 if composite.
 * Used to get prime for hashing, on the order of the number of product slots.
 */
static int
isprime(unsigned long n) {
  unsigned long d;

  log_assert(n <= 4294967290UL);   /* if larger: infinite loop with 32 bit longs */
  if (n <= 1)
    return 0;
  if (n <= 19)
    if (n==2 || n==3 || n==5 || n==7 || n==11 || n==13 || n==17 || n==19)
      return 1;
  if ( n%2==0||n%3==0||n%5==0||n%7==0||n%11==0||n%13==0||n%17==0||n%19==0)
    return 0;
  for(d = 23; d*d <= n; d += 2) {
    if (n % d == 0)
      return 0;
  }
  return 1;
}

static unsigned long
prevprime(unsigned long n) {/* find largest prime <= n */
  log_assert (n > 1);
  if(n == 2)
    return n;
  if(n%2 == 0)
    n--;
  while(n > 0) {
    if(isprime(n))
      return n;
    n -= 2;
  }
  return 0;                     /* NOT REACHED */
}

/* Tuning parameter, expected length of hash chain lists, hence the
 * expected number of list elements to be examined in an unsuccessful
 * search.  Making this smaller will decrease region insertion,
 * deletion, and find times at the expense of more space in the queue
 * to hold a larger number of hash chain lists.  */
#define RL_EXP_CHAIN_LEN  4

/*
 * Returns number of chains required for the specified number of elements.
 */
static inline size_t
rlhash_nchains(size_t const nelems) 
{
  return prevprime(nelems / RL_EXP_CHAIN_LEN);
}

/*
 * For an rlhash which is nelems long, return how much space it will
 * consume.
 */
static inline size_t
rlhash_sz(size_t nelems)
{
        size_t sz = sizeof(rlhash) - sizeof(size_t) * RL_NALLOC_INITIAL;
        sz += nelems * sizeof(size_t);
        return sz;
}

/*
 * For a region list which is nelems long, return how much space it will
 * consume, *without* the auxilliary rlhash structure.
 */
static inline size_t
rlwo_sz(size_t nelems) 
{
        size_t sz = sizeof(regionl) - sizeof(region) * RL_NALLOC_INITIAL;
        /* extra slots for RL_FREE_HD, RL_FREE_TL, ... */
        sz += (nelems + RL_FREE_OVERHEAD) * sizeof(region);
        return sz;
}

typedef struct {
    size_t prev_nelems;
    size_t size;
} RlPar;

/*
 * For a region list which is nelems long,
 * return how much space the rl will consume.
 */
static size_t
rl_sz(const size_t nelems)
{
    log_assert(nelems);
    static size_t prev_nelems = 0;
    static size_t size;
    if (nelems != prev_nelems) {
        prev_nelems = nelems;
        size = rlwo_sz(nelems) + rlhash_sz(rlhash_nchains(nelems));
    }
    return size;
}

/* 
 * Hash function for offset.
 */
static inline size_t
rl_hash(size_t nchains, const off_t offset) 
{
    unsigned int n = offset;
    return n % nchains;
}

/*
 * Initialize an rlhash, with all chains empty.
 */
static void
rlhash_init(rlhash *const rlhp, size_t const nchains)
{
    size_t i;
        
    rlhp->magic = RL_MAGIC;     /* used to check we have mapped it right */
    for(i = 0; i < nchains; i++) {
        rlhp->chains[i] = RL_NONE;
    }
    return;
}

/*
 * Initialize freelist skip list by offset.
 */
static void
rl_foff_init(regionl *const rl)
{
    region *rlrp = rl->rp;
    const off_t huge_off_t =
         ((off_t)1 << (sizeof(off_t)*CHAR_BIT - 2)) +
        (((off_t)1 << (sizeof(off_t)*CHAR_BIT - 2)) - 1);
    region *foff_hd;
    region *foff_tl;
    int maxlevel;
    fblk_t *fblkp;
    int i;
    fb *fbp = (fb *)((char *)rl + rl->fbp_off);

    /* create psuedo-regions to use for head and tail of freelist
       skip list by offset, makes list maintenance cleaner */
#define RL_FOFF_HD 0
#define RL_FOFF_TL 1
    foff_hd = rlrp + RL_FOFF_HD; /* head of skip list by offset */
    foff_hd->offset = 0;
    foff_hd->extent = 0;
    maxlevel = fbp->maxsize - 1; /* maximum level of fblks */
    foff_hd->next = fb_get(fbp, maxlevel);
    foff_hd->prev = 0;  /* not used */

    foff_tl = rlrp + RL_FOFF_TL; /* tail of skip list by offset */
    foff_tl->offset = huge_off_t;
    foff_tl->extent = 0;
    foff_tl->next = fb_get(fbp, 0); /* not used */
    foff_tl->prev = 0; /* not used */

    /* set all forward "pointers" of RL_FOFF_HD to RL_FOFF_TL */
    fblkp = &fbp->fblks[foff_hd->next];
    for(i=0; i < fbp->maxsize; i++) {
        fblkp[i] = RL_FOFF_TL;
    }

    rl->level_foff = 0;
    rl->foff = RL_FOFF_HD;
}


/* The maximum value of a "size_t": */
static const size_t MAX_SIZE_T = ~(size_t)0;


/*
 * Initialize freelist skip list by extent.
 */
static void
rl_fext_init(regionl *const rl)
{
    region *rlrp = rl->rp;
    region *fext_hd;
    region *fext_tl;
    int maxlevel;
    fblk_t *fblkp;
    int i;
    fb *fbp = (fb *)((char *)rl + rl->fbp_off);

    /* create psuedo-regions to use for head and tail of freelist
       skip list by extent, makes list maintenance cleaner */
#define RL_FEXT_HD 2
#define RL_FEXT_TL 3
    fext_hd = rlrp + RL_FEXT_HD; /* head of skip list by extent */
    fext_hd->offset = 0;
    fext_hd->extent = 0;
    maxlevel = fbp->maxsize - 1; /* maximum level of fblks */
    fext_hd->next = 0;  /* not used */
    fext_hd->prev = fb_get(fbp, maxlevel);

    fext_tl = rlrp + RL_FEXT_TL; /* tail of skip list by extent */
    fext_tl->offset = 0;
    fext_tl->extent = MAX_SIZE_T;
    clear_IsAlloc(fext_tl);
    fext_tl->next = 0;  /* not used */
    fext_tl->prev = fb_get(fbp, 0); /* not used */

    /* set all forward "pointers" of RL_FEXT_HD to RL_FEXT_TL */
    fblkp = &fbp->fblks[fext_hd->prev];
    for(i=0; i < fbp->maxsize; i++) {
        fblkp[i] = RL_FEXT_TL;
    }

    rl->level_fext = 0;
    rl->fext = RL_FEXT_HD;
}

/*
 * Initialize a regionl and its associated rlhash for looking up by offset.
 */
static void
rl_init(regionl *const rl, size_t const nalloc, fb *fbp)
{
    region *rlrp = rl->rp;
    region *const end = rlrp + (nalloc + RL_FREE_OVERHEAD);
    rlhash *rlhp;

    log_assert(fbp->magic == FB_MAGIC); /* sanity check */

    rlhp = (rlhash *)end;       /* associated chains */
    
    rl->nalloc = nalloc;
    rl->nchains = rlhash_nchains(nalloc);
    rlhash_init(rlhp, rl->nchains);
    
    log_assert(rlhp->magic == RL_MAGIC); /* sanity check */
    
    rp_init(rl);                /* create list of empty region slots */
    rl->empty = RL_EMPTY_HD;    /* rp array starts out as all empty list */
    rl->nelems = 0;
    rl->maxelems = rl->nelems;
    rl->nempty = nalloc;
    rl->minempty = rl->nempty;
    rl->nbytes = 0;
    rl->maxbytes = rl->nbytes;
    /* cache offset to skip list blocks, so we can find them from only rl */
    rl->fbp_off = (char *)fbp - (char *)rl;

    rl->nfree = 0;
    rl->maxfree = rl->nfree;
    rl->maxfextent = 0;
    
    rl_foff_init(rl);
    rl_fext_init(rl);

    /* This assertion should always be preserved by conversions among
       used, free and empty regions */
    log_assert(rl->nelems + rl->nfree + rl->nempty == rl->nalloc);
    return;
}

/*
 * Affirm that that another element can be added to rl.
 */
static inline int
rl_HasSpace(const regionl *const rl)
{
        return (rl->nempty > 0);
}


/*
 * Find previous region by extent on freelist using extent skip list, in 
 * O(log nfree) time, where nfree is number of regions on freelist.
 * rl is pointer to region list.
 * rlix is index in associated region array (rl->rp) of the free region.
 * Returns index in associated array of the previous free region by extent, or
 * RL_FEXT_HD if this is the region on the freelist with the smallest extent.
 */
static size_t
rl_fext_prev(regionl *const rl, size_t rlix)
{
    region *rlrp = rl->rp;
    region *rep = rlrp + rlix;
    size_t spix;
    region *spp;
    region *sqp;
    int k;
    fb *fbp = (fb *)((char *)rl + rl->fbp_off);

    log_assert(IsFree(rep));
    log_assert(fbp->magic == FB_MAGIC); /* check for sanity */

    spix = rl->fext;            /* head of skip list by extent, p */
    spp = rlrp + spix;
    k = rl->level_fext;
    do {
        /* q = p->forward[k]; */
        size_t sqix = fbp->fblks[spp->prev + k];
        sqp = rlrp + sqix;
        /*      while(q->key < key) { */
        while(sqp->extent < rep->extent 
              || (sqp->extent == rep->extent && sqp->offset < rep->offset)) {
            spix = sqix;
            spp = sqp;
            /* q = p->forward[k]; */
            sqix = fbp->fblks[spp->prev + k];
            sqp = rlrp + sqix;
        }
    } while(--k >= 0);
    /* now p is spix is previous by extent */

#if !defined(NDEBUG)
    {
        region *left = rlrp + spix;

        log_assert(IsFree(left));
        log_assert(left->extent <= rep->extent);
    }
#endif

    return spix;
}


/*
 * Recompute the maximum extent of all the regions on the freelist,
 * rl->maxfextent, in O(log(nfree)) time.  Used after taking the free
 * region with maximum extent off of the freelist.  
*/
static inline size_t
rl_maxfextent(regionl *const rl) {
    region *rlrp = rl->rp;
    size_t rmix;                /* index of region with maximum extent */
    region *rmp;                /* region with maximum extent */

    rmix = rl_fext_prev(rl, RL_FEXT_TL);
    rmp = rlrp + rmix;

    return rmp->extent;
}


/*
 * Delete free region rlix from skip list by offset in O(log(nfree)) time.
 */
static void
rl_foff_del(regionl *rl, size_t rlix)
{
    int k, m;
    size_t spix;
    size_t sqix;
    const region *spp;
    const region *sqp;
    size_t update[MAXLEVELS];
    region *rlrp = rl->rp;
    region *rep = rlrp + rlix;
    fb *fbp = (fb *)((char *)rl + rl->fbp_off);

    log_assert(fbp->magic == FB_MAGIC); /* check for sanity */
    /* p = l->header; */
    spix = rl->foff;    /* head of skip list by offset */
    spp = rlrp + spix;
    /* k = l->level; */
    m = rl->level_foff;
    k = m;
    do {
        /* q = p->forward[k]; */
        sqix = fbp->fblks[spp->next + k];
        sqp = rlrp + sqix;
        log_assert((sqix == RL_FOFF_TL) || (rl->foff < sqix && sqix < rl->nalloc + RL_FREE_OVERHEAD));
        /* while(q->key < key) { */
        while(sqp->offset < rep->offset) {
            spix = sqix;
            spp = sqp;
            /* q = p->forward[k]; */
            sqix = fbp->fblks[spp->next + k];
            sqp = rlrp + sqix;
        }
        update[k] = spix;
    } while(--k >= 0);
    /* q may have key equal or greater than the specified key.  */
    log_assert((sqix == RL_FOFF_TL) || (rl->foff < sqix && sqix < rl->nalloc + RL_FREE_OVERHEAD));
    /* if (q->key == key) { */
    if (sqp->offset == rep->offset) {
        for(k = 0; k <= m; k++) {
            spix = update[k];
            spp = rlrp + spix;
            /* if (p->forward[k] != q) { */
            if (fbp->fblks[spp->next + k] != sqix) {
                break;
            }
            /* p->forward[k] = q->forward[k]; */
            fbp->fblks[spp->next + k] = fbp->fblks[sqp->next + k];
        }
        /* free associated fblk */
        fb_rel(fbp, k, sqp->next);
        
        /* update level of list, in case we just deleted highest level */
        /* while( l->header->forward[m] == NIL && m > 0 ) { */
        spix = rl->foff;
        spp = rlrp + spix;
        while(fbp->fblks[spp->next + m] == RL_FOFF_TL && m > 0) {
            m--;
        }
        rl->level_foff = m;
    }
}

/*
 * Delete free region rlix from skip list by extent in O(log(nfree)) time.
 */
static void
rl_fext_del(regionl *rl, size_t rlix)
{
    int k, m;
    size_t spix;
    size_t sqix;
    const region *spp;
    const region *sqp;
    size_t update[MAXLEVELS];
    region *rlrp = rl->rp;
    region *rep = rlrp + rlix;
    fb *fbp = (fb *)((char *)rl + rl->fbp_off);

    log_assert(fbp->magic == FB_MAGIC); /* check for sanity */
    /* p = l->header; */
    spix = rl->fext;    /* head of skip list by extent */
    spp = rlrp + spix;
    /* k = l->level; */
    m = rl->level_fext;
    k = m;
    do {
        /* q = p->forward[k]; */
        sqix = fbp->fblks[spp->prev + k];
        sqp = rlrp + sqix;
        log_assert((sqix == RL_FEXT_TL) || (rl->fext < sqix && sqix < rl->nalloc + RL_FREE_OVERHEAD));
        /* while(q->key < key) { */
        log_assert(sqp->extent > 0);
        /* regions with equal extents may be on freelist, so need to find right one to delete */
        while(sqp->extent < rep->extent
              || (sqp->extent == rep->extent && sqp->offset < rep->offset)) {
            spix = sqix;
            spp = sqp;
            /* q = p->forward[k]; */
            sqix = fbp->fblks[spp->prev + k];
            sqp = rlrp + sqix;
        }
        update[k] = spix;
    } while(--k >= 0);
    /* q may have key equal or greater than the specified key.  */
    /* if (q->key == key) { */
    if (sqp->extent == rep->extent) {
        for(k = 0; k <= m; k++) {
            spix = update[k];
            spp = rlrp + spix;
            /* if (p->forward[k] != q) { */
            if (fbp->fblks[spp->prev + k] != sqix) {
                break;
            }
            /* p->forward[k] = q->forward[k]; */
            fbp->fblks[spp->prev + k] = fbp->fblks[sqp->prev + k];
        }
        /* free associated fblk */
        sqp = rlrp + sqix;
        fb_rel(fbp, k, sqp->prev);
        
        /* update level of list, in case we just deleted highest level */
        /* while( l->header->forward[m] == NIL && m > 0 ) { */
        spix = rl->fext;
        spp = rlrp + spix;
        while(fbp->fblks[spp->prev + m] == RL_FEXT_TL && m > 0) {
            m--;
        }
        rl->level_fext = m;
    }
}


/*
 * Find best-fit free region from skip list by extent in O(log(nfree)) time.
 */
static size_t
rl_fext_find(regionl *rl, size_t extent)
{
    int k;
    size_t spix;
    size_t sqix;
    const region *spp;
    const region *sqp;
    region *rlrp = rl->rp;
    fb *fbp = (fb *)((char *)rl + rl->fbp_off);

    log_assert(fbp->magic == FB_MAGIC); /* check for sanity */
    /* p = l->header; */
    spix = rl->fext;    /* head of skip list by extent */
    spp = rlrp + spix;
    /* k = l->level; */
    k = rl->level_fext;
    do {
        /* q = p->forward[k]; */
        sqix = fbp->fblks[spp->prev + k];
        sqp = rlrp + sqix;
        /* while(q->key < key) { */
        /* regions with equal extents may be on freelist, so need to find right one to delete */
        while(sqp->extent < extent) {
            spp = sqp;
            /* q = p->forward[k]; */
            sqix = fbp->fblks[spp->prev + k];
            sqp = rlrp + sqix;
        }
    } while(--k >= 0);
    /* q may have key equal or greater than the specified key.  */
    return sqix;
}


/*
 * Get index of an available region for a specified extent off the
 * list of free regions, using a best fit algorithm.  Returns
 * RL_NONE if none available. Increments the number of regions in use if
 * successful. This function is the complement of `rl_put()`.
 */
static size_t
rl_get(regionl *const rl, size_t extent) 
{
    region *rlrp = rl->rp;
    region *rep;

    if(extent > rl->maxfextent)
        return RL_NONE;

    size_t sqbest = rl_fext_find(rl, extent);      /* index of best fit */
    if(sqbest == RL_FEXT_TL) {
        return RL_NONE;
    }
    rep = rlrp + sqbest;

    /* Remove free region from offset and extent skip lists */
    rl_foff_del(rl, sqbest);
    rl_fext_del(rl, sqbest);

    rl->nfree--;
    if(rep->extent == rl->maxfextent) { /* recompute maxfextent from remaining 
                                           freelist regions */
        rl->maxfextent = rl_maxfextent(rl);
    }
    rl->nelems++;
    if (rl->nelems > rl->maxelems)
        rl->maxelems = rl->nelems;
    return sqbest;
}

/* rlhash must be aligned to start right after pq->rlp */
#define RLHASHP(rl) ((rlhash *)(&(rl)->rp[(rl)->nalloc + RL_FREE_OVERHEAD]))

/*
 * Delete elem from region hashtable by offset. This function is the complement
 * of `rlhash_add()`.
 */
static void
rlhash_del(regionl *const rl, size_t rlix)
{
    region *rlrp = rl->rp;
    region *rep = rlrp + rlix;
    size_t rpix = rep->prev;    /* previous element */
    size_t rnix = rep->next;    /* next element */

    if(rpix != RL_NONE) {
        region *rpp = rlrp + rpix;
        rpp->next = rnix;
    } else {                    /* deleting head of chain */
        size_t try;
        rlhash *rlhp;

        try = rl_hash(rl->nchains, rep->offset);
        rlhp = RLHASHP(rl);
        rlhp->chains[try] = rnix;
    }
    if(rnix != RL_NONE) {
        region *rnp = rlrp + rnix;
        log_assert(IsAlloc(rnp));
        rnp->prev = rpix;
    }
    return;
}

/**
 * Add recycled region to freelist skip list by offset in O(log(nfree))
 * time.
 *
 * @param[in,out] rl    Region list
 * @param[in]     rlix  Offset of region entry
 * @retval 0            Success
 * @retval PQ_SYSTEM    Couldn't get new node for skip-list. log_error() called.
 */
static int
rl_foff_add(regionl *const rl, size_t rlix)
{
    int     status;
    region *rlrp = rl->rp;
    region *rep = rlrp + rlix;
    int k;
    size_t update[MAXLEVELS];
    size_t spix;
    region *spp;
    region *sqp;
    fb *fbp = (fb *)((char *)rl + rl->fbp_off);
    
    log_assert(fbp->magic == FB_MAGIC); /* check for sanity */
    spix = rl->foff;            /* head of skip list by offset, p */
    spp = rlrp + spix;
    k = rl->level_foff;
    do {
        /* q = p->forward[k]; */
        size_t sqix = fbp->fblks[spp->next + k];
        sqp = rlrp + sqix;
        /*      while(q->key < key) { */
        while(sqp->offset < rep->offset) {
            spix = sqix;
            spp = sqp;
            /* q = p->forward[k]; */
            sqix = fbp->fblks[spp->next + k];
            sqp = rlrp + sqix;
        }
        update[k] = spix;
    } while(--k >= 0);
                                /* found where to put new node, after spix & before sqix */
    k = fb_ranlev(fbp);
    /* Note, following hack limits increment in level to 1, messes up
       distribution of random levels slightly.  This could be left out
       for "purist" implementation. */
    if (k > rl->level_foff) {
        rl->level_foff++;
        k = rl->level_foff;
        update[k] = rl->foff;
    }
    /* get new fblk of level k */
    rep->next = fb_get(fbp, k);
    if (rep->next == (fblk_t)OFF_NONE) {
        log_error("Couldn't get skip-list node of level %d", k);
        status = PQ_SYSTEM;
    }
    else {
        do {
            spix = update[k];
            spp = rlrp + spix;
            /* q->forward[k] = p->forward[k]; */
            fbp->fblks[rep->next + k] = fbp->fblks[spp->next + k];
            /* p->forward[k] = q; */
            fbp->fblks[spp->next + k] = rlix; /* forward pointer to new region */
        } while(--k >= 0);
        status = 0;
    }
    return status;
}

/**
 * Add recycled region to freelist skip list by extent in O(log(nfree))
 * time.
 *
 * @param[in,out] rl  Region list
 * @param[in]         Offset to region entry
 * @retval PQ_SYSTEM  Couldn't get new skip-list node. `log_error()` called.
 * @retval 0          Success
 */
static int
rl_fext_add(regionl *const rl, size_t rlix)
{
    int     status;
    region *rlrp = rl->rp;
    region *rep = rlrp + rlix;
    int k;
    size_t update[MAXLEVELS];
    size_t spix;
    region *spp;
    region *sqp;
    fb *fbp = (fb *)((char *)rl + rl->fbp_off);
    
    log_assert(fbp->magic == FB_MAGIC); /* check for sanity */
    spix = rl->fext;            /* head of skip list by extent, p */
    spp = rlrp + spix;
    k = rl->level_fext;
    /* Can have multiple identical extents in list */
    do {
        /* q = p->forward[k]; */
        size_t sqix = fbp->fblks[spp->prev + k];
        sqp = rlrp + sqix;
        /*      while(q->key < key) { */
        while(sqp->extent < rep->extent
              || (sqp->extent == rep->extent && sqp->offset < rep->offset)) {
            spix = sqix;
            spp = sqp;
            /* q = p->forward[k]; */
            sqix = fbp->fblks[spp->prev + k];
            sqp = rlrp + sqix;
        }
        update[k] = spix;
    } while(--k >= 0);
                                    /* found where to put new node, after spix & before sqix */
    k = fb_ranlev(fbp);
    /* Note, following hack limits increment in level to 1, messes up
       distribution of random levels slightly.  This could be left out
       for "purist" implementation. */
    if (k > rl->level_fext) {
        rl->level_fext++;
        k = rl->level_fext;
        update[k] = rl->fext;
    }
    /* get new fblk of level k */
    rep->prev = fb_get(fbp, k);
    if (rep->prev == (fblk_t)OFF_NONE) {
        log_error("Couldn't get new skip-list node of level %d", k);
        status = PQ_SYSTEM;
    }
    else {
        do {
            spix = update[k];
            spp = rlrp + spix;
            /* q->forward[k] = p->forward[k]; */
            fbp->fblks[rep->prev + k] = fbp->fblks[spp->prev + k];
            /* p->forward[k] = q; */
            fbp->fblks[spp->prev + k] = rlix; /* forward pointer to new region */
        } while(--k >= 0);
        status = 0;
    }
    return status;
}


#if 0
static void
rl_foff_dump(regionl *const rl)
{
    region *rlrp = rl->rp;
    region *rep;
    size_t spix;
    size_t sqix;
    const region *spp;
    const region *sqp;
    fb *fbp = rl->fbp;
    off_t prev_offset = 0;

    /* p = l->header; */
    spix = rl->foff;    /* head of skip list by offset */
    spp = rlrp + spix;
    /* q = p->forward[0]; */
    sqix = fbp->fblks[spp->next];
    sqp = rlrp + sqix;
    log_debug("** Offsets:\t");                    /* debugging */
    while(sqix != RL_FOFF_TL) {
        /* p = q */
        spix = sqix;
        spp = rlrp + spix;
        log_debug("%u ", spp->offset);             /* debugging */
        log_assert(spp->offset > prev_offset);
        prev_offset = spp->offset;
        /* q = p->forward[0]; */
        sqix = fbp->fblks[spp->next];
        sqp = rlrp + sqix;
    }
}
#endif


/**
 * Return region with index rlix to the free list.
 *
 * @param[in,out] rl    Region list
 * @param[in]     rlix  Offset of region entry
 * @retval PQ_SYSTEM    Couldn't get new skip-list node. `log_error()` called.
 * @retval 0            Success
 */
static int
rl_rel(regionl *const rl, size_t rlix) 
{
    // Add to freelist skip list by offset */
    int status = rl_foff_add(rl, rlix);
    if (status) {
        log_error("Couldn't add to offset free-list");
    }
    else {
        // Add to freelist skip list by extent
        status = rl_fext_add(rl, rlix);
        if (status) {
            log_error("Couldn't add to extent free-list");
            rl_foff_del(rl, rlix);
        }
        else {
            rl->nfree++;
        }
    }
    return status;
}

/*
 * Find next region by offset on freelist using offset skip list, in O(1) time.
 * rl is pointer to region list.
 * rlix is index in associated region array (rl->rp) of the free region.
 * Returns index in associated array of the next free region by offset, or
 * RL_FOFF_TL if this is the region on the freelist with the largest offset.
 */
static size_t
rl_foff_next(regionl *const rl, size_t rlix)
{
    region *rlrp = rl->rp;
    region *rep = rlrp + rlix;
    size_t rnix;
    fb *fbp = (fb *)((char *)rl + rl->fbp_off);

    log_assert(IsFree(rep));
    log_assert(fbp->magic == FB_MAGIC); /* check for sanity */
    rnix = fbp->fblks[rep->next];

#if !defined(NDEBUG)
    {
        region *rght = rlrp + rnix;

        log_assert(IsFree(rght));
        log_assert(rght->offset > rep->offset);
    }
#endif

    return rnix;
}

/*
 * Find previous region by offset on freelist using offset skip list, in 
 * O(log nfree) time, where nfree is number of regions on freelist.
 * rl is pointer to region list.
 * rlix is index in associated region array (rl->rp) of the free region.
 * Returns index in associated array of the previous free region by offset, or
 * RL_FOFF_HD if this is the region on the freelist with the smallest offset.
 */
static size_t
rl_foff_prev(regionl *const rl, size_t rlix)
{
    region *rlrp = rl->rp;
    region *rep = rlrp + rlix;
    size_t spix;
    region *spp;
    region *sqp;
    int k;
    fb *fbp = (fb *)((char *)rl + rl->fbp_off);

    log_assert(IsFree(rep));
    log_assert(fbp->magic == FB_MAGIC); /* check for sanity */

    spix = rl->foff;            /* head of skip list by offset, p */
    spp = rlrp + spix;
    k = rl->level_foff;
    do {
        /* q = p->forward[k]; */
        size_t sqix = fbp->fblks[spp->next + k];
        sqp = rlrp + sqix;
        /*      while(q->key < key) { */
        while(sqp->offset < rep->offset) {
            spix = sqix;
            spp = sqp;
            /* q = p->forward[k]; */
            sqix = fbp->fblks[spp->next + k];
            sqp = rlrp + sqix;
        }
    } while(--k >= 0);
    /* now p is spix is previous by offset */

#if !defined(NDEBUG)
    {
        region *left = rlrp + spix;

        log_assert(IsFree(left));
        log_assert(left->offset < rep->offset);
    }
#endif

    return spix;
}


/*
 * Given a newly free'd region at rpix, merge it with any free neighbors.
 * We are using "consolidate on free" strategy.
 */
static void
rl_consolidate(regionl *const rl, size_t rpix)
{
    region *rlrp = rl->rp;
    region *rep = rlrp + rpix;
    size_t rghtix;
    size_t leftix;
    int nmerges = 0;

    rghtix = rl_foff_next(rl, rpix); /* if last, returns RL_FOFF_TL */
    leftix = rl_foff_prev(rl, rpix); /* if first, returns RL_FOFF_HD */
    
    if(rghtix != RL_FOFF_TL) { /* not last free region */
        region *rght = rlrp + rghtix;
        if(rep->offset + rep->extent == rght->offset) { /* mergeable */
            rl_fext_del(rl, rpix); /* since extent will change, delete from extent skip list first */
            rep->extent += rght->extent;
            rl_fext_add(rl, rpix); /* reinsert to keep extent skip list sorted by extent */
            rl->nfree--;
            rl_foff_del(rl, rghtix);
            rl_fext_del(rl, rghtix);
            rp_rel(rl, rghtix); /* now put right back in empty region slots */
            nmerges++;
        }
    }
    if(leftix != RL_FOFF_HD) { /* not first region */
        region *left = rlrp + leftix;
        if(left->offset + left->extent == rep->offset) /* mergeable */ {
            rl_fext_del(rl, leftix); /* since extent will change, delete from extent skip list first */
            left->extent += rep->extent;
            rl_fext_add(rl, leftix); /* reinsert to keep extent skip list sorted by extent */
            rl->nfree--;
            rl_foff_del(rl, rpix);
            rl_fext_del(rl, rpix);
            rp_rel(rl, rpix); /* put back in empty region slots */
            nmerges++;
            rep = left;
        }
    }
    if(rep->extent > rl->maxfextent)
        rl->maxfextent = rep->extent;
}


/*
 * Search the region list 'rl' for an in-use region whose offset is
 * 'offset'.  Returns the index in rl->rp[] of the region if found.
 * Otherwise, returns RL_NONE.  
 */
static size_t
rl_find(const regionl *const rl, off_t const offset)
{
    const region* rep;
    size_t try;
    size_t next;
    rlhash *rlhp;
    const region *rlrp = rl->rp;
    size_t ret;

    rlhp = RLHASHP(rl);
    log_assert(rlhp->magic == RL_MAGIC);
    
    ret = RL_NONE;
    try = rl_hash(rl->nchains, offset);
    next = rlhp->chains[try];
    while (next != RL_NONE) {
        rep = rlrp + next;
        if(offset == rep->offset) { /* found */
            ret = next;
            log_assert(IsAlloc(rep)); /* verify region is in use */
            break;
        }
        next = rep->next;
    }
    return ret;
}

/*
 * Search the regionl 'rl' for an in-use region whose offset is
 * 'offset'.  Returns 1 and sets *rpp to match if found.  Otherwise,
 * returns 0 and sets *rpp to NULL.  (For backward compatibility with
 * a previous interface) 
*/
static int
rl_r_find(regionl *const rl, off_t const offset, region **rpp)
{
        size_t rlix;
        region *rlrp = rl->rp;

        rlix = rl_find(rl, offset);
        if(rlix == RL_NONE) {
            *rpp = NULL;
            return 0;
        }
        *rpp = rlrp + rlix;
        return 1;
}


/*
 * Add in-use region to region hashtable by offset. This function is the
 * complement of `rlhash_del()`.
 */
static void
rlhash_add(regionl *const rl, size_t rpix)
{
    region *rlrp = rl->rp;
    region *rep;
    size_t try;
    size_t next;
    rlhash *rlhp;
    rlhp = RLHASHP(rl);
    log_assert(rlhp->magic == RL_MAGIC);

    rep = rlrp + rpix;
    log_assert(IsAlloc(rep));
    try = rl_hash(rl->nchains, rep->offset);
    /* link new element on front of chain */
    next = rlhp->chains[try];
    if (next != RL_NONE) {
        region *rnp = rlrp + next;
        log_assert(IsAlloc(rnp));
        rnp->prev = rpix;
    }
    rep->next = next;
    rep->prev = RL_NONE;
    rlhp->chains[try] = rpix;

    return;
}


/*
 * Allocate a new region and add it to free region list.
 * Return NULL if no more region slots left.
 */
static region *
rl_add(regionl *const rl, off_t const offset, size_t const extent)
{
    region *rlrp = rl->rp;
    region *rep = NULL;
    size_t rpix;

    rpix = rp_get(rl);          /* get an empty region */
    if (rpix == RL_NONE) {
        /* This shouldn't happen if enough product slots are allocated
           by pqcreate ... */
        log_error("Need more product slots, allocate more when creating queue");
        /* Can't call pq_del_oldest(), because that's who might have called us */
        return NULL;
    }
    log_assert(rl->nelems < rl->nalloc);
    
    rep = rlrp + rpix;
    rep->offset = offset;
    rep->extent = extent;

    // Insert into free list.  No need to consolidate.
    int status = rl_rel(rl, rpix);
    if (status) {
        log_error("Couldn't insert region into free region list");
        rp_rel(rl, rpix);
        rep = NULL;
    }
    else {
        if(rl->nfree > rl->maxfree)
            rl->maxfree = rl->nfree;
        log_assert(rl->nelems + rl->nfree + rl->nempty == rl->nalloc);
    }
    return rep;
}

/*
 * Split the soon to be in-use region indexed by rlix, into two
 * extents, putting the remainder back in free table.  If there are no
 * more empty slots from which to make a new region, returns ENOMEM,
 * otherwise ENOERR.  
*/
static int
rl_split(regionl *const rl, size_t rlix, size_t const extent)
{
        region *rlrp = rl->rp;
        region *new;
        off_t newoff;
        size_t rem;
        int status = ENOERR;

        region *low = rlrp + rlix;
        log_assert(low != NULL);
        log_assert(IsFree(low));
        log_assert(extent <= low->extent);

        rem = low->extent - extent;
        newoff = low->offset + (off_t)extent;
        new = rl_add(rl, newoff, rem);
        if(new) {
            log_assert(IsFree(new));
            low->extent = extent; /* can change extent, because this is not on freelist */
            if(rem > rl->maxfextent)
                rl->maxfextent = rem;
        } else {                /* out of empty slots, not enough allocated */
            log_error("Couldn't add split-off region to free region list");
            status = ENOMEM;
        }
        return status;
}


/**
 * Returns a region to the list of free regions. This function is the complement
 * of `rl_get()`.
 *
 * @param[in,out] rl    Pointer to the regions object.
 * @param[in]     rlix  Index of the region to be returned. Shall be less than
 *                      `rl->nalloc`.
 */
static void
rl_put(regionl *const rl, const size_t rlix)
{
    log_assert(rlix < rl->nalloc);

    rl->nelems--;

    /* Return region with index `rlix` to the free list. */
    rl_rel(rl, rlix); // increments number of free regions, `rl->nfree`
    rl_consolidate(rl, rlix); // updates maximum free extent

    /* Update statistics */
    if(rl->nfree > rl->maxfree)
        rl->maxfree = rl->nfree;

    log_assert(rl->nelems + rl->nfree + rl->nempty == rl->nalloc);
}


/*
 * Low level region free.
 * Mark region indexed by rpix as free, consolidate, and add to the free list.
 */
static void
rl_free(regionl *const rl, size_t rpix)
{
    region *rlrp = rl->rp;
    region *rep = rlrp + rpix;

    clear_IsAlloc(rep);
    rl->nbytes -= rep->extent;
    rlhash_del(rl, rpix);
    rl->nelems--;
    rl_rel(rl, rpix);           /* add to skip list indices by offset and extent */
    rl_consolidate(rl, rpix);

    /* update statistics */
    if(rl->nfree > rl->maxfree)
        rl->maxfree = rl->nfree;
    log_assert(rl->nelems + rl->nfree + rl->nempty == rl->nalloc);
}
/* End regionl */
/* Begin sx */

/*
 * The last index is by "signature".
 * This is a 128 bit md5 checksum on the
 * _data_ portion of a product.
 * This index is used for duplicate detection and
 * suppression.
 *
 * The implementation uses hashing with chaining.  (Open chaining
 * using double hashing won't work, because deletions are as common
 * as searching and insertion; every signature is eventually deleted.)  
 */

#define SX_NONE ((size_t)(-1))

/* Tuning parameter, expected length of hash chain lists, hence the
 * expected number of list elements to be examined in an unsuccessful
 * search.  Making this smaller will decrease signature insertion,
 * deletion, and find times at the expense of more space in the queue
 * to hold a larger number of hash chain lists.  */
#define SX_EXP_CHAIN_LEN  4

struct sxelem {
  signaturet sxi;             /* the signature of a product (128-bit MD5) */
  off_t offset;               /* of product associated with this signature */
  size_t next;                /* for linking sxelems on lists */
};
typedef struct sxelem sxelem;

/* The array of sxelems is both a threaded list of free sxelems and
 * the chains of sxelems that hash to the same bin
 * (sxhash->chains[i]).  */
struct sx {
#define SX_NALLOC_INITIAL       9
  size_t nalloc;                  /* including free list elements */
  size_t nelems;                  /* current number of signatures  */
  size_t nchains;                 /* actual number of chain slots */
  size_t free;                    /* index of free list for signatures */
  size_t nfree;                   /* number of free slots left */
  sxelem sxep[SX_NALLOC_INITIAL]; /* actually nalloc long */
};
typedef struct sx sx;

/* Heads of hash chain lists.  The size of this struct depends on the
 * number of products (pq->nalloc).  It is placed directly after the
 * sx struct.  */
struct sxhash {
#define SX_MAGIC        0x53584841
  size_t magic;                 /* "SXHA" to check alignment, endianness */
#define SXHASH_NALLOC_INITIAL   2
  size_t chains[SXHASH_NALLOC_INITIAL]; /* heads of lists of sxelems */
};
typedef struct sxhash sxhash;

/*
 * Returns number of chains required for the specified number of elements.
 */
static inline size_t
nchains(size_t const nelems) 
{
  return prevprime(nelems / SX_EXP_CHAIN_LEN);
}

/*
 * For an sxhash which is nelems long, return how much space it will
 * consume.
 */
static inline size_t
sxhash_sz(size_t nelems)
{
        size_t sz = sizeof(sxhash) - sizeof(off_t) * SXHASH_NALLOC_INITIAL;
        sz += nelems * sizeof(off_t);
        return sz;
}

/*
 * For a sx which is nelems long, return how much space it will
 * consume, *without* the auxilliary sxhash structure.
 */
static inline size_t
sxwo_sz(size_t nelems) 
{
        size_t sz = sizeof(sx) - sizeof(sxelem) * SX_NALLOC_INITIAL;
        sz += nelems * sizeof(sxelem);
        return sz;
}

typedef struct {
    size_t prev_nelems;
    size_t size;
} SxPar;

/*
 * For a sx which is nelems long, return how much space it will
 * consume, including the auxilliary sxhash structure.
 */
static size_t
sx_sz(const size_t nelems)
{
    log_assert(nelems);
    static size_t prev_nelems = 0;
    static size_t size;
    if (nelems != prev_nelems) {
        prev_nelems = nelems;
        size = sxwo_sz(nelems) + sxhash_sz(nchains(nelems));
    }
    return size;
}

/* 
 * Hash function for signature.
 */
static size_t 
sx_hash(size_t nchains, const signaturet sig) 
{
  size_t h;
  int i;
  unsigned int n;

  n = 0;
  for(i=0; i<4; i++)
    n = 256*n + sig[i];
  h = n % nchains;
  return h;
}

/*
 * Initialize an sxhash, with all chains empty.
 */
static void
sxhash_init(sxhash *const sxhp, size_t const nchains)
{
  size_t i;
        
  sxhp->magic = SX_MAGIC;       /* used to check we have mapped it right */
  for(i = 0; i < nchains; i++) {
    sxhp->chains[i] = SX_NONE;
  }
  return;
}


/*
 * Initialize an sx (and its associated sxhash).
 * We define number of chains so that expected length of each chain will be
 * SX_EXP_CHAIN_LEN.
 */
static void
sx_init(sx *const sx, size_t const nalloc)
{
        sxelem *sxep;
        sxelem *const end = &sx->sxep[nalloc];
        sxhash *sxhp;
        off_t isx = 1;

        sxhp = (sxhash *)end;   /* associated chains */

        sx->nalloc = nalloc;
        sx->nelems = 0;
        sx->nchains = nchains(nalloc);
        sxhash_init(sxhp, sx->nchains);

        log_assert(sxhp->magic == SX_MAGIC); /* sanity check */

        for(sxep = &sx->sxep[0]; sxep < end; sxep++, isx++)
        {
                memset(sxep->sxi, 0, sizeof(signaturet));
                sxep->offset = OFF_NONE;
                sxep->next = isx; /* link up free list */
        }
        sxep = &sx->sxep[isx-2];
        sxep->next = SX_NONE;     /* reset last pointer to end of free list */
        sx->free = 0;             /* sxep array starts out as all free list */
        sx->nfree = nalloc;
        return;
}

/*
 * Comparison function used in sx_find() below.  
 * Returns 1 if sig1 equals sig2, 0 otherwise.
 */
static inline int
sx_compare(const signaturet sig1, const signaturet sig2)
{
  return 0 == memcmp(sig1, sig2, sizeof(signaturet));
}

/*
 * Get index of an available sxelem off the free list.
 * Returns SX_NONE if none available.
 */
static size_t
sxelem_new(sx *const sx) 
{
    size_t avail;
    sxelem *sxep;

    if (sx->nfree == 0) {
        return SX_NONE;
    }
    avail = sx->free;
    sxep = &sx->sxep[avail];
    sx->free = sxep->next;
    sx->nfree--;
    return avail;
}

/*
 * Return sxelem[sxix] to the free list.
 */
static void
sxelem_free(sx *const sx, size_t sxix) 
{
    sxelem *sxep = &sx->sxep[sxix];
    sxep->offset = OFF_NONE;
    sxep->next = sx->free;
    sx->free = sxix;
    sx->nfree++;
}

/**
 * Searches the signature-index for an entry.
 *
 * @param[in]  sx     The signature-index to search.
 * @param[in]  sig    The signature to find.
 * @param[out] sxepp  The entry corresponding to `sig`.
 * @retval 1          Success. `*sxepp` is set.
 * @retval 0          Failure. No corresponding entry found.
 */
static int
sx_find(sx *const sx, const signaturet sig, sxelem **sxepp)
{
    sxelem* sxep;
    size_t try;
    size_t next;
    sxhash *sxhp;
    int status = 0;
        /* sxhp = (sxhash *)((char *)(sx) + sxwo_sz(sx->nalloc)); */
    sxhp = (sxhash *)(&sx->sxep[sx->nalloc]);
    log_assert(sxhp->magic == SX_MAGIC);

    /*
    log_assert(sx->nalloc != 0);
    log_assert(sx->nfree + sx->nelems == sx->nalloc);
    */

    *sxepp = (sxelem *) 0;

    try = sx_hash(sx->nchains, sig);
    next = sxhp->chains[try];
    while (next != SX_NONE) {
        sxep = &sx->sxep[next];
        if(sx_compare(sig, sxep->sxi)) { /* found */
            *sxepp = sxep;
            status = 1;
            break;
        }
        next = sxep->next;
    }
    return status;
}

/*
 * Add elem to (signature, offset) hashtable.
 * Returns added elem, or NULL if no space left to add
 */
static sxelem *
sx_add(sx *const sx, const signaturet sig, off_t const offset)
{
    sxelem* sxep;
    size_t sxix;
    size_t try;
    size_t next;                /* head of a list of signatures */
    sxhash *sxhp;
    /* sxhp = (sxhash *)((char *)(sx) + sxwo_sz(sx->nalloc)); */
    sxhp = (sxhash *)(&sx->sxep[sx->nalloc]);
    log_assert(sxhp->magic == SX_MAGIC);

    log_assert(sx->nalloc != 0);
    log_assert(sx->nfree + sx->nelems == sx->nalloc);

    /* get a new sxelem from the front of free list */
    sxix = sxelem_new(sx);
    if (sxix == SX_NONE) {
        log_error("sx_add: no slots for signatures, too many products?");
        return 0;
    }
    sxep = &sx->sxep[sxix];
    memcpy((void *)sxep->sxi, (void *)sig, sizeof(signaturet));
    sxep->offset = offset;

    try = sx_hash(sx->nchains, sig);
    /* link new element on front of chain */
    next = sxhp->chains[try];
    sxep->next = next;
    sxhp->chains[try] = sxix;

    sx->nelems++;

    /*
    LOG_DEBUG("%s:sx_add(): Added signature 0x%02x%02x%02x%02x%02x%02x%02x%02x%02x"
            "%02x%02x%02x%02x%02x%02x%02x", __FILE__, sig[0], sig[1], sig[2],
            sig[3], sig[4], sig[5], sig[6], sig[7], sig[8], sig[9], sig[10],
            sig[11], sig[12], sig[13], sig[14], sig[15]);
    */

    return sxep;
}

/*
 * Find and then delete from index.
 * Returns 1 if found and deleted, returns 0 if not found.
 */
static int
sx_find_delete(sx *const sx, const signaturet sig) 
{
    sxelem* sxep;
    size_t try;
    size_t next;
    sxhash *sxhp;
    /* sxhp = (sxhash *)((char *)(sx) + sxwo_sz(sx->nalloc)); */
    sxhp = (sxhash *)(&sx->sxep[sx->nalloc]);
    log_assert(sxhp->magic == SX_MAGIC);
    log_assert(sx->nfree + sx->nelems == sx->nalloc);

    /* find chain */
    try = sx_hash(sx->nchains, sig);
    next = sxhp->chains[try];
    sxep = &sx->sxep[next];
    if(sx_compare(sig, sxep->sxi)) { /* found */
        sxhp->chains[try] = sxep->next;
        sxelem_free(sx, next);
        sx->nelems--;
        return 1;
    }
    next = sxep->next;
    while (next != SX_NONE) {
        sxelem* osxep = sxep;
        sxep = &sx->sxep[next];
        if(sx_compare(sig, sxep->sxi)) { /* found */
            osxep->next = sxep->next;
            sxelem_free(sx, next);
            sx->nelems--;
            return 1;
        }
        next = sxep->next;
    }
    return 0;              /* not found */
}

/* End sx */
/* Begin ix */

/*
 * We use 'ix' to refer to the collection of shared
 * indexes at the end of the file.
 * (pq->rlp & pq->tqp)
 */

typedef struct {
    size_t prev_nelems_sz;
    size_t size;
    size_t prev_nelems_ptrs;
    size_t rl_size;
    size_t tq_size;
    size_t fb_size;
    size_t sx_size;
} IxPar;

/*
 * Return the amount of space required to store a
 * collection of indices, each of 'nelems'.
 */
static size_t
ix_sz(const size_t nelems, const size_t align)
{
    log_assert(nelems);
    static size_t prev_nelems;
    static size_t size;
    if (nelems != prev_nelems) {
        prev_nelems = nelems;
        size = _RNDUP(rl_sz(nelems), align) + _RNDUP(tq_sz(nelems), align)
           + _RNDUP(fb_sz(nelems), align) + _RNDUP(sx_sz(nelems), align);
    }
    return size;
}


/**
 * Computes and returns pointers to the indexes.
 *
 * @param[in]  ix      Start of index region
 * @param[in]  ixsz    Extent of the index region in bytes
 * @param[in]  nelems  Capacity of product-queue in number of products
 * @param[in]  align   Alignment parameter in bytes
 * @param[out] rlpp    Pointer to region index
 * @param[out] tqpp    Pointer to time index
 * @param[out] fbpp    Pointer to "fblk" index
 * @param[out] sxpp    Pointer to signature index
 * @retval     1       Success. `*rlpp`, `*tqpp`, `*fbpp`, and `*sxpp` are set
 * @retval     0       Failure. log_log() called.
 */
static int
ix_ptrs(
        void* const restrict     ix,
        const size_t             ixsz,
        const size_t             nelems,
        const size_t             align,
        regionl** const restrict rlpp,
        tqueue** const restrict  tqpp,
        fb** const restrict      fbpp,
        sx** const restrict      sxpp)
{
    log_assert(nelems);
    /*
     * Profiling revealed that the program pqact(1) spent about 1/3 of its time
     * in the function isprime(), which is indirectly called by the functions
     * rl_sz() and sx_sz(); thus, the following optimization. SRE 2016-06-21
     */
    static size_t prev_nelems = 0;
    static size_t rl_size;
    static size_t tq_size;
    static size_t fb_size;
    static size_t sx_size;
    if (nelems != prev_nelems) {
        prev_nelems = nelems;
        rl_size = rl_sz(nelems);
        tq_size = tq_sz(nelems);
        fb_size = fb_sz(nelems);
        sx_size = sx_sz(nelems);
    }
    *rlpp = (regionl*)ix;
    *tqpp =  (tqueue*)_RNDUP((intptr_t)((char*)(*rlpp) + rl_size), align);
    *fbpp =      (fb*)_RNDUP((intptr_t)((char*)(*tqpp) + tq_size), align);
    *sxpp =      (sx*)_RNDUP((intptr_t)((char*)(*fbpp) + fb_size), align);
    /*
     * Can't set cached `tq->fbp` and `rl->fbp` here because they are in a
     * memory-mapped file, which might be open read-only.
     */
    bool bounds_check = ((char*)(*sxpp) + sx_size) <= ((char*)ix + ixsz);
#ifdef NDEBUG
    if (!bounds_check) {
        log_error("ix=%p, ixsz=%zu, nelems=%zu, align=%zu, rl_size=%zu, "
                "tq_size=%zu, fb_size=%zu, sx_size=%zu, *sxpp=%p",
                ix, ixsz, nelems, align, rl_size, tq_size, fb_size, sx_size,
                *sxpp);
        return 0;
    }
#else
    log_assert(bounds_check);
#endif
    return 1;
}

/* End ix */
/* Begin bsrch */
/*
 * Code is derived from berkeley bsearch(),
 * But we want to know where we were on failure, so
 * we can do sorted insertions.
 */
/*
 * Perform a binary search.
 *
 * The code below is a bit sneaky.  After a comparison fails, we
 * divide the work in half by moving either left or right. If lim
 * is odd, moving left simply involves halving lim: e.g., when lim
 * is 5 we look at item 2, so we change lim to 2 so that we will
 * look at items 0 & 1.  If lim is even, the same applies.  If lim
 * is odd, moving right again involves halving lim, this time moving
 * the base up one item past p: e.g., when lim is 5 we change base
 * to item 3 and make lim 2 so that we will look at items 3 and 4.
 * If lim is even, however, we have to shrink it by one before
 * halving: e.g., when lim is 4, we still looked at item 2, so we
 * have to make lim 3, then halve, obtaining 1, so that we will only
 * look at item 3.
 */
static int
bsrch(const void *key,
        const void *base0,
        size_t nmemb,
        size_t size,
        int (*compar)(const void *, const void *),
        const void **resultp
        )
{
        const char *base = base0;
        size_t lim;
        int cmp = 0;
        const void *p = base0;

        for (lim = nmemb; lim != 0; lim /= 2) {
                p = base + (lim/2) * size;
                cmp = (*compar)(key, p);
                if (cmp == 0)
                {
                        /* found it */
                        *resultp = p;
                        return 1;
                }
                if (cmp > 0) {  /* key > p: move right */
                        base = (const char *)p + size;
                        lim--;
                }               /* else move left */
        }
        /* didn't find it */
        if(cmp > 0)
                *resultp = (const char *)p + size;
        else    
                *resultp = p;
        return 0;
}
/* End bsrch */
/* Begin riu */

/*
 * This structure is used to keep track of
 * a region which this process has 'in use'.
 * It appears on the process private list (riul *) pq->riulp.
 * 'offset' is the seek offset of the region in the queue file,
 * 'extent' is it's size,
 * 'vp' is the memory handle being used to access the region,
 * and 'rflags' stashes the RGN_* flags with which the region was gotten.
 */
struct riu {
        off_t offset;
        size_t extent;
        void *vp;
        int rflags;
};
typedef struct riu riu;

/* End riu */
/* Begin riul */

/*
 * The process private list of regions in use.
 * Sorted by offset.
 *
 * TODO: This table typically contains 3 to 5 entries (verify).
 * Maintaining sort and using binary search is overkill.
 */
struct riul
{
#define RIU_NALLOC_INITIAL      255
        size_t sz;
        size_t nalloc;
        size_t nelems;
        size_t maxelems;        /* max nelems so far */
        riu rp[RIU_NALLOC_INITIAL]; /* actually nalloc long */
};
typedef struct riul riul;
#define MIN_RIUL_SZ (3 * sizeof(size_t))


/*
 * Return the nalloc (array size) 
 * of a riul which will fit into 'sz'.
 */
static size_t
riul_nalloc(size_t sz)
{
        log_assert(sz >  MIN_RIUL_SZ);
        sz -= sizeof(size_t); /* sz */
        sz -= sizeof(size_t); /* nalloc */
        sz -= sizeof(size_t); /* nelems */
        sz -= sizeof(size_t); /* maxelems */
        sz /= sizeof(riu);
        return sz;
}


/*
 * Initialize (re Initialize) a riul
 */
static void
riul_init(riul *const rl, size_t const nelems, size_t sz)
{
        riu *rp;
        size_t nalloc = riul_nalloc(sz);
        riu *const end = &rl->rp[nalloc];

        log_assert(rl != NULL);
        log_assert(sz != 0);
        log_assert(nelems == 0 || rl->nelems == nelems);
        log_assert(nelems == 0 || rl->nalloc < nalloc);

        rl->sz = sz;
        rl->nalloc = nalloc;
        rl->nelems = nelems;
        if(nelems == 0)
            rl->maxelems = 0;
        for(rp = &rl->rp[nelems];
                        rp < end; rp++)
        {
                rp->offset = OFF_NONE;
                rp->extent = 0;
                rp->vp = NULL;
                rp->rflags = 0;
        }
}


/*
 * Affirm that that another element can be added to rl
 */
static int
riul_HasSpace(const riul *const rl)
{
        log_assert(rl->nelems <= rl->nalloc);
        return (rl->nelems < rl->nalloc);
}


/**
 * Comparison function used by bsrch() in riul_r_find() below.
 * The riul is sorted by region offset.
 */
static int
riul_r_compare(const void *vp1, const void *vp2)
{
    const riu*  riu1 = vp1;
    const riu*  riu2 = vp2;
    const off_t diff = riu1->offset - riu2->offset;
    return (diff < 0)
            ? -1
            : (diff == 0)
              ? 0
              : 1;
}


/**
 * Searches a list of regions-in-use for one with a given or greater offset.
 *
 * @param[in]  rl      List of regions-in-use
 * @param[in]  offset  Offset to search for
 * @param[out] rpp     Pointer to region-in-use with given offset or to the next
 *                     region in the list with a greater offset
 * @retval     1       Region with given offset found
 * @retval     0       Region with given offset not found
 */
static int
riul_r_find(
        const riul* const restrict rl,
        off_t const                offset,
        riu* const* const restrict rpp)
{
        int status;
        riu rgn;
        rgn.offset = offset;
        status = bsrch(&rgn,
                rl->rp, rl->nelems, sizeof(riu), riul_r_compare,
                        (const void **)rpp);
        log_assert(status == 0 || (*rpp)->vp != NULL);
        return status;
}


/**
 * Finds a region-in-use in a regions-in-use list.
 *
 * @param[in] rl          List of regions-in-use
 * @param[in] offset      Offset of region to be found
 * @retval    NULL        No such region found
 * @return                Pointer to region-in-use entry
 */
static riu*
riul_find(
        const riul* const rl,
        const off_t       offset)
{
        riu* rp;
        return riul_r_find(rl, offset, &rp)
                ? rp
                : NULL;
}

/**
 * Add elem to list, maintain sort by offset
 *
 * @param[in,out] riulpp  List of regions-in-use. `*riulpp` may be modified on
 *                        return.
 * @param[in] growby      Amount, in bytes, to grow the list by if necessary
 * @param[in] offset      Offset to region in bytes
 * @param[in] extent      Extent of region in bytes
 * @param[in] vp          Pointer to region's data
 * @param[in] rflags      Region flags: bitwise OR of
 *                        - RGN_NOLOCK  Don't lock region; locking handled
 *                                      elsewhere
 *                        - RGN_WRITE   Region will be modified
 *                        - RGN_NOWAIT  Return immediately if can't lock, else
 *                                      wait
 * @retval 0              Success.
 * @retval ENOMEM         Insufficient memory is available.
 */
static int
riul_add(
        riul** const restrict riulpp,
        const size_t          growby,
        const off_t           offset,
        const size_t          extent,
        void* const restrict  vp,
        const int             rflags,
        riu** const restrict  rpp)
{
        riu *rp;
        riul *rl = *riulpp;
        riu *end;

        if(!riul_HasSpace(rl))
        {
                /* get space */
                size_t newsz = rl->sz + growby;
                riul *nriulp = (riul *)realloc(rl, newsz);
                if(nriulp == NULL)
                        return errno;
                riul_init(nriulp, nriulp->nelems, newsz);
                *riulpp = rl = nriulp;
        }

        /*
         * "end" should be set after any potential reallocation of the
         * regions-in-use structure to ensure that "end" points into the
         * (possibly new) "rl->rp" array.
         */
        end = &rl->rp[rl->nelems];

        {
            int found = riul_r_find(rl, offset, &rp);

            log_assert(found == 0);
        }

        if(rp < end)
        {
                /* shuffle right */
                /* LOG_DEBUG("riul_add memmove: %ld", (char* )end - (char *)rp); */
                memmove(rp +1, rp, (char *)end - (char *)rp);
        }
        
        rp->offset = offset;
        rp->extent = extent;
        rp->vp = vp;
        rp->rflags = rflags;

        *rpp = rp;

        rl->nelems++;
        if(rl->nelems > rl->maxelems) {
            rl->maxelems = rl->nelems;
        }
        /* DEBUG */ /* log_assert(riul_r_find(rl, offset, &rp) != 0); */

        return ENOERR;
}

/**
 * Remove a region-in-use from the list of regions-in-use
 *
 * @param[in,out] rl  List of regions-in-use
 * @param[in]     rp  In-use region to be removed from list
 */
static void
riul_delete(
        riul* const restrict rl,
        riu* const restrict  rp)
{
        riu *end = &rl->rp[rl->nelems];
        riu *rght = rp +1;

        log_assert(&rl->rp[0] <= rp && rp < end);

        if(rght < end)
        {
                /* shuffle left */
                /* LOG_DEBUG("riul_delete memmove: %ld", (char* )end - (char *)rght); */
                memmove(rp, rght, (char *)end - (char *)rght);
        }
        end--;

        end->offset = OFF_NONE;
        end->extent = 0;
        end->vp = NULL;
        end->rflags = 0;
        rl->nelems--;
}

/* End riul */
/* Begin pqctl */

/*
 * Shared, on disk, pq control structure.
 * Fixed size, never grows.
 * At beginning of file.
 */
struct pqctl {
#define PQ_MAGIC        0x50515545      /* PQUE */
        size_t          magic;
#define PQ_VERSION      7
        size_t          version;
        off_t           datao;          /* beginning of data segment */
        off_t           ixo;            /* beginning of index segment */
        size_t          ixsz;           /* size of index segement */
        size_t          nalloc;         /* slots allocated for products */
        size_t          align;
        /* stats */
        off_t           highwater;
        size_t          maxproducts;
#define WRITE_COUNT_MAGIC       PQ_MAGIC
        unsigned        write_count_magic;
#define MAX_WRITE_COUNT         ~0u
        unsigned        write_count;
#define METRICS_MAGIC           (PQ_MAGIC+1)
        unsigned        metrics_magic;
        timestampt      mostRecent;     /* time of most recent insertion */
        timestampt      minVirtResTime; /* minimum virtual residence time */
        int             isFull;         /* is the queue full? */
#define METRICS_MAGIC_2         (PQ_MAGIC+2)
        unsigned        metrics_magic_2;
        off_t           mvrtSize;       /* data-usage in bytes when MVRT set */
        size_t          mvrtSlots;      /* slot-usage when MVRT set */
};
typedef struct pqctl pqctl;

/* End pqctl */
/* Begin pq */

/* function for putting memory to disk and releasing the memory; does a
 * possible unmap() or write() followed by an unlock */
typedef int mtofFunc(pqueue *const pq,
        off_t const offset,
        int const rflags);

/* function for getting stuff on disk into memory; does a lock possibly followed
 * by an mmap() or read() */
typedef int ftomFunc(pqueue *const pq,
        const off_t offset,
        const size_t extent,
        const int rflags,
        void **const ptrp);

/*
 * The process private pq info. (Internal structure)
 */
struct pqueue {
#define PQ_SIGSBLOCKED  0x1000  /* sav_set is valid */
        /**
         * Product-queue flags. Bitwise OR of
         * - Persistent flags:
         *   + PQ_MAPRGNS      Map region by region, default is whole file if
         *                     possible; otherwise, by region
         *   + PQ_NOCLOBBER    Don't replace an existing product-queue
         *   + PQ_NOLOCK       Disable locking (DANGER!)
         *   + PQ_NOMAP        Use `malloc/read/write/free` instead of `mmap()`
         *   + PQ_PRIVATE      `mmap()` the file `MAP_PRIVATE`. Default is
         *                     `MAP_SHARED`.
         *   + PQ_READONLY     Product-queue is read-only. Default is
         *                     read/write.
         * - Transient flag:
         *   + PQ_SIGSBLOCKED  Critical-section signals are blocked
         */
        int              pflags;
        size_t           pagesz;
        ftomFunc*        ftom;
        mtofFunc*        mtof;
        size_t           riusz;
        riul*            riulp;

        int              fd;

        pqctl*           ctlp;

        off_t            datao;
        /// start of memory-mapped file
        void*            base;

        /// Where are the indexes
        off_t            ixo;
        size_t           ixsz;
        void*            ixp;
        /// Slots allocated for products
        size_t           nalloc;

        /// Region-list index
        regionl*         rlp;
        /// timestamp index
        tqueue*          tqp;
        /// Skip-list blocks, needed in both region list and timestamp layers
        fb*              fbp;
        /// Signature index
        sx*              sxp;
        /// Private, current position in queue
        timestampt       cursor;
        /// Private, current offset in queue
        off_t            cursor_offset;
        sigset_t         sav_set;
        /// Pathname of the product-queue
        char             pathname[PATH_MAX];
        /// Number of reserved products
        long             pqe_count;
        /// Number of locked, read-only products
        long             locked_count;
        /// Smallest amount of data seen so far
		size_t           smallest_extent_seen;

        /// Mutex for concurrent access by multiple threads
        pthread_mutex_t  mutex;
        /// Thread cancellation state
		int              cancelState;
};

/* The total size of a product-queue in bytes: */
#define TOTAL_SIZE(pq) ((off_t)((pq)->ixo + (pq)->ixsz))

/* Begin OS */

/*
 * What is the system pagesize?
 */
static inline long
pagesize(void)
{
/* Hmm, aren't standards great? */
#ifndef CRAY
#if defined(_SC_PAGE_SIZE) && !defined(_SC_PAGESIZE)
#define _SC_PAGESIZE _SC_PAGE_SIZE
#endif
#ifdef _SC_PAGESIZE
        return sysconf(_SC_PAGESIZE);
#else
        return (long)getpagesize();
#endif
#else
        return 4096L;
#endif
}

/*
 * Sortof like ftruncate, except won't make the
 * file shorter.
 * May have side effect of leaving the
 * current position hosed.
 * If sparse == 0, fill in all the zero blocks (slow); 
 * else extend sparsely, without allocating zero blocks.
 *
 * Returns:
 *      EBADF   The "fd" argument is not a file descriptor open for writing.
 *      EIO     An I/O error occurred while reading from the file system.
 *      EOVERFLOW
 *              The file size in bytes or the number of blocks allocated to the
 *              file or the file serial number cannot be represented correctly.
 *      EINTR   A signal was caught during execution.
 *      EINVAL  The "len" argument was less than 0.
 *      EFBIG or EINVAL
 *              The "len" argument was greater than the maximum file size.
 *      EFBIG   The file is a regular file and length is greater than the offset
 *              maximum established in the open file description associated with
 *              "fd".
 *      EROFS   The named file resides on a read-only file system.
 */
static int
fgrow(const int fd, const off_t len, int sparse)
{
        struct stat sb;
        if (fstat(fd, &sb) < 0)
                return errno;
        if (len < sb.st_size)
                return ENOERR;
        if (sparse != 0) {
#ifdef HAVE_FTRUNCATE
            if (ftruncate(fd, len) < 0)
                return errno;
#else
            {
                int dumb = 0;
                /* beware position moved as side effect */
                if (lseek(fd, len-sizeof(dumb), SEEK_SET) < 0)
                        return errno;
                if(write(fd, &dumb, sizeof(dumb)) < 0)
                        return errno;
            }
#endif /* HAVE_FTRUNCATE */
        } else {                /* else, fill in all the zeros */
#define N_ZEROS_GROW 8192
            static const int zeros[N_ZEROS_GROW];
            size_t zsize = N_ZEROS_GROW * sizeof(int);
            size_t clen = sb.st_size;
            off_t ii;
            /* beware position moved as side effect */
            if (lseek(fd, clen, SEEK_SET) < 0)
                return errno;
            if(write(fd, zeros, (len-clen) % zsize) < 0)
                return errno;
            for(ii = clen + (len-clen) % zsize; ii < len; ii += zsize) {
                if(write(fd, zeros, zsize) < 0)
                    return errno;
            }
        }
        return ENOERR;
}

/******************************************************************************
 * File Locking Functions:
 ******************************************************************************/

/* 
 * Decode fcntl() command argument to string.
 * DEBUG
 */
static char *
s_fcntlcmd(const int cmd)
{
        switch (cmd) {
        case F_SETLKW: return "F_SETLKW";
        case F_SETLK: return "F_SETLK";
        case F_GETLK: return "F_GETLK";
        case F_DUPFD: return "F_DUPFD";
        case F_GETFD: return "F_GETFD";
        case F_SETFD: return "F_SETFD";
        case F_GETFL: return "F_GETFL";
        case F_SETFL: return "F_SETFL";
        case F_GETOWN: return "F_GETOWN";
        case F_SETOWN: return "F_SETOWN";
        }
        return "Unknown command";
}

/*
 * Decode flock l_type member to string.
 * DEBUG
 */
static char *
s_ltype(const short l_type)
{
        switch (l_type) {
        case F_WRLCK: return "F_WRLCK";
        case F_RDLCK: return "F_RDLCK";
        case F_UNLCK: return "F_UNLCK";
        }
        return "Unknown type";
}

/* 
 * Decode flock whence member to string.
 * DEBUG
 */
static char *
s_whence(const short whence)
{
        switch (whence) {
        case SEEK_SET: return "SEEK_SET";
        case SEEK_CUR: return "SEEK_CUR";
        case SEEK_END: return "SEEK_END";
        }
        return "Unknown whence";
}


/*
 * If this process would be unable to obtain a lock,
 * return the pid of the process holding the conflicting lock.
 * Else return zero or -1 on error.
 */
static pid_t
fd_isLocked(const int fd, const short l_type,
        const off_t offset, const short l_whence, const size_t extent)
{
        int cmd = F_GETLK;
        struct flock lock;
        lock.l_type = l_type;
        lock.l_start = offset;
        lock.l_whence = l_whence;
        lock.l_len = (off_t)extent;
        if(fcntl(fd, cmd, &lock) < 0)
                return ((pid_t)-1);
        return (lock.l_type == F_UNLCK ? 0 : lock.l_pid);
}


/*
 * convenience wrapper around fcntl locking
 *
 * Returns:
 *      0       Success
 *      EACCES  The "cmd" argument is F_SETLK: the type of lock (l_type) is a
 *              shared (F_RDLCK) or exclusive (F_WRLCK) lock and the segment
 *              of a file to be locked is already exclusive-locked by another
 *              process, or the type is an exclusive lock and some portion of
 *              the segment of a file to be locked is already shared-locked or
 *              exclusive-locked by another process. `log_error()` not called.
 *      EBADF   The "fd" argument is not a valid open file descriptor, or the
 *              argument "cmd" is F_SETLK or F_SETLKW, the type of lock, l_type,
 *              is a shared lock (F_RDLCK), and "fd" is not a valid file
 *              descriptor open for reading, or the type of lock l_type, is an
 *              exclusive lock (F_WRLCK), and "fd" is not a valid file
 *              descriptor open for writing. `log_error()` called.
 *      EINVAL  The "cmd" argument is invalid, or the "cmd" argument is F_GETLK,
 *              F_SETLK or F_SETLKW and "l_type", "offset", "l_whence", or
 *              "extent" is not valid, or "fd" refers to a file that does not 
 *              support locking. `log_error()` called.
 *      ENOLCK  The argument "cmd" is F_SETLK or F_SETLKW and satisfying the
 *              lock or unlock request would result in the number of locked
 *              regions in the system exceeding a system-imposed limit.
 *              `log_error()` called.
 *      EOVERFLOW
 *              The "cmd" argument is F_GETLK, F_SETLK or F_SETLKW and the
 *              smallest or, if "extent" is non-zero, the largest offset of any
 *              byte in the requested segment cannot be represented correctly
 *              in an object of type off_t. `log_error()` called.
 *      EDEADLK The "cmd" argument is F_SETLKW, the lock is blocked by some lock
 *              from another process and putting the calling process to sleep,
 *              waiting for that lock to become free would cause a deadlock.
 *              `log_error()` called.
 */
static int
fd_lock(
        const int     fd,
        const int     cmd,
        const short   l_type,
        const off_t   offset,
        const short   l_whence,
        const size_t  extent)
{
    int          status;
    struct flock lock;

    lock.l_type = l_type;
    lock.l_start = offset;
    lock.l_whence = l_whence;
    lock.l_len = (off_t)extent;

    if (fcntl(fd, cmd, &lock) < 0) {
        status = errno;

        if (status == EAGAIN) {
            status = EACCES;
        }
        else if (status == EDEADLK) {
            pid_t conflict = fd_isLocked(fd, l_type, offset, l_whence, extent);
            log_errno(status, "fcntl(%d, %s) deadlock for region {whence: %s, "
                    "off: %ld, extent: %zu} due to PID %ld", fd,
                    s_ltype(l_type), s_whence(l_whence), (long)offset, extent,
                    (long)conflict);
        }
        else if (status != EACCES) {
            log_syserr("fcntl() failed: fd=%d, cmd=%s, lock={type=%s, start=%ld, whence=%s, "
                    "len=%zu}", fd, s_fcntlcmd(cmd), s_ltype(l_type), (long)offset,
                    s_whence(l_whence), extent);
        }
    }
    else {
        status = 0;
    }

    return status;
}

/******************************************************************************
 * Memory-Mapping Functions:
 ******************************************************************************/

#ifdef HAVE_MMAP
/*
 * Wrapper around mmap()
 *
 * Returns:
 *      EACCES  The "fd" argument is not open for read, regardless of the
 *              protection specified, or "fd" is not open for write and
 *              PROT_WRITE was specified for a MAP_SHARED type mapping.
 *      EAGAIN  The mapping could not be locked in memory, if required by
 *              mlockall(), due to a lack of resources.
 *      EBADF   The "fd" argument is not a valid open file descriptor.
 *      EINVAL  The "*ptrp" argument (if MAP_FIXED was specified) or "offset" is
 *              not a multiple of the page size as returned by sysconf(), or
 *              are considered invalid by the implementation.
 *      EINVAL  The value of "mflags" is invalid (neither MAP_PRIVATE nor 
 *              MAP_SHARED is set).
 *      EMFILE  The number of mapped regions would exceed an
 *              implementation-dependent limit (per process or per system).
 *      ENODEV  The "fd" argument refers to a file whose type is not supported
 *              by mmap().
 *      ENOMEM  MAP_FIXED was specified, and the range [*ptrp, *ptrp + extent)
 *              exceeds that allowed for the address space of a process; or if
 *              MAP_FIXED was not specified and there is insufficient room in
 *              the address space to effect the mapping.
 *      ENOMEM  The mapping could not be locked in memory, if required by
 *              mlockall(), because it would require more space than the system
 *              is able to supply.
 *      ENOTSUP The implementation does not support the combination of accesses
 *              requested in the "prot" argument.
 *      ENXIO   Addresses in the range [offset, offset + extent) are invalid for
 *              the object specified by "fd".
 *      ENXIO   MAP_FIXED was specified in "mflags" and the combination of
 *              "*ptrp", "extent" and "offset" is invalid for the object
 *              specified by "fd".
 *      EOVERFLOW
 *              The file is a regular file and the value of "offset" plus 
 *              "extent" exceeds the offset maximum established in the open file
 *              description associated with "fd". 
 */
static int
mapwrap(const int fd,
        const off_t offset,
        const size_t extent,
        const int prot,
        int mflags,
        void **ptrp)
{
        int status = ENOERR;
        void *mm;

#ifdef MAP_FILE /* HP-UX */
        fSet(mflags, MAP_FILE);
#endif
#ifdef MAP_VARIABLE /* HP-UX */
        if(!fIsSet(mflags, MAP_FIXED))
                fSet(mflags, MAP_VARIABLE);
#endif
        
        mm = (void *) mmap(*ptrp, extent, prot, mflags, fd, offset);
        if(mm == MAP_FAILED)
        {
                status = errno;
                log_syserr("mmap() failure: *ptrp=%p, offset=%ld, extent=%lu",
                    *ptrp, (long)offset, (unsigned long)extent);
                log_error("Product-queue size too big?");
                return status;
        }
#if TRACE_MMAP
        log_debug("%p = mmap: %p %ld %lu", mm, *ptrp,
                (long)offset, (unsigned long)extent );
#endif

        *ptrp = mm;
        return status;
}


/*
 * Wrapper around munmap
 */
/*ARGSUSED*/
static int
unmapwrap(void *const ptr,
        const off_t offset,
        const size_t extent,
        const int mflags)
{
        int status = ENOERR;

#ifdef USE_MSYNC
        /* if(!fIsSet(mflags, M_DISCARD)) */
        if(1)
        {
#ifdef MS_ASYNC
                if(msync(ptr, extent, MS_ASYNC) == -1)
                        log_syserr("msync: %ld %lu MS_ASYNC",
                                (long)offset, (unsigned long)extent);
#else
                if(msync(ptr, extent) == -1)
                        log_syserr("msync: %ld %lu",
                                (long)offset, (unsigned long)extent);
#endif
        }
#endif /* USE_MSYNC */

#if TRACE_MMAP
        log_debug("unmap: %p %ld %lu", ptr, (long)offset, (unsigned long)extent);
#endif
        if(munmap(ptr, extent) == -1)
        {
                status = errno;
                log_syserr("munmap: %ld %lu", (long)offset, (unsigned long)extent);
                return status;
        }

        return status;
}
#endif /*HAVE_MMAP*/

/******************************************************************************
 * Lower-Level Data-Product Data-Region Functions:
 ******************************************************************************/

/**
 * Get a lock on (offset, extent).
 *
 * @param[in] rflags  Region flags: bitwise OR of
 *                    - RGN_NOLOCK  Don't lock region; locking handled elsewhere
 *                    - RGN_WRITE   Region will be modified
 *                    - RGN_NOWAIT  Return immediately if can't lock, else wait
 * @retval 0          Success
 * @retval EACCES or EAGAIN
 *                    The "cmd" argument is F_SETLK: the type of lock (l_type)
 *                    is a shared (F_RDLCK) or exclusive (F_WRLCK) lock and the
 *                    segment of a file to be locked is already exclusive-locked
 *                    by another process, or the type is an exclusive lock and
 *                    some portion of the segment of a file to be locked is
 *                    already shared-locked or exclusive-locked by another
 *                    process.
 * @retval EBADF      The "fd" argument is not a valid open file descriptor, or
 *                    the argument "cmd" is F_SETLK or F_SETLKW, the type of
 *                    lock, l_type, is a shared lock (F_RDLCK), and "fd" is not
 *                    a valid file descriptor open for reading, or the type of
 *                    lock l_type, is an exclusive lock (F_WRLCK), and "fd" is
 *                    not a valid file descriptor open for writing.
 * @retval EINVAL     The "cmd" argument is invalid, or the "cmd" argument is
 *                    F_GETLK, F_SETLK or F_SETLKW and "l_type", "offset",
 *                    "l_whence", or "extent" is not valid, or "fd" refers to a
 *                    file that does not support locking.
 * @retval ENOLCK     The argument "cmd" is F_SETLK or F_SETLKW and satisfying
 *                    the lock or unlock request would result in the number of
 *                    locked regions in the system exceeding a system-imposed
 *                    limit.
 * @retval EOVERFLOW  The "cmd" argument is F_GETLK, F_SETLK or F_SETLKW and the
 *                    smallest or, if "extent" is non-zero, the largest offset
 *                    of any byte in the requested segment cannot be represented
 *                    correctly in an object of type off_t.
 * @retval EDEADLK    The "cmd" argument is F_SETLKW, the lock is blocked by
 *                    some lock from another process and putting the calling
 *                    process to sleep, waiting for that lock to become free
 *                    would cause a deadlock.
 */
static int
rgn2_lock(pqueue *const pq,
        const off_t offset,
        const size_t extent,
        const int rflags)
{
#ifndef NDEBUG
        if(offset == pq->ixo && extent == pq->ixsz)
                log_assert(fIsSet(rflags, RGN_NOLOCK));
        else
                log_assert(!fIsSet(rflags, RGN_NOLOCK));
#endif

        if(fIsSet(rflags, RGN_NOLOCK) || fIsSet(pq->pflags, PQ_NOLOCK))
                return ENOERR;
        
        /* else */
        {
                int cmd = fIsSet(rflags, RGN_NOWAIT) ?  F_SETLK : F_SETLKW;
                short l_type = fIsSet(rflags, RGN_WRITE) ? F_WRLCK : F_RDLCK;

                int status =  fd_lock(pq->fd, cmd, l_type,
                                offset, SEEK_SET, extent);
#if TRACE_LOCK
        log_debug("%s (%ld, %lu)",
                s_ltype(l_type),
                (long)offset, (unsigned long)extent);
#endif
                return status;
        }
}


/**
 * Release lock on (offset, extent) according to the RGN_* flags rflags.
 *
 * @param[in] rflags  Region flags: bitwise OR of
 *                    - RGN_NOLOCK  Region wasn't locked
 *                    - RGN_WRITE   Region was modified
 *                    - RGN_NOWAIT  Ignored
 * @retval 0          Success.
 * @retval EBADF      The product-queue's file descriptor is invalid.
 * @retval EINVAL     The region is not valid.
 * @retval EINVAL     The region is not locked.
 * @retval EOVERFLOW  The region is too large.
 */
static int
rgn2_unlock(
        pqueue *const pq,
        const off_t offset,
        const size_t extent,
        const int rflags)
{
#ifndef NDEBUG
        if(offset == pq->ixo && extent == pq->ixsz)
                log_assert(fIsSet(rflags, RGN_NOLOCK));
#endif

        if(fIsSet(rflags, RGN_NOLOCK) || fIsSet(pq->pflags, PQ_NOLOCK))
                return ENOERR;
        /* else */
#if TRACE_LOCK
        log_debug("F_UNLCK (%ld, %lu)",
                (long)offset, (unsigned long)extent);
#endif
        return fd_lock(pq->fd, F_SETLK, F_UNLCK,
                        offset, SEEK_SET, extent);
}

/**
 * Reserves a region in a product-queue for reading or writing.
 * - Locks the region in the product-queue file
 * - Adds the region to the list of regions-in-use
 *
 * @param[in,out] pq       Product queue
 * @param[in]     offset   Offset to region in bytes
 * @param[in]     extent   Extent of region in bytes
 * @param[in]     rflags   Region flags. Bitwise OR of
 *                         - RGN_NOLOCK  Don't lock region; locking handled
 *                                       elsewhere
 *                         - RGN_WRITE   Region will be modified
 *                         - RGN_NOWAIT  Return immediately if can't lock, else
 *                                       wait
 * @param[in]     vp       Data area of region
 * @param[out]    rpp      Region in use entry
 * @retval        0        Success
 * @retval        EACCES   Region already reserved. `log_error()` *not* called.
 * @retval        EDEADLK  Deadlock condition detected. `log_error()` called.
 * @retval        ENOLCK   Number of file-locks would exceed system limit.
 *                         `log_error()` called.
 * @retval        ENOMEM   Insufficient memory available. `log_error()` called.
 * @see `rgn2_release()`
 */
static int
rgn2_reserve(
        pqueue* const restrict pq,
        const off_t            offset,
        const size_t           extent,
        const int              rflags,
        void* const restrict   vp,
        riu** const restrict   rpp)
{
    int status;

    if (riul_find(pq->riulp, offset)) {
        status = EACCES;
    }
    else {
        status = rgn2_lock(pq, offset, extent, rflags);

        if (status != EACCES) {
            if (status == EAGAIN) {
                status = EACCES;
            }
            else if (status) {
                log_error("rgn2_lock() failure");
            }
            else {
                status = riul_add(&pq->riulp, pq->pagesz, offset, extent, vp,
                        rflags, rpp);

                if (status) {
                    log_errno(status, "riul_add() failure");
                    (void)rgn2_unlock(pq, offset, extent,
                            fMask(rflags, RGN_MODIFIED|RGN_NOWAIT));
                }
            } // Region in file locked
        } // Locking region didn't return `EACCES`
    } // Region not in list of regions-in-use

    return status;
}

/**
 * Releases a reserved region in a product-queue by
 *   - Deleting the relevant entry in the list of regions-in-use
 *   - Unlocking the relevant section of the product-queue file
 *
 * @param[in,out] pq      Product queue
 * @param[in]     offset  Offset to region in bytes
 * @param[in]     rflags  Region flags. Bitwise OR of
 *                         - RGN_NOLOCK     Don't lock region; locking handled
 *                                          elsewhere
 *                         - RGN_MODIFIED   Region was modified
 * @retval        0       Success
 * @retval        EINVAL  Region with given offset isn't in use
 * @retval        EINVAL  The region is not locked.
 * @see `region_reserve()`
 */
static int
rgn2_release(
        pqueue* const pq,
        const off_t   offset,
        int const     rflags)
{
    int  status;
    riu* rp;

    if (riul_r_find(pq->riulp, offset, &rp) == 0) {
        log_error("Region with offset %ld is not in use", (long)offset);
        status = EINVAL;
    }
    else {
        log_assert(rp->offset == offset);
        log_assert(0 < rp->extent && rp->extent < TOTAL_SIZE(pq));

        size_t extent = rp->extent;

        log_assert(pq->base == NULL || (rp->vp != NULL
                 && pq->base <= rp->vp
                && (char *)rp->vp <= (char *)pq->base + pq->ixo));
        log_assert(pIf(fIsSet(rflags, RGN_MODIFIED),
                        fIsSet(rp->rflags, RGN_WRITE)));
        log_assert(fIsSet(rflags, RGN_NOLOCK) ==
                        fIsSet(rp->rflags, RGN_NOLOCK));

        riul_delete(pq->riulp, rp);
        status = rgn2_unlock(pq, offset, extent, rflags);
    }

    return status;
}

/******************************************************************************
 * Memory <-> File Synchronization Functions:
 ******************************************************************************/

/**
 * Synchronizes a region in the product-queue file to memory using `lseek(2)`,
 * `malloc(3)`, and `read(2)`. Locks the file-region.
 *
 * @param[in,out] pqueue  Product queue
 * @param[in]     offset  Offset to file region in bytes
 * @param[in]     extent  Extent of region in bytes
 * @param[in]     rflags  Region flags. Bitwise OR of
 *                        - RGN_NOLOCK  Don't lock region; locking handled
 *                                      elsewhere
 *                        - RGN_WRITE   Region will be modified
 *                        - RGN_NOWAIT  Return immediately if can't lock, else
 *                                      wait
 * @param[out]    ptrp    Location of region in memory
 * @retval        0       Success
 * @retval        EACCES  Region already reserved. `log_error()` *not* called.
 * @retval        EDEADLK Deadlock condition detected. `log_error()` called.
 * @retval        ENOLCK  Number of file-locks would exceed system limit.
 *                        `log_error()` called.
 * @retval        ENOMEM  Insufficient memory available. `log_error()` called.
 * @retval        EIO     An I/O error occurred while accessing the file system.
 *                        `log_error()` called.
 * @retval        EINTR   A signal was caught during execution. `log_error()`
 *                        called.
 * @see `f_mtof()`
 */
static int
f_ftom(
        pqueue *const pq,
        const off_t   offset,
        const size_t  extent,
        const int     rflags,
        void **const  ptrp)
{
    log_assert(pq != NULL);
    log_assert(pq->datao > 0);
    log_assert(pq->datao % pq->pagesz == 0);
    log_assert(pq->ixo >= pq->datao);
    log_assert(pq->ixo % pq->pagesz == 0);
    log_assert(pq->ixsz >= pq->pagesz);
    log_assert(pq->ixsz % pq->pagesz == 0);

    log_assert(0 <= offset && offset <= pq->ixo);
    log_assert(0 != extent && extent < TOTAL_SIZE(pq));

    log_assert(pIf(fIsSet(rflags, RGN_WRITE),
                !fIsSet(pq->pflags, PQ_READONLY)));

    void* vp = NULL;
    riu*  rp;
    int   status = rgn2_reserve(pq, offset, extent, rflags, vp, &rp);

    if (status != EACCES) {
        if (status) {
            log_error("Couldn't reserve %lu bytes starting at offset %ld",
                    (unsigned long)extent, (long)offset);
        }
        else {
            vp = malloc(extent);

            if (vp == NULL) {
                log_syserr("Couldn't malloc() %lu bytes",
                        (unsigned long)extent);
                status = errno;
            }
            else {
                rp->vp = vp; // Set region-in-use pointer; was `NULL`

                (void)memset(vp, 0, extent);

                ssize_t nread = pread(pq->fd, vp, extent, offset);

                if (nread == -1) {
                    log_syserr("Couldn't read %lu bytes from "
                            "product-queue starting at offset %ld",
                            (unsigned long)extent, (long)offset);
                    status = errno;
                }
                else if (nread && nread != extent) {
                    log_error("Read %ld bytes from product-queue at offset %ld; "
                            "expected to read %lu", (long)nread, (long)offset,
                            (unsigned long)extent);
                    status = EIO;
                }
                else {
                    // Requested number of bytes read or none at all.
                    // It's okay to read zero bytes because of pq_create(3)
                    *ptrp = vp;
                }

                if (status)
                    free(vp);
            } // Memory successfully `malloc()`ed

            if (status)
                (void)rgn2_release(pq, offset, rflags);
        } // Region successfully reserved
    } // Region not found in list of regions-in-use

    return status;
}

/**
 * Synchronizes memory to a region of a product-queue file using `write(2)`, and
 * `free(3)` for a region in use. Unlocks the file-region in question.
 *
 * @param[in,out] pq      Product queue
 * @param[in]     offset  Offset to region in file in bytes
 * @param[in]     rflags  Region flags. Bitwise inclusive OR of
 *                        - RGN_NOLOCK     Don't lock region; locking handled
 *                                         elsewhere
 *                        - RGN_MODIFIED   Region was modified
 * @retval        0       Success
 * @retval        EBADF   Product-queue isn't open for writing. `log_error()`
 *                        called.
 * @retval        EINVAL  Region with given offset isn't in use. `log_error()`
 *                        called.
 * @retval        EIO     I/O error. `log_error()` called.
 * @see `mm_ftom()`
 */
static int
f_mtof( pqueue *const pq,
        off_t const offset,
        int const rflags)
{
    log_assert(pq != NULL); /* would have core dumped already initializing */
    log_assert(pq->datao > 0);
    log_assert(pq->datao % pq->pagesz == 0);
    log_assert(pq->ixo >= pq->datao);
    log_assert(pq->ixo % pq->pagesz == 0);
    log_assert(pq->ixsz >= pq->pagesz);
    log_assert(pq->ixsz % pq->pagesz == 0);

    log_assert(pIf(fIsSet(rflags, RGN_MODIFIED),
                    !fIsSet(pq->pflags, PQ_READONLY)));

    int  status;
    riu* rp;

    if (riul_r_find(pq->riulp, offset, &rp) == 0) {
        log_error("Region with offset %ld isn't in use", (long)offset);
        status = EINVAL;
    }
    else {
        log_assert(rp->vp != NULL);

        size_t extent = rp->extent;
        void*  vp = rp->vp;

        if (!fIsSet(rflags, RGN_MODIFIED)) {
            status = 0;
        }
        else {
            log_assert(!fIsSet(pq->pflags, PQ_READONLY));

            ssize_t nwrote = pwrite(pq->fd, vp, extent, offset);

            if (nwrote == -1) {
                log_syserr("Couldn't write %lu bytes to product-queue "
                        "starting at offset %ld", (unsigned long)extent,
                        (long)offset);
                status = errno;
            }
            else if (nwrote != extent) {
                log_error("Wrote %ld bytes to product-queue at offset %ld; "
                        "expected to write %lu", nwrote, (long)offset,
                        (unsigned long)extent);
                status = EIO;
            }
            else {
                status = 0;
            }
        }

        (void)rgn2_release(pq, offset, rflags);
        free(vp);
    } // Region found in list of regions-in-use

    return status;
}


#ifdef HAVE_MMAP
/**
 * Synchronizes a region of the product-queue file to memory using `mmap(2)`.
 * Locks the file-region in question.
 *
 * @param[in,out] pqueue  Product queue
 * @param[in]     offset  Offset to region in bytes
 * @param[in]     extent  Extent of region in bytes
 * @param[in]     rflags  Region flags. Bitwise OR of
 *                        - RGN_NOLOCK  Don't lock region; locking handled
 *                                      elsewhere
 *                        - RGN_WRITE   Region will be modified
 *                        - RGN_NOWAIT  Return immediately if can't lock, else
 *                                      wait
 * @param[out]    ptrp    Location of mapped region in memory
 * @retval        0       Success
 * @retval        EACCES  Region already reserved. `log_error()` *not* called.
 * @retval        EDEADLK Deadlock condition detected. `log_error()` called.
 * @retval        ENOLCK  Number of file-locks would exceed system limit.
 *                        `log_error()` called.
 * @retval        ENOMEM  Insufficient memory available. `log_error()` called.
 * @retval        EIO     An I/O error occurred while accessing the file system.
 *                        `log_error()` called.
 * @retval        EINTR   A signal was caught during execution. `log_error()`
 *                        called.
 * @retval        EMFILE  The number of mapped regions would exceed an
 *                        implementation-dependent limit (per process or per
 *                        system). `log_error()` called.
 * @see `mm_mtof()`
 */
static int
mm_ftom(pqueue* const pq,
        const off_t   offset,
        const size_t  extent,
        const int     rflags,
        void** const  ptrp)
{
    int    mflags = fIsSet(pq->pflags, PQ_PRIVATE) ? MAP_PRIVATE : MAP_SHARED;
    int    prot = (fIsSet(pq->pflags, PQ_READONLY) && !fIsSet(rflags, RGN_WRITE))
            ? PROT_READ
            : (PROT_READ|PROT_WRITE);
    size_t rem = offset % pq->pagesz;
    size_t pagext = _RNDUP(rem + extent, pq->pagesz);

    log_assert(pq != NULL); /* would have core dumped already initializing */
    log_assert(pq->datao > 0);
    log_assert(pq->datao % pq->pagesz == 0);
    log_assert(pq->ixo >= pq->datao);
    log_assert(pq->ixo % pq->pagesz == 0);
    log_assert(pq->ixsz >= pq->pagesz);
    log_assert(pq->ixsz % pq->pagesz == 0);

    log_assert(0 <= offset && offset <= pq->ixo);
    log_assert(0 != extent && extent < TOTAL_SIZE(pq));

    log_assert(pIf(fIsSet(rflags, RGN_WRITE),
                    !fIsSet(pq->pflags, PQ_READONLY)));

    void* vp = NULL;
    riu*  rp;
    int   status = rgn2_reserve(pq, offset, extent, rflags, vp, &rp);

    if (status != EACCES) {
        if (status) {
            log_error("rgn2_reserve() failure");
        }
        else {
            if (fIsSet(prot, PROT_WRITE)) {
                status = fgrow(pq->fd, offset+extent,
                        fIsSet(pq->pflags, PQ_SPARSE));
                if (status)
                    log_errno(status, "fgrow() failure");
            }

            if (status == 0) {
                off_t  pageo = offset - rem;

                log_assert(pageo % pq->pagesz == 0);
                log_assert(pagext % pq->pagesz == 0);

                status = mapwrap(pq->fd, pageo, pagext, prot, mflags, &vp);

                if (status == 0) {
                    vp = (char*)vp + rem;
                    rp->vp = vp; // Set region-in-use pointer. Was `NULL`.
                    *ptrp = vp; // Nothing wrong with this
                } // Region was successfully memory-mapped
            } // Region in file was successfully extended if necessary

            if (status)
                (void)rgn2_release(pq, offset, rflags);
        } // Region was successfully reserved
    } // Locking file didn't return `EACCES`

    return status;
}

/**
 * Synchronizes memory to a region of the file using `mmap(2)` for a region in
 * use. Unlocks the file-region in question.
 *
 * @param[in,out] pq      Product queue
 * @param[in]     offset  Offset to region in file in bytes
 * @param[in]     rflags  Region flags. Bitwise inclusive OR of
 *                        - RGN_NOLOCK     Don't lock region; locking handled
 *                                         elsewhere
 *                        - RGN_MODIFIED   Region was modified
 * @retval        0       Success
 * @retval        EINVAL  Region with given offset isn't in use
 * @see `mm_ftom()`
 */
static int
mm_mtof(pqueue *const pq,
        off_t const   offset,
        int const     rflags)
{
    int status;

    log_assert(pq != NULL); /* would have core dumped already initializing */
    log_assert(pq->datao > 0);
    log_assert(pq->datao % pq->pagesz == 0);
    log_assert(pq->ixo >= pq->datao);
    log_assert(pq->ixo % pq->pagesz == 0);
    log_assert(pq->ixsz >= pq->pagesz);
    log_assert(pq->ixsz % pq->pagesz == 0);

    log_assert(pIf(fIsSet(rflags, RGN_MODIFIED),
                    !fIsSet(pq->pflags, PQ_READONLY)));

    riu *rp = NULL;
    if (riul_r_find(pq->riulp, offset, &rp) == 0) {
        log_error("Region with offset %ld is not in use", (long)offset);
        status = EINVAL;
    }
    else {
        log_assert(rp->vp != NULL);

        int    mflags = 0; /* TODO: translate rflags to mflags */
        off_t  rem = offset % (off_t)pq->pagesz;
        size_t extent = rp->extent;
        void*  vp = rp->vp;

        if (rem == 0) {
            status =  unmapwrap(vp, offset, extent, mflags);
        }
        else {
            off_t pageno = offset / (off_t)pq->pagesz;
            size_t pagext = _RNDUP(rem + extent, (off_t)pq->pagesz);
            vp = (char *)vp - rem;
            status = unmapwrap(vp, pageno*(off_t)pq->pagesz, pagext, mflags);
        }

        if (status) {
            log_errno(status, "unmapwrap() failure");
        }
        else {
            status = rgn2_release(pq, offset, rflags);
            if (status)
                log_errno(status, "rgn2_release() failure");
        }
    } // Region found in regions-in-use list

    return status;
}

/**
 * Memory-maps the entire product-queue.
 *
 * @param[in] pq  Pointer to the product-queue object.
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
 */
static int
mm0_map(pqueue *const pq)
{
        int status = ENOERR;
        void *vp = pq->base;
        struct stat sb;
        off_t st_size = TOTAL_SIZE(pq);
        log_assert(st_size >= 0);
        int mflags = fIsSet(pq->pflags, PQ_PRIVATE) ?
                        MAP_PRIVATE : MAP_SHARED;
        int prot = fIsSet(pq->pflags, PQ_READONLY) ?
                        PROT_READ : (PROT_READ|PROT_WRITE);

        if(fstat(pq->fd, &sb) < 0)
                return errno;
        if(st_size < sb.st_size)
        {
                st_size = sb.st_size;
        }
        else if(!fIsSet(pq->pflags, PQ_READONLY))
        {
                status = fgrow(pq->fd, st_size,
                               fIsSet(pq->pflags, PQ_SPARSE));
                if(status != ENOERR)
                        return status;
        }
        if(vp != NULL)
                fSet(mflags, MAP_FIXED);
        log_debug("Mapping %ld", (long)st_size);
        if (MAX_SIZE_T < st_size) {
            log_error("File is too big to memory-map");
            pq->base = NULL;
            status = EFBIG;
            return status;
        }
        status = mapwrap(pq->fd, 0, st_size, prot, mflags, &vp);
        log_assert(status != EACCES);
        if(status != ENOERR)
        {
                pq->base = NULL;
                return status;
        }
        log_assert(vp != NULL);
        log_assert(pIf(pq->base != NULL, pq->base == vp));
        pq->base = vp;
        return status;
}

/**
 * Synchronizes a region in the product-queue file to memory using `mmap()` to
 * map the whole file. Locks the region in question.
 *
 * @param[in] rflags  Region flags: bitwise OR of
 *                    - RGN_NOLOCK  Don't lock region; locking handled elsewhere
 *                    - RGN_WRITE   Region will be modified
 *                    - RGN_NOWAIT  Return immediately if can't lock, else wait
 *
 * `log_error()` is called for all of the following:
 * @retval EACCES     The region is already locked by another process.
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
 */
/*ARGSUSED*/
static int
mm0_ftom(pqueue *const pq,
        const off_t offset,
        const size_t extent,
        const int rflags,
        void **const ptrp)
{
    int status = 0;

    log_assert(pq != NULL);
    log_assert(pq->datao > 0);
    log_assert(pq->datao % pq->pagesz == 0);
    log_assert(pq->ixo >= pq->datao);
    log_assert(pq->ixo % pq->pagesz == 0);
    log_assert(pq->ixsz >= pq->pagesz);
    log_assert(pq->ixsz % pq->pagesz == 0);

    log_assert(0 <= offset && offset <= pq->ixo);
    log_assert(0 != extent && extent < TOTAL_SIZE(pq));

    if (!pIf(fIsSet(rflags, RGN_WRITE),
                    !fIsSet(pq->pflags, PQ_READONLY))) {
        log_error("Write access requested to readonly product-queue");
        abort();
    }

    if (pq->base == NULL) {
        status = mm0_map(pq); // First time
        if (status)
            log_errno(status, "mm0_map() failure");
    }

    if (status == 0) {
        void* const vp = (char*)pq->base + offset;
        riu*        rp; // Not used

        status = rgn2_reserve(pq, offset, extent, rflags, vp, &rp);

        if (status != EACCES) {
            if (status) {
                log_error("rgn2_reserve() failure");
            }
            else {
                *ptrp = vp;
            }
        } // File locking didn't return `EACCES`
    } // Product-queue file was successfully memory-mapped if necessary

    return status;
}

/**
 * Transfers a product-queue region from memory to file given that the entire
 * product-queue is memory-mapped.
 *
 * @param[in,out] pq      Product-queue
 * @param[in]     offset  Offset to region in bytes
 * @param[in]     rflags  Region flags. Bitwise OR of
 *                         - RGN_NOLOCK     Don't lock region; locking handled
 *                                          elsewhere
 *                         - RGN_MODIFIED   Region was modified
 * @retval EINVAL     The region could not be found in the list of in-use
 *                    regions.
 * @retval EINVAL     The region is not locked.
 * @retval EBADF      The product-queue's file descriptor is invalid.
 * @retval EOVERFLOW  The (offset + extent) specification of the region is
 *                    invalid.
 */
static int
mm0_mtof(
        pqueue *const pq,
        off_t const   offset,
        int const     rflags)
{
    log_assert(pq != NULL); /* would have core dumped already initializing */
    log_assert(pq->datao > 0);
    log_assert(pq->datao % pq->pagesz == 0);
    log_assert(pq->ixo >= pq->datao);
    log_assert(pq->ixo % pq->pagesz == 0);
    log_assert(pq->ixsz >= pq->pagesz);
    log_assert(pq->ixsz % pq->pagesz == 0);

    log_assert(pIf(fIsSet(rflags, RGN_MODIFIED),
                    !fIsSet(pq->pflags, PQ_READONLY)));

    return rgn2_release(pq, offset, rflags);
}
/* End OS */
#endif /*HAVE_MMAP*/


/**
 * Indicates if memory-mapping by individual data-products is necessary.
 *
 * Arguments:
 *      pq      Pointer to the product-queue structure.
 * Returns:
 *      0       If and only if it is not necessary to memory-map the
 *              product-queue by individual data-products.
 */
static inline int
isProductMappingNecessary(
    const pqueue* const pq)
{
    return (TOTAL_SIZE(pq) > MAX_SIZE_T)
#if __FreeBSD__ == 4
        /*
         * The operating-system is 32-bit Free BSD 4, which has a limit of
         * 2 gigabytes on the extent argument of an mmap(2) call.
         */
        || (sizeof(size_t) == 4 && TOTAL_SIZE(pq) > 2000000000)
#endif
        ;
}

/******************************************************************************
 * Higher-Level Data-Product Data-Region Functions:
 ******************************************************************************/

/*
 * Release/unlock a data region. This function is the complement of `rgn_get()`.
 *
 * @retval EINVAL     The region could not be found in the list of in-use
 *                    regions.
 * @retval EINVAL     The region is not locked.
 * @retval EBADF      The product-queue's file descriptor is invalid.
 * @retval EOVERFLOW  The (offset + extent) specification of the region is
 *                    invalid.
 */
static inline int
rgn_rel(pqueue *const pq, off_t const offset, int const rflags)
{
        log_assert(offset >= pq->datao && offset < pq->ixo);
        return (pq->mtof)(pq, offset, rflags);
}

/*
 * Get/lock a data region. This function is the complement of `rgn_rel()`. The
 * region is added to the regions-in-use list.
 *
 * Returns:
 *      0       Success
 *      `log_error()` is called for all of the following:
 *      EACCESS or EAGAIN
 *              "rflags" contains RGN_NOWAIT and the segment of a file to
 *              be locked is already exclusive-locked by another process,
 *              or "rflags" contains RGN_WRITE and some portion of the
 *              segment of the file to be locked is already shared-locked or
 *              exclusive-locked by another process.
 *      EACCES  "pq->fd" is not open for read, regardless of the protection
 *              specified, or "pq->fd" is not open for write and PROT_WRITE was
 *              specified for a MAP_SHARED type mapping.
 *      EAGAIN  The mapping could not be locked in memory, if required by
 *              mlockall(), due to a lack of resources.
 *      EBADF   "pq->fd" is not a valid open file descriptor, or "rflags"
 *              doesn't contain RGN_WRITE and "pq->fd" is not a valid file
 *              descriptor open for reading, or "rflags" contains RGN_WRITE
 *              and "pq->fd" is not a valid file descriptor open for writing.
 *      EDEADLK "rflags" doesn't contain RGN_NOWAIT, the lock is blocked by some
 *              lock from another process and putting the calling process to
 *              sleep, waiting for that lock to become free would cause a
 *              deadlock.
 *      EFBIG or EINVAL
 *              The "extent" argument was greater than the maximum file size.
 *      EFBIG   The file is a regular file and length is greater than the offset
 *              maximum established in the open file description associated with
 *              "pq->fd".
 *      EINTR   A signal was caught during execution.
 *      EINVAL  "offset", or "extent" is not valid, or "pq->fd" refers to a file
 *              that does not support locking.
 *      EINVAL  "offset" is not a multiple of the page size as returned by
 *              sysconf(), or is considered invalid by the implementation.
 *      EIO     An I/O error occurred while reading from the file system.
 *      EMFILE  The number of mapped regions would exceed an
 *              implementation-dependent limit (per process or per system).
 *      ENODEV  "pq->fd" refers to a file whose type is not supported by mmap().
 *      ENOLCK  Satisfying the request would result in the number of locked
 *              regions in the system exceeding a system-imposed limit.
 *      ENOMEM  There is insufficient room in the address space to effect the
 *              necessary mapping.
 *      ENOMEM  The mapping could not be locked in memory, if required by
 *              mlockall(), because it would require more space than the system
 *              is able to supply.
 *      ENOMEM  Insufficient memory is available.
 *      ENOTSUP The implementation does not support the combination of accesses
 *              requested in "pq->pflags" and "rflags".
 *      ENXIO   Addresses in the range [offset, offset + extent) are invalid for
 *              the object specified by "pq->fd".
 *      EOVERFLOW
 *              The smallest or, if "extent" is non-zero, the largest offset of
 *              any byte in the requested segment cannot be represented
 *              correctly in an object of type off_t.
 *      EOVERFLOW
 *              The file size in bytes or the number of blocks allocated to the
 *              file or the file serial number cannot be represented correctly.
 *      EOVERFLOW
 *              The file is a regular file and the value of "offset" plus
 *              "extent" exceeds the offset maximum established in the open file
 *              description associated with "pq->fd".
 *      EROFS   The file resides on a read-only file system.
 */
static inline int
rgn_get(pqueue *const pq, off_t const offset, size_t const extent,
         int const rflags, void **const vpp)
{
        log_assert(offset >= pq->datao && offset < pq->ixo);
        log_assert(extent >= MIN_RGN_SIZE  && extent <= pq->ixo - pq->datao);

        log_assert(pq->riulp->nelems <= pq->rlp->nelems +1);

        return (pq->ftom)(pq, offset, extent, rflags, vpp);
}

/******************************************************************************
 * XDR Functions:
 ******************************************************************************/

static void *
xinfo_i(void *buf, size_t size, enum xdr_op op,
        prod_info *infop)
{
        XDR xdrs[1] ;

        xdrmem_create(xdrs, buf, (u_int)size, op) ;

        if(!xdr_prod_info(xdrs, infop))
        {
                log_error("xinfo:%s xdr_prod_info() failed",
                        infop->ident) ;
                return NULL;
        }
        /* return data ptr. Unwarranted intimacy with xdr_mem implementation */
        return xdrs->x_private;
}


/*
 * XDR Encode or Decode "prod" to or from "buf" of size "size".
 */
static ptrdiff_t
xproduct(void *buf, size_t size, enum xdr_op op, product *prod)
{
        XDR xdrs[1] ;
        xdrmem_create(xdrs, buf, (u_int)size, op);

        if (!xdr_product(xdrs, prod))
        {
                log_error("%s xdr_product() failed",
                        prod->info.ident);
                return 0;
        }

        /*      return xlen_product(prod) ; */
        return (xdrs->x_private - xdrs->x_base);
}

/******************************************************************************
 * Lower-Level Product-Queue Functions:
 ******************************************************************************/

/**
 * Deletes a data-product if the product is not locked.
 *
 * @param[in]  pq          The product-queue.
 * @param[in]  tqep        The entry in the time-map.
 * @param[in]  rlix        The index of the entry in the region-map.
 * @param[out] info        The data-product metadata. On success, caller should
 *                         unconditionally call `xdr_free(xdr_prod_info,
 *                         (char*)info)` when it's no longer needed.
 * @retval     0           Success. `*info` is set. Caller should
 *                         unconditionally call `xdr_free(xdr_prod_info,
 *                         (char*)info)` when it's no longer needed.
 * @retval     EACCES      Product is locked (alias "Permission denied")
 * @retval     PQ_CORRUPT  The product-queue is corrupt. Error-messaged logged.
 * @retval     PQ_SYSTEM   System error. Error-message logged.
 */
static int
pq2_try_del_prod(
    pqueue* const restrict    pq,
    tqelem* const restrict    tqep,
    size_t                    rlix,
    prod_info* const restrict info)
{
    region* const rep = pq->rlp->rp + rlix;
    const off_t   offset = rep->offset;
    int           status;

    if (offset != tqep->offset) {
        log_error("Offset-to-region mismatch: time-entry=%ld, "
                "region-entry=%ld", (long)tqep->offset, (long)offset);
        status = PQ_CORRUPT;
    }
    else {
        void* vp;

        status = rgn_get(pq, offset, Extent(rep), RGN_WRITE|RGN_NOWAIT, &vp);
        if (status) {
            if (status == EACCES || status == EAGAIN) {
                log_clear();
                status = EACCES;
            }
            else {
                log_syserr("Couldn't get region (offset=%ld,extent=%zu)", offset, Extent(rep));
                status = PQ_SYSTEM;
            }
        }
        else {
            /* Get the metadata of the data-product. */
            XDR xdrs;

            xdrmem_create(&xdrs, vp, Extent(rep), XDR_DECODE);
            // Necessary for `xdr_prod_info()`
            (void)memset(info, 0, sizeof(prod_info));

            if (!xdr_prod_info(&xdrs, info)) {
                log_error("Couldn't XDR_DECODE data-product metadata");
                status = PQ_CORRUPT;
            }
            else {
                /*
                 * Remove the corresponding entry from the signature-map.
                 */
                if (sx_find_delete(pq->sxp, info->signature) == 0) {
                    log_error("pq_try_del_prod(): signature %s: Not Found",
                            s_signaturet(NULL, 0, info->signature));
                    status = PQ_CORRUPT;
                }
                else {
                    /*
                     * Remove the corresponding entry from the time-map.
                     */
                    tq_delete(pq->tqp, tqep);

                    /*
                     * Remove the corresponding entry from the region-map.
                     */
                    rl_free(pq->rlp, rlix);
                }

                if (status)
                    xdr_free(xdr_prod_info, (char*)info);
            } // `xdr_prod_info()` successful

            xdr_destroy(&xdrs);

            /* Release the data-region. */
            (void)rgn_rel(pq, offset, 0);
        } // `rgn_get()` successful
    } // region-offsets match

    return status;
}


/**
 * Set the minimum virtual residence time metrics if appropriate.
 *
 * @param[in] pq             The product-queue.
 * @param[in] receptionTime  The time the data-product was inserted into the
 *                           product-queue.
 * @param[in] info           The data-product metadata.
 */
static void
pq2_set_mvrt(
    pqueue* const restrict     pq,
    timestampt* const restrict receptionTime,
    prod_info* const restrict  info)
{
    const timestampt*           creationTime = &info->arrival;
    timestampt                  now;

    /*
     * If the product was received before it was created, then use the product's
     * reception-time as its creation-time in the computation of the product's
     * virtual residence-time.
     */
    if (TV_CMP_LT(*receptionTime, *creationTime))
        creationTime = receptionTime;

    (void)set_timestamp(&now);

    /*
     * Compute the product's residence time only if the product was
     * created before now.
     */
    if (TV_CMP_LT(*creationTime, now)) {
        timestampt      virtResTime = diff_timestamp(&now, creationTime);

        if (tvIsNone(pq->ctlp->minVirtResTime) ||
                TV_CMP_LT(virtResTime, pq->ctlp->minVirtResTime)) {
            log_info("MVRT product: %s",
                    s_prod_info(NULL, 0, info, log_is_enabled_debug));
            pq->ctlp->minVirtResTime = virtResTime;
            pq->ctlp->mvrtSize = pq->rlp->nbytes;
            pq->ctlp->mvrtSlots = pq->rlp->nelems;
        }
    }
}

/**
 * Deletes the oldest product in a product queue that is not locked.  In the
 * unlikely event that all the products in the queue are locked or a deadlock
 * is detected, returns an error status other than ENOERR.  Sets the "isFull",
 * "minVirtResTime", "mvrtSize", and "mvrtSlots" members of the product-queue
 * control block on success.
 *
 * @param[in] pq          Pointer to the product-queue object.  Shall not be
 *                        NULL.
 * @retval    0           Success.  "pq->ctlp->isFull" set to true.
 * @retval    EACESS      No unlocked products left to delete. Error-message
 *                        logged.
 * @retval    PQ_CORRUPT  The product-queue is corrupt. Error message logged.
 * @retval    PQ_SYSTEM   System error. Error-message logged.
 */
static int
pq2_del_oldest(
    pqueue* const pq)
{
    int status = EACCES;

    log_assert(pq != NULL);
    log_assert(pq->ctlp != NULL && pq->tqp != NULL);

    /* Delete the oldest unlocked data-product. */
    size_t  rlix;
    size_t  numLocked = 0;
    for (tqelem* tqep = tqe_first(pq->tqp);
            tqep && (rlix = rl_find(pq->rlp, tqep->offset)) != RL_NONE;
            tqep = tq_next(pq->tqp, tqep)) {
        prod_info  info;
        timestampt insertionTime = tqep->tv;
        status = pq2_try_del_prod(pq, tqep, rlix, &info);
        if (status == 0) {
            pq->ctlp->isFull = 1; // Mark the queue as full.
            /* Adjust the minimum virtual residence time. */
            pq2_set_mvrt(pq, &insertionTime, &info);
            xdr_free(xdr_prod_info, (char*)&info);
            return 0;
        }
        if (status != EACCES)
            return status;

        ++numLocked;
    }

    log_error("All %zu products are locked. No unlocked products left to delete!",
            numLocked);

    return status;

#if 0
    tqelem*             tqep = tqe_first(pq->tqp);
    int                 status = ENOERR;
    void*               vp = NULL;
    region*             rep = NULL;
    size_t              rlix;

    rlix = rl_find(pq->rlp, tqep->offset);
    log_assert(rlix != RL_NONE);
    rep = pq->rlp->rp + rlix;
    status = rgn_get(pq, rep->offset, Extent(rep), RGN_WRITE|RGN_NOWAIT, &vp);

    while (status != ENOERR) {
        /*
         * The current data-product is locked.  Try the next one.
         */
        tqep = tq_next(pq->tqp, tqep);
        rlix = rl_find(pq->rlp, tqep->offset);

        if (rlix == RL_NONE) {
            log_error("no unlocked products left to delete!");
            break;
        }

        rep = pq->rlp->rp + rlix;
        status =
            rgn_get(pq, rep->offset, Extent(rep), RGN_WRITE|RGN_NOWAIT, &vp);
    }

    if (status == ENOERR) {
        /*
         * Got the data region.  Start releasing associated entries.
         */
        off_t           offset = rep->offset;
        unsigned char*  signature;
        InfoBuf         infoBuf;

        /*
         * Get the metadata of the data-product.
         */
        (void)xinfo_i(vp, Extent(rep), XDR_DECODE, ib_init(&infoBuf));

        /*
         * Set the minimum virtual residence time metrics if appropriate.
         */
        {
            const timestampt*           creationTime = &infoBuf.info.arrival;
            const timestampt* const     receptionTime = &tqep->tv;
            timestampt                  now;

            /*
             * If the product was received before it was created, then use the
             * product's reception-time as its creation-time in the computation
             * of the product's virtual residence-time.
             */
            if ((TV_CMP_LT(*receptionTime, *creationTime))) {
                creationTime = receptionTime;
            }

            (void)set_timestamp(&now);

            /*
             * Compute the product's residence time only if the product was
             * created before now.
             */
            if (TV_CMP_LT(*creationTime, now)) {
                timestampt      virtResTime =
                    diff_timestamp(&now, creationTime);

                if (tvIsNone(pq->ctlp->minVirtResTime) ||
                        (TV_CMP_LT(virtResTime, pq->ctlp->minVirtResTime)))  {
                    log_info("MVRT product: %s",
                            s_prod_info(NULL, 0, &infoBuf.info, ulogIsDebug()));
                    pq->ctlp->minVirtResTime = virtResTime;
                    pq->ctlp->mvrtSize = pq->rlp->nbytes;
                    pq->ctlp->mvrtSlots = pq->rlp->nelems;
                }
            }
        }

        /*
         * Remove the corresponding entry from the time-list.
         */
        tq_delete(pq->tqp, tqep);

        /*
         * Remove the corresponding entry from the signature-list.
         */
        signature = infoBuf.info.signature;

        if (sx_find_delete(pq->sxp, signature) == 0) {
            log_error("signature %s: Not Found",
                    s_signaturet(NULL, 0, signature));
            status = EINVAL;
        }

        /*
         * Remove the corresponding entry from the region-list.
         */
        rl_free(pq->rlp, rlix);

        /*
         * Release the data region.
         */
        (void) rgn_rel(pq, offset, 0);

        /*
         * Mark the queue as full.
         */
        pq->ctlp->isFull = 1;
    }                                   /* got data region */

    return status;
#endif
}


/******************************************************************************
 * Lower-Level Reserved Product-Region Functions:
 ******************************************************************************/


/*
 * Free a region, by offset
 */
static int
rpqe_free(pqueue *pq, off_t offset, const signaturet signature)
{
        region *rp = NULL;
        size_t rlix;

        rlix = rl_find(pq->rlp, offset);
        if(rlix == RL_NONE)
        {
                log_error("offset 0x%08lx: Not Found", (long)offset);
                return EINVAL;
        }
        rp = pq->rlp->rp + rlix;
        if(IsFree(rp))
        {
                log_error("0x%08lx: Already free", (long)offset);
                return EINVAL;
        }

        if(sx_find_delete(pq->sxp, signature) == 0)
        {
                log_error("signature %s: Not Found",
                        s_signaturet(NULL, 0, signature));
                return EINVAL;
        }
        rl_free(pq->rlp, rlix);

        return ENOERR;
}


/**
 * Delete oldest elements until you have space for 'extent' Returns in *rixp the
 * region list index for a suitable region. Increments the number of regions in
 * use if successful.
 *
 * @retval    0           Success.
 * @retval    ENOMEM      No data-products to delete.
 * @retval    EACESS      No unlocked products left to delete. Error-message
 *                        logged.
 * @retval    PQ_CORRUPT  The product-queue is corrupt. Error message logged.
 * @retval    PQ_SYSTEM   System error. Error-message logged.
 */
static int
rpqe_mkspace(pqueue *const pq, size_t const extent, size_t *rixp)
{
        size_t rlix;

        log_debug("%s:rpqe_mkspace(): Deleting oldest to make space for %ld bytes",
                __FILE__, (long)extent);

        do {
                if(pq->rlp->nelems == 0)
                        return ENOMEM;

                int status = pq2_del_oldest(pq);

                if(status != ENOERR)
                        return status;

                rlix = rl_get(pq->rlp, extent);
        } while (rlix == RL_NONE) ;

        *rixp = rlix;

        return ENOERR;
}

/**
 * Delete oldest elements until a consolidation has occurred, making
 * an rl element available.  If this gets called much, you didn't
 * allocate enough product slots or a big enough queue.
 *
 * @retval    0           Success.
 * @retval    ENOMEM      No data-products to delete.
 * @retval    EACESS      No unlocked products left to delete. Error-message
 *                        logged.
 * @retval    PQ_CORRUPT  The product-queue is corrupt. Error message logged.
 * @retval    PQ_SYSTEM   System error. Error-message logged.
 */
static int
rpqe_mkslot(pqueue *const pq)
{
        /* LOG_NOTICE("Deleting oldest to get a queue slot"); */

        do {
                if(pq->rlp->nelems == 0)
                        return ENOMEM;

                int status = pq2_del_oldest(pq);

                if(status != ENOERR)
                        return status;

        } while (!rl_HasSpace(pq->rlp)) ;

        return ENOERR;
}


/**
 * Allocate a new region for a data-product from the data section (which may
 * eventually get handed to the user). Delete products in the queue, as
 * necessary, in order to make room. If successful, then
 * - A new region is added to the regions-in-use list;
 * - The number of regions in use is incremented;
 * - The number of bytes in use is incremented;
 * - The maximum number of regions in use is updated;
 * - The maximum number of bytes in use is updated;
 * - The data portion of the region is locked.
 *
 * @param[in]  pq      The product-queue.
 * @param[in]  extent  The size of the product in bytes.
 * @param[in]  sxi     The signature of the product.
 * @param[out] vpp     Pointer to the start of the data region.
 * @param[out] sxepp   Pointer to the existing signature entry if the product
 *                     already exists in the product-queue.
 * @retval PQ_DUP      A product with the same signature already exists in the
 *                     product-queue.
 * @retval PQ_CORRUPT  The product-queue is corrupt. Error message logged.
 * @retval PQ_SYSTEM   System error. Error-message logged.
 * @retval ENOMEM      Insufficient memory is available.
 * @retval ENOMEM      There is insufficient room in the address space to effect
 *                     the necessary mapping.
 * @retval EACESS      No unlocked products left to delete.
 * @retval EAGAIN      A mapping could not be locked in memory, if required by
 *                     mlockall(), due to a lack of resources.
 * @retval EDEADLK     The region's lock is blocked by some lock from another
 *                     process and putting the calling process to sleep, waiting
 *                     for that lock to become free would cause a deadlock.
 * @retval EFBIG or EINVAL
 *                     The `extent` argument was greater than the maximum file
 *                     size.
 * @retval EINTR       A signal was caught during execution.
 * @retval EINVAL      `extent` is not valid, or "pq->fd" refers to a file
 *                     that does not support locking.
 * @retval EIO         An I/O error occurred while reading from the file system.
 * @retval EMFILE      The number of mapped regions would exceed an
 *                     implementation-dependent limit (per process or per
 *                     system).
 * @retval ENODEV      `pq->fd` refers to a file whose type is not supported by
 *                     `mmap()`.
 * @retval ENOLCK      Satisfying the request would result in the number of
 *                     locked regions in the system exceeding a system-imposed
 *                     limit.
 */
static int
rpqe_new(pqueue *pq, size_t extent, const signaturet sxi,
        void **vpp, sxelem **sxepp)
{
    int           status = ENOERR;
    size_t        rlix;            /* region list index */
    region*       hit = NULL;

    /*
     * Check for duplicate
     */
    // log_debug_1("Checking for duplicate");
    if (sxi && sx_find(pq->sxp, sxi, sxepp) != 0) {
        log_debug("PQ_DUP");
        return PQ_DUP;
    }

    /* We may need to split what we find */
    if (!rl_HasSpace(pq->rlp)) {
        /* get one slot */
        // log_debug_1("Making a slot");
        status = rpqe_mkslot(pq);
        if (status != ENOERR)
            return status;
    }

    extent = _RNDUP(extent, pq->ctlp->align);
    if (extent < pq->smallest_extent_seen)
        pq->smallest_extent_seen = extent;

    // log_debug_1("Getting a region");
    rlix = rl_get(pq->rlp, extent);
    if (rlix == RL_NONE) {
        status = rpqe_mkspace(pq, extent, &rlix);
        if (status != ENOERR)
            return status;
    }
    hit = pq->rlp->rp + rlix;
    log_assert(IsFree(hit));
    #define PQ_FRAGMENT_HEURISTIC 64
    /* Don't bother to split off tiny fragments too small for any
       product we've seen */
    if (extent + pq->smallest_extent_seen + PQ_FRAGMENT_HEURISTIC < hit->extent) {
        // log_debug_1("Splitting region");
        status = rl_split(pq->rlp, rlix, extent);
        if (status != ENOERR)
            goto rl_split_failure;
    }

    log_assert((hit->offset % pq->ctlp->align) == 0);
    set_IsAlloc(hit);
    // log_debug_1("Adding region hash");
    rlhash_add(pq->rlp, rlix);

    // log_debug_1("Getting region");
    status = rgn_get(pq, hit->offset, Extent(hit), RGN_WRITE, vpp);
    if (status != ENOERR)
        goto rgn_get_failure;

    if (sxi) {
        // log_debug_1("Adding signature");
        sxelem* const sxelem = sx_add(pq->sxp, sxi, hit->offset);
        if (sxelem == NULL) {
            log_error("sx_add() failure");
            status = ENOMEM;
            goto sx_add_failure;
        }
        *sxepp = sxelem;
    }

    /* Update stats */
    off_t         highwater = hit->offset + (off_t)Extent(hit) -
            pq->ctlp->datao;
    if (highwater > pq->ctlp->highwater)
        pq->ctlp->highwater = highwater;
    if (pq->rlp->nelems >  pq->ctlp->maxproducts)
        pq->ctlp->maxproducts = pq->rlp->nelems;
    pq->rlp->nbytes += (off_t)Extent(hit);
    if (pq->rlp->nbytes > pq->rlp->maxbytes)
        pq->rlp->maxbytes = pq->rlp->nbytes;

    return status;

    sx_add_failure:
        // log_debug_1("Releasing region");
        (void)rgn_rel(pq, hit->offset, 0); // region's data portion unmodified
    rgn_get_failure:
        // log_debug_1("Deleting region hash");
        rlhash_del(pq->rlp, rlix);
        clear_IsAlloc(hit);
    rl_split_failure:
        // log_debug_1("Unsplitting region");
        rl_put(pq->rlp, rlix); // undoes `rl_get()` and `rpqe_mkspace()`
        return status;
}


/******************************************************************************
 * Control-Header Functions:
 ******************************************************************************/

/*
 * The 'ctl_xxx' functions deal with pqctl pq->ctlp AND
 * the indexes pq->rlp, pq->tqp, pq->sxp, pq->fbp via ixXXX.
 * Accesses to these areas operate on a single lock, the lock
 * on (0, pgsz), ctlp.
 */

/**
 * Release the ctl lock and write back any changes.
 *
 * @retval EINVAL     The region could not be found in the list of in-use
 *                    regions.
 * @retval EBADF      The product-queue's file descriptor is invalid.
 * @retval EOVERFLOW  The (offset + extent) specification of the region is
 *                    invalid.
 */
static int
ctl_rel(pqueue *const pq, const int rflags)
{
        int status = ENOERR;

        log_assert(pq->ctlp != NULL);
        log_assert(pq->ixp != NULL);

        if(pq->ixp != NULL)
        {
                const int stat = (pq->mtof)(pq, pq->ixo, rflags|RGN_NOLOCK);
                if(stat) {
                        log_error("mtof() failure on indexes");
                        status = stat;
                }
                pq->ixp = NULL;
                pq->rlp = NULL;
                pq->tqp = NULL;
                pq->sxp = NULL;
                pq->fbp = NULL;
        }
        
        if(pq->ctlp != NULL)
        {
                const int stat = (pq->mtof)(pq, 0, rflags);
                if(stat) {
                        log_error("mtof() failure on control block");
                        if (status == 0)
								status = stat;
                }
                pq->ctlp = NULL;
        }

        if(fIsSet(pq->pflags, PQ_SIGSBLOCKED))
        {
                /* something was set, end critical section */
                sigset_t sav_set = pq->sav_set;

                (void)sigemptyset(&pq->sav_set);
                fClr(pq->pflags, PQ_SIGSBLOCKED);

                (void) pthread_sigmask(SIG_SETMASK, &sav_set, NULL);
        }

        return status;
}


/*
 * Initialize the on disk state (ctl and indexes) of a
 * new queue file. Called by pq_create().
 */
static int
ctl_init(pqueue *const pq, size_t const align)
{
        int status = ENOERR;
        void *vp = NULL;
        size_t nalloc;

        log_assert(pq != NULL);
        log_assert(pq->pagesz != 0);
        log_assert(pq->datao > 0);
        log_assert(pq->datao % pq->pagesz == 0);
        log_assert(pq->ixo > pq->datao);
        log_assert(pq->ixo % pq->pagesz == 0);
        log_assert(pq->ixsz >= pq->pagesz);
        log_assert(pq->ixsz % pq->pagesz == 0);
        log_assert(align != 0);

        /*
         * The following isn't a critical section that's protected by
         * `pthread_sigmask(SIG_BLOCK,...)` because any error (including EINTR)
         * unlinks the created file.
         */

        /*
         * bring in the pqctl
         * N.B. No wait. Another lock implies create collision, error
         */
        status = (pq->ftom)(pq,
                         0, (size_t)pq->datao, RGN_WRITE|RGN_NOWAIT, &vp);
#ifdef HAVE_MMAP
        if(status == EIO && (pq->ftom == mm_ftom || pq->ftom == mm0_ftom))
        {
                log_notice("EIO => remote file system");
                /* try again */
                pq->ftom = f_ftom;
                pq->mtof = f_mtof;
                status = (pq->ftom)(pq,
                        0, (size_t)pq->datao, RGN_WRITE|RGN_NOWAIT, &vp);
        }
#endif
        if(status != ENOERR)
                return status;

        pq->ctlp = (pqctl *)vp;
        pq->ctlp->magic = PQ_MAGIC;
        pq->ctlp->version = PQ_VERSION;
        pq->ctlp->write_count_magic = WRITE_COUNT_MAGIC;
        pq->ctlp->write_count = 1;              /* this process is writer */
        pq->ctlp->datao = pq->datao;
        pq->ctlp->ixo = pq->ixo;
        pq->ctlp->ixsz = pq->ixsz;
        pq->ctlp->nalloc = pq->nalloc;
        pq->ctlp->highwater = 0;
        pq->ctlp->maxproducts = 0;
        pq->ctlp->align = align;
        pq->ctlp->metrics_magic = METRICS_MAGIC;
        pq->ctlp->mostRecent = TS_NONE;
        pq->ctlp->minVirtResTime = TS_NONE;
        pq->ctlp->isFull = 0;
        pq->ctlp->metrics_magic_2 = METRICS_MAGIC_2;
        pq->ctlp->mvrtSize = -1;
        pq->ctlp->mvrtSlots = 0;

        /* bring in the indexes */
        status = (pq->ftom)(pq,
                         pq->ixo, pq->ixsz, RGN_WRITE|RGN_NOLOCK, &pq->ixp);
        if(status != ENOERR)
        {
                (void)(pq->mtof)(pq, 0, 0);
                return status;
        }

        (void)ix_ptrs(pq->ixp, pq->ixsz, pq->nalloc, align, &pq->rlp, &pq->tqp, &pq->fbp, &pq->sxp);
        nalloc = pq->nalloc;    /* ix_ptrs computed this in version 3 */

        /* initialize fb for skip list blocks */
        fb_init(pq->fbp, nalloc);

        /* initialize tqueue */
        tq_init(pq->tqp, nalloc, pq->fbp);

        /* initialize regionl, adding one huge region for data */
        rl_init(pq->rlp, nalloc, pq->fbp);
        {
                off_t  datasz = pq->ixo - pq->datao;

                if (MAX_SIZE_T < datasz) {
                    log_error("Data portion of file is too big for one region");
                    return EFBIG;
                }
                {
                    size_t extent0 = (size_t)datasz;
                    region *rp = rl_add(pq->rlp, pq->datao, extent0);

                    pq->rlp->maxfextent = extent0;
                    log_assert(rp != NULL
                            && rp->offset == pq->datao
                            && rp->extent == extent0);
                }
        }

        sx_init(pq->sxp, nalloc);
        
        return status;
}

/*
 * Sets the functions to be used to access the product-queue.
 *
 * Arguments:
 *      pq      Pointer to product-queue structure.  The following members
 *              must be set to their final values: pflags, ixo, ixsz.
 */
static void
ctl_setAccessFunctions(
    pqueue* const       pq)
{
    const int           pflags = pq->pflags;

    log_assert(NULL != pq);

#ifdef HAVE_MMAP
    if (fIsSet(pflags, PQ_NOMAP)) {
#endif
        /*
         * The product-queue will be accessed via read() and write().
         */
        pq->ftom = f_ftom;
        pq->mtof = f_mtof;
#ifdef HAVE_MMAP
    }
    else {
        if (fIsSet(pflags, PQ_MAPRGNS) || isProductMappingNecessary(pq)) {
            /*
             * The product-queue will be accessed by being memory-mapped
             * on a region-by-region basis.
             */
            pq->ftom = mm_ftom;
            pq->mtof = mm_mtof;
        }
        else
        {
            /*
             * The product-queue will be accessed by being memory-mapped
             * once, in its entirety.
             */
            pq->ftom = mm0_ftom;
            pq->mtof = mm0_mtof;
        }
    }
#endif
}


/*
 * Initialize the in-memory state of pq from an existing file. Called by
 * pq_open().  On successful return, the control region (pq->ctlp) will be
 * mapped and/or locked.
 *
 * Arguments:
 *      pq      Pointer to product-queue structure to be set.
 *      path    Pathname of product-queue.
 *
 * Returns:
 *      0           Success
 *      PQ_CORRUPT  The  product-queue is internally inconsistent.
 *      else        <errno.h> error-code.
 */
static int
ctl_gopen(pqueue *const pq, const char *path)
{
        int status = ENOERR;
        pqctl *ctlp;
        size_t ctlsz; 
        void *vp = NULL;

        log_assert(pq != NULL);
        log_assert(pq->pagesz > 0);
        log_assert(pq->ixp == NULL && pq->rlp == NULL && pq->tqp == NULL);

        ctlsz = pq->pagesz;

        /*FALLTHROUGH*/
remap:
        status = (pq->ftom)(pq, 0, ctlsz, 0, &vp);
#ifdef HAVE_MMAP
        if(status == EIO && (pq->ftom == mm_ftom || pq->ftom == mm0_ftom))
        {
                log_warning("Product-queue can't be memory-mapped!  "
                    "Continuing with slower read/write I/O.");
                /* try again */
                pq->ftom = f_ftom;
                pq->mtof = f_mtof;
                status = (pq->ftom)(pq, 0, ctlsz, 0, &vp);
        }
#endif
        if(status != ENOERR)
                return status;

        ctlp = (pqctl *)vp;
        if(ctlp->magic != PQ_MAGIC)
        {
                /* Not a product queue */
                log_error("%s: Not a product queue", path);
                status = EINVAL;
                goto unwind_map;
        }
        if (PQ_VERSION != ctlp->version && 7 != ctlp->version)
        {
                log_error("%s: Product queue is version %d instead of expected version %d",
                       path, ctlp->version, PQ_VERSION);
                status = EINVAL;
                goto unwind_map;
        }
        if(ctlp->datao % pq->pagesz != 0)
        {
                /* Can't align */
                /* TODO: If we use read()/write() not fatal ??? */
                log_error("%s: Can't align", path);
                status = EINVAL;
                goto unwind_map;
        }
        if((size_t)ctlp->datao != ctlsz)
        {
                /* we guessed wrong, try again */
                if(ctlsz != pq->pagesz)
                {
                        /* don't try more than once */
                        status = EINVAL;
                        goto unwind_map;
                }
                ctlsz = (size_t)ctlp->datao;
                (void)(pq->mtof)(pq, 0, 0);
                goto remap;
        }

        pq->datao = ctlp->datao;
        pq->ixo = ctlp->ixo;
        pq->ixsz = ctlp->ixsz;
        pq->nalloc = ctlp->nalloc;
        pq->ctlp = ctlp;

        if (!(pq->datao > 0) ||
            !(pq->datao % pq->pagesz == 0) ||
            !(pq->ixo > pq->datao) ||
            !(pq->ixo % pq->pagesz == 0) ||
            !(pq->ixsz >= pq->pagesz) ||
            !(pq->ixsz % pq->pagesz == 0)) {

            log_error("pq->datao=%lu, pq->pagesz=%lu, pq->ixo=%lu, "
                "pq->ixsz=%lu",
                (unsigned long)pq->datao, (unsigned long)pq->pagesz, 
                (unsigned long)pq->ixo, (unsigned long)pq->ixsz);
            status = PQ_CORRUPT;
            goto unwind_map;
        }

        /*
         * Reset the product-queue access-functions based on the product-queue's
         * actual size.
         */
        ctl_setAccessFunctions(pq);

        /* bring in the indexes */
        status = (pq->ftom)(pq, pq->ixo, pq->ixsz, RGN_NOLOCK, &pq->ixp);
        if(status != ENOERR)
                goto unwind_map;

        if (!ix_ptrs(pq->ixp, pq->ixsz, pq->nalloc, pq->ctlp->align,
                &pq->rlp, &pq->tqp, &pq->fbp, &pq->sxp)) {
            status = PQ_CORRUPT;
            goto unwind_map;
        }

        if (!(pq->rlp->nalloc == pq->nalloc && pq->tqp->nalloc == pq->nalloc
                        && pq->sxp->nalloc == pq->nalloc)) { 
                log_error("pq->rlp->nalloc=%lu, pq->nalloc=%lu, "
                    "pq->tqp->nalloc=%lu, pq->sxp->nalloc=%lu",
                    (unsigned long)pq->rlp->nalloc,
                    (unsigned long)pq->nalloc, 
                    (unsigned long)pq->tqp->nalloc, 
                    (unsigned long)pq->sxp->nalloc);
                status = PQ_CORRUPT;
                goto unwind_map;
        }

        return ENOERR;

unwind_map:
        (void)(pq->mtof)(pq, 0, 0);
        return status;
}


/**
 * Get/lock the ctl for access by this process
 *
 * @param[in] rflags  Region flags: bitwise OR of
 *                    - RGN_NOLOCK  Don't lock region; locking handled elsewhere
 *                    - RGN_WRITE   Region will be modified
 *                    - RGN_NOWAIT  Return immediately if can't lock, else wait
 * @retval ENOERR     Success.
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
 */
static int
ctl_get(pqueue *const pq, int const rflags)
{
        int status = ENOERR;

#if _NOMAP || !defined(HAVE_MMAP)
        log_assert(pq->mtof == f_mtof);
        log_assert(pq->ctlp == NULL);
        log_assert(pq->ixp == NULL);
#endif

        if(fIsSet(rflags, RGN_WRITE) && !fIsSet(pq->pflags, PQ_SIGSBLOCKED))
        {
                /* We are beginning a critical section */
                sigset_t set;

                (void) sigfillset(&set);
                (void) sigdelset(&set, SIGABRT);
                (void) sigdelset(&set, SIGFPE);
                (void) sigdelset(&set, SIGILL);
                (void) sigdelset(&set, SIGSEGV);
                (void) sigdelset(&set, SIGBUS);
                status = pthread_sigmask(SIG_BLOCK, &set, &pq->sav_set);
                log_assert(0 == status);
                fSet(pq->pflags, PQ_SIGSBLOCKED);
        }
        if(pq->ctlp == NULL)
        {
                /* bring in the pqctl */
                status = (pq->ftom)(pq,
                                 0, (size_t)pq->datao, rflags,
                                (void **)&pq->ctlp);
                if(status != ENOERR)
                        goto unwind_mask;
        }
        log_assert(pq->ctlp->magic == PQ_MAGIC);
        log_assert(PQ_VERSION == pq->ctlp->version || 7 == pq->ctlp->version);
        log_assert(pq->ctlp->datao == pq->datao);
        log_assert(pq->ctlp->ixo == pq->ixo);
        log_assert(pq->ctlp->ixsz == pq->ixsz);

        if(pq->ixp == NULL)
        {
                /* bring in the indexes */
                status = (pq->ftom)(pq,
                                 pq->ixo, pq->ctlp->ixsz, rflags|RGN_NOLOCK,
                                &pq->ixp);
                if(status != ENOERR)
                        goto unwind_ctl;
        }

        ix_ptrs(pq->ixp, pq->ixsz, pq->nalloc, pq->ctlp->align, &pq->rlp,
            &pq->tqp, &pq->fbp, &pq->sxp);
        log_assert(pq->rlp->nalloc == pq->nalloc && pq->tqp->nalloc == pq->nalloc
                        && pq->sxp->nalloc == pq->nalloc);

        return ENOERR;
unwind_ctl:
        (void) (pq->mtof)(pq, 0, 0);
        pq->ctlp = NULL;
unwind_mask:
        {
            sigset_t set = pq->sav_set;

            (void) sigemptyset(&pq->sav_set);
            fClr(pq->pflags, PQ_SIGSBLOCKED);
            (void) pthread_sigmask(SIG_SETMASK, &set, NULL);
        }
        return status;
}

/******************************************************************************
 * Product-Queue Functions:
 ******************************************************************************/
static void
pq_lockIf(pqueue* const pq)
{
    if (fIsSet(pq->pflags, PQ_THREADSAFE)) {
        (void)pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &pq->cancelState);

        int status = pthread_mutex_lock(&pq->mutex);

        if (status) {
            log_errno(status, "pthread_mutex_lock() failure");
            abort();
        }
    }
}

static void
pq_unlockIf(pqueue* const pq)
{
    if (fIsSet(pq->pflags, PQ_THREADSAFE)) {
        int status = pthread_mutex_unlock(&pq->mutex);

        if (status) {
            log_errno(status, "pthread_mutex_unlock() failure");
            abort();
        }

        int lockState;
        (void)pthread_setcancelstate(pq->cancelState, &lockState);
    }
}

/*
 * Sets the offset and size fields of a product-queue structure.
 *
 * Arguments:
 *      pq              Pointer to the product-queue structure to have its
 *                      "pagesz", "datao", "ixo", "ixsz", and "nalloc" fields
 *                      set.
 *      align           Alignment reguirement in bytes.
 *      initsz          Initial size of the data segment in bytes.
 *      nregions        The capacity of the product-queue in products.
 */
static void
pq_setOffsetsAndSizes(
    pqueue* const       pq,
    const size_t        align,
    const off_t         initsz,
    const size_t        nregions)
{
    /* Size of an I/O page in bytes: */
    pq->pagesz = (size_t)pagesize();
    /* Offset to the data segment in bytes: */
    pq->datao = lcm(pq->pagesz, align);
    log_assert(pq->datao >= sizeof(pqctl));
    /* Offset to the index segment in bytes: */
    pq->ixo = pq->datao + _RNDUP(initsz, pq->pagesz);
    /* The capacity of the product-queue in products: */
    pq->nalloc = nregions;

    /* Size of the index segement: */
    if (nregions == 0) {
        pq->ixsz = pq->pagesz;
    }
    else {
        pq->ixsz = ix_sz(nregions, align);
        pq->ixsz = _RNDUP(pq->ixsz, pq->pagesz);
    }
}


/**
 * Allocates and initializes a product-queue structure.
 *
 * @param[in] pflags     Product-queue flags. Bitwise OR of
 *                       - PQ_MAPRGNS    Map region by region, default is whole
 *                                       file if possible; otherwise, by region
 *                       - PQ_NOCLOBBER  Don't replace an existing product-queue
 *                       - PQ_NOLOCK     Disable locking (DANGER!)
 *                       - PQ_NOMAP      Use `malloc/read/write/free` instead of
 *                                       `mmap()`
 *                       - PQ_PRIVATE    `mmap()` the file `MAP_PRIVATE`.
 *                                       Default is `MAP_SHARED`.
 *                       - PQ_READONLY   Product-queue is read-only. Default is
 *                                       read/write.
 * @param[in] align      Alignment reguirement in bytes.
 * @param[in] initialsz  Capacity of the product-queue in bytes.
 * @param[in] maxProds   Capacity of the product-queue in number of products.
 * @retval    NULL       Failure. "errno" set.
 * @return               Pointer to the product-queue structure. The client
 *                       should call "pq_delete(pq)" when the structure is no
 *                       longer needed.
 */
static pqueue*
pq_new(
    const int           pflags,
    const size_t        align,
    const off_t         initialsz,
    const size_t        maxProds)
{
    pqueue* const       pq = (pqueue*)malloc(sizeof(pqueue));

    if (pq == NULL)
        return NULL;

    (void)memset(pq, 0, sizeof(pqueue));
    (void)sigemptyset(&pq->sav_set);

    /*
     * This is a convenient place to overide things at compile time.
     */

    pq->pflags = pflags;

    fSet(pq->pflags, PQ_NOGROW); /* always set for this version of pq! */
    pq_setOffsetsAndSizes(pq, align, initialsz, maxProds);

    if (pq->ixo < pq->datao) {
        log_error("Queue-size not supported by environment: initialsz=%ld, "
                "sizeof(off_t)=%lu, sizeof(size_t)=%lu", (long)initialsz,
                (unsigned long)sizeof(off_t), (unsigned long)sizeof(size_t));
        errno = EINVAL;
        free(pq);
        return NULL;
    }

    if (isProductMappingNecessary(pq)) {
        /*
         * The entire product-queue can't be memory-mapped in one mmap(2)
         * call; consequently, each data-product will be individually
         * memory-mapped.
         */
        fSet(pq->pflags, PQ_MAPRGNS);
    }

#if __hpux
    if (fIsSet(pq->pflags, PQ_MAPRGNS)) {
        /*
         * HP-UX requires that memory-mapped segments be aligned on page
         * boundaries.
         */
        if (align < pq->pagesz) {
            /* LOG_NOTICE("forcing alignment %u", pq->pagesz); */
            pq_setOffsetsAndSizes(pq, pq->pagesz, initialsz, maxProds);
        }
    }
#endif

    pq->riulp = (riul*)malloc(pq->pagesz);
    if (pq->riulp == NULL) {
        free(pq);
        return NULL;
    }
    riul_init(pq->riulp, 0, pq->pagesz);

    {
        pthread_mutexattr_t attr;
        int                 status = pthread_mutexattr_init(&attr);
        if (status) {
            log_errno(status, "Couldn't initialize mutex attributes");
        }
        else {
            (void)pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
            (void)pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);
            status = pthread_mutex_init(&pq->mutex, &attr);
            if (status)
                log_errno(status, "Couldn't initialize mutex");
            (void)pthread_mutexattr_destroy(&attr);
        }
        if (status) {
            free(pq->riulp);
            free(pq);
            return NULL;
        }
    }

    /*
     * Set the product-queue access-functions.
     */
    ctl_setAccessFunctions(pq);

    pq->fd = -1;
    pq->cursor = TS_NONE;
    pq->cursor_offset = OFF_NONE;
    pq->pqe_count = 0;
    pq->locked_count = 0;
	pq->smallest_extent_seen = UINT_MAX;

    return pq;
}

/**
 * Frees a product-queue.
 *
 * @param[in,out]  Product queue
 */
static void
pq_free(pqueue *const pq)
{
        if(pq == NULL)
                return;
        (void)pthread_mutex_destroy(&pq->mutex);
        if(pq->riulp != NULL)
        {
                free(pq->riulp);
                pq->riulp = NULL;
        }
        free(pq);
}


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
        pqueue **pqp)
{
        int oflags = (O_RDWR|O_CREAT|O_TRUNC);
        int fd;
        pqueue *pq = NULL;
        int status = ENOERR;

        /* enforce minimum alignment */
        align = (align == 0 ? M_RND_UNIT : _RNDUP(align, M_RND_UNIT));
        /* TODO: check for absurd align values ? */

        if(initialsz != 0)
                initialsz = (off_t) _RNDUP(initialsz, align);
        else
                initialsz = (off_t) align;

        pq = pq_new(pflags, align, initialsz, nproducts);
        if(pq == NULL)
                return errno;

        if(fIsSet(pflags, PQ_NOCLOBBER))
                fSet(oflags, O_EXCL);
        fd = open(path, oflags, mode);
        if(fd < 0)
        {
                status = errno;
                goto unwind_new;
        }
        (void)ensure_close_on_exec(fd);

        pq->fd = fd;

        status = ctl_init(pq, align);
        if(status != ENOERR)
                goto unwind_open;

        (void)strncpy(pq->pathname, path, sizeof(pq->pathname));
        pq->pathname[sizeof(pq->pathname)-1] = 0;

        *pqp = pq;

        status = ctl_rel(pq, RGN_MODIFIED);
        if (status) {
        	log_error("ctl_rel() failure");
        	goto unwind_open;
        }

        return ENOERR;

unwind_open:
        (void)close(fd);
        (void)unlink(path);
        /*FALLTHROUGH*/
unwind_new:
        pq_free(pq);
        return status;
}


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
    pqueue** const    pqp)
{
    int               status;
    pqueue*           pq = pq_new(pflags, M_RND_UNIT, 0, 0);

    if (NULL == pq) {
        status = errno;
    }
    else {
        pq->fd = open(path, fIsSet(pflags, PQ_READONLY) ? O_RDONLY : O_RDWR, 0);

        if (0 > pq->fd) {
            status = errno;
        }
        else {
            (void)ensure_close_on_exec(pq->fd);
            status = ctl_gopen(pq, path);

            if (!status) {
                status = ctl_rel(pq, 0);           /* release control-block */

                if (status) {
                	log_error("ctl_rel() failure");
                }
                else {
					if (!fIsSet(pflags, PQ_READONLY)) {
						status = ctl_get(pq, RGN_WRITE);

						if (!status) {
							int      rflags = 0;    // control-block unmodified
							pqctl*   ctlp = pq->ctlp;

							if (WRITE_COUNT_MAGIC != ctlp->write_count_magic) {
								/*
								 * This process is the first one of this version
								 * of the LDM to open the product-queue for
								 * writing. Initialize the "write count"
								 * mechanism.
								 */
								ctlp->write_count_magic = WRITE_COUNT_MAGIC;
								ctlp->write_count = 0;
								rflags = RGN_MODIFIED;
							}

							if (MAX_WRITE_COUNT > ctlp->write_count) {
								ctlp->write_count++;
								rflags = RGN_MODIFIED;
							}
							else {
								log_error("Too many writers (%u) to "
										"product-queue (%s)", ctlp->write_count,
										path);

								status = EACCES;    /* too many writers */
							}

							if (!status) {
								if (METRICS_MAGIC != ctlp->metrics_magic) {
									/*
									 * This process is the first one of this
									 * version of the LDM to open the
									 * product-queue for writing.  Initialize
									 * the additional metrics.
									 */
									ctlp->metrics_magic = METRICS_MAGIC;
									ctlp->mostRecent = TS_NONE;
									ctlp->minVirtResTime = TS_NONE;
									ctlp->isFull = 0;
									rflags = RGN_MODIFIED;
								}
								if (METRICS_MAGIC_2 != ctlp->metrics_magic_2) {
									/*
									 * This process is the first one of this
									 * version of the LDM to open the
									 * product-queue for writing.  Initialize
									 * the additional metrics.
									 */
									ctlp->metrics_magic_2 = METRICS_MAGIC_2;
									ctlp->mvrtSize = -1;
									ctlp->mvrtSlots = 0;
									rflags = RGN_MODIFIED;
								}

								(void)strncpy(pq->pathname, path,
										sizeof(pq->pathname));
								pq->pathname[sizeof(pq->pathname)-1] = 0;
							}

							const int stat = ctl_rel(pq, rflags);
							if (stat) {
								log_error("ctl_rel() failure");
								if (status == 0)
									status = stat;
							}
						}                           /* ctl_get() success */
					}                               /* open for writing */
                } // `ctl_rel()` success
            }                                   /* ctl_gopen() success */

            if (status) {
                (void)close(pq->fd);
                pq->fd = -1;
            }
        }                                       /* pq->fd >= 0 */

        if (status) {
            pq_free(pq);
        }
        else {
            *pqp = pq;
        }
    }                                           /* pq != NULL */

    return status;
}


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
        pqueue* const pq)
{
    pq_lockIf(pq);
        int pflags = pq->pflags;
    pq_unlockIf(pq);

    return pflags;
}

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
pq_close(pqueue *pq)
{
    int status = ENOERR;                    /* success */
    int fd = -1;

    if (pq == NULL)
            return 0;

    pq_lockIf(pq);
        fd = pq->fd;

        if(pq->riulp != NULL)
        {
                while(pq->riulp->nelems > 2)
                {
                        off_t offset =
                                pq->riulp->rp[pq->riulp->nelems -1].offset;
                        if(offset == pq->ixo || offset == 0)
                                continue;
                        (void) (pq->mtof)(pq, offset, 0);
                }
        }

        if(pq->rlp != NULL)
        {
                (void)(pq->mtof)(pq, pq->ixo, RGN_NOLOCK);
                pq->rlp = NULL;
        }

        if (fIsSet(pq->pflags, PQ_READONLY)) {
            if (NULL != pq->ctlp) {
                status = ctl_rel(pq, 0);

                if (status)
                	log_error("ctl_rel() failure");
            }
        }
        else {
            status = ctl_get(pq, RGN_WRITE);

            if (!status) {
                int rflags;

                if (0 < pq->ctlp->write_count) {
                    pq->ctlp->write_count--;
                    rflags = RGN_MODIFIED;
                }
                else {
                    log_error("Write-count of product-queue prematurely 0");

                    rflags = 0;                 /* unmodified */
                    status = EOVERFLOW;
                }

                const int stat = ctl_rel(pq, rflags);

                if (stat) {
                	log_error("ctl_rel() failure");
                	if (status == 0)
                		status = stat;
                }
            }
        }                                       /* was opened for writing */

#ifdef HAVE_MMAP
        if(pq->base != NULL && pq->ftom == mm0_ftom)
        {
                /* special case, time to unmap the whole thing */
                int mflags = 0; /* TODO: translate rflags to mflags */
                (void) unmapwrap(pq->base, 0, TOTAL_SIZE(pq), mflags);
                pq->base = NULL;
        }
#endif

    pq_unlockIf(pq);
    pq_free(pq);

    if(fd > -1 && close(fd) < 0 && !status)
            status = errno;

    return status;
}


/**
 * Returns the pathname of a product-queue as given to `pq_create()` or
 * `pq_open()`.
 *
 * @param[in] pq  The product-queue.
 * @return        The pathname of the product-queue as given to `pq_create()` or
 *                `pq_open()`.
 */
const char* pq_getPathname(
        pqueue* pq)
{
    pq_lockIf(pq);
        const char* pathname = pq->pathname;
    pq_unlockIf(pq);

    return pathname;
}


/*
 * Let the user find out the pagesize.
 */
int
pq_pagesize(pqueue *pq)
{
    /*
     * Allow the caller to figure out what the default would be prior to
     * calling `pq_create()`.
     */
    if(pq == NULL)
        return (int)pagesize();

    // Else, tell the caller what it is
    pq_lockIf(pq);
        int pagesz = (int) pq->pagesz;
    pq_unlockIf(pq);

    return pagesz;
}


/**
 * Returns the size of the data portion of a product-queue.
 *
 * @param[in] pq  Pointer to the product-queue object.
 * @return        The size, in bytes, of the data portion of the product-queue.
 */
size_t
pq_getDataSize(
    pqueue* const       pq)
{
    pq_lockIf(pq);
        size_t size = pq->ixo - pq->datao;
    pq_unlockIf(pq);

    return size;
}


typedef struct {
    time_t      start;
    /*
     * Hostnames are limited to 255 bytes.  See
     * <http://www.opengroup.org/onlinepubs/007908799/xns/gethostname.html>.
     */
    char        hostname[256];
} FutureEntry;


static inline int
compareFutureEntries(
    const void* const   entry1,
    const void* const   entry2)
{
    const char* name1 = ((FutureEntry*)entry1)->hostname;
    const char* name2 = ((FutureEntry*)entry2)->hostname;

    return strcmp(name1, name2);
}


static void
vetCreationTime(
    const prod_info* const      info)
{
    /*
     * Vet the creation-time of a data-product.
     */
    timestampt  initialSearchTime = info->arrival;
    timestampt  now;

    /*
     * Keep the following consonant with the temporal backoff in
     * pq_setCursorFromSignature().
     */
    initialSearchTime.tv_sec -= SEARCH_BACKOFF;

    (void)set_timestamp(&now);

    if (tvCmp(initialSearchTime, now, >)) {
        const char* const       origin = info->origin;
        char*                   cp = strstr(origin, "_v_");
        size_t                  len =
            cp == NULL ?  strlen(origin) : (size_t)(cp - origin);

        if (log_is_enabled_info) {
            log_warning("Future product from \"%*s\". "
                    "Fix local or ingest clock. %s",
                    (int)len, origin, s_prod_info(NULL, 0, info, 0));
        }
        else {
            FutureEntry         targetEntry;
            FutureEntry* const* treeEntry;
            static void*        root = NULL;
#           define              FUTURE_INTERVAL (60*60) /* 1 h */

            if (len > (sizeof(targetEntry.hostname)-1))
                len = sizeof(targetEntry.hostname) - 1;
            (void)memcpy(targetEntry.hostname, origin, len);
            targetEntry.hostname[len] = 0;

            treeEntry = tfind(&targetEntry, &root, compareFutureEntries);

            if (treeEntry != NULL) {
                FutureEntry*    entry = *treeEntry;
                time_t          now = time(NULL);

                if (entry->start <= now) {
                    log_warning("Future product from \"%s\". "
                            "Fix local or ingest clock. %s", entry->hostname,
                            s_prod_info(NULL, 0, info, 0));

                    entry->start = now + FUTURE_INTERVAL;
                }
            }
            else {
                FutureEntry*    newEntry = malloc(sizeof(FutureEntry));

                log_warning("Future product from \"%s\". "
                        "Fix local or ingest clock. %s", targetEntry.hostname,
                    s_prod_info(NULL, 0, info, 0));

                if (newEntry != NULL) {
                    newEntry->start = time(NULL) + FUTURE_INTERVAL;
                    (void)strcpy(newEntry->hostname, targetEntry.hostname);

                    (void)tsearch(newEntry, &root, compareFutureEntries);
                }                       /* "newEntry" allocated */
            }                           /* host not in database */
        }                               /* non-verbose logging */
    }                                   /* product created in future */
}


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
pq_insertNoSig(pqueue *pq, const product *prod)
{
    int status = ENOERR;

    pq_lockIf(pq);
        size_t extent;
        void *vp = NULL;
        sxelem *sxep;
        
        log_assert(pq != NULL);
        log_assert(prod != NULL);

        if(fIsSet(pq->pflags, PQ_READONLY)) {
                log_debug("pq_insertNoSig(): queue is read-only");
                status = EACCES;
                goto unwind_lock;
        }

        // log_debug_1("Getting product size");
        extent = xlen_product(prod);
        if (extent > pq_getDataSize(pq)) {
                log_debug("pq_insertNoSig(): product is too big");
                status = PQ_BIG;
                goto unwind_lock;
        }

        /*
         * Write lock pq->ctl.
         */
        // log_debug_1("Getting control header");
        status = ctl_get(pq, RGN_WRITE);
        if(status != ENOERR) {
                log_debug("pq_insertNoSig(): ctl_get() failure");
                goto unwind_lock;
        }

        // log_debug_1("Getting space for product");
        status = rpqe_new(pq, extent, prod->info.signature, &vp, &sxep);
        if(status != ENOERR) {
                log_debug("pq_insertNoSig(): rpqe_new() failure");
                goto unwind_ctl;
        }

        // log_debug_1("XDR-ing product");
                                                /* cast away const'ness */
        if(xproduct(vp, extent, XDR_ENCODE, (product *)prod) == 0)
        {
                log_debug("pq_insertNoSig(): xproduct() failure");
                status = EIO;
                goto unwind_rgn;
        }

        log_assert(pq->tqp != NULL && tq_HasSpace(pq->tqp));
        status = tq_add(pq->tqp, sxep->offset);
        if(status != ENOERR) {
                log_debug("pq_insertNoSig(): tq_add() failure");
                goto unwind_rgn;
        }

        // log_debug_1("Setting timestamp");
        (void)set_timestamp(&pq->ctlp->mostRecent);
        // log_debug_1("Vetting creation time");
        vetCreationTime(&prod->info);
        /*FALLTHROUGH*/

unwind_rgn:
        // log_debug_1("Releasing region");
        (void) rgn_rel(pq, sxep->offset, status == ENOERR ? RGN_MODIFIED : 0);
        /*FALLTHROUGH*/

unwind_ctl:
        // log_debug_1("Releasing control header");
        (void) ctl_rel(pq, RGN_MODIFIED);
        /*FALLTHROUGH*/

unwind_lock:
        // log_debug_1("Unlocking");
        // log_debug_1("Returning %d", status);
    pq_unlockIf(pq);

    return status;
}


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
pq_insert(pqueue *pq, const product *prod)
{
        int status = pq_insertNoSig(pq, prod);
        if(status == ENOERR)
        {
                /*
                 * Inform others in our process group
                 * that there is new data available.
                 * (see pq_suspend() below.)
                 *  SIGCONT is ignored by default...
                 */
                (void)kill(0, SIGCONT);
        }
        return status;
}


/*
 * Returns some useful, "highwater" statistics of a product-queue.  The
 * statistics are since the queue was created.
 *
 * Arguments:
 *      pq              Pointer to the product-queue.  Shall not be NULL.
 *      highwaterp      Pointer to the maxium number of bytes used in the
 *                      data portion of the product-queue.  Shall not be NULL.
 *                      Set upon successful return.
 *      maxproductsp    Pointer to the maximum number of data-products that the
 *                      product-queue has held since it was created.  Shall not
 *                      be NULL.  Set upon successful return.
 * Returns:
 *      0               Success.  "*highwaterp" and "*maxproductsp" are set.
 *      else            <errno.h> error code.
 */
int
pq_highwater(pqueue *pq, off_t *highwaterp, size_t *maxproductsp)
{
    pq_lockIf(pq);
        /* Read lock pq->xctl. */
        int status = ctl_get(pq, 0);

        if(status == ENOERR) {
            if(highwaterp)
                    *highwaterp = pq->ctlp->highwater;
            if(maxproductsp)
                    *maxproductsp = pq->ctlp->maxproducts;
            (void) ctl_rel(pq, 0);
        }
    pq_unlockIf(pq);

    return status;
}


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
    int* const          isFull)
{
    pq_lockIf(pq);
    int status = ctl_get(pq, 0);
    if (status == ENOERR) {
        *isFull = pq->ctlp->isFull;
        (void)ctl_rel(pq, 0);
    }                                   /* "pq->ctlp" allocated */

    pq_unlockIf(pq);
    return status;
}


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
    timestampt* const   mostRecent)
{
    pq_lockIf(pq);
    int status = ctl_get(pq, 0);
    if (status == ENOERR) {
        *mostRecent = pq->ctlp->mostRecent;
        (void)ctl_rel(pq, 0);
    }                                   /* "pq->ctlp" allocated */

    pq_unlockIf(pq);
    return status;
}


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
    size_t* const       slots)
{
    pq_lockIf(pq);

    int status = ctl_get(pq, 0);
    if (status == ENOERR) {
        pqctl*   ctlp = pq->ctlp;

        if (METRICS_MAGIC == ctlp->metrics_magic) {
            *minVirtResTime = ctlp->minVirtResTime;
        }
        else {
            *minVirtResTime = TS_NONE;
        }

        if (METRICS_MAGIC_2 == ctlp->metrics_magic_2) {
            *size = ctlp->mvrtSize;
            *slots = ctlp->mvrtSlots;
        }
        else {
            *size = -1;
            *slots = 0;
        }

        (void)ctl_rel(pq, 0);
    }                                   /* "pq->ctlp" allocated */

    pq_unlockIf(pq);
    return status;
}

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
    pqueue* const       pq)
{
    pq_lockIf(pq);

    int status = ctl_get(pq, RGN_WRITE);
    if (status == ENOERR) {
        pq->ctlp->minVirtResTime = TS_NONE;
        pq->ctlp->mvrtSize = -1;
        pq->ctlp->mvrtSlots = 0;

        (void)ctl_rel(pq, RGN_MODIFIED);
    }                                   /* "pq->ctlp" allocated */

    pq_unlockIf(pq);
    return status;
}


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
        size_t* const maxextentp)
{
    pq_lockIf(pq);
        /* Read lock pq->ctl. */
        int status = ctl_get(pq, 0);

        if(status == ENOERR) {
            if(nprodsp)
                *nprodsp = pq->rlp->nelems;
            if(nfreep)
                *nfreep = pq->rlp->nfree;
            if(maxextentp)
                *maxextentp = pq->rlp->maxfextent;
            if(nemptyp)
                *nemptyp = pq->rlp->nempty;
            if(nbytesp)
                *nbytesp = pq->rlp->nbytes;
            if(maxprodsp)
                *maxprodsp = pq->rlp->maxelems;
            if(maxfreep)
                *maxfreep = pq->rlp->maxfree;
            if(minemptyp)
                *minemptyp = pq->rlp->minempty;
            if(maxbytesp)
                *maxbytesp = pq->rlp->maxbytes;
            if(age_oldestp) {
                timestampt ts0;
                tqelem *tqep;

                tqep = tqe_first(pq->tqp); /* get oldest */
                if(tqep != NULL) {
                  (void)set_timestamp(&ts0);
                  *age_oldestp = d_diff_timestamp(&ts0, &tqep->tv);
                } else {
                  *age_oldestp = 0;
                }
            }

            (void) ctl_rel(pq, 0);
        }
    pq_unlockIf(pq);

    return status;
}

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
    pqueue* const       pq)
{
    pq_lockIf(pq);
    size_t nalloc = pq->nalloc;
    pq_unlockIf(pq);
    return nalloc;
}


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
    timestampt* const   oldestCursor)
{
    pq_lockIf(pq);

    int status = ctl_get(pq, 0);
    if (status == ENOERR) {
        tqelem*         tqep = tqe_first(pq->tqp);

        if (tqep == NULL) {
            *oldestCursor = TS_NONE;
        }
        else {
            *oldestCursor = tqep->tv;
        }

        (void)ctl_rel(pq, 0);
    }                                   /* control region locked */

    pq_unlockIf(pq);
    return status;
}


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
    unsigned* const     count)
{
    int status;

    if (NULL == path || NULL == count) {
        status = EINVAL;
    }
    else {
        pqueue* pq = NULL; // Assignment quiets scan-build(1)

        status = pq_open(path, PQ_READONLY, &pq);

        if (status == 0) {
            /*
             * Get the control-block.
             */
            status = ctl_get(pq, 0);            /* readonly */

            if (!status) {
                pqctl*  ctlp = pq->ctlp;

                if (WRITE_COUNT_MAGIC != ctlp->write_count_magic) {
                    status = ENOSYS;
                }
                else {
                    *count = pq->ctlp->write_count;
                }
                
                (void)ctl_rel(pq, 0);
            }

            (void)pq_close(pq);
        }
    }

    return status;
}


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
pq_clear_write_count(const char* const path)
{
    int status;

    if (NULL == path) {
        status = EINVAL;
    }
    else {
        pqueue* pq = NULL; // Assignment quiets scan-build(1)

        status = pq_open(path, 0, &pq);         /* open for writing */

        if (status == 0) {
            status = ctl_get(pq, RGN_WRITE);

            if (status == 0) {
                int    rflags = 0;              /* control-block unmodified */
                pqctl* ctlp = pq->ctlp;

                if (ctlp->write_count != 1) {
                    ctlp->write_count = 1;      /* pq_close() will decrement */
                    rflags = RGN_MODIFIED;      /* control-block modified */
                }

                ctl_rel(pq, rflags);
            }

            (void)pq_close(pq);
        }
    }

    return status;
}


/*
 * For debugging: dump extents of regions on free list, in order by extent.
 */
int
pq_fext_dump(pqueue *const pq)
{
    int status;

    pq_lockIf(pq);
        regionl *rl;
        region *rlrp;
        size_t spix;
        size_t sqix;
        const region *spp;
        fb *fbp;
#if !defined(NDEBUG)
        size_t prev_extent = 0;
#endif

        /* Read lock pq->ctl. */
        status = ctl_get(pq, 0);
        if(status == ENOERR) {
            rl = pq->rlp;
            rlrp = rl->rp;
            fbp = pq->fbp;

            /* p = l->header; */
            spix = rl->fext;    /* head of skip list by extent */
            spp = rlrp + spix;
            /* q = p->forward[0]; */
            sqix = fbp->fblks[spp->prev];
            log_debug("** Free list extents:\t");                  /* debugging */
            while(sqix != RL_FEXT_TL) {
                /* p = q */
                spix = sqix;
                spp = rlrp + spix;
                log_debug("%u ", spp->extent);             /* debugging */
#if !defined(NDEBUG)
                log_assert(spp->extent >= prev_extent);
                prev_extent = spp->extent;
#endif
                /* q = p->forward[0]; */
                sqix = fbp->fblks[spp->prev];
            }
            (void) ctl_rel(pq, 0);
        }
    pq_unlockIf(pq);

    return status;
}



/*
 * Set cursor used by pq_sequence() or pq_seqdel().
 */
void
pq_cset(pqueue *pq, const timestampt *tvp)
{
    pq_lockIf(pq);
        log_assert(    tvp->tv_sec  >= TS_ZERO.tv_sec
                && tvp->tv_usec >= TS_ZERO.tv_usec
                && tvp->tv_sec  <= TS_ENDT.tv_sec
                && tvp->tv_usec <= TS_ENDT.tv_usec);
        pq->cursor = *tvp;
        if(tvEqual(*tvp, TS_ENDT)) {
            pq->cursor_offset = OFF_NONE;
        } else if (tvEqual(*tvp, TS_ZERO)) {
            pq->cursor_offset = 0;
        }
    pq_unlockIf(pq);
}



/*
 * Set cursor_offset used by pq_sequence() to disambiguate among
 * multiple products with identical queue insertion times.
 */
void
pq_coffset(pqueue *pq, off_t c_offset)
{
    pq_lockIf(pq);
        pq->cursor_offset = c_offset;
    pq_unlockIf(pq);
}


/*
 * Get current cursor value used by pq_sequence() or pq_seqdel().
 */
void
pq_ctimestamp(pqueue *pq, timestampt *tvp)
{
        pq_lockIf(pq);
        *tvp = pq->cursor;
        pq_unlockIf(pq);
}


/*
 * Figure out the direction of scan of clssp, and set *mtp to it.
 * Set the cursor to include all of clssp time range in the queue.
 * (N.B.: For "reverse" scans, this range may not include all
 * the arrival times.)
 */
int
pq_cClassSet(pqueue *pq,  pq_match *mtp, const prod_class_t *clssp)
{
    pq_lockIf(pq);
        int status = ENOERR;
        pq_match otherway = TV_LT;
        tqelem *tqep;

        if(clssp == NULL || tvIsNone(clssp->from) || tvIsNone(clssp->to)) {
                pq_unlockIf(pq);
                return EINVAL;
        }

        pq_cset(pq, &clssp->from);

        if(tvCmp(clssp->from, clssp->to, >))
        {
                /* reversed scan */
                if(tvEqual(clssp->from, TS_ENDT))
                {
                        /* Edge case */
                        if(mtp != NULL)
                                *mtp = TV_LT;
                        pq_unlockIf(pq);
                        return ENOERR;
                }
                /* else */
                otherway = TV_GT;
        }
        else
        {
                if(tvEqual(clssp->from, TS_ZERO))
                {
                        /* Edge case */
                        if(mtp != NULL)
                                *mtp = TV_GT;
                        pq_unlockIf(pq);
                        return ENOERR;
                }
        }

        /* Read lock pq->xctl.  */
        status = ctl_get(pq, 0);
        if(status == ENOERR) {
            /* find specified que element just outside the clssp time range */
            tqep = tqe_find(pq->tqp, &clssp->from, otherway);
            if(tqep != NULL)
            {
                    /* update cursor */
                    pq_cset(pq, &tqep->tv);
                    pq_coffset(pq, tqep->offset);
            }
            if(mtp != NULL)
                    *mtp = otherway == TV_LT ? TV_GT : TV_LT;
            (void) ctl_rel(pq, 0);
        }
    pq_unlockIf(pq);
    return status;
}


/**
 * Returns the product-information associated with a data-product.
 *
 * @pre               The control-region is locked.
 * @param[in] pq      Pointer to the product-queue structure.
 * @param[in] offset  The offset to the region in the product-queue that contains
 *                    the data-product.
 * @param[out] info   Pointer to the product-information structure to be filled.
 *                    If the `origin` or `ident` member is non-NULL, then the
 *                    pointed-to byte-array will be used; otherwise,
 *                    `xdr_prod_info()` will allocate memory and the caller
 *                    should call `xdr_free(xdr_prod_info, info)` when `*info`
 *                    is no longer needed. It is the caller's responsibility to
 *                    *either* pass-in a fully-allocated structure *or* clear
 *                    the structure before passing it in and call `xdr_free()`
 *                    on it later.
 * @retval 0          Success.  "*info" is set.
 * @retval EACCES     "pq->fd" is not open for read or "pq->fd" is not open for
 *                    write and PROT_WRITE was specified for a MAP_SHARED type
 *                    mapping.
 * @retval EAGAIN     The mapping could not be locked in memory, if required by
 *                    mlockall(), due to a lack of resources.
 * @retval EBADF      "pq->fd" is not a valid file descriptor open for reading.
 * @retval EDEADLK    The necessary lock is blocked by some lock from another
 *                    process and putting the calling process to sleep, waiting
 *                    for that lock to become free would cause a deadlock.
 * @retval EFBIG or EINVAL
 *                    The extent of the region is greater than the maximum file
 *                    size.
 * @retval EFBIG      The file is a regular file and the extent of the region is
 *                    greater than the offset maximum established in the open
 *                    file description associated with "pq->fd".
 * @retval EINTR      A signal was caught during execution.
 * @retval EINVAL     The region's offset or extent is not valid, or "pq->fd"
 *                    refers to a file that does not support locking.
 * @retval EINVAL     The region's offset is not a multiple of the page size as
 *                    returned by sysconf(), or is considered invalid by the
 *                    implementation.
 * @retval EIO        An I/O error occurred while reading from the file system.
 * @retval EIO        The metadata of the data-product in the product-queue at
 *                    offset "offset" could not be decoded.
 * @retval EMFILE     The number of mapped regions would exceed an
 *                    implementation-dependent limit (per process or per system).
 * @retval ENODEV     "pq->fd" refers to a file whose type is not supported by
 *                    mmap().
 * @retval ENOLCK     Satisfying the request would result in the number of
 *                    locked regions in the system exceeding a system-imposed
 *                    limit.
 * @retval ENOMEM     There is insufficient room in the address space to effect
 *                    the necessary mapping.
 * @retval ENOMEM     The region's mapping could not be locked in memory, if
 *                    required by mlockall(), because it would require more
 *                    space than the system is able to supply.
 * @retval ENOMEM     Insufficient memory is available.
 * @retval ENOTSUP    The implementation does not support the access requested
 *                    in "pq->pflags".
 * @retval ENXIO      The region's location is invalid for the object specified
 *                    by "pq->fd".
 * @retval EOVERFLOW  The smallest or, if the region's extent is non-zero, the
 *                    largest offset of any byte in the requested segment cannot
 *                    be represented correctly in an object of type off_t.
 * @retval EOVERFLOW
 *                    The file size in bytes or the number of blocks allocated
 *                    to the file or the file serial number cannot be
 *                    represented correctly.
 * @retval EOVERFLOW
 *                    The file is a regular file and the region's offset plus
 *                    extent exceeds the offset maximum established in the open
 *                    file description associated with "fd".
 * @retval EROFS      The file resides on a read-only file system.
 * @retval PQ_NOTFOUND
 *                    There is no data-product at the given offset.
 */
static int
pq_getMetadataFromOffset(
    pqueue* const restrict    pq,
    const off_t               offset,
    prod_info* const restrict info)
{
    int                         status;
    const regionl* const        rlp = pq->rlp;
    const size_t                rlix = rl_find(rlp, offset);

    if (RL_NONE == rlix) {
        status = PQ_NOTFOUND;
    }
    else {
        void*                   vp;
        const region* const     rp = rlp->rp + rlix;
        const size_t            extent = Extent(rp);

        /*
         * Lock the data-product's data-region.
         */
        status = rgn_get(pq, offset, extent, 0, &vp);

        if (0 != status) {
            log_syserr("Couldn't lock data-product's "
                    "data-region in product-queue");
        }
        else {
            XDR       xdrs;

            xdrmem_create(&xdrs, vp, extent, XDR_DECODE);

            /*
             * Decode the data-product's metadata.
             */
            if (!xdr_prod_info(&xdrs, info)) {
                log_error("xdr_prod_info() failed");

                status = EIO;
            }
            else {
                status = 0;             /* success */
            }

            xdr_destroy(&xdrs);
            (void)rgn_rel(pq, offset, 0);
        }                               /* data-product region locked */
    }                                   /* associated data-product exists */

    return status;
}


/**
 * Finds the entry in the time map of a product-queue corresponding to a
 * data-product with a given signature.
 *
 * @param[in]  pq     The product-queue.
 * @param[in]  sig    The signature of the data-product.
 * @param[out] tqepp  The time entry.
 * @retval     0      Success. `*tqepp` is set.
 * @retval     PQ_CORRUPT   The product-queue is corrupt.
 * @retval     PQ_NOTFOUND  No entry in signature map corresponding to `sig`.
 */
static int
pq_findTimeEntryBySignature(
        pqueue* const restrict  pq,
        const signaturet        sig,
        tqelem** const restrict tqepp)
{
    int     status;
    sxelem* signatureEntry;

    /*
     * Get the relevant entry in the signature-map.
     */
    if (!sx_find(pq->sxp, sig, &signatureEntry)) {
        status = PQ_NOTFOUND;
    }
    else {
        InfoBuf     infoBuf;
        // Necessary for `getMetadataFromOffset()`
        prod_info*  info = ib_init(&infoBuf);

        /*
         * Get the metadata of the data-product referenced by the
         * signature-entry.
         */
        status =  pq_getMetadataFromOffset(pq, signatureEntry->offset, info);

        if (PQ_NOTFOUND == status) {
            log_error("data-product region "
                "of signature-map entry doesn't exist");
            status = PQ_CORRUPT;
        }
        else if (0 == status) {
            timestampt searchTime = info->arrival;
            tqelem*    timeEntry;

            /*
             * Start the time-map search beginning a little before
             * the creation-time of the target data-product.  This will
             * work if 1) the data-product is in the queue; and 2) the
             * clock on the origination host agrees with the clock on
             * this host.
             *
             * Keep the following consonant with the temporal backoff in
             * vetCreationTime().
             */
            searchTime.tv_sec -= SEARCH_BACKOFF;
            timeEntry = tqe_find(pq->tqp, &searchTime, TV_LT);

            if (NULL == timeEntry) {
                timeEntry = tqe_find(pq->tqp, &searchTime, TV_EQ);

                if (NULL == timeEntry)
                    timeEntry = tqe_find(pq->tqp, &searchTime, TV_GT);
            }

            if (NULL == timeEntry) {
                log_error("The product-queue appears to be empty");
                status = PQ_CORRUPT;
            }
            else {
                /*
                 * Search forward in the time-map from the initial entry to
                 * find the matching entry.
                 */
                const tqelem*       initialTimeEntry = timeEntry;
                const fb* const     fbp =
                    (fb*)((char*)pq->tqp + pq->tqp->fbp_off);

                for (;;) {
                    if (OFF_NONE == timeEntry->offset) {
                        /*
                         * The current entry is the end of the
                         * product-queue.
                         */
                        status = PQ_NOTFOUND;
                        break;
                    }               /* entry is end-of-queue */

                    if (timeEntry->offset == signatureEntry->offset) {
                        /*
                         * Found it.
                         */
                        *tqepp = timeEntry;
                        status = 0;
                        break;
                    }

                   /*
                    * Advance to the very next entry in the time-map.
                    */
                    timeEntry = &pq->tqp->tqep[fbp->fblks[timeEntry->fblk]];
                }                   /* time-map entry loop */

                if (status == PQ_NOTFOUND) {
                    /*
                     * The data-product wasn't found.  This could be
                     * because of an egregious discrepancy between the
                     * clock on the origination system and this system's
                     * clock (the origination clock might be fast, our
                     * clock might be slow, or both).  Therefore, search
                     * from the beginning of the product-queue to the
                     * initial data-product (sigh).
                     */
                    timeEntry = tqe_find(pq->tqp, &TS_ZERO, TV_GT);

                    for (;;) {
                        if (initialTimeEntry == timeEntry) {
                            /*
                             * The current entry is the initial entry.
                             */
                            break;
                        }

                        if (timeEntry->offset == signatureEntry->offset) {
                            /*
                             * Found it.
                             */
                            *tqepp = timeEntry;
                            status = 0;
                            break;
                        }

                       /*
                        * Advance to the very next entry in the time-map.
                        */
                        timeEntry = &pq->tqp->tqep[fbp->fblks[timeEntry->fblk]];
                    }               /* time-map entry loop */
                }                   /* product not where it should be */
            }                       /* non-empty product-queue */
        }                           /* have product metadata */
    }                               /* have signature-map entry */

    return status;
}


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
    const signaturet    signature)
{
    int status;

    pq_lockIf(pq);
        /*
         * Read-lock the control-region of the product-queue.
         */
        status = ctl_get(pq, 0);

        if (ENOERR != status) {
            log_syserr("Couldn't lock control-region of product-queue");
        }
        else {
            tqelem* timeEntry;

            status = pq_findTimeEntryBySignature(pq, signature, &timeEntry);

            if (status == 0) {
                pq_cset(pq, &timeEntry->tv);
                pq_coffset(pq, timeEntry->offset);
            }

            /*
             * Release control-region of product-queue.
             */
            (void)ctl_rel(pq, 0);
        }                                   /* control region locked */
    pq_unlockIf(pq);

    return status;
}

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
        void* const       optArg)
{
    pq_lockIf(pq);
    bool instanceLocked = true;

    int status;

    /*
     * Read-lock the control-region of the product-queue to prevent concurrent
     * write-access by another process.
     */
    if (ctl_get(pq, 0)) {
        log_syserr("Couldn't lock control-region of product-queue");
        status = PQ_SYSTEM;
    }
    else {
        bool controlRegionLocked = true;

        /*
         * Find the relevant entry in the signature-map.
         */
        const sxelem* sigEntry;
        if (!sx_find(pq->sxp, sig, (sxelem**)&sigEntry)) {
            status = PQ_NOTFOUND;
        }
        else {
            /*
             * Find the region-entry corresponding to the signature-entry.
             */
            const regionl* const rlp = pq->rlp;
            const size_t         offset = sigEntry->offset;
            const size_t         rlix = rl_find(rlp, offset);

            if (RL_NONE == rlix) {
                log_error("Signature-entry has no corresponding region-entry");
                status = PQ_CORRUPT;
            }
            else {
                /*
                 * Lock the corresponding data-product's data-region to prevent
                 * it from being modified while being processed.
                 */
                void*               vp;
                const region* const rp = rlp->rp + rlix;
                const size_t        extent = Extent(rp);

                if (rgn_get(pq, offset, extent, 0, &vp)) {
                    log_error("Couldn't lock data-product's data-region");
                    status = PQ_SYSTEM;
                }
                else {
                    /*
                     * The control-region is unlocked to allow concurrent
                     * write-access by another process. This may safely be done
                     * because the data-product's data-region is locked,
                     */
                    if (ctl_rel(pq, 0)) {
                        log_error("Couldn't unlock control-region");
                        status = PQ_SYSTEM;
                    }
                    else {
                        controlRegionLocked = false;

                        /*
                         * This instance is unlocked to allow concurrent access
                         * on another thread. This is safe because the data-
                         * product's region is locked.
                         */
                        pq_unlockIf(pq);
                        instanceLocked = false;

                        /*
                         * Decode the data-product's metadata to pass to the
                         * processing function.
                         */
                        prod_info info;
                        XDR       xdrs;

                        xdrmem_create(&xdrs, vp, extent, XDR_DECODE);
                        // necessary for `xdr_prod_info()`
                        (void)memset(&info, 0, sizeof(info));

                        if (!xdr_prod_info(&xdrs, &info)) {
                            log_error("xdr_prod_info() failed");
                            status = PQ_SYSTEM;
                        }
                        else {
                            /*
                             * Process the data-product while its data-region
                             * is locked.
                             */
                            status = func(&info, xdrs.x_private, vp, extent,
                                    optArg);
                            xdr_free(xdr_prod_info, (char*)&info);
                        }

                        xdr_destroy(&xdrs);
                    }                   // control-region unlocked

                    (void)rgn_rel(pq, offset, 0); // Unlocks data-product
                }                       // data-product region locked
            }                           // corresponding region-entry exists
        }                               // signature-entry found

        /*
         * Release the control-region of the product-queue to allow write-access
         * by another process.
         */
        if (controlRegionLocked)
            (void)ctl_rel(pq, 0);
    }                                   // control region locked

    if (instanceLocked)
        pq_unlockIf(pq);

    return status;
}


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
 * above spec, return PQ_END.
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
 *                        NULL, then, upon return, the product is unlocked and may
 *                        be deleted to make room for a new product. If
 *                        non-NULL, then this variable is set before `ifMatch()`
 *                        is called and the product's state upon return from
 *                        this function depends on that function's return-value:
 *                          - 0     The product is locked against deletion and
 *                                  the caller should call `pq_release(*off)`
 *                                  when the product may be deleted
 *                          - else  The product is unlocked and may be deleted
 * @retval     PQ_CORRUPT Product-queue is corrupt (NB: <0). `log_add()` called.
 * @retval     PQ_END     No next product (NB: <0)
 * @retval     PQ_INVAL   Invalid argument (NB: <0). `log_add()` called.
 * @retval     PQ_SYSTEM  System error (NB: <0). `log_add()` called.
 * @retval     0          Product didn't match
 * @return                Return-value of `ifMatch()`
 */
static int
pq_sequenceHelper(
		pqueue* restrict             pq,
		pq_match                     mt,
        const prod_class_t* restrict clss,
        pq_seqfunc*                  ifMatch,
        void*                        otherargs,
        off_t* const restrict        off)
{
    int status;

    if (pq == NULL) {
        log_add("Product-queue is NULL");
        status = PQ_INVAL;
    }
    else {
        pq_lockIf(pq); {
            bool threadLocked = true;

            /* If necessary, initialize cursor */
            if (tvIsNone(pq->cursor)) {
                log_assert(mt != TV_EQ);
                if (mt == TV_LT) {
                    pq->cursor = TS_ENDT;
                }
                else {
                    pq->cursor = TS_ZERO;
                }
            }

            // Read lock the control-header
            status = ctl_get(pq, 0);

            if (status) {
                log_add("ctl_get() failure");
                status = PQ_SYSTEM;
            }
            else {
                bool ctlLocked = true;

                // Find the specified queue element
                tqelem* tqep = tqe_find(pq->tqp, &pq->cursor, mt);

                if (tqep == NULL) {
                    status = PQ_END;
                }
                else {
                    // Update cursor
                    pq_cset(pq, &tqep->tv);
                    pq_coffset(pq, tqep->offset);

                    /*
                     * Spec'ing clss NULL or ifMatch NULL _just_ sequences
                     * the cursor. This feature used by the 'pqexpire' program.
                     */
                    if (clss == NULL || ifMatch == NULL) {
                        log_debug("NOOP");
                    }
                    else {
                        // Get the actual data region
                        region* rp;

                        status = rl_r_find(pq->rlp, tqep->offset, &rp);

                        if (status == 0
                                 || rp->offset != tqep->offset
                                 || Extent(rp) > pq_getDataSize(pq)) {
                            char ts[20];

                            (void)sprint_timestampt(ts, sizeof(ts), &tqep->tv);
                            log_add("Queue corrupt: tq: %s %s at %ld",
                                    ts,
                                    status ? "invalid region" : "no data",
                                    tqep->offset);
                            /*
                             * We can't fix it (tq_delete(pq->tqp, tqep)) here
                             * since we don't have write permission
                             */
                            status = PQ_CORRUPT;
                        }
                        else {
                            void* vp = NULL;

                            status = rgn_get(pq, rp->offset, Extent(rp), 0, &vp);

                            if (status) {
                                log_add("rgn_get() failure");
                                status = PQ_SYSTEM;
                            }
                            else {
								pq->locked_count++;
								log_debug("locked_count: %ld",
										pq->locked_count);

								log_assert(vp != NULL);

                                size_t extent = Extent(rp);
                                off_t  offset = rp->offset;
                                // Did product match `clss`?
                                bool   matched = false;

                                /*
                                 * Delay to process product, useful to see if
                                 * it's falling behind
                                 */
                                if (log_is_enabled_debug) {
                                    timestampt now;

                                    if (gettimeofday(&now, 0) == 0) {
                                        double delay = d_diff_timestamp(&now,
                                                &tqep->tv);
                                        log_debug("Delay: %.4f sec", delay);
                                    }
                                }

                                /*
                                 * We've got the data, so we can let go of the
                                 * control-header
                                 */
                                status = ctl_rel(pq, 0);
                                log_assert(status == 0);
                                ctlLocked = false;

                                /*
                                 * No race conditions from here on. Also,
                                 * calling a foreign function with an acquired
                                 * lock might result in deadlock.
                                 */
                                pq_unlockIf(pq);
                                threadLocked = false;

                                /* All this to avoid malloc in the xdr calls */
                                struct infobuf {
                                    prod_info b_i;
                                    char      b_origin[HOSTNAMESIZE + 1];
                                    char      b_ident[KEYSIZE + 1];
                                }          buf;
                                (void)memset(&buf, 0, sizeof(buf));
                                prod_info* info ;
                                info = &buf.b_i;
                                info->origin = &buf.b_origin[0];
                                info->ident = &buf.b_ident[0];

                                // Decode the product's information
                                XDR xdrs;
                                xdrmem_create(&xdrs, vp, (u_int)extent,
                                        XDR_DECODE) ;

                                if (!xdr_prod_info(&xdrs, info)) {
                                    log_add("xdr_prod_info() failure") ;
                                    status = PQ_SYSTEM;
                                }
                                else {
                                    log_assert(info->sz <= xdrs.x_handy);

                                    /*
                                     * Rather than copy the data, just use the
                                     * existing buffer
                                     */
                                    void* datap = xdrs.x_private;

#if PQ_SEQ_TRACE
                                    log_debug("%s %u",
                                            s_prod_info(NULL, 0, info, 1),
                                            xdrs.x_handy) ;
#endif

                                    /*
                                     * Log time-interval from product-creation
                                     * to queue-insertion.
                                     */
                                    if (log_is_enabled_debug) {
                                        double latency = d_diff_timestamp(
                                                &tqep->tv, &info->arrival);
                                        log_debug("time(insert)-time(create): "
                                                "%.4f s", latency);
                                    }

                                    // Do the work.
                                    if (clss == PQ_CLASS_ALL ||
                                            prodInClass(clss, info)) {
                                        matched = true;

                                        {
                                            // Change extent into xlen_product
                                            const size_t xsz =
                                                    _RNDUP(info->sz, 4);
                                            if (xdrs.x_handy > xsz)
                                                extent -= (xdrs.x_handy - xsz);
                                        }

                                        if (off) {
                                        	// In case `otherargs == off`
											*off = offset;
                                        }

                                        status = ifMatch(info, datap, vp,
                                                extent, otherargs);

                                        if (status) {
                                        	// Problem with `ifMatch()`
                                            /*
                                             * Back up, presumes clock tick >
                                             * usec (not always true)
                                             */
                                            if (mt == TV_GT) {
                                                timestamp_decr(&pq->cursor);
                                                pq_coffset(pq, OFF_NONE);
                                            }
                                            else if (mt == TV_LT) {
                                                pq_coffset(pq, offset + 1);
                                            }
                                        } // Problem with `ifMatch()`
                                    } // Product matches
                                } // Product information decoded

                                xdr_destroy(&xdrs);

                                // Release the data segment if appropriate
                                if (off == NULL || status || !matched) {
                                    (void)rgn_rel(pq, offset, 0);
									pq->locked_count--;
								}
                            } // Region locked
                        } // Region found
                    } // `clss != NULL && ifMatch != NULL`
                } // Time-queue element found

                if (ctlLocked)
                    (void)ctl_rel(pq, 0);
            } // Control-header was locked

            if (threadLocked)
                pq_unlockIf(pq);
        } // `pq_lockIf() was called
    } // `pq != NULL`

    return status;
}

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
        void* const               otherargs)
{
    return pq_sequenceHelper(pq, mt, clss, ifMatch, otherargs, NULL);
}

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
        off_t* const                       offset)
{
    return pq_sequenceHelper(pq, mt, clss, ifMatch, otherargs, offset);
}

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
        void* const restrict               app_par)
{
    int status;

    if (pq == NULL || clss == NULL || func==NULL) {
        log_error("Invalid argument: pq=%p, clss=%p, func=%p", pq, clss, func);
        status = PQ_INVAL;
    }
    else {
        pq_lockIf(pq);

        // If necessary, initialize product-queue time-cursor
        if (tvIsNone(pq->cursor))
            pq->cursor = reverse ? TS_ENDT : TS_ZERO;

        // Read-lock control-header
        status = ctl_get(pq, 0);
        if (status) {
            log_errno(status, "Couldn't get control-header");
            status = PQ_SYSTEM;
        }
        else {
            bool ctl_locked = true;

            queue_par_t queue_par;
            queue_par.is_full = pq->ctlp->isFull;

            // Find next element in time-queue
            tqelem* tqep = tqe_find(pq->tqp, &pq->cursor,
                    reverse ? TV_LT : TV_GT);
            if (tqep == NULL) {
                status = PQUEUE_END;
            }
            else {
                timestampt oldest = tqe_first(pq->tqp)->tv;
                queue_par.early_cursor = tvCmp(pq->cursor, oldest, <=);

                // Update product-queue time-cursor
                pq_cset(pq, &tqep->tv);
                pq_coffset(pq, tqep->offset);

                queue_par.inserted = tqep->tv;

                // Find region in product-queue that contains product
                region* rp;
                status = rl_r_find(pq->rlp, tqep->offset, &rp);
                if (status == 0 || rp->offset != tqep->offset
                        || Extent(rp) > pq_getDataSize(pq)) {
                    char ts[20];
                    (void)sprint_timestampt(ts, sizeof(ts), &tqep->tv);
                    log_error("Queue corrupt: tq: %s %s at %ld",
                            ts,
                            status ? "invalid region" : "no data",
                            tqep->offset);
                    /*
                     * Can't be fixed (tq_delete(pq->tqp, tqep)) here because no
                     * write permission
                     */
                    status = 0;
                }
                else {
                    // Following avoids calls to malloc() in XDR module
                    char ident[KEYSIZE + 1];
                    char origin[HOSTNAMESIZE + 1];
                    prod_par_t prod_par = {
                            .info.ident = ident,
                            .info.origin = origin,
                            .size = Extent(rp)
                    };
                    // Lock region in product-queue that contains product
                    status = rgn_get(pq, rp->offset, prod_par.size, 0,
                            &prod_par.encoded);
                    if (status) {
                        log_errno(status, "Couldn't get product region");
                        status = PQ_SYSTEM;
                    }
                    else {
                        log_assert(prod_par.encoded != NULL);

                        /*
                         * Because data-product is locked, control-header can
                         * be released so that another process can access
                         * product-queue. NB: This makes `tqep` invalid.
                         */
                        status = ctl_rel(pq, 0);
                        log_assert(status == 0);
                        ctl_locked = false;

                        /*
                         * If appropriate, log delay since product insertion to
                         * indicate if processing is falling behind.
                         */
                        if (log_is_enabled_debug) {
                            timestampt now;
                            if (gettimeofday(&now, 0) == 0) {
                                double delay = d_diff_timestamp(&now,
                                        &queue_par.inserted);
                                log_debug("Delay: %.4f sec", delay);
                            }
                        }

                        // Decode data-product metadata
                        XDR xdrs;
                        xdrmem_create(&xdrs, prod_par.encoded,
                                (u_int)prod_par.size, XDR_DECODE) ;
                        if (!xdr_prod_info(&xdrs, &prod_par.info)) {
                            log_error("xdr_prod_info() failed") ;
                            status = PQ_SYSTEM;
                        }
                        else {
                            log_assert(prod_par.info.sz <= xdrs.x_handy);

                            #if PQ_SEQ_TRACE
                                log_debug("%s %u",
                                        s_prod_info(NULL, 0, &prod_par.info, 1),
                                        xdrs.x_handy) ;
                            #endif

                            /*
                             * If appropriate, log time-interval from
                             * product-creation to queue-insertion.
                             */
                            if (log_is_enabled_debug) {
                                double latency =
                                        d_diff_timestamp(&queue_par.inserted,
                                                &prod_par.info.arrival);
                                log_debug("time(insert)-time(create): %.4f s",
                                        latency);
                            }

                            // If appropriate, apply caller-supplied function.
                            if (clss == PQ_CLASS_ALL ||
                                    prodInClass(clss, &prod_par.info)) {
                                log_assert(func != NULL);
                                {
                                    // Change extent into xlen_product */
                                    const size_t xsz =
                                            _RNDUP(prod_par.info.sz, 4);
                                    if (xdrs.x_handy > xsz)
                                        prod_par.size -= (xdrs.x_handy - xsz);
                                }
                                /*
                                 * Copying data is avoided by using existing
                                 * buffer.
                                 */
                                prod_par.data = xdrs.x_private;
                                queue_par.offset = rp->offset;
                                /*
                                 * Product-queue is unlocked because calling a
                                 * foreign function with an acquired lock can
                                 * result in deadlock:
                                 */
                                func(&prod_par, &queue_par, app_par);
                            } // Product matches
                        } // xdr_prod_info() succeeded
                        xdr_destroy(&xdrs);
                        if (!keep_locked)
                            (void)rgn_rel(pq, rp->offset, 0);
                    } // rgn_get() succeeded
                } // rl_r_find() succeeded
            } // tqe_find() succeeded
            if (ctl_locked)
                (void)ctl_rel(pq, 0);
        } // ctl_get() succeeded

        pq_unlockIf(pq);
    } // Valid arguments

    return status;
}

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
        const off_t   offset)
{
    pq_lockIf(pq);
        int status = rgn_rel(pq, offset, 0);

        if (status == 0)
            pq->locked_count--;
    pq_unlockIf(pq);

    if (status)
        log_errno(status, "Couldn't release offset %ld", offset);

    return status == EBADF
            ? PQ_INVAL
            : status == EINVAL
              ? PQ_NOTFOUND
              : status
                ? PQ_CORRUPT
                : 0;
}


/*
 * Boolean function to check that the cursor time is in the time range specified
 * by clssp. Returns non-zero if this is the case, zero if not.
 */
int
pq_ctimeck(pqueue *pq, pq_match mt, const prod_class_t *clssp,
        const timestampt *maxlatencyp)
{
        pq_lockIf(pq);
        timestampt cursor = pq->cursor;

        if(clssp == NULL || tvIsNone(cursor)) {
                pq_unlockIf(pq);
                return 0;
        }

        if(clss_eq(clssp, PQ_CLASS_ALL)) {
                pq_unlockIf(pq);
                return 1;
        }

        if(mt == TV_LT)
        {
                /* reversed scan */
                if(tvCmp(cursor, clssp->to, <)) {
                        pq_unlockIf(pq);
                        return 0;
                }
        }
        else
        {
                timestampt to = timestamp_add(&clssp->to, maxlatencyp);
                if(tvCmp(cursor, to, >)) {
                        pq_unlockIf(pq);
                        return 0;
                }
        }
        /* else, it's in the time range */
        pq_unlockIf(pq);
        return 1;
}


/*
 * Like pq_sequence(), but the ifmatch action is to remove the product from
 * inventory. If wait is nonzero, then wait for locks.
 *
 */
/* TODO: add class filter */
/*ARGSUSED*/
int
pq_seqdel(
        pqueue* const       pq,
        pq_match            mt,
        const prod_class_t* clss,
        const int           wait,
        size_t* const       extentp,
        timestampt* const   timestampp)
{
    if(pq == NULL)
        return EINVAL;

    pq_lockIf(pq);
        int        status = ENOERR;
        tqelem*    tqep;
        region*    rp = NULL;
        off_t      offset = OFF_NONE;
        size_t     extent = 0;
        void*      vp = NULL;
        prod_info* info ;
        XDR        xdrs;
        int const  rflags = wait ? RGN_WRITE : (RGN_WRITE | RGN_NOWAIT);
        size_t     rlix;
        struct infobuf {
            prod_info b_i;
            char b_origin[HOSTNAMESIZE + 1];
            char b_ident[KEYSIZE + 1];
        } buf;

        /* all this to avoid malloc in the xdr calls */
        info = &buf.b_i;
        info->origin = &buf.b_origin[0];
        info->ident = &buf.b_ident[0];

        /* if necessary, initialize cursor */
        /* We don't need to worry about disambiguating products with
           identical timestamps using offsets here (as in
           pq_sequence), because after a product is deleted, it won't
           be found again */
        if(tvIsNone(pq->cursor)) {
            if(mt == TV_LT) {
                pq->cursor = TS_ENDT;
                pq->cursor_offset = OFF_NONE;
            }
            else {
                pq->cursor = TS_ZERO;
                pq->cursor_offset = 0;
            }
        }

        /* write lock pq->ctl.  */
        status = ctl_get(pq, RGN_WRITE);
        if(status != ENOERR)
            goto unwind_lock;

        /* find the specified que element */
        tqep = tqe_find(pq->tqp, &pq->cursor, mt);
        if(tqep == NULL) {
            status = PQUEUE_END;
            goto unwind_ctl;
        }
        /* update cursor below, after we get the data */


        /* get the actual data region */
        rlix = rl_find(pq->rlp, tqep->offset);
        log_assert(rlix != RL_NONE);

        rp = pq->rlp->rp + rlix;
        log_assert(rp->offset == tqep->offset);
        log_assert(Extent(rp) <= pq_getDataSize(pq));

        status = rgn_get(pq, rp->offset, Extent(rp), rflags, &vp);

        if(status != ENOERR) {
            goto unwind_ctl;
        }
        log_assert(vp != NULL);

        /* update cursor */
        /* pq->cursor = tqep->tv; */
        pq_cset(pq, &tqep->tv);
        pq_coffset(pq, OFF_NONE);

        offset = rp->offset;
        extent = Extent(rp);

        /*
         * Decode it
         */
        xdrmem_create(&xdrs, vp, (u_int)extent, XDR_DECODE) ;

        if(!xdr_prod_info(&xdrs, info)) {
            log_error("xdr_prod_info() failed") ;
            status = EIO;
            goto unwind_rgn;
        }
        log_assert(info->sz <= xdrs.x_handy);

        /* return timestamp value even if we don't delete it */
        if(timestampp)
            *timestampp = info->arrival;


        log_assert(clss != NULL);
        if(clss != PQ_CLASS_ALL && !prodInClass(clss, info)) {
            /* skip this one */
            if(log_is_enabled_debug)
                    log_debug("skip %s", s_prod_info(NULL, 0, info, 1));
            goto unwind_rgn;
        }

        /*
         * else, Doit
         */
        if(log_is_enabled_info)
            log_info("del %s", s_prod_info(NULL, 0, info, 1));

        /* return extent value */
        if(extentp)
            *extentp = extent;

        tq_delete(pq->tqp, tqep); {
            const int found = sx_find_delete(pq->sxp, info->signature);
            if(found == 0) {
                char ts[20];
                (void) sprint_timestampt(ts, sizeof(ts), &tqep->tv);
                log_error("Queue corrupt: pq_seqdel: %s no signature at %ld",
                        ts, tqep->offset);
            }
        }
        rl_free(pq->rlp, rlix);

        /*FALLTHROUGH*/
    unwind_rgn:
        xdr_destroy(&xdrs);
        /* release the data segment */
        (void) rgn_rel(pq, offset, 0);
        /*FALLTHROUGH*/

    unwind_ctl:
        (void) ctl_rel(pq, 0);
        /*FALLTHROUGH*/

unwind_lock:
    pq_unlockIf(pq);

    return status;
}


/*
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
        const signaturet       sig)
{
    pq_lockIf(pq);
    int status = ctl_get(pq, RGN_WRITE);
    if (status) {
        log_error("Couldn't lock the control-header of product-queue %s",
                pq->pathname);
        status = PQ_SYSTEM;
    }
    else {
        sxelem* sxep;
        if (!sx_find(pq->sxp, sig, &sxep)) {
            status = PQ_NOTFOUND;
        }
        else {
            char buf[2*sizeof(signaturet)+1];
            size_t rlix = rl_find(pq->rlp, sxep->offset);
            if (rlix == RL_NONE) {
                (void)sprint_signaturet(buf, sizeof(buf), sig);
                log_error("Data-product with signature %s doesn't have a "
                        "corresponding region-map entry in product-queue %s",
                        buf, pq->pathname);
                status = PQ_CORRUPT;
            }
            else {
                tqelem* timeEntry;
                status = pq_findTimeEntryBySignature(pq, sig, &timeEntry);
                if (status) {
                    (void)sprint_signaturet(buf, sizeof(buf), sig);
                    log_error("Data-product with signature %s doesn't have a "
                            "corresponding time-map entry in product-queue %s",
                            buf, pq->pathname);
                    status = PQ_CORRUPT;
                }
                else {
                    prod_info prodInfo;
                    status = pq2_try_del_prod(pq, timeEntry, rlix, &prodInfo);
                    if (status == EACCES) {
                        status = PQ_LOCKED;
                    }
                    else if (status) {
                        (void)sprint_signaturet(buf, sizeof(buf), sig);
                        log_error("Couldn't remove map entries for "
                                "data-product with signature %s from "
                                "product-queue %s",
                                buf, pq->pathname);
                    }
                    else {
                        xdr_free(xdr_prod_info, (char*)&prodInfo);
                    }
                } // Entry found in time-map
            } // Entry found in region-map
        } // Entry found in signature-map
        (void) ctl_rel(pq, 0);
    } // Control-header locked
    pq_unlockIf(pq);
    return status;
}


/*
 * Used only by pq_last() below.
 */
/*ARGSUSED*/
static inline int
didmatch(
        const prod_info* const infop,
        const void* const      datap,
        void* const            xprod,
        const size_t           size,
        void* const            vp)
{
        timestampt* tsp = (timestampt *)vp;

        if(tsp != NULL)
                *tsp = infop->arrival;

        log_debug("lastmatch: %s", s_prod_info(NULL, 0, infop, 1));

        return PQUEUE_END; /* done with scan on the first hit */
}


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
        timestampt* const         tsp)
{
    int status = ENOERR;

    pq_lockIf(pq);
        pq_cset(pq, &clssp->to); /* Start at the end and work backwards */

        while ((status = pq_sequence(pq, TV_LT, clssp, didmatch, tsp))
                        == ENOERR) {
           if ((tsp != NULL) && (pq->cursor.tv_sec < tsp->tv_sec)) {
                log_debug("cursor reset: stop searching");
                pq_unlockIf(pq);
                return status;
           }
        }

        if (status != PQUEUE_END) {
            log_error("seq:%s (errno = %d)", strerror(status), status);
        }
        else {
            status = ENOERR;
        }

        if (tvEqual(pq->cursor, TS_ENDT)) {
            /* clssp->to is TS_ENDT and queue is empty */
            pq->cursor = TS_NONE; /* clear cursor */
            pq->cursor_offset = OFF_NONE;
        }
    pq_unlockIf(pq);

    return status;
}


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
        prod_class_t* const clssp)
{
    pq_lockIf(pq);
        timestampt ts = clssp->from;
        int        status = pq_last(pq, clssp, &ts);

        if(status == ENOERR) {
            if (tvEqual(ts, clssp->from)) {
                status = PQUEUE_END;
            }
            else {
                clssp->from = ts;
            }
        }

        pq->cursor = TS_NONE; /* clear cursor */
        pq->cursor_offset = OFF_NONE;
    pq_unlockIf(pq);

    return status;
}

/*** ? TODO, move this to another file. Doesn't use internal knowlege **/

#include "timestamp.h"

static volatile sig_atomic_t sigalrm_received = 0;

static void
hndlr_noop(int sig)
{
        switch(sig) {
        case SIGALRM :
#ifndef NDEBUG
                log_debug("SIGALRM") ;
#endif
                sigalrm_received = 1;
                return ;
        case SIGCONT :
#ifndef NDEBUG
                log_debug("SIGCONT") ;
#endif
                return;
        }
#ifndef NDEBUG
        log_debug("hndlr_noop: unhandled signal: %d", sig) ;
#endif
        /* nothing to do, just wake up */
        return;
}


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
        const int          numSigs)
{
    struct sigaction sigact, csavact, asavact;
    sigset_t         mask, savmask;
    time_t           start;

    /* Block SIGCONT and SIGALRM while we set up. */
    sigemptyset(&mask);
    sigaddset(&mask, SIGCONT);
    if (maxsleep)
        sigaddset(&mask, SIGALRM);
    (void)pthread_sigmask(SIG_BLOCK, &mask, &savmask);

    /* Set up handlers for SIGCONT and SIGALRM, stashing old. */
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;
    sigact.sa_handler = hndlr_noop;
    (void)sigaction(SIGCONT, &sigact, &csavact);
    if (maxsleep) {
        /* Set the alarm. */
        (void) sigaction(SIGALRM, &sigact, &asavact);
        (void) alarm(maxsleep);
    }

    /* Set the signal mask to be used during suspension. */
    mask = savmask;
    (void)sigdelset(&mask, SIGCONT);
    if (maxsleep)
        (void)sigdelset(&mask, SIGALRM);
    for (int i = 0; i < numSigs; i++)
        (void)sigdelset(&mask, unblockSigs[i]);

    /* Nighty night... */
    (void)time(&start);
    sigalrm_received = 0;
    (void)sigsuspend(&mask);

    /* Now we are back, restore state */
    if(maxsleep) {
        (void)alarm(0);
        (void)sigaction(SIGALRM, &asavact, NULL );
    }
    (void)sigaction(SIGCONT, &csavact, NULL );
    (void)pthread_sigmask(SIG_SETMASK, &savmask, NULL);

    return sigalrm_received
        ? 0
        : (unsigned)(time(NULL) - start);
}

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
pq_suspend(unsigned int maxsleep)
{
    return pq_suspendAndUnblock(maxsleep, NULL, 0);
}


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
    const int error)
{
    pq_lockIf(pq);
        const char* msg;

        if (0 == error) {
            msg = "Success";
        }
        else if (0 < error) {
            msg = strerror(error);
        }
        else {
            switch (error) {
                case PQ_END:
                    msg = "End of product-queue reached";
                    break;
                case PQ_NOTFOUND:
                    msg = "Desired data-product not found";
                    break;
                case PQ_CORRUPT:
                    msg = "Product-queue is corrupt";
                    break;
                default:
                    msg = "Unknown error-code";
            }
        }
    pq_unlockIf(pq);

    return msg;
}


/******************************************************************************
 * Reserved Data-Region in Product-Queue Functions:
 ******************************************************************************/


const pqe_index _pqenone = {
        OFF_NONE,
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}
};


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
        void **ptrp, pqe_index *indexp)
{
    int status = ENOERR;

    log_assert(pq != NULL);
    log_assert(infop != NULL);
    log_assert(ptrp != NULL);
    log_assert(indexp != NULL);

    pq_lockIf(pq);
        size_t extent;
        void *vp = NULL;
        sxelem *sxep;

        if(infop->sz == 0) {
                log_error("zero product size");
                status = EINVAL;
                goto unwind_lock;
        }

        if (infop->sz > pq_getDataSize(pq)) {
                log_error("Product too big: product=%u bytes; queue=%lu bytes",
                    infop->sz, (unsigned long)pq_getDataSize(pq));
                status = PQ_BIG;
                goto unwind_lock;
        }

        if(fIsSet(pq->pflags, PQ_READONLY)) {
            status = EACCES;
            goto unwind_lock;
        }

        /*
         * Write lock pq->xctl.
         */
        status = ctl_get(pq, RGN_WRITE);
        if(status != ENOERR) {
                log_debug("pqe_new(): ctl_get() failure");
                goto unwind_lock;
        }

        extent = xlen_prod_i(infop);
        status = rpqe_new(pq, extent, infop->signature, &vp, &sxep);
        if(status != ENOERR) {
                log_debug("pqe_new(): rpqe_new() failure");
                goto unwind_ctl;
        }

                                                /* cast away const'ness */
        *ptrp = xinfo_i(vp, extent, XDR_ENCODE, (prod_info *)infop);
        if(*ptrp == NULL)
        {
                log_debug("pqe_new(): xinfo_i() failure");
                status = EIO;
                goto unwind_ctl;
        }

        log_assert(((char *)(*ptrp) + infop->sz) <= ((char *)vp + extent));

        indexp->offset = sxep->offset;
        memcpy(indexp->signature, sxep->sxi, sizeof(signaturet));
        indexp->sig_is_set = true;
        pq->pqe_count++;
        /*FALLTHROUGH*/

unwind_ctl:
        (void) ctl_rel(pq, RGN_MODIFIED);
        /*FALLTHROUGH*/

unwind_lock:
    pq_unlockIf(pq);

    return status;
}


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
    pqe_index* const restrict indexp)
{
    int status;

    /*
     * Vet arguments.
     */
    if (pq == NULL || ptrp == NULL || indexp == NULL || signature == NULL) {
        log_error("Invalid argument: pq=%p, ptrp=%p, indexp=%p, signature=%p", pq, ptrp, indexp,
                signature);
        status = EINVAL;
    }
    else {
        pq_lockIf(pq);

        if (size > pq_getDataSize(pq)) {
            log_error("Product too big: product=%lu bytes; queue=%lu bytes",
                    (unsigned long)size, (unsigned long)pq_getDataSize(pq));
            status = PQ_BIG;
        }
        else {
            if (fIsSet(pq->pflags, PQ_READONLY)) {
                log_error("Product-queue is read-only");
                status = EACCES;
            }
            else {
                /*
                 * Write-lock the product-queue control-section.
                 */
                if ((status = ctl_get(pq, RGN_WRITE)) != 0) {
                    log_error("ctl_get() failure");
                }
                else {
                    sxelem* sxep;

                    /*
                     * Obtain a new region.
                     */
                    status = rpqe_new(pq, size, signature, ptrp, &sxep);
                    if (status) {
                        if (status != PQ_DUP)
                            log_error("rpqe_new() failure: {size: %zu}", size);
                    }
                    else {
                        /*
                         * Save the region information in the caller-supplied index
                         * structure.
                         */
                        indexp->offset = sxep->offset;
                        (void)memcpy(indexp->signature, sxep->sxi,
                                sizeof(signaturet));
                        indexp->sig_is_set = true;
                        pq->pqe_count++;
                    }

                    (void)ctl_rel(pq, RGN_MODIFIED);
                } /* product-queue control-section locked */
            } // product-queue is writable
        } /* arguments vetted */

        pq_unlockIf(pq);
    } // `pq` is valid

    return status;
}


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
        const pqe_index* const restrict index)
{
    pq_lockIf(pq);
        int   status;
        off_t offset = pqeOffset(*index);

        status = (pq->mtof)(pq, offset, 0);
        if(status) {
        	log_error("Couldn't unlock reserved region with offset %ld",
        			(long)offset);
        }
        else {
            /*
             * Write lock pq->xctl.
             */
            status = ctl_get(pq, RGN_WRITE);

            if (status) {
            	log_error("Couldn't get control block");
            }
            else {
                status = rpqe_free(pq, offset, index->signature);

                if (status) {
                	log_error("Couldn't free reserved region");
                }
                else {
					pq->pqe_count--;
                }

				(void)ctl_rel(pq, RGN_MODIFIED);
            } // Got control block
        } // Reserved region saved and unlocked
    pq_unlockIf(pq);

    return status;
}


/*
 * LDM 4 convenience funct.
 * Change signature, Insert at rear of queue, send SIGCONT to process group
 */
int
pqe_xinsert(pqueue *pq, pqe_index index, const signaturet realsignature)
{
        pq_lockIf(pq);
        int status = ENOERR;
        off_t offset = pqeOffset(index);

        /* correct the signature in the product */
        {
                riu *rp = NULL;
                char *xp;
                if(riul_r_find(pq->riulp, offset, &rp) == 0)
                {
                        log_error("Couldn't riul_r_find %ld", (long)offset);
                        status = EINVAL;
                        goto unwind_lock;
                }
                xp = rp->vp;
                log_assert(xp != NULL);
                xp += 8; /* xlen_timestampt */
                memcpy(xp, realsignature, sizeof(signaturet));
        }

        status =  (pq->mtof)(pq, offset, RGN_MODIFIED);
        if(status != ENOERR)
                goto unwind_lock;

        /*
         * Write lock pq->xctl.
         */
        status = ctl_get(pq, RGN_WRITE);
        if(status != ENOERR)
                goto unwind_lock;

        {
          sxelem *sxep;
          /*
           * Check for duplicate
           */
          if(sx_find(pq->sxp, realsignature, &sxep) != 0)
            {
              log_debug("PQ_DUP");
              status = PQ_DUP;
              (void) rpqe_free(pq, offset, index.signature);
              goto unwind_ctl;
            }
          /* else */
          /* correct the signature in the index */

          if(sx_find_delete(pq->sxp, index.signature) == 0)
            {
              log_error("old signature %s: Not Found",
                     s_signaturet(NULL, 0, index.signature));
            }
          sxep = sx_add(pq->sxp, realsignature, offset);
        }

        log_assert(pq->tqp != NULL && tq_HasSpace(pq->tqp));

        status = tq_add(pq->tqp, offset);
        if(status != ENOERR)
                goto unwind_ctl;

        /*
         * Inform others in our process group
         * that there is new data available.
         * (see pq_suspend() below.)
         *  SIGCONT is ignored by default...
         */
        (void)kill(0, SIGCONT);

        /*FALLTHROUGH*/
unwind_ctl:
        (void) ctl_rel(pq, RGN_MODIFIED);
unwind_lock:
        pq_unlockIf(pq);
        return status;
}

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
        const pqe_index* const restrict index)
{
    int  status;

#if 1
    pq_lockIf(pq);
        riu* rp;

        if (riul_r_find(pq->riulp, index->offset, &rp) == 0) {
            log_error("riul_r_find() failed");
            status = PQ_NOTFOUND;
        }
        else {
            InfoBuf    infoBuf;
            prod_info* info = ib_init(&infoBuf);
            XDR        xdrs;
            xdrmem_create(&xdrs, rp->vp, rp->extent, XDR_DECODE);
            if (!xdr_prod_info(&xdrs, info)) {
                log_error("xdr_prod_info() failed; "
                        "product-queue might now be corrupt");
                status = PQ_CORRUPT;
            }
            else if (xlen_prod_i(info) > rp->extent) {
                log_error("Product larger than allocated space; "
                        "product-queue now likely corrupted: "
                        "info->sz=%lu, rp->extent=%lu",
                        (unsigned long)info->sz, (unsigned long)rp->extent);
                status = PQ_BIG;
            }
            else if (pq->mtof(pq, index->offset, RGN_MODIFIED)) {
                log_error("pq->mtof() failed");
                status = PQ_SYSTEM;
            }
            else if (ctl_get(pq, RGN_WRITE)) {
                log_error("ctl_get() failed");
                status = PQ_SYSTEM;
            }
            else {
                log_assert(pq->tqp != NULL && tq_HasSpace(pq->tqp));
                if (tq_add(pq->tqp, index->offset)) {
                    log_error("tq_add() failed");
                    status = PQ_SYSTEM;
                }
                else {
                    (void)set_timestamp(&pq->ctlp->mostRecent);
                    pq->pqe_count--;
                    /*
                     * Inform our process group that there is new data available
                     * (see pq_suspend() below). SIGCONT is ignored by default.
                     */
                    (void)kill(0, SIGCONT);
                    status = 0;
                } // entry made in time-queue
                (void)ctl_rel(pq, RGN_MODIFIED);
            } // `ctl_get()` succeeded
            xdr_destroy(&xdrs);

            if (status)
                (void)pqe_discard(pq, index);
        } // data-product was found in region-in-use list

    pq_unlockIf(pq);

    return status;
#else
        int   status = ENOERR;
        off_t offset = pqeOffset(index);

        status =  (pq->mtof)(pq, offset, RGN_MODIFIED);
        if(status != ENOERR)
                goto unwind_lock;

        /*
         * Write lock pq->xctl.
         */
        status = ctl_get(pq, RGN_WRITE);
        if(status != ENOERR)
                goto unwind_lock;

        log_assert(pq->tqp != NULL && tq_HasSpace(pq->tqp));

        status = tq_add(pq->tqp, offset);
        if(status != ENOERR)
                goto unwind_ctl;

        set_timestamp(&pq->ctlp->mostRecent);

        /*
         * Inform others in our process group
         * that there is new data available.
         * (see pq_suspend() below.)
         *  SIGCONT is ignored by default...
         */
        (void)kill(0, SIGCONT);

        /*FALLTHROUGH*/
unwind_ctl:
        (void) ctl_rel(pq, RGN_MODIFIED);
destroy_xdrs:
        xdr_destroy(&xdrs);
unwind_lock:
        return status;
#endif
}

/**
 * Returns the number of outstanding product reservations (i.e., the number of
 * times `pqe_new()` and `pqe_newDirect()` have been called minus the number of
 * times `pqe_insert()` and `pqe_discard()` have been called.
 *
 * @param[in] pq  Product queue
 * @return        Number of outstanding product reservations
 */
long pqe_get_count(
        pqueue* const pq)
{
    pq_lockIf(pq);
    const long pqe_count = pq->pqe_count;
    pq_unlockIf(pq);
    return pqe_count;
}

/**
 * Returns the magic number of a product-queue.
 *
 * @param[in]  pq     Product-queue
 * @retval     0      `pq == NULL`
 * @retval     -1     `pq->base == NULL`
 * @return            The magic number of `pq`
 */
size_t pq_getMagic(const pqueue* const pq)
{
    return pq == NULL
            ? 0
            : pq->base == NULL
              ? -1
              : ((pqctl*)pq->base)->magic;
}
