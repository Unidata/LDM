/*
 * Copyright 2013 University Corporation for Atmospheric Research. All rights
 * reserved.
 *
 * See file COPYRIGHT in the top-level source-directory for legal conditions.
 */

/**
 * @file uldb.c
 *
 * This module implements a database of upstream LDM process metadata that can
 * be shared amongst separate processes.
 */

#include <config.h>

#include "ldm.h"
#include "uldb.h"
#include "globals.h"
#include "log.h"
#include "ldmprint.h"
#include "semRWLock.h"
#include "prod_class.h"

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>

/**
 * Parameters for creating the key for the shared-memory segment and read/write
 * lock:
 */
#define DEFAULT_KEY_PATH    getQueuePath()
#define KEY_INDEX   1

static const char* const VALID_STRING = __FILE__;

/**
 * A product-specification as implemented in an entry.
 * Keep consonant with sm_getSizeofEntry().
 */
typedef struct {
    size_t size; /* size of this structure in bytes */
    feedtypet feedtype; /* feedtype of the data-request */
    char pattern[1]; /* ERE pattern of the data-request */
} EntryProdSpec;

/**
 * A product-class as implemented in an entry.
 * Keep consonant with sm_getSizeofEntry().
 */
typedef struct {
    timestampt from;
    timestampt to;
    size_t prodSpecsSize; /* size, in bytes, of the product-specifications */
    EntryProdSpec prodSpecs[1];
} EntryProdClass;

/**
 * An entry.
 * Keep consonant with entry_sizeof().
 */
struct uldb_Entry {
    size_t size; /* size of this structure in bytes */
    struct sockaddr_in sockAddr;
    pid_t pid;
    int protoVers;
    int isNotifier;
    int isPrimary;
    EntryProdClass prodClass;
};

/**
 * The segment structure
 */
typedef struct {
    size_t entriesCapacity;
    size_t entriesSize;
    unsigned numEntries;
    uldb_Entry entries[1];
} Segment;

/**
 * An iterator over a snapshot of the database.
 */
struct uldb_Iter {
    Segment* segment;
    const uldb_Entry* entry;
};

/**
 * The shared-memory structure
 */
typedef struct {
    Segment* segment;
    key_t key;
    int shmId;
} SharedMemory;

/**
 * The upstream LDM database
 */
typedef struct {
    const char* validString;
    srwl_Lock* lock;
    SharedMemory sharedMemory;
} Database;

/**
 * This module's singleton database
 */
static Database database;
/**
 * The alignment of an entry
 */
static size_t entryAlignment;
/**
 * The alignment of an entry's product-class.
 */
static size_t prodClassAlignment;
/**
 * The alignment of an entry's product-specification
 */
static size_t prodSpecAlignment;
/**
 * Protection modes for the shared-memory segment.
 */
static mode_t read_only;
static mode_t read_write;
/**
 * Whether or not this module has been initialized.
 */
static int moduleInitialized = 0;
/**
 * Signal set for critical sections.
 */
static sigset_t cs_blockedSigSet;

/**
 * Initializes the critical-section module.
 */
static void cs_init(void)
{
    (void)sigfillset(&cs_blockedSigSet);
    (void)sigdelset(&cs_blockedSigSet, SIGABRT);
    (void)sigdelset(&cs_blockedSigSet, SIGFPE);
    (void)sigdelset(&cs_blockedSigSet, SIGILL);
    (void)sigdelset(&cs_blockedSigSet, SIGSEGV);
    (void)sigdelset(&cs_blockedSigSet, SIGBUS);
}

/**
 * Enters a critical section.
 *
 * @param origSigSet    [out] Pointer to the place to store the original signal
 *                      set.
 */
static void cs_enter(
        sigset_t* const origSigSet)
{
    (void)pthread_sigmask(SIG_BLOCK, &cs_blockedSigSet, origSigSet);
}

/**
 * Leaves a critical section.
 *
 * @param origSigSet    [in] Pointer to the original signal set given to the
 *                      previous "cs_enter()".
 */
static void cs_leave(
        const sigset_t* const   origSigSet)
{
    (void)pthread_sigmask(SIG_SETMASK, origSigSet, NULL);
}

/**
 * Returns the smallest multiple of a base value that is greater than or equal
 * to another value.
 *
 * @param value         The value to be rounded-up
 * @param alignment     The base value
 */
static size_t roundUp(
        size_t value,
        size_t base)
{
    return ((value + (base - 1)) / base) * base;
}

/**
 * Returns the alignment of a structure.
 *
 * @param size  The size of the structure as determined by "sizeof"
 * @return      The alignment parameter for the structure
 */
static size_t getAlignment(
        size_t size)
{
    int             i;
    static size_t   alignments[] = { sizeof(double), sizeof(long), sizeof(int),
            sizeof(short)};

    for (i = 0; i < sizeof(alignments)/sizeof(alignments[0]); i++) {
        if ((size % alignments[i]) == 0)
            return alignments[i];
    }

    return size; /* equivalent to byte-alignment */
}

/**
 * Indicates if the IP addresses of two socket Internet address are equal.
 *
 * @param addr1     [in] The first socket Internet address
 * @param addr2     [in] The second socket Internet address
 * @retval 0        The addresses are unequal
 * @retval 1        The addresses are equal
 */
static int ipAddressesAreEqual(
        const struct sockaddr_in* const addr1,
        const struct sockaddr_in* const addr2)
{
    return memcmp(&addr1->sin_addr.s_addr, &addr2->sin_addr.s_addr,
            sizeof(addr1->sin_addr.s_addr)) == 0;
}

/**
 * Returns the size, in bytes, that a product-specification will occupy in an
 * entry given the pattern that it will contain.
 *
 * @param pattern       [in] Pointer to the pattern that the
 *                      product-specification will contain.
 * @return              The size, in bytes, of the corresponding
 *                      product-specification in an entry
 */
static size_t eps_sizeof(
        const char* const pattern)
{
    /* Terminating EOS accounted for in following. */
    return roundUp(sizeof(EntryProdSpec) + strlen(pattern), prodSpecAlignment);
}

/**
 * Initializes an entry product-specification.
 *
 * @param eps           [in/out] Pointer to the entry product-specification to
 *                      be initialized.
 * @param feedtype      [in] The product-specification feedtype
 *                      initialization-value.
 * @param pattern       [in] The product-specification pattern
 *                      initialization-value.
 */
static void eps_init(
        EntryProdSpec* const    eps,
        const feedtypet         feedtype,
        const char* const       pattern)
{
    (void)strcpy(eps->pattern, pattern);
    eps->feedtype = feedtype;
    eps->size = eps_sizeof(pattern);
}

/**
 * Returns a pointer to the next product-specification after a given one.
 *
 * @param prodSpec      [in] Pointer to the current product-specification.
 * @return              Pointer to the next product-specification.
 */
static EntryProdSpec* eps_next(
        const EntryProdSpec* const  prodSpec)
{
    return (EntryProdSpec*) ((char*)prodSpec + prodSpec->size);
}

/**
 * Removes the feedtype of an entry's product-specification from a given
 * product-specification if and only if the patterns of the two
 * product-specifications are identical.
 *
 * @param entryProdSpec     [in] Pointer to a product-specification in an entry
 * @param prodSpec          [in/out] Pointer to the given product-specification
 */
static void eps_remove_prod_spec(
        const EntryProdSpec* const entryProdSpec,
        prod_spec* const prodSpec)
{
    if (strcmp(entryProdSpec->pattern, prodSpec->pattern) == 0)
        prodSpec->feedtype &= ~entryProdSpec->feedtype;
}

/**
 * Indicates if the patterns of two product-specifications are identical and if
 * the feedtype of an entry's product-specification is a subset (proper or
 * improper) of the feedtype of a given product-specification.
 *
 * @param eps       [in] The entry's product-specification
 * @param ps        [in] The given product-specification
 * @retval 0        The entry's product-specification isn't a subset
 * @retval 1        The entry's product-specification is a subset
 */
static int eps_isSubsetOf(
        const EntryProdSpec* const  eps,
        const prod_spec* const      ps)
{
    return ((eps->feedtype & ~ps->feedtype) == 0)
            && (strcmp(eps->pattern, ps->pattern) == 0);
}

/**
 * Returns the product-specification of an entry's product-specification. The
 * product-specification is copied.
 *
 * @param eps           [in] Pointer to the entry's product-specification
 * @param ps            [out] Pointer to the product-specification
 * @retval 0            Success.
 * @retval ULDB_SYSTEM  Failure. log_add() called.
 */
static uldb_Status eps_get(
        const EntryProdSpec* const  eps,
        prod_spec* const            ps)
{
    prod_spec   tmp;

    tmp.feedtype = eps->feedtype;
    tmp.pattern = (char*)eps->pattern;

    int status = cp_prod_spec(ps, &tmp);
    if (status) {
        log_add_errno(status, "Couldn't copy product-specification");
        return ULDB_SYSTEM;
    }

    return 0;
}

/**
 * Returns the size, in bytes, of an entry product-class given the size, in
 * bytes, of the product-specifications.
 *
 * @param epc           [in] Pointer to the entry product-class.
 * @param prodSpecsSize [in] The size, in bytes, of the product-specifications.
 * @return              The size, in bytes, of the entry product-class.
 */
static size_t epc_sizeof_internal(
        const size_t    prodSpecsSize)
{
    return roundUp(sizeof(EntryProdClass) - sizeof(EntryProdSpec) +
            prodSpecsSize, prodClassAlignment);
}

/**
 * Returns the size, in bytes, of an entry product-class.
 *
 * @param epc       [in] Pointer to the entry product-class.
 * @return          The size, in bytes, of the given entry product-class.
 */
static size_t epc_getSize(
        const EntryProdClass* const epc)
{
    return epc_sizeof_internal(epc->prodSpecsSize);
}

/**
 * Returns the size, in bytes, that an entry product-class will have given the
 * product-class that it will contain.
 *
 * @param prodClass     [in] Pointer to the product-class.
 * @return              The size, in bytes, of the entry product-class
 *                      that will contain the given product-class.
 */
static size_t epc_sizeof(
        const prod_class* const prodClass)
{
    const prod_spec*    prodSpec;
    size_t              size = 0;

    for (prodSpec = prodClass->psa.psa_val;
            prodSpec < prodClass->psa.psa_val + prodClass->psa.psa_len;
            prodSpec++) {
        size += eps_sizeof(prodSpec->pattern);
    }

    return epc_sizeof_internal(size);
}

/**
 * Initializes an entry product-class.
 *
 * @param epc           [in/out] Pointer to the entry product-class to be
 *                      initialized.
 * @param prodClass     [in] Pointer to the product-class to be mined for
 *                      initialization values.
 */
static void epc_init(
        EntryProdClass* const   epc,
        const prod_class* const prodClass)
{
    EntryProdSpec*      eps = epc->prodSpecs;
    const prod_spec*    prodSpec;

    for (prodSpec = prodClass->psa.psa_val;
            prodSpec < prodClass->psa.psa_val + prodClass->psa.psa_len;
            prodSpec++) {
        eps_init(eps, prodSpec->feedtype, prodSpec->pattern);
        eps = eps_next(eps);
    }

    epc->from = prodClass->from;
    epc->to = prodClass->to;
    epc->prodSpecsSize = (size_t)((char*)eps - (char*)epc->prodSpecs);
}

/**
 * Returns a pointer to the first product-specification in a product-class.
 *
 * @param prodClass     [in] Pointer to the product-class
 * @retval NULL         No entries in the product-class
 * @return A pointer to the first entry in the product-class
 */
static const EntryProdSpec* epc_firstProdSpec(
        const EntryProdClass* const prodClass)
{
    return prodClass->prodSpecsSize == 0 ? NULL : prodClass->prodSpecs;
}

/**
 * Returns a pointer to the next product-specification or NULL.
 *
 * @param prodClass     [in] Pointer to the product-class
 * @param prodSpec      [in] Pointer to a product-specification in the
 *                      product-class
 * @retval NULL         No more product-specifications in the product-class
 * @return              A pointer to the product-specification in the
 *                      product-class that's just after "prodSpec"
 */
static EntryProdSpec* epc_nextProdSpec(
        const EntryProdClass* const prodClass,
        const EntryProdSpec* const prodSpec)
{
    EntryProdSpec* const nextProdSpec = eps_next(prodSpec);

    return ((char*) nextProdSpec
            < ((char*) prodClass->prodSpecs + prodClass->prodSpecsSize)) ?
            nextProdSpec : NULL ;
}

/**
 * Returns the number of product-specifications in an entry's product-class.
 *
 * @param prodClass     [in] Pointer to the entry's product-class in question
 * @return              The number of product-specifications in the product-
 *                      class
 */
static unsigned epc_numProdSpecs(
        const EntryProdClass* const prodClass)
{
    unsigned n = 0;
    const EntryProdSpec* prodSpec;

    for (prodSpec = epc_firstProdSpec(prodClass); NULL != prodSpec; prodSpec =
            epc_nextProdSpec(prodClass, prodSpec))
        n++;

    return n;
}

/**
 * Returns a product-class that contains everything from an entry's
 * product-class except the product-specifications.
 *
 * @param epc           [in] Pointer to the entry's product-class
 * @param prod_class    [out] Address of a pointer to a product-class. The
 *                      client should call free_prod_class(*prod_class) when
 *                      the product-class is no longer needed.
 * @retval ULDB_SUCCESS Success
 * @retval ULDB_SYSTEM  System error. log_add() called.
 */
static uldb_Status epc_getEverythingButProdSpecs(
        const EntryProdClass* const epc,
        prod_class** const prodClass)
{
    int status;
    unsigned psa_len = epc_numProdSpecs(epc);
    prod_class* pc = new_prod_class(psa_len);

    if (NULL == pc) {
        log_add("Couldn't allocate product-class with %u specifications",
                psa_len);
        status = ULDB_SYSTEM;
    }
    else {
        pc->from = epc->from;
        pc->to = epc->to;
        *prodClass = pc;
        status = ULDB_SUCCESS;
    }

    return status;
}

/**
 * Removes all product-specifications in an entry's subscription from a given
 * subscription for all product-specifications for which the patterns are
 * identical. The time limits of the given subscription are not modified.
 *
 * @param epc           [in] Pointer to an entry's product-class
 * @param givenSub      [in/out] The given subscription
 */
static void epc_remove_prod_specs(
        const EntryProdClass* const epc,
        prod_class* const           givenSub)
{
    int i;

    for (i = 0; i < givenSub->psa.psa_len; i++) {
        prod_spec* prodSpec = givenSub->psa.psa_val + i;
        const EntryProdSpec* eps;

        for (eps = epc_firstProdSpec(epc); eps != NULL ;
                eps = epc_nextProdSpec(epc, eps)) {
            eps_remove_prod_spec(eps, prodSpec);
        }
    }
}

/**
 * Indicates if the subscription of an entry is a subset (proper or improper)
 * of a given subscription. The time-limits of the subscriptions are ignored.
 *
 * @param entrySub      [in] Pointer to the entry's subscription
 * @param givenSub      [in] Pointer to the given subscription
 * @retval 0            The entry's subscription isn't a subset
 * @retval 1            The entry's subscription is a subset
 */
static int epc_isSubsetOf(
        const EntryProdClass* const entrySub,
        const prod_class* const     givenSub)
{
    const EntryProdSpec* eps;

    for (eps = epc_firstProdSpec(entrySub); eps != NULL;
            eps = epc_nextProdSpec(entrySub, eps)) {
        int i;

        for (i = 0; i < givenSub->psa.psa_len; i++) {
            prod_spec* prodSpec = givenSub->psa.psa_val + i;

            if (eps_isSubsetOf(eps, prodSpec))
                break;
        }

        if (i >= givenSub->psa.psa_len)
            return 0;
    }

    return 1;
}

/**
 * Returns the size of an entry, in bytes, given the size, in bytes, of its
 * product-class.
 *
 * @param prodClassSize     [in] The size, in bytes, of the entry's
 *                          product-class.
 * @return                  The size, in bytes, of the corresponding entry.
 */
static size_t entry_sizeof_internal(
        const size_t    prodClassSize)
{
    return roundUp(sizeof(uldb_Entry) - sizeof(EntryProdClass) + prodClassSize,
            entryAlignment);
}

/**
 * Returns the size, in bytes, that an entry will have given the product-class
 * that it will contain.
 *
 * @param prodClass     [in] Pointer to the product-class that the entry will
 *                      contain.
 * @return              The size, in bytes, of the corresponding entry.
 */
static size_t entry_sizeof(
        const prod_class* const prodClass)
{
    return entry_sizeof_internal(epc_sizeof(prodClass));
}

/**
 * Initializes an entry.
 *
 * @param[out] entry       Pointer to the entry.
 * @param[in]  pid         PID of the upstream LDM
 * @param[in]  protoVers   Protocol version number (e.g., 5 or 6)
 * @param[in]  isNotifier  Type of the upstream LDM
 * @param[in]  isPrimary   Whether the upstream LDM is in primary transfer
 *                         mode or not
 * @param[in]  sockAddr    Socket Internet address of the downstream LDM
 * @param[in]  prodClass   Data-request of the downstream LDM
 */
static void entry_init(
        uldb_Entry* const           entry,
        const pid_t                 pid,
        const int                   protoVers,
        const int                   isNotifier,
        const int                   isPrimary,
        const struct sockaddr_in*   sockAddr,
        const prod_class* const     prodClass)
{
    EntryProdClass* const   epc = &entry->prodClass;

    epc_init(epc, prodClass);

    (void) memcpy(&entry->sockAddr, sockAddr, sizeof(*sockAddr));
    entry->pid = pid;
    entry->protoVers = protoVers;
    entry->isNotifier = isNotifier;
    entry->isPrimary = isPrimary;
    entry->size = entry_sizeof_internal(epc_getSize(epc));
}

/**
 * Returns the PID of an entry.
 *
 * @param entry     [in] Pointer to the entry
 * @return          The PID of the entry
 */
static pid_t entry_getPid(
        const uldb_Entry* const entry)
{
    return entry->pid;
}

/**
 * Returns the protocol version (e.g., 5 or 6) of an entry.
 *
 * @param entry     [in] Pointer to the entry
 * @return          The protocol version of the entry
 */
static int entry_getProtocolVersion(
        const uldb_Entry* const entry)
{
    return entry->protoVers;
}

/**
 * Indicates if the upstream LDM of an entry is a notifier or not.
 *
 * @param entry     [in] Pointer to the entry
 * @retval 0        The associated upstream LDM is not sending
 *                  only data-notifications to the downstream LDM.
 * @retval 1        The associated upstream LDM is sending only
 *                  data-notifications to the downstream LDM.
 */
static pid_t entry_isNotifier(
        const uldb_Entry* const entry)
{
    return entry->isNotifier;
}

/**
 * Indicates if the upstream LDM of an entry is in primary transfer mode.
 *
 * @param entry     [in] Pointer to the entry
 * @retval 0        The associated upstream LDM is in ALTERNATE transfer mode
 * @retval 1        The associated upstream LDM is in PRIMARY transfer mode.
 */
static pid_t entry_isPrimary(
        const uldb_Entry* const entry)
{
    return entry->isPrimary;
}

/**
 * Returns the socket Internet address of the downstream LDM of an entry.
 *
 * @param entry     [in] Pointer to the entry
 * @return          Pointer to the socket Internet address of the downstream
 *                  LDM of the entry
 */
static const struct sockaddr_in* entry_getSockAddr(
        const uldb_Entry* const entry)
{
    return &entry->sockAddr;
}

#if 0
/**
 * Returns an entry's product-class.
 *
 * @param entry         [in] The entry to have its product-class returned
 * @return A pointer to the entry's product-class
 */
static const EntryProdClass* entry_getEntryProdClass(
        const uldb_Entry* const entry)
{
    return &entry->prodClass;
}
#endif

/**
 * Returns the product-class of an entry.
 *
 * @param entry     [in] Pointer to the entry to have its product-class returned
 * @param prodClass [out] Address of pointer to returned product-class. The
 *                  client should call free_prod_class(*prodClass) when the
 *                  product-class is no longer needed.
 * @retval 0            Success
 * @retval ULDB_SYSTEM  System error. log_add() called.
 */
static uldb_Status entry_getProdClass(
        const uldb_Entry* const entry,
        prod_class** const prodClass)
{
    int status;
    const EntryProdClass* const epc = &entry->prodClass;
    prod_class* pc;

    if ((status = epc_getEverythingButProdSpecs(epc, &pc)) != 0) {
        log_add("Couldn't get most of product-class from entry");
    }
    else {
        const EntryProdSpec* eps;
        prod_spec* ps = pc->psa.psa_val;

        status = ULDB_SUCCESS;

        for (eps = epc_firstProdSpec(epc); NULL != eps;
                eps = epc_nextProdSpec(epc, eps)) {
            if ((status = eps_get(eps, ps)) != 0)
                break;

            ps++;
        }

        if (status) {
            free_prod_class(pc);
        }
        else {
            *prodClass = pc;
        }
    }

    return status;
}

/**
 * Returns the size, in bytes, of an entry.
 *
 * @param entry         [in] Pointer to the entry.
 * @return              The size, in bytes, of the entry.
 */
static size_t entry_getSize(
        const uldb_Entry* const entry)
{
    return entry->size;
}

/**
 * Indicates if the subscription of an entry is a subset (proper or improper)
 * of a given subscription. The time-limits of the subscriptions are ignored.
 *
 * @param entry         [in] Pointer to the entry
 * @param givenSub      [in] Pointer to the given subscription
 * @retval 0            The entry's subscription isn't a subset
 * @retval 1            The entry's subscription is a subset
 */
static int entry_isSubsetOf(
        const uldb_Entry* const entry,
        const prod_class* const givenSub)
{
    return epc_isSubsetOf(&entry->prodClass, givenSub);
}

/**
 * Removes an entry's subscription from a given subscription. The time-limits
 * of the subscriptions are ignored.
 *
 * @param entry         [in] Pointer to the entry
 * @param sub           [in/out] Pointer to a subscription
 */
static void entry_removeSubscriptionFrom(
        const uldb_Entry* const entry,
        prod_class* const       sub)
{
    epc_remove_prod_specs(&entry->prodClass, sub);
    clss_scrunch(sub);
}

/**
 * Returns the string encoding of an entry.
 *
 * @param entry         [in] The entry to be encoded
 * @param buf           [in/out] Pointer to a buffer into which to encode the
 *                      entry
 * @param size          [in] Size of the buffer in bytes
 * @return              The number of bytes that would have been written to the
 *                      output buffer had its size been sufficiently large
 *                      excluding the terminating NUL byte
 */
static int entry_toString(
        const uldb_Entry* const entry,
        char* const             buf,
        const size_t            size)
{
    prod_class_t*   prodClass;
    int             nbytes;

    if (entry_getProdClass(entry, &prodClass)) {
        const char* const   msg = "Couldn't format entry";

        log_error_q("%s", msg);
        nbytes = snprintf(buf, size, "%s", msg);
    }
    else {
        nbytes = snprintf(buf, size,
                "(addr=%s, pid=%ld, vers=%d, type=%s, mode=%s, sub=(%s))",
                inet_ntoa(entry->sockAddr.sin_addr), (long)entry->pid,
                entry->protoVers, entry->isNotifier ? "notifier" : "feeder",
                entry->isPrimary ? "primary" : "alternate",
                s_prod_class(NULL, 0, prodClass));

        free_prod_class(prodClass);
    }

    return nbytes;
}

/**
 * Returns the size of a segment given the amount of space for entries.
 *
 * @param entriesCapacity   [in] Amount of space for entries in bytes
 * @return                  Size of corresponding segment in bytes
 */
static size_t seg_size(
        const size_t entriesCapacity)
{
    return sizeof(Segment) - sizeof(uldb_Entry)
            + roundUp(entriesCapacity, entryAlignment);
}

/**
 * Returns the amount of space for entries given the size of a segment.
 *
 * @param nbytes            [in] Size of the segment
 * @return                  Amount of space for entries in bytes
 */
static size_t seg_entriesCapacity(
        const size_t nbytes)
{
    return nbytes - sizeof(Segment) + sizeof(uldb_Entry);
}

/**
 * Initializes a segment.
 *
 * @param segment           [in/out] Pointer to segment
 * @param nbytes            [in] Size of segment in bytes
 */
static void seg_init(
        Segment* const segment,
        size_t nbytes)
{
    segment->entriesCapacity = seg_entriesCapacity(nbytes);
    segment->entriesSize = 0;
    segment->numEntries = 0;
}

/**
 * Returns the capacity of a segment in bytes.
 *
 * @param segment   [in] Pointer to segment
 * @retval          Capcity of given segment
 */
static size_t seg_getCapacity(
        const Segment* const segment)
{
    return segment->entriesCapacity;
}

/**
 * Returns the capacity a segment would need in order to accommodate another
 * entry of a given size.
 *
 * @param segment   [in] Pointer to segment
 * @param size      [in] Size of entry in bytes
 * @return          Capacity, in bytes, the segment would need
 */
static size_t seg_getNeededCapacity(
        const Segment* const segment,
        const size_t size)
{
    return segment->entriesSize + size;
}

/**
 * Copies entries from a source segment to a destination segment. The
 * destination segment becomes a copy of the source segment.
 *
 * @param dest          [out] Pointer to destination segment
 * @param src           [in] Pointer to source segment
 * @retval ULDB_SUCCESS Success
 * @retval ULDB_SYSTEM  Destination is too small to hold the source. log_add()
 *                      called.
 */
static uldb_Status seg_copy(
        Segment* const dest,
        const Segment* const src)
{
    int status;

    if (src->entriesSize > dest->entriesCapacity) {
        log_add("Destination is smaller than source: %lu < %lu",
                (unsigned long)dest->entriesCapacity,
                (unsigned long) src->entriesCapacity);
        status = ULDB_SYSTEM;
    }
    else {
        (void) memmove(dest->entries, src->entries, src->entriesSize);
        dest->entriesSize = src->entriesSize;
        dest->numEntries = src->numEntries;
        status = ULDB_SUCCESS;
    }

    return status;
}

/**
 * Clones a segment.
 *
 * @param segment           [in] Pointer to segment to be cloned
 * @param clone             [out] Address of pointer to clone. Upon successful
 *                          return, the client should call "seg_free(*clone)"
 *                          when the clone is no longer needed.
 * @retval ULDB_SUCCESS     Success
 * @retval ULDB_SYSTEM      System error. log_add() called.
 */
static uldb_Status seg_clone(
        const Segment* const segment,
        Segment** const clone)
{
    int status;
    size_t nbytes = seg_size(segment->entriesCapacity);
    Segment* const copy = (Segment*) malloc(nbytes);

    if (NULL == copy) {
        log_add_syserr("Couldn't allocate %lu-byte clone-buffer", nbytes);
        status = ULDB_SYSTEM;
    }
    else {
        seg_init(copy, nbytes);

        if ((status = seg_copy(copy, segment)) != 0) {
            log_add("Couldn't copy entries into clone-buffer");
            free(copy);
        }
        else {
            *clone = copy;
            status = ULDB_SUCCESS;
        }
    } // `copy` allocated

    return status;
}

/**
 * Frees a segment returned by seg_clone().
 *
 * @param clone         [in] Pointer to the segment
 */
static void seg_free(
        Segment* const clone)
{
    free(clone);
}

/**
 * Returns a pointer to the first entry in a segment.
 *
 * @param segment       [in] Pointer to the segment
 * @retval NULL         No entries in the segment
 * @return              A pointer to the first entry in the segment
 */
static const uldb_Entry* seg_firstEntry(
        const Segment* const segment)
{
    return segment->entriesSize == 0 ? NULL : segment->entries;
}

/**
 * Returns a pointer to the next entry in a segment.
 *
 * @param segment       [in] Pointer to a segment
 * @param entry         [in] Pointer to an entry in the segment
 * @retval NULL         No more entries in the segment
 * @return              Pointer to the next entry
 */
static const uldb_Entry* seg_nextEntry(
        const Segment* const segment,
        const uldb_Entry* const entry)
{
    uldb_Entry* const nextEntry = (uldb_Entry*) ((char*) entry + entry->size);

    return (char*) nextEntry
            >= (char*) segment->entries + segment->entriesSize ?
            NULL : nextEntry;
}

/**
 * Returns a pointer to the first unset entry in a segment.
 *
 * @param segment   [in] Pointer to a segment
 * @return          Pointer to the first unset entry
 */
static uldb_Entry* seg_tailEntry(
        Segment* const segment)
{
    return (uldb_Entry*) (((char*) segment->entries) + segment->entriesSize);
}

/**
 * Returns the number of entries in a shared-memory segment.
 *
 * @param segment       [in] Pointer to the shared-memory segment
 * @return              The number of entries in the shared-memory segment
 */
static unsigned seg_getSize(
        const Segment* const segment)
{
    return segment->numEntries;
}

/**
 * Clears a shared-memory structure.
 */
static void sm_clear(
        SharedMemory* const sm)
{
    sm->segment = NULL;
    sm->shmId = -1;
}

/**
 * Gets an existing shared-memory segment: gets the shared-memory identifier.
 *
 * @param sm            [in/out] Pointer to the shared-memory structure
 * @retval ULDB_SUCCESS Success
 * @retval ULDB_EXIST   The shared-memory segment corresponding to "sm->key"
 *                      doesn't exist. log_add() called.
 * @retval ULDB_SYSTEM  System error. log_add() called.
 */
static uldb_Status sm_setShmId(
        SharedMemory* const sm)
{
    int status;

    sm->shmId = shmget(sm->key, 0, read_write);

    if (-1 == sm->shmId) {
        log_add_syserr(NULL);
        status = (ENOENT == errno) ? ULDB_EXIST : ULDB_SYSTEM;
    }
    else {
        status = ULDB_SUCCESS;
    }

    return status;
}

/**
 * Attaches an existing shared-memory segment to a shared-memory structure:
 * sets the "shmId" and "segment" members of the structure.
 *
 * @param sm            [in/out] Pointer to shared-memory structure
 * @retval ULDB_SUCCESS Success
 * @retval ULDB_SYSTEM  System error. log_add() called.
 */
static uldb_Status sm_attach(
        SharedMemory* const sm)
{
    int status = sm_setShmId(sm);

    if (status == 0) {
        /*
         * Attach the shared-memory segment.
         */
        Segment* segment = (Segment*) shmat(sm->shmId, NULL, 0);

        if ((Segment*) -1 == segment) {
            log_add_syserr("Couldn't attach shared-memory segment %d",
                    sm->shmId);

            sm->shmId = -1;
            status = ULDB_SYSTEM;
        }
        else {
            sm->segment = segment;
            status = ULDB_SUCCESS;
        }
    } /* "sm->shmId" set */

    return status;
}

/**
 * Detaches a shared-memory segment from a shared-memory structure: clears the
 * "shmID" and "segment" members of the structure. Upon return, the
 * shared-memory segment cannot be accessed until sm_attach() is called.
 *
 * Idempotent.
 *
 * @param[in] sm           Pointer to the shared-memory structure
 * @retval    ULDB_SUCCESS Success
 * @retval    ULDB_SYSTEM  System error. log_add() called.
 */
static uldb_Status sm_detach(
        SharedMemory* const sm)
{
    int status;

    if (NULL == sm->segment) {
        status = ULDB_SUCCESS;
    }
    else {
        if (shmdt((void*)sm->segment)) {
            log_add_syserr(
                    "Couldn't detach shared-memory segment %d at address %p",
                    sm->shmId, sm->segment);

            status = ULDB_SYSTEM;
        }
        else {
            status = ULDB_SUCCESS;
        }

        sm->segment = NULL;
        sm->shmId = -1;
    }

    return status;
}

/**
 * Initializes a shared-memory structure from an existing shared-memory segment.
 *
 * @param sm[out]       Pointer to shared-memory structure
 * @param key[in]       IPC key of shared-memory segment
 * @retval ULDB_SUCCESS Success
 * @retval ULDB_EXIST   The corresponding shared-memory segment doesn't exist.
 *                      log_add() called.
 * @retval ULDB_SYSTEM  System error. log_add() called.
 */
static uldb_Status sm_init(
        SharedMemory* const sm,
        const key_t         key)
{
    sm_clear(sm);
    sm->key = key;

    int status = sm_attach(sm);

    if (status == 0) {
        status = sm_detach(sm);
        if (status)
            log_add("Couldn't detach shared-memory segment");
    }

    return status;
}

/**
 * Deletes a shared-memory segment. The shared-memory structure shall have
 * been initialized. The shared-memory segment must exist.
 *
 * @param sm            [in] Pointer to the shared-memory structure
 * @retval ULDB_SUCCESS Success
 * @retval ULDB_EXIST   The shared-memory segment doesn't exist. log_add()
 *                      called.
 * @retval ULDB_SYSTEM  System error. log_add() called.
 */
static uldb_Status sm_delete(
        SharedMemory* const sm)
{
    int status = sm_setShmId(sm);

    if (status) {
        log_add("Couldn't get shared-memory segment");
    }
    else {
        if (shmctl(sm->shmId, IPC_RMID, NULL )) {
            struct shmid_ds shmDs;

            log_add_syserr("Couldn't delete shared-memory segment %d",
                    sm->shmId);

            if (shmctl(sm->shmId, IPC_STAT, &shmDs)) {
                log_add_syserr(
                        "Couldn't read data-structure of shared-memory segment: %s",
                        strerror(errno));
            }
            else {
                log_add("UID=%d, GID=%d, mode=%#o", shmDs.shm_perm.uid,
                        shmDs.shm_perm.gid, shmDs.shm_perm.mode);
            }

            status = ULDB_SYSTEM;
        }
        else {
            status = ULDB_SUCCESS;
        }

        sm->shmId = -1;
    } /* the shared-memory segment is gotten */

    return status;
}

/**
 * Deletes a shared-memory segment by IPC key.
 *
 * @param key           The IPC key
 * @retval ULDB_SUCCESS Success
 * @retval ULDB_EXIST   The shared-memory segment doesn't exist. log_add()
 *                      called.
 * @retval ULDB_SYSTEM  System error. log_add() called.
 */
static uldb_Status sm_deleteByKey(
        const key_t key)
{
    SharedMemory sm;
    int status = sm_init(&sm, key);

    if (ULDB_SUCCESS == status)
        status = sm_delete(&sm);

    return status;
}

/**
 * Creates a shared-memory segment.
 *
 * @param[in] sm        Pointer to the shared-memory structure
 * @param[in] key       The IPC key for the shared-memory
 * @param[in] size      The initial size, in bytes, of the data portion of
 *                      the shared-memory segment
 * @retval ULDB_SUCCESS Success
 * @retval ULDB_EXIST   The shared-memory segment already exists. log_add()
 *                      called.
 * @retval ULDB_SYSTEM  System error. log_add() called.
 */
static uldb_Status sm_create(
        SharedMemory* const sm,
        const key_t key,
        size_t size)
{
    int status;
    int shmId;
    size_t nbytes = seg_size(size);

    sm_clear(sm);

    /*
     * Create the shared-memory segment
     */
    int flags = IPC_CREAT | IPC_EXCL | read_write;
    shmId = shmget(key, nbytes, flags);

    if (-1 == shmId) {
        log_add_syserr("shmget() failure: key=%#lx, nbytes=%zu, flags=%#o",
                (unsigned long)key, nbytes, flags);

        if (EEXIST != errno) {
            status = ULDB_SYSTEM;
        }
        else {
            struct shmid_ds shmDs;

            shmId = shmget(key, 0, read_only);

            if (-1 == shmId) {
                log_add_syserr("Couldn't get shared-memory segment");
            }
            else if (shmctl(shmId, IPC_STAT, &shmDs)) {
                log_add_syserr(
                        "Couldn't read metadata of shared-memory segment");
            }
            else {
                log_add(
                        "Shared-memory segment already exists: "
                        "size=%lu, pid=%ld, #attach=%lu",
                        (unsigned long)shmDs.shm_segsz, (long)shmDs.shm_cpid,
                        (unsigned long)shmDs.shm_nattch);
            }

            status = ULDB_EXIST;
        }
    }
    else {
        /*
         * Initialize the shared-memory segment.
         */
        sm->key = key;

        if ((status = sm_attach(sm)) != 0) {
            log_add("Couldn't attach shared-memory segment");

            (void) sm_delete(sm);
        }
        else {
            seg_init(sm->segment, nbytes);

            if ((status = sm_detach(sm)) != 0) {
                log_add("Couldn't detach shared-memory segment");
            }
        }
    } /* valid "shmId" */

    return status;
}

/**
 * Returns the number of entries in a shared-memory
 *
 * @param sm            [in] Pointer to the shared-memory structure
 * @return              The number of entries in the shared-memory
 */
static unsigned sm_getSize(
        const SharedMemory* const sm)
{
    return seg_getSize(sm->segment);
}

/**
 * Ensures that a shared-memory segment has space for an additional entry.
 *
 * @param sm        [in/out] Pointer to the shared-memory structure
 * @param size      [in] The size of the new entry in bytes
 * @retval ULDB_SUCCESS Success
 * @retval ULDB_SYSTEM  System error. log_add() called.
 */
static uldb_Status sm_ensureSpaceForEntry(
        SharedMemory* const sm,
        const unsigned size)
{
    int status;
    Segment* const segment = sm->segment;
    const size_t neededCapacity = seg_getNeededCapacity(segment, size);

    if (neededCapacity <= seg_getCapacity(segment)) {
        status = ULDB_SUCCESS;
    }
    else {
        Segment* clone;

        if ((status = seg_clone(segment, &clone)) != 0) {
            log_add("Couldn't clone shared-memory segment");
        }
        else {
            if ((status = sm_detach(sm)) != 0) {
                log_add("Couldn't detach old shared-memory");
            }
            else if ((status = sm_delete(sm)) != 0) {
                log_add("Couldn't delete old shared-memory");
            }
            else {
                if ((status = sm_create(sm, sm->key, 2 * neededCapacity)) != 0) {
                    log_add("Couldn't create new shared-memory segment");
                }
                else if ((status = sm_attach(sm)) != 0) {
                    log_add( "Couldn't attach new shared-memory segment");
                }
                else if ((status = seg_copy(sm->segment, clone)) != 0) {
                    log_add(
                            "Couldn't copy clone-buffer into new shared-memory segment");
                }
            } /* old shared-memory segment deleted */

            seg_free(clone);
        }
    } /* shared-memory segment must be expanded */

    return status;
}

/**
 * Unconditionally appends an entry to the shared-memory segment. The
 * shared-memory segment must have sufficient room for the new entry.
 *
 * @param sm            [in/out] Pointer to the shared-memory structure
 * @param pid           [in] PID of the upstream LDM
 * @param protoVers     [in] Protocol version number (e.g., 5 or 6)
 * @param isNotifier    [in] Type of the upstream LDM
 * @param isPrimary     [in] Whether the upstream LDM is in primary transfer
 *                      mode or not
 * @param sockAddr      [in] Socket Internet address of the downstream LDM
 * @param prodClass     [in] Data-request of the downstream LDM
 */
static void sm_append(
        SharedMemory* const         sm,
        const pid_t                 pid,
        const int                   protoVers,
        const int                   isNotifier,
        const int                   isPrimary,
        const struct sockaddr_in*   sockAddr,
        const prod_class* const     prodClass)
{
    Segment* const      segment = sm->segment;
    uldb_Entry* const   entry = seg_tailEntry(segment);

    entry_init(entry, pid, protoVers, isNotifier, isPrimary, sockAddr,
            prodClass);

    segment->entriesSize += entry_getSize(entry);
    segment->numEntries++;
}

/**
 * Adds an entry for an upstream LDM. Increases the amount of shared-memory
 * if necessary.
 *
 * @param sm            [in/out] Pointer to the shared-memory structure
 * @param pid           [in] PID of the upstream LDM
 * @param protoVers     [in] Protocol version number (e.g., 5 or 6)
 * @param isNotifier    [in] Type of the upstream LDM
 * @param isPrimary     [in] Whether the upstream LDM is in primary transfer
 *                      mode or not
 * @param sockAddr      [in] Socket Internet address of the downstream LDM
 * @param prodClass     [in] Data-request of the downstream LDM
 * @retval ULDB_SUCCESS     Success
 * @retval ULDB_SYSTEM      System error. log_add() called.
 */
static uldb_Status sm_addUpstreamLdm(
        SharedMemory* const sm,
        const pid_t pid,
        const int protoVers,
        const int isNotifier,
        const int isPrimary,
        const struct sockaddr_in* sockAddr,
        const prod_class* const prodClass)
{
    int status;
    size_t size = entry_sizeof(prodClass);

    if ((status = sm_ensureSpaceForEntry(sm, size)) != 0) {
        log_add("Couldn't ensure sufficient shared-memory");
    }
    else {
        sm_append(sm, pid, protoVers, isNotifier, isPrimary, sockAddr, prodClass);
        status = ULDB_SUCCESS;
    }

    return status;
}

/**
 * Vets a new upstream LDM. Reduces the subscription according to existing
 * subscriptions from the same downstream host and terminates every
 * previously-existing upstream LDM process that's feeding (not notifying) a
 * subset of the subscription to the same IP address.
 *
 * @param sm            [in/out] Pointer to shared-memory structure
 * @param myPid         [in] PID of the upstream LDM process
 * @param protoVers     [in] Protocol version number (e.g., 5 or 6)
 * @param isNotifier    [in] Type of the upstream LDM process
 * @param isPrimary     [in] Whether the upstream LDM is in primary transfer
 *                      mode or not
 * @param sockAddr      [in] Socket Internet address of the downstream LDM
 * @param desired       [in] The subscription desired by the downstream LDM
 * @param allowed       [out] The allowed subscription. Equal to the desired
 *                      subscription reduced by existing subscriptions from the
 *                      same host. Might specify an empty subscription. The
 *                      client should free when it's no longer needed.
 * @retval 0            Success. "*allowed" is set. Might specify an empty
 *                      subscription.
 * @retval ULDB_EXIST   Entry for PID already exists. log_add() called.
 * @retval ULDB_SYSTEM  System error. log_add() called.
 */
static uldb_Status sm_vetUpstreamLdm(
    SharedMemory* const restrict       sm,
    const pid_t                        myPid,
    const int                          protoVers,
    const int                          isNotifier,
    const int                          isPrimary,
    const struct sockaddr_in* restrict sockAddr,
    const prod_class* const restrict   desired,
    prod_class** const restrict        allowed)
{
    int                  status = 0; /* success */
    const Segment* const segment = sm->segment;
    const uldb_Entry*    entry;
    prod_class_t*        allow = dup_prod_class(desired);

    if (NULL == allow) {
        log_add("Couldn't duplicate desired subscription");
        status = ULDB_SYSTEM;
    }
    else {
        for (entry = seg_firstEntry(segment); entry != NULL ; entry =
                seg_nextEntry(segment, entry)) {
            if (myPid == entry->pid) {
                log_add("Entry already exists for PID %ld", myPid);
                status = ULDB_EXIST;
                break;
            }

            if (protoVers == entry->protoVers &&
            		ipAddressesAreEqual(sockAddr, entry_getSockAddr(entry))
                    && !isNotifier && !entry_isNotifier(entry)) {
                if (entry_isSubsetOf(entry, allow)) {
                    char    buf[1024];

                    (void)entry_toString(entry, buf, sizeof(buf));

                    if (kill(entry_getPid(entry), SIGTERM)) {
                        log_warning_q(
                                "Couldn't terminate redundant upstream LDM %s",
                                buf);
                    }
                    else {
                        log_notice_q("Terminated redundant upstream LDM %s",
                        		buf);
                    }
                }
                else {
                    entry_removeSubscriptionFrom(entry, allow);

                    if (0 >= allow->psa.psa_len)
                        break;
                }
            } /* upstream LDM matches entry */
        } /* entry loop */

        if (status) {
            free_prod_class(allow);
        }
        else {
            *allowed = allow;
        }
    } /* "allow" allocated */

    return status;
}

/**
 * Adds an upstream LDM entry to shared-memory. Increases the amount of
 * shared-memory if necessary. Reduces the subscription according to existing
 * subscriptions from the same downstream host and, if `isAntiDosEnabled()`
 * returns `true`, terminates every previously-existing upstream LDM process
 * that's feeding (not notifying) a subset of the subscription to the same IP
 * address.
 *
 * @param sm            [in/out] Pointer to shared-memory structure
 * @param pid           [in] PID of the upstream LDM process
 * @param protoVers     [in] Protocol version number (e.g., 5 or 6)
 * @param isNotifier    [in] Type of the upstream LDM process
 * @param isPrimary     [in] Whether the upstream LDM is in primary transfer
 *                      mode or not
 * @param sockAddr      [in] Socket Internet address of the downstream LDM
 * @param desired       [in] The subscription desired by the downstream LDM
 * @param allowed       [out] The allowed subscription. Equal to the desired
 *                      subscription reduced by existing subscriptions from the
 *                      same host. Might specify an empty subscription. The
 *                      client should free when it's no longer needed.
 * @retval 0            Success. "*allowed" is set. If the resulting
 *                      subscription is the empty set, however, then the
 *                      shared-memory will not have been modified.
 * @retval ULDB_INIT    Module not initialized. log_add() called.
 * @retval ULDB_EXIST   Entry for PID already exists. log_add() called.
 * @retval ULDB_SYSTEM  System error. log_add() called.
 */
static uldb_Status sm_add(
    SharedMemory* const restrict       sm,
    const pid_t                        pid,
    const int                          protoVers,
    const int                          isNotifier,
    const int                          isPrimary,
    const struct sockaddr_in* restrict sockAddr,
    const prod_class* const restrict   desired,
    prod_class** const restrict        allowed)
{
    int         status;
    prod_class* sub;

    if (isAntiDosEnabled()) {
        status = sm_vetUpstreamLdm(sm, pid, protoVers, isNotifier, isPrimary,
                sockAddr, desired, &sub);
    }
    else {
        if ((sub = dup_prod_class(desired)) == NULL) {
            log_add("Couldn't duplicate desired subscription");
            status = ULDB_SYSTEM;
        }
        else {
            status = 0;
        }
    }

    if (0 == status) {
        if (0 < sub->psa.psa_len) {
            if ((status = sm_addUpstreamLdm(sm, pid, protoVers, isNotifier,
                    isPrimary, sockAddr, sub)) != 0) {
                log_add("Couldn't add request from %s",
                        inet_ntoa(sockAddr->sin_addr));
            }
        }

        if (status) {
            free_prod_class(sub);
        }
        else {
            *allowed = sub;
        }
    } /* "sub" allocated */

    return status;
}

/**
 * Removes a PID from the shared-memory.
 *
 * @param sm            [in/out] Pointer to the shared-memory structure
 * @param pid           [in] PID to be removed
 * @retval ULDB_SUCCESS Success
 * @retval ULDB_EXIST   No corresponding entry found. log_add() called.
 * @retVal ULDB_SYSTEM  System error. log_add() called.
 */
static uldb_Status sm_remove(
        SharedMemory* const sm,
        const pid_t pid)
{
    int status;
    Segment* const segment = sm->segment;
    const uldb_Entry* entry;

    for (entry = seg_firstEntry(segment); NULL != entry;
            entry = seg_nextEntry(segment, entry)) {
        if (entry->pid == pid)
            break;
    }

    if (NULL == entry) {
        log_add("Entry for PID %d not found", pid);
        status = ULDB_EXIST;
    }
    else {
        unsigned entrySize = entry->size;
        const uldb_Entry* const nextEntry = seg_nextEntry(segment, entry);

        if (NULL != nextEntry) {
            const uldb_Entry* const tail = seg_tailEntry(segment);

            (void) memmove((void*) entry, nextEntry,
                    (char*) tail - (char*) nextEntry);
        }

        segment->entriesSize -= entrySize;
        segment->numEntries--;
        status = ULDB_SUCCESS;
    }

    return status;
}

/**
 * Indicates whether or not a database is open.
 *
 * @param db            [in] Pointer to the database structure
 * @retval 1            The database is open
 * @retval 0            The database is not open
 * @retval ULDB_INIT    Database is not open. log_add() called.
 */
static int db_isOpen(
        const Database* const db)
{
    return VALID_STRING == db->validString;
}

/**
 * Verifies that a database is open.
 *
 * @param db            [in] Pointer to the database structure
 * @retval ULDB_SUCCESS Success
 * @retval ULDB_INIT    The database is not open. log_add() called.
 */
static uldb_Status db_verifyOpen(
        const Database* const db)
{
    if (db_isOpen(db))
        return ULDB_SUCCESS;

    log_add("Database is not open");

    return ULDB_INIT;
}

/**
 * Verifies that a database is closed.
 *
 * @param db            [in] Pointer to the database structure
 * @retval ULDB_SUCCESS Success
 * @retval ULDB_INIT    The database is open. log_add() called.
 */
static uldb_Status db_verifyClosed(
        const Database* const db)
{
    if (!db_isOpen(db))
        return ULDB_SUCCESS;

    log_add("Database is open");

    return ULDB_INIT;
}

/**
 * Locks a database. Upon successful return, the client should call db_unlock()
 * when done accessing the database.
 *
 * @param db                [in/out]  Pointer to a database structure
 * @param forWriting        [in] Prepare for writing or reading?
 * @retval ULDB_SUCCESS     Success. Database is locked.
 * @retval ULDB_INIT        Database is not open. log_add() called.
 * @retval ULDB_SYSTEM      System error. log_add() called.
 */
static uldb_Status db_lock(
        Database* const db,
        const int forWriting)
{
    int status = db_verifyOpen(db);

    if (ULDB_SUCCESS == status) {
        if (forWriting) {
            if ((status = srwl_writeLock(db->lock)) != 0)
                log_add("Couldn't lock database for writing");
        }
        else {
            if ((status = srwl_readLock(db->lock)) != 0)
                log_add("Couldn't lock database for reading");
        }

        if (status) {
            status = ULDB_SYSTEM;
        }
        else {
            if ((status = sm_attach(&db->sharedMemory)) != 0) {
                log_add("Couldn't attach shared-memory");
                (void) srwl_unlock(db->lock);
            }
        } /* lock is locked */
    }

    return status;
}

/**
 * Locks a database for reading. Client should call db_unlock() when done.
 *
 * @param db                [in/out]  Pointer to a database structure
 * @retval ULDB_SUCCESS     Success. Database is locked for reading.
 * @retval ULDB_INIT        Database is not open. log_add() called.
 * @retval ULDB_SYSTEM      System error. log_add() called.
 */
static uldb_Status db_readLock(
        Database* const db)
{
    return db_lock(db, 0);
}

/**
 * Locks a database for writing. Client should call db_unlock() when done.
 *
 * @param db                [in/out]  Pointer to a database structure
 * @retval ULDB_SUCCESS     Success. Database is locked for writing.
 * @retval ULDB_INIT        Database is not open. log_add() called.
 * @retval ULDB_SYSTEM      System error. log_add() called.
 */
static uldb_Status db_writeLock(
        Database* const db)
{
    return db_lock(db, 1);
}

/**
 * Unlocks a database (i.e., releases it for access by another process).
 *
 * @param db                [in/out] Pointer to a database structure
 * @retval ULDB_SUCCESS     Success
 * @retval ULDB_SYSTEM      System error. log_add() called.
 */
static uldb_Status db_unlock(
        Database* const db)
{
    int status = sm_detach(&db->sharedMemory);

    if (status) {
        log_add("Couldn't detach shared-memory");
    }
    else if (srwl_unlock(db->lock)) {
        log_add("Couldn't unlock database");

        status = ULDB_SYSTEM;
    }

    return status;
}

/**
 * Ensures that this module is initialized.
 */
static void uldb_ensureModuleInitialized(
        void)
{
    if (!moduleInitialized) {
        mode_t um = umask(0);

        umask(um);

        read_only = 0444 & ~um;
        read_write = 0666 & ~um;
        prodSpecAlignment = getAlignment(sizeof(EntryProdSpec));
        prodClassAlignment = getAlignment(sizeof(EntryProdClass));
        entryAlignment = getAlignment(sizeof(uldb_Entry));

        cs_init();

        moduleInitialized = 1;
    }
}

/**
 * Returns the IPC key.
 *
 * @param path              [in] Pathname of an existing file to associate with
 *                          the database or NULL to obtain the default
 *                          association. Different pathnames obtain different
 *                          databases.
 * @param key               [out] The IPC key corresponding to "path"
 * @retval ULDB_SUCCESS     Success
 * @retval ULDB_SYSTEM      System error. log_add() called.
 */
static uldb_Status uldb_getKey(
        const char* path,
        key_t* const key)
{
    int status;
    key_t k;

    if (NULL == path)
        path = DEFAULT_KEY_PATH;

    k = ftok(path, KEY_INDEX);

    if ((key_t)-1 == k) {
        log_add_syserr("Couldn't get IPC key for path \"%s\", index %d", path,
                KEY_INDEX);
        status = ULDB_SYSTEM;
    }
    else {
        *key = k;
        status = ULDB_SUCCESS;
    }

    return status;
}

/**
 * Initializes the database and returns the IPC key.
 *
 * @param[in]  path         Pathname of an existing file to associate with
 *                          the database or NULL to obtain the default
 *                          association. Different pathnames obtain different
 *                          databases.
 * @param[out] key          Pointer to IPC key
 * @retval ULDB_SUCCESS     Success
 * @retval ULDB_INIT        Database already open. log_add() called.
 * @retval ULDB_SYSTEM      System error. log_add() called.
 */
static uldb_Status uldb_init(
        const char* const restrict path,
        key_t* const restrict      key)
{
    uldb_ensureModuleInitialized();

    int status = db_verifyClosed(&database);
    if (status) {
        log_add("Database already open");
    }
    else {
        status = uldb_getKey(path, key);
        if (status)
            log_add("Couldn't get IPC key");
    }

    return status;
}

/**
 * Creates the database.
 *
 * @param path          [in] Pathname of an existing file to associate with the
 *                      database or NULL to obtain the default association.
 *                      Different pathnames obtain different databases.
 * @param capacity      [in] Initial capacity of the database in bytes
 * @retval ULDB_SUCCESS Success.
 * @retval ULDB_INIT    Database already open. log_add() called.
 * @retval ULDB_EXIST   Database already exists. log_add() called.
 * @retval ULDB_SYSTEM  System error. log_add() called.
 */
uldb_Status uldb_create(
        const char* const path,
        unsigned capacity)
{
    key_t key;
    int status = uldb_init(path, &key);

    if (status) {
        log_add("Couldn't initialize database");
    }
    else if ((status = sm_create(&database.sharedMemory, key, capacity)) != 0) {
        log_add("Couldn't create shared-memory component");
    }
    else if (srwl_create(key, &database.lock)) {
        log_add("Couldn't create lock component");
        (void) sm_delete(&database.sharedMemory);
        status = ULDB_SYSTEM;
    }
    else {
        database.validString = VALID_STRING;
    }

    return status;
}

/**
 * Opens the existing database.
 *
 * @param path           [in] Pathname of an existing file to associate with the
 *                       database or NULL to obtain the default association.
 *                       Different pathnames obtain different databases.
 * @retval ULDB_SUCCESS  Success.
 * @retval ULDB_INIT     Database already open. log_add() called.
 * @retval ULDB_EXIST    The database doesn't exist. log_add() called.
 * @retval ULDB_SYSTEM   System error. log_add() called.
 */
uldb_Status uldb_open(
        const char* const path)
{
    key_t key;
    int status = uldb_init(path, &key);

    if (status) {
        log_add("Couldn't initialize database");
    }
    else {
        status = sm_init(&database.sharedMemory, key);

        if (status == 0) {
            if (srwl_get(key, &database.lock)) {
                log_add("Couldn't get existing lock component");
                status = ULDB_SYSTEM;
            }
            else {
                database.validString = VALID_STRING;
            }
        }
    }

    return status;
}

/**
 * Closes the database, freeing any system resources. Upon successful return,
 * the database still exists but may not be accessed until uldb_open() is
 * called.
 *
 * @retval ULDB_SUCCESS  Success.
 * @retval ULDB_INIT     Database not open. log_add() called.
 * @retval ULDB_SYSTEM   System error. loc_start() called. Resulting module
 *                       state is unspecified.
 */
uldb_Status uldb_close(
        void)
{
    int status = ULDB_INIT;

    if (moduleInitialized && db_isOpen(&database)) {
        if (srwl_free(database.lock)) {
            log_add("Couldn't free lock component");
            status = ULDB_SYSTEM;
        }
        else {
            database.lock = NULL;
            database.validString = NULL;
            status = ULDB_SUCCESS;
        }
    }

    return status;
}

/**
 * Unconditionally deletes the database.
 *
 * @param path           [in] Pathname of an existing file associated with the
 *                       database or NULL to obtain the default association.
 *                       Different pathnames obtain different databases.
 * @retval ULDB_SUCCESS  Success
 * @retval ULDB_EXIST    The database didn't exist. log_add() called.
 * @retval ULDB_SYSTEM   System error. log_add() called. Resulting module
 *                       state is unspecified.
 */
uldb_Status uldb_delete(
        const char* const path)
{
    int     status;
    key_t   key;

    uldb_ensureModuleInitialized();

    status = uldb_getKey(path, &key);

    if (status) {
        log_add("Couldn't get IPC key for database");
    }
    else {
        status = sm_deleteByKey(key);

        if (status && ULDB_EXIST != status) {
            log_add(
                    "Couldn't delete existing shared-memory database by IPC key");
        }
        else {
            log_clear();
            int lockStatus = srwl_deleteByKey(key);

            if (status)
                log_add("Shared-memory database doesn't exist");

            if (lockStatus) {
                if (RWL_EXIST != lockStatus) {
                    log_add(
                            "Couldn't delete existing semaphore-based read/write lock by IPC key");

                    status = ULDB_SYSTEM;
                }
                else {
                    log_add(
                            "Semaphore-based read/write lock doesn't exist");

                    if (ULDB_SUCCESS == status)
                        status = ULDB_EXIST;
                }
            } /* couldn't delete lock */
        } /* shared-memory deleted or doesn't exist */
    } /* got IPC key */

    database.validString = NULL;

    return status;
}

/**
 * Returns the number of entries in the database.
 *
 * @param size          [out] Pointer to the size of the database
 * @retval ULDB_SUCCESS Success
 * @retval ULDB_INIT    The database is closed. log_add() called.
 * @retval ULDB_SYSTEM  System error. log_add() called.
 */
uldb_Status uldb_getSize(
        unsigned* const size)
{
    int status;

    uldb_ensureModuleInitialized();

    if ((status = db_readLock(&database)) != 0) {
        log_add("Couldn't lock database for reading");
    }
    else {
        *size = sm_getSize(&database.sharedMemory);

        if ((status = db_unlock(&database)) != 0) {
            log_add("Couldn't unlock database");
        }
    } /* database is locked */

    return status;
}

/**
 * Adds an upstream LDM process to the database, if appropriate. This is a
 * potentially lengthy process. Most signals are blocked while this function
 * operates. Reduces the subscription according to existing subscriptions from
 * the same downstream host and, if `isAntiDosEnabled()` returns `true`,
 * terminates every previously-existing upstream LDM process that's feeding (not
 * notifying) a subset of the subscription to the same IP address.
 *
 * @param pid[in]         PID of upstream LDM process
 * @param protoVers[in]   Protocol version number (e.g., 5 or 6)
 * @param sockAddr[in]    Socket Internet address of downstream LDM
 * @param desired[in]     The subscription desired by the downstream LDM
 * @param allowed[out]    The allowed subscription or `NULL`. Equal to the
 *                        desired subscription reduced by existing subscriptions
 *                        from the same host. Might specify an empty
 *                        subscription. Upon successful return, the client
 *                        should call "free_prod_class(*allowed)" when the
 *                        allowed subscription is no longer needed.
 * @param isNotifier[in]  Whether the upstream LDM is a notifier or a feeder
 * @param isPrimary[in]   Whether the upstream LDM is in primary transfer
 *                        mode or not
 * @retval 0              Success. "*allowed" is set if non-NULL. The database
 *                        is unmodified, however, if the allowed subscription is
 *                        the empty set. The client should call
 *                        "free_prod_class(*allowed)" when the allowed
 *                        subscription is no longer needed.
 * @retval ULDB_INIT      Module not initialized. log_add() called.
 * @retval ULDB_ARG       Invalid PID. log_add() called.
 * @retval ULDB_EXIST     Entry for PID already exists. log_add() called.
 * @retval ULDB_SYSTEM    System error. log_add() called.
 */
uldb_Status uldb_addProcess(
    const pid_t                              pid,
    const int                                protoVers,
    const struct sockaddr_in* const restrict sockAddr,
    const prod_class* const restrict         desired,
    prod_class** const restrict              allowed,
    const int                                isNotifier,
    const int                                isPrimary)
{
    int status;

    if (pid <= 0) {
        log_add("Invalid PID: %ld", (long)pid);
        status = ULDB_ARG;
    }
    else {
        sigset_t    origSigSet;

        uldb_ensureModuleInitialized();
        cs_enter(&origSigSet);

        if ((status = db_writeLock(&database)) != 0) {
            log_add("Couldn't lock database");
        }
        else {
            prod_class* sub = NULL;

            status = sm_add(&database.sharedMemory, pid, protoVers,
                    isNotifier, isPrimary, sockAddr, desired, &sub);
            if (status)
            	log_add("Couldn't add program to shared-memory database");

            const int unlockStatus = db_unlock(&database);
            if (unlockStatus) {
                log_add("Couldn't unlock database");
                status = ULDB_SYSTEM;
            }

            if (status || allowed == NULL) {
                free_prod_class(sub); /* NULL safe */
            }
            else if (allowed) {
                *allowed = sub;
            }
        } /* database is locked */

        cs_leave(&origSigSet);
    } /* valid "pid" */

    return status;
}

/**
 * Removes an entry. This is a potentially lengthy operation. Most signals
 * are blocked while this function operates.
 *
 * @param pid                [in] PID of upstream LDM process
 * @retval ULDB_SUCCESS      Success. Corresponding entry found and removed.
 * @retval ULDB_INIT         Module not initialized. log_add() called.
 * @retval ULDB_ARG          Invalid PID. log_add() called.
 * @retval ULDB_EXIST        No corresponding entry found. log_add() called.
 * @retval ULDB_SYSTEM       System error. See "errno". log_add() called.
 */
uldb_Status
uldb_remove(const pid_t pid)
{
    int status;

    if (pid <= 0) {
        log_add("Invalid PID: %ld", (long)pid);
        status = ULDB_ARG;
    }
    else {
        sigset_t    origSigSet;

        uldb_ensureModuleInitialized();
        cs_enter(&origSigSet);

        if ((status = db_writeLock(&database)) != 0) {
            log_add("Couldn't lock database");
        }
        else {
            if ((status = sm_remove(&database.sharedMemory, pid)) != 0) {
                log_add("Couldn't remove process from database");
            }

            if ((status = db_unlock(&database)) != 0) {
                log_add("Couldn't unlock database");
                status = ULDB_SYSTEM;
            }
        } /* database is locked */

        cs_leave(&origSigSet);
    } /* valid "pid" */

    return status;
}

/**
 * Locks the upstream LDM database for reading. The caller should call
 * `uldb_unlock()` when the lock is no longer needed.
 *
 * @retval 0                Success. Database is locked for reading.
 * @retval ULDB_INIT        Database is not open. log_add() called.
 * @retval ULDB_SYSTEM      System error. log_add() called.
 */
uldb_Status uldb_readLock(void)
{
    return db_readLock(&database);
}

/**
 * Locks the upstream LDM database for writing. The caller should call
 * `uldb_unlock()` when the lock is no longer needed.
 *
 * @retval 0                Success. Database is locked for writing.
 * @retval ULDB_INIT        Database is not open. log_add() called.
 * @retval ULDB_SYSTEM      System error. log_add() called.
 */
uldb_Status uldb_writeLock(void)
{
    return db_writeLock(&database);
}

/**
 * Unlocks the upstream LDM database.
 *
 * @retval 0                Success. Database is locked for writing.
 * @retval ULDB_SUCCESS     Success
 * @retval ULDB_SYSTEM      System error. log_add() called.
 */
uldb_Status uldb_unlock(void)
{
    return db_unlock(&database);
}

/**
 * Returns an iterator over a snapshot of the database at the time this
 * function is called. Subsequent changes to the database will not be reflected
 * by the iterator.
 *
 * @param iterator      [out] Address of the pointer to the iterator. The
 *                      client should call uldb_iter_free(*iterator) when the
 *                      iterator is no longer needed.
 * @retval ULDB_SUCCESS Success
 * @retval ULDB_INIT    The database is not open. log_add() called.
 * @retval ULDB_SYSTEM  System error. log_add() called.
 *
 */
uldb_Status uldb_getIterator(
        uldb_Iter** const iterator)
{
    int         status;
    size_t      nbytes = sizeof(uldb_Iter);
    uldb_Iter*  iter = (uldb_Iter*) malloc(nbytes);

    uldb_ensureModuleInitialized();

    if (NULL == iter) {
        log_add_syserr("Couldn't allocate %lu-bytes for iterator", nbytes);
        status = ULDB_SYSTEM;
    }
    else {
        status = db_readLock(&database);

        if (status) {
            log_add("Couldn't lock database");
        }
        else {
            if ((status = seg_clone(database.sharedMemory.segment,
                    &iter->segment)) != 0) {
                log_add("Couldn't copy database");
            }
            else {
                iter->entry = NULL;
                *iterator = iter;
            }

            if (db_unlock(&database)) {
                log_add("Couldn't unlock database");

                if (ULDB_SUCCESS == status)
                    status = ULDB_SYSTEM;
            }
        } /* database is locked */

        if (status)
            free(iter);
    } /* "iter" allocated */

    return status;
}

/**
 * Frees an iterator.
 *
 * @param iter      [in] Pointer to the iterator
 */
void uldb_iter_free(
        uldb_Iter* const iter)
{
    seg_free(iter->segment);

    iter->segment = NULL;

    free(iter);
}

/**
 * Returns the first entry.
 *
 * @param iter          [in/out] Pointer to the iterator
 * @retval NULL         There are no entries
 * @return              Pointer to the first entry
 */
const uldb_Entry* uldb_iter_firstEntry(
        uldb_Iter* const iter)
{
    return iter->entry = seg_firstEntry(iter->segment);
}

/**
 * Returns the next entry.
 *
 * @param iter          [in/out] Pointer to the iterator
 * @retval NULL         No more entries. Unspecified behavior will result from
 *                      a subsequent call to this function without an
 *                      intervening call to uldb_iter_firstEntry().
 * @return              Pointer to the next entry
 */
const uldb_Entry* uldb_iter_nextEntry(
        uldb_Iter* const iter)
{
    return iter->entry = seg_nextEntry(iter->segment, iter->entry);
}

/**
 * Returns the PID of an entry.
 *
 * @param entry     Pointer to the entry
 * @return          The PID of the entry
 */
pid_t uldb_entry_getPid(
        const uldb_Entry* const entry)
{
    return entry_getPid(entry);
}

/**
 * Returns the protocol version (e.g., 5 or 6) of an entry.
 *
 * @param entry     Pointer to the entry
 * @return          The protocol version of the entry
 */
int uldb_entry_getProtocolVersion(
        const uldb_Entry* const entry)
{
    return entry_getProtocolVersion(entry);
}

/**
 * Indicates if the upstream LDM of an entry is a notifier or not.
 *
 * @param entry     Pointer to the entry
 * @retval 0        The associated upstream LDM is not sending
 *                  only data-notifications to the downstream LDM.
 * @retval 1        The associated upstream LDM is sending only
 *                  data-notifications to the downstream LDM.
 */
int uldb_entry_isNotifier(
        const uldb_Entry* const entry)
{
    return entry_isNotifier(entry);
}

/**
 * Indicates if the upstream LDM of an entry is in primary transfer mode.
 *
 * @param entry     Pointer to the entry
 * @retval 0        The associated upstream LDM is in ALTERNATE transfer mode
 * @retval 1        The associated upstream LDM is in PRIMARY transfer mode
 */
int uldb_entry_isPrimary(
        const uldb_Entry* const entry)
{
    return entry_isPrimary(entry);
}

/**
 * Returns the socket Internet address of the downstream LDM associated with an
 * entry.
 *
 * @param entry     Pointer to the entry
 * @return          Pointer to the socket Internet address of the associated
 *                  downstream LDM
 */
const struct sockaddr_in* uldb_entry_getSockAddr(
        const uldb_Entry* const entry)
{
    return entry_getSockAddr(entry);
}

/**
 * Returns the product-class of an entry.
 *
 * @param entry     Pointer to the entry
 * @param prodClass [out] Address of pointer to returned product-class. The
 *                  client should call free_prod_class(*prodClass) when the
 *                  product-class is no longer needed.
 * @retval ULDB_SUCCESS Success
 * @retval ULDB_SYSTEM  System error. log_add() called.
 */
uldb_Status uldb_entry_getProdClass(
        const uldb_Entry* const entry,
        prod_class** const prodClass)
{
    return entry_getProdClass(entry, prodClass);
}
