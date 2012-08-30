/*
 * Copyright 2012 University Corporation for Atmospheric Research. All rights
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

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include <ldm.h>

#include "uldb.h"
#include "globals.h"
#include "log.h"
#include "ldmprint.h"
#include "semRWLock.h"
#include "prod_class.h"

/**
 * Parameters for creating the key for the shared-memory segment and read/write
 * lock:
 */
#define KEY_PATH    getQueuePath()
#define KEY_INDEX   1

const char* VALID_STRING = __FILE__;

/**
 * A product-specification as implemented in an entry.
 * Keep consonant with sm_getSizeofEntry().
 */
typedef struct {
    unsigned size; /* size of this structure in bytes */
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
    unsigned prodSpecsSize;
    EntryProdSpec prodSpecs[1];
} EntryProdClass;

/**
 * An entry.
 * Keep consonant with sm_getSizeofEntry().
 */
struct uldb_Entry {
    unsigned size; /* size of this structure in bytes */
    struct sockaddr_in sockAddr;
    pid_t pid;
    int protoVers;
    int isNotifier;
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
 * The alignment of an entry's product-specification
 */
static size_t prodSpecAlignment;
/**
 * Golden ratio:
 */
static const double PHI = 1.6180339;
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
 * @retval 0    The alignment can't be found. log_start() called.
 * @return      The alignment parameter for the structure
 */
static size_t getAlignment(
        size_t size)
{
    size_t* alignment;
    static size_t alignments[] = { sizeof(double), sizeof(long), sizeof(int),
            sizeof(short), sizeof(char), 0 };

    for (alignment = alignments; 0 != *alignment; alignment++) {
        if ((size % *alignment) == 0) {
            break;
        }
    }

    if (0 == *alignment) {
        LOG_START1("Couldn't determine alignment for %lu-byte structure",
                (unsigned long)size);
    }

    return *alignment;
}

/**
 * Indicates if two socket Internet address are equal.
 *
 * @param addr1     [in] The first socket Internet address
 * @param addr2     [in] The second socket Internet address
 * @retval 0        The addresses are unequal
 * @retval 1        The addresses are equal
 */
static int areSocketAddressesEqual(
        const struct sockaddr_in* const addr1,
        const struct sockaddr_in* const addr2)
{
    return memcmp(&addr1->sin_addr, &addr2->sin_addr, sizeof(addr1->sin_addr))
            == 0;
}

/**
 * Returns the size, in bytes, that a product-specification will occupy in an
 * entry.
 *
 * @param pattern       [in] Pointer to the pattern of the product-specification
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
 * Vets a product-specification against one in an existing entry.
 *
 * @param entryProdSpec     [in] Pointer to a product-specification in an entry
 * @param prodSpec          [in] Pointer to a product-specification requested
 *                          by a downstream LDM
 * @retval ULDB_SUCCESS     The product-specification is valid and may be
 *                          accepted
 * @retval ULDB_DISALLOWED  The product-specification conflicts with the
 *                          existing specification. log_start() called.
 */
static uldb_Status eps_vet(
        const EntryProdSpec* const entryProdSpec,
        const prod_spec* const prodSpec)
{
    const feedtypet feedtype = prodSpec->feedtype;
    const char* const pattern = prodSpec->pattern;
    char entryFeedtype[129];
    char ldmFeedtype[129];

    if (feedtype == NONE)
        return ULDB_SUCCESS;

    if (feedtype == entryProdSpec->feedtype) {
        if (strcmp(pattern, entryProdSpec->pattern) == 0) {
            (void) sprint_feedtypet(ldmFeedtype, sizeof(ldmFeedtype), feedtype);
            LOG_START2("Duplicate pattern \"%s\" for feedtype %s", pattern,
                    ldmFeedtype);
            return ULDB_DISALLOWED;
        }

        return ULDB_SUCCESS;
    }

    if (feedtype & entryProdSpec->feedtype) {
        (void) sprint_feedtypet(ldmFeedtype, sizeof(ldmFeedtype), feedtype);
        (void) sprint_feedtypet(entryFeedtype, sizeof(entryFeedtype),
                entryProdSpec->feedtype);
        LOG_START2("Overlapping feedtypes: requested=%s, extant=%s",
                ldmFeedtype, entryFeedtype);
        return ULDB_DISALLOWED;
    }

    return ULDB_SUCCESS;
}

/**
 * Returns the product-specification of an entry's product-specification.
 *
 * @param eps       [in] Pointer to the entry's product-specification
 * @param ps        [out] Pointer to the product-specification
 */
static void eps_get(
        EntryProdSpec* const eps,
        prod_spec* const ps)
{
    ps->feedtype = eps->feedtype;
    ps->pattern = eps->pattern;
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
 * Returns a pointer to the next product-specification in a product-class
 *
 * @param prodClass     [in] Pointer to the product-class
 * @param prodSpec      [in] Pointer to a product-specification in the
 *                      product-class
 * @retval NULL         No more product-specifications in the product-class
 * @return              A pointer to the product-specification in the
 *                      product-class that's just after "prodSpec"
 */
static const EntryProdSpec* epc_nextProdSpec(
        const EntryProdClass* const prodClass,
        const EntryProdSpec* const prodSpec)
{
    EntryProdSpec* const nextProdSpec = (EntryProdSpec*) ((char*) prodSpec
            + prodSpec->size);

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
            epc_nextProdSpec((EntryProdClass*) prodClass, prodSpec))
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
 * @retval ULDB_SYSTEM  System error. log_start() called.
 */
static uldb_Status epc_getEverythingButProdSpecs(
        const EntryProdClass* const epc,
        prod_class** const prodClass)
{
    int status;
    unsigned psa_len = epc_numProdSpecs(epc);
    prod_class* pc = new_prod_class(psa_len);

    if (NULL == pc) {
        LOG_ADD1("Couldn't allocate product-class with %u specifications",
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
 * Vets a data request against a potentially conflicting entry.
 *
 * @param prodClass     [in] The data-request by the downstream LDM
 * @param entry         [in] Pointer to a potentially conflicting entry
 * @retval ULDB_SUCCESS     The data request is allowed
 * @retval ULDB_DISALLOWED  The data request is disallowed. log_start() called.
 */
static entry_vet(
        const uldb_Entry* const entry,
        const prod_class* const prodClass)
{
    int status = ULDB_SUCCESS;
    const prod_spec* prodSpec = prodClass->psa.psa_val;
    unsigned numProdSpecs = prodClass->psa.psa_len;
    int i;

    for (i = 0; i < numProdSpecs; i++) {
        const EntryProdClass* const entryProdClass = &entry->prodClass;
        const EntryProdSpec* entryProdSpec;

        for (entryProdSpec = epc_firstProdSpec(entryProdClass);
                entryProdSpec != NULL ;
                entryProdSpec = epc_nextProdSpec(entryProdClass,
                        entryProdSpec)) {
            if (status = eps_vet(entryProdSpec, prodSpec + i))
                break;
        }

        if (status)
            break;
    }

    return status;
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

/**
 * Returns the product-class of an entry.
 *
 * @param entry     [in] Pointer to the entry to have its product-class returned
 * @param prodClass [out] Address of pointer to returned product-class. The
 *                  client should call free_prod_class(*prodClass) when the
 *                  product-class is no longer needed.
 * @retval ULDB_SUCCESS Success
 * @retval ULDB_SYSTEM  System error. log_start() called.
 */
static uldb_Status entry_getProdClass(
        const uldb_Entry* const entry,
        prod_class** const prodClass)
{
    int status;
    const EntryProdClass* const epc = &entry->prodClass;
    prod_class* pc;

    if (status = epc_getEverythingButProdSpecs(epc, &pc)) {
        LOG_ADD0("Couldn't get most of product-class from entry");
    }
    else {
        const EntryProdSpec* eps;
        prod_spec* ps = pc->psa.psa_val;

        status = ULDB_SUCCESS;

        for (eps = epc_firstProdSpec(epc); NULL != eps;
                eps = epc_nextProdSpec(epc, eps)) {
            prod_spec prodSpec;

            eps_get((EntryProdSpec*) eps, &prodSpec);

            if (cp_prod_spec(ps, &prodSpec)) {
                LOG_SERROR0("Couldn't copy product-specification");
                status = ULDB_SYSTEM;
                break;
            }

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
 * Copies a shared-memory segment.
 *
 * @param segment       [in] Pointer to the shared-memory segment to be copied
 * @param copy          [out] Address of pointer to the copy. Client should call
 *                      "seg_free(*copy)" when the copy is no longer needed.
 * @retval ULDB_SUCCESS Success
 * @retval ULDB_SYSTEM  System error. log_start() called.
 */
static uldb_Status seg_copy(
        const Segment* const segment,
        Segment** const copy)
{
    int status;
    const size_t nbytes = sizeof(Segment) + segment->entriesSize;
    Segment* const clone = (Segment*) malloc(nbytes);

    if (NULL == clone) {
        LOG_SERROR1("Couldn't allocate %lu-byte buffer for copy",
                (unsigned long)nbytes);
        status = ULDB_SYSTEM;
    }
    else {
        (void) memcpy(clone, segment, nbytes);
        *copy = clone;
        status = ULDB_SUCCESS;
    }

    return status;
}

/**
 * Frees a copy of a shared-memory segment.
 *
 * @param copy          [in] Pointer to the copy
 */
static void seg_free(
        Segment* const copy)
{
    free(copy);
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
 * Initializes a shared-memory structure.
 *
 * @param sm    [out] Pointer to the shared-memory structure
 * @param key   [in] The IPC key for the shared-memory segment
 * @retval ULDB_SUCCESS Success
 * @retval ULDB_SYSTEM  System error. log_start() called.
 */
static void sm_init(
        SharedMemory* const sm,
        const key_t key)
{
    sm->key = key;
    sm->segment = NULL;
    sm->shmId = -1;
}

/**
 * Gets the shared-memory segment (i.e., sets the "shmId" member of the
 * shared-memory structure. The shared-memory segment must exist but must not
 * be already gotten.
 *
 * @param sm            [in/out] Pointer to the shared-memory structure
 * @retval ULDB_SUCCESS Success. The "shmId" member of "sm" is set.
 * @retval ULDB_INIT    The shared-memory segment is already gotten. log_start()
 *                      called.
 * @retval ULDB_EXIST   The shared-memory segment corresponding to "sm->key"
 *                      doesn't exist. log_start() called.
 * @retval ULDB_SYSTEM  System error. log_start() called.
 */
static uldb_Status sm_get(
        SharedMemory* const sm)
{
    int status;

    if (0 <= sm->shmId) {
        LOG_START1("The shared-memory segment is already gotten: shmId=%d",
                sm->shmId);
        status = ULDB_INIT;
    }
    else {
        int shmId = shmget(sm->key, 0, read_write);

        if (-1 == shmId) {
            LOG_SERROR0("Couldn't get shared-memory segment");
            status = (ENOENT == errno) ? ULDB_EXIST : ULDB_SYSTEM;
        }
        else {
            sm->shmId = shmId;
            status = ULDB_SUCCESS;
        }
    }

    return status;
}

/**
 * Deletes a shared-memory segment. The shared-memory structure shall have
 * been initialized. The shared-memory segment must exist but must have been
 * already gotten.
 *
 * @param sm            [in] Pointer to the shared-memory structure
 * @retval ULDB_SUCCESS Success
 * @retval ULDB_INIT    The shared-memory segment is already gotten. log_start()
 *                      called.
 * @retval ULDB_EXIST   The shared-memory segment doesn't exist. log_start()
 *                      called.
 * @retval ULDB_SYSTEM  System error. log_start() called.
 */
static uldb_Status sm_delete(
        SharedMemory* const sm)
{
    int status = sm_get(sm);

    if (status) {
        LOG_ADD0("Couldn't get shared-memory segment");
    }
    else if (shmctl(sm->shmId, IPC_RMID, NULL )) {
        struct shmid_ds shmDs;

        LOG_SERROR1("Couldn't delete shared-memory segment %d", sm->shmId);

        if (shmctl(sm->shmId, IPC_STAT, &shmDs)) {
            LOG_ADD1(
                    "Couldn't read data-structure of shared-memory segment: %s",
                    strerror(errno));
        }
        else {
            LOG_ADD3("UID=%d, GID=%d, mode=%#o", shmDs.shm_perm.uid,
                    shmDs.shm_perm.gid, shmDs.shm_perm.mode);
        }

        status = ULDB_SYSTEM;
    }
    else {
        sm->shmId = -1;
        status = ULDB_SUCCESS;
    }

    return ULDB_SUCCESS;
}

/**
 * Deletes a shared-memory segment by IPC key.
 *
 * @param key           The IPC key
 * @retval ULDB_SUCCESS Success
 * @retval ULDB_EXIST   The shared-memory segment doesn't exist. log_start()
 *                      called.
 * @retval ULDB_SYSTEM  System error. log_start() called.
 */
static uldb_Status sm_deleteByKey(
        const key_t key)
{
    int status;
    SharedMemory sm;

    sm_init(&sm, key);

    return sm_delete(&sm);
}

/**
 * Creates a shared-memory segment.
 *
 * @param sm            [in] Pointer to the shared-memory structure
 * @param key           [in] The IPC key for the shared-memory
 * @param size          [in] The initial size, in bytes, of the data portion of
 *                          the shared-memory segment
 * @retval ULDB_SUCCESS Success
 * @retval ULDB_EXIST   The shared-memory segment already exists. log_start()
 *                      called.
 * @retval ULDB_SYSTEM  System error. log_start() called.
 */
static uldb_Status sm_create(
        SharedMemory* const sm,
        const key_t key,
        size_t size)
{
    int status;
    int shmId;
    size_t nbytes = sizeof(Segment) + size; /* includes sentinel */

    shmId = shmget(key, nbytes, IPC_CREAT | IPC_EXCL | read_write);

    if (-1 == shmId) {
        LOG_SERROR1("Couldn't create %u-byte shared-memory segment", nbytes);

        if (EEXIST == errno) {
            struct shmid_ds shmDs;

            shmId = shmget(key, 0, read_only);

            if (-1 == shmId) {
                LOG_ADD1("Couldn't get shared-memory segment: %s",
                        strerror(errno));
            }
            else if (shmctl(shmId, IPC_STAT, &shmDs)) {
                LOG_ADD1("Couldn't read metadata of shared-memory segment: %s",
                        strerror(errno));
            }
            else {
                LOG_ADD3(
                        "Shared-memory segment already exists: size=%lu, pid=%ld, #attach=%lu",
                        (unsigned long)shmDs.shm_segsz, (long)shmDs.shm_cpid,
                        (unsigned long)shmDs.shm_nattch);
            }

            status = ULDB_EXIST;
        }
        else {
            status = ULDB_SYSTEM;
        }
    }
    else {
        sm_init(sm, key);
        status = ULDB_SUCCESS;
    }

    return status;
}

/**
 * Attaches the shared-memory segment. The shared-memory segment is both gotten
 * (i.e., shmget()) and attached (i.e., shmat()). This is done in order to see
 * any expansion of the shared-memory segment by another process. The
 * shared-memory segment must exist but must not already be gotten.
 *
 * @param sm            [in/out] Pointer to the shared-memory structure
 * @retval ULDB_SUCCESS Success
 * @retval ULDB_INIT    The shared-memory segment is already gotten. log_start()
 *                      called.
 * @retval ULDB_SYSTEM  System error. log_start() called.
 */
static uldb_Status sm_attach(
        SharedMemory* const sm)
{
    int status = sm_get(sm);

    if (status) {
        LOG_SERROR0("Couldn't get shared-memory segment");
    }
    else {
        /*
         * Attach the shared-memory segment.
         */
        Segment* segment = (Segment*) shmat(sm->shmId, NULL, 0);

        if ((Segment*) -1 == segment) {
            LOG_SERROR0("Couldn't attach shared-memory segment");
            status = ULDB_SYSTEM;
        }
        else {
            sm->segment = segment;
            status = ULDB_SUCCESS;
        }
    }

    return status;
}

/**
 * Detaches shared-memory.
 *
 * @param sm            [in] Pointer to the shared-memory structure
 * @retval ULDB_SUCCESS Success
 * @retval ULDB_SYSTEM  System error. log_start() called.
 */
static uldb_Status sm_detach(
        SharedMemory* const sm)
{
    if (shmdt(sm->segment)) {
        LOG_SERROR1("Couldn't detach shared-memory segment at address %p",
                sm->segment);
        return ULDB_SYSTEM;
    }

    sm->shmId = -1;

    return ULDB_SUCCESS;
}

/**
 * Returns the size that an entry will have.
 *
 * @param sm            [in] Pointer to the shared-memory structure
 * @param prodClass     [in] Data-request of the downstream LDM
 * @return The size of the corresponding entry in bytes
 */
static size_t sm_getSizeofEntry(
        const SharedMemory* const sm,
        const prod_class* const prodClass)
{
    size_t size = sizeof(uldb_Entry);
    const prod_spec* prodSpec = prodClass->psa.psa_val;
    int i;

    for (i = 0; i < prodClass->psa.psa_len; i++) {
        /*
         * Keep consonant with type "EntryProdSpec" and sm_append().
         */
        size += eps_sizeof(prodSpec[i].pattern);
    }

    return size;
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
 * @retval ULDB_SYSTEM  System error. log_start() called.
 */
static uldb_Status sm_ensureSpaceForEntry(
        SharedMemory* const sm,
        const unsigned size)
{
    int status;
    Segment* const segment = sm->segment;
    size_t newEntriesSize = segment->entriesSize + size;

    if (newEntriesSize > segment->entriesCapacity) {
        size_t newSegmentSize;

        newEntriesSize = (size_t) (PHI * newEntriesSize);
        newSegmentSize = sizeof(Segment)
                + roundUp(newEntriesSize, entryAlignment);
        void* buf = malloc(newSegmentSize);

        if (NULL == buf) {
            LOG_SERROR1("Couldn't allocate %lu-byte buffer",
                    (unsigned long)newSegmentSize);
            status = ULDB_SYSTEM;
        }
        else {
            size_t oldSegmentSize = sizeof(Segment) + segment->entriesCapacity;

            (void) memcpy(buf, segment, oldSegmentSize);

            if (status = sm_detach(sm)) {
                LOG_ADD0("Couldn't detach old shared-memory");
            }
            else if (status = sm_delete(sm)) {
                LOG_ADD0("Couldn't delete old shared-memory");
            }
            else {
                if (status = sm_create(sm, sm->key, newEntriesSize)) {
                    LOG_ADD0("Couldn't create new shared-memory segment");
                }
                else if (status = sm_attach(sm)) {
                    LOG_ADD0( "Couldn't attach new shared-memory segment");
                }
                else {
                    (void) memcpy(sm->segment, buf, oldSegmentSize);
                    status = ULDB_SUCCESS;
                }
            }

            free(buf);
        } /* "buf" allocated */
    } /* extant shared-memory segment is too small */

    return status;
}

/**
 * Unconditionally appends an entry to the shared-memory segment.
 *
 * @param sm            [in/out] Pointer to the shared-memory structure
 * @param pid           [in] PID of the upstream LDM
 * @param protoVers     [in] Protocol version number (e.g., 5 or 6)
 * @param isNotifier    [in] Type of the upstream LDM
 * @param sockAddr      [in] Socket Internet address of the downstream LDM
 * @param prodClass     [in] Data-request of the downstream LDM
 */
static void sm_append(
        SharedMemory* const sm,
        const pid_t pid,
        const int protoVers,
        const int isNotifier,
        const struct sockaddr_in* sockAddr,
        const prod_class* const prodClass)
{
    size_t entrySize = sizeof(uldb_Entry);
    Segment* const segment = sm->segment;
    uldb_Entry* const entry = seg_tailEntry(segment);
    EntryProdClass* const epc = &entry->prodClass;
    EntryProdSpec* eps = epc->prodSpecs;
    const prod_spec* prodSpec = prodClass->psa.psa_val;
    unsigned numProdSpecs = prodClass->psa.psa_len;
    size_t prodSpecsSize = 0;
    unsigned i;

    for (i = 0; i < numProdSpecs; i++) {
        /*
         * Keep consonant with type "EntryProdSpec" and sm_getSizeofEntry().
         */
        const prod_spec* const prodSpec = prodClass->psa.psa_val + i;
        const char* const pattern = prodSpec->pattern;
        size_t nbytes = eps_sizeof(pattern);

        eps->size = nbytes;
        eps->feedtype = prodSpec->feedtype;
        (void) strcpy(eps->pattern, pattern);
        eps = (EntryProdSpec*) epc_nextProdSpec(epc, eps);
        prodSpecsSize += nbytes;
    }

    entrySize += prodSpecsSize;
    entry->size = (unsigned) entrySize;
    (void) memcpy(&entry->sockAddr, sockAddr, sizeof(*sockAddr));
    entry->pid = pid;
    entry->protoVers = protoVers;
    entry->isNotifier = isNotifier;
    entry->prodClass.from = prodClass->from;
    entry->prodClass.to = prodClass->to;
    entry->prodClass.prodSpecsSize = (unsigned) prodSpecsSize;

    segment->entriesSize += entrySize;
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
 * @param sockAddr      [in] Socket Internet address of the downstream LDM
 * @param prodClass     [in] Data-request of the downstream LDM
 * @retval ULDB_SUCCESS     Success
 * @retval ULDB_SYSTEM      System error. log_start() called.
 */
static uldb_Status sm_addUpstreamLdm(
        SharedMemory* const sm,
        const pid_t pid,
        const int protoVers,
        const int isNotifier,
        const struct sockaddr_in* sockAddr,
        const prod_class* const prodClass)
{
    int status;
    size_t size = sm_getSizeofEntry(sm, prodClass);

    if (status = sm_ensureSpaceForEntry(sm, size)) {
        LOG_ADD0("Couldn't ensure sufficient shared-memory");
    }
    else {
        sm_append(sm, pid, protoVers, isNotifier, sockAddr, prodClass);
        status = ULDB_SUCCESS;
    }

    return status;
}

/**
 * Vets an upstream LDM.
 *
 * @param sm            [in/out] Pointer to shared-memory structure
 * @param pid           [in] PID of the upstream LDM process
 * @param protoVers     [in] Protocol version number (e.g., 5 or 6)
 * @param isNotifier    [in] Type of the upstream LDM process
 * @param sockAddr      [in] Socket Internet address of the downstream LDM
 * @param prodClass     [in] The data-request by the downstream LDM
 * @retval ULDB_SUCCESS     Success
 * @retval ULDB_EXIST       Entry for PID already exists. log_start() called.
 * @retval ULDB_DISALLOWED  Entry is disallowed. log_start() called.
 */
static uldb_Status sm_vetUpstreamLdm(
        SharedMemory* const sm,
        const pid_t pid,
        const int protoVers,
        const int isNotifier,
        const struct sockaddr_in* sockAddr,
        const prod_class* const prodClass)
{
    int status = ULDB_SUCCESS;
    const Segment* const segment = sm->segment;
    const uldb_Entry* entry;

    for (entry = seg_firstEntry(segment); entry != NULL ;
            entry = seg_nextEntry(segment, entry)) {
        if (entry->pid == pid) {
            LOG_START1("Entry already exists for PID %ld", pid);
            status = ULDB_EXIST;
            break;
        }

        if (areSocketAddressesEqual(sockAddr, &entry->sockAddr)) {
            if (status = entry_vet(entry, prodClass)) {
                LOG_ADD0("Upstream LDM is disallowed");
                break;
            }
        }
    }

    return status;
}

/**
 * Adds an upstream LDM entry to shared-memory. Increases the amount of
 * shared-memory if necessary.
 *
 * @param sm            [in/out] Pointer to shared-memory structure
 * @param pid           [in] PID of the upstream LDM process
 * @param protoVers     [in] Protocol version number (e.g., 5 or 6)
 * @param isNotifier    [in] Type of the upstream LDM process
 * @param sockAddr      [in] Socket Internet address of the downstream LDM
 * @param prodClass     [in] The data-request by the downstream LDM
 * @retval ULDB_SUCCESS     Success
 * @retval ULDB_EXIST       Entry for PID already exists. log_start() called.
 * @retval ULDB_DISALLOWED  Entry is disallowed. log_start() called.
 * @retval ULDB_SYSTEM      System error. log_start() called.
 */
static uldb_Status sm_add(
        SharedMemory* const sm,
        const pid_t pid,
        const int protoVers,
        const int isNotifier,
        const struct sockaddr_in* sockAddr,
        const prod_class* const prodClass)
{
    int status = sm_vetUpstreamLdm(sm, pid, protoVers, isNotifier, sockAddr,
            prodClass);

    if (status) {
        LOG_ADD1("Disallowed request from %s", inet_ntoa(sockAddr->sin_addr));
    }
    else if (status = sm_addUpstreamLdm(sm, pid, protoVers, isNotifier,
            sockAddr, prodClass)) {
        LOG_ADD1("Couldn't add request from %s", inet_ntoa(sockAddr->sin_addr));
    }

    return status;
}

/**
 * Removes a PID from the shared-memory.
 *
 * @param sm            [in/out] Pointer to the shared-memory structure
 * @param pid           [in] PID to be removed
 * @retval ULDB_SUCCESS Success
 * @retval ULDB_EXIST   No corresponding entry found. log_start() called.
 * @retVal ULDB_SYSTEM  System error. log_start() called.
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
        LOG_START1("Entry for PID %d not found", pid);
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
 * @retval ULDB_INIT    Database is not open. log_start() called.
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
 * @retval ULDB_INIT    The database is not open. log_start() called.
 */
static uldb_Status db_verifyOpen(
        const Database* const db)
{
    if (db_isOpen(db))
        return ULDB_SUCCESS;

    LOG_START0("Database is not open");

    return ULDB_INIT;
}

/**
 * Verifies that a database is closed.
 *
 * @param db            [in] Pointer to the database structure
 * @retval ULDB_SUCCESS Success
 * @retval ULDB_INIT    The database is open. log_start() called.
 */
static uldb_Status db_verifyClosed(
        const Database* const db)
{
    if (!db_isOpen(db))
        return ULDB_SUCCESS;

    LOG_START0("Database is open");

    return ULDB_INIT;
}

/**
 * Prepares a database. Upon successful return, the client should call unlock()
 * when done accessing the database.
 *
 * @param db                [in/out]  Pointer to a database structure
 * @param forWriting        [in] Prepare for writing or reading?
 * @retval ULDB_SUCCESS     Success. Database is locked.
 * @retval ULDB_INIT        Database is not open. log_start() called.
 * @retval ULDB_SYSTEM      System error. log_start() called.
 */
static uldb_Status db_prep(
        Database* const db,
        const int forWriting)
{
    int status = db_verifyOpen(db);

    if (ULDB_SUCCESS == status) {
        if (forWriting) {
            if (status = srwl_writeLock(db->lock))
                LOG_ADD0("Couldn't lock database for writing");
        }
        else {
            if (status = srwl_readLock(db->lock))
                LOG_ADD0("Couldn't lock database for reading");
        }

        if (status) {
            status = ULDB_SYSTEM;
        }
        else {
            if (status = sm_attach(&db->sharedMemory)) {
                LOG_ADD0("Couldn't attach shared-memory");
                (void) srwl_unlock(db->lock);
            }
        } /* lock is locked */
    }

    return status;
}

/**
 * Prepares a database for reading. Client should call unprepare() when done.
 *
 * @param db                [in/out]  Pointer to a database structure
 * @retval ULDB_SUCCESS     Success. Database is locked for reading.
 * @retval ULDB_INIT        Database is not open. log_start() called.
 * @retval ULDB_SYSTEM      System error. log_start() called.
 */
static uldb_Status db_prepForReading(
        Database* const db)
{
    return db_prep(db, 0);
}

/**
 * Prepares a database for writing. Client should call unprepare() when done.
 *
 * @param db                [in/out]  Pointer to a database structure
 * @retval ULDB_SUCCESS     Success. Database is locked for writing.
 * @retval ULDB_INIT        Database is not open. log_start() called.
 * @retval ULDB_SYSTEM      System error. log_start() called.
 */
static uldb_Status db_prepForWriting(
        Database* const db)
{
    return db_prep(db, 1);
}

/**
 * Unprepares a database. If the database couldn't be unprepared, then a message
 * is logged.
 *
 * @param db                [in/out] Pointer to a database structure
 */
static void db_unprep(
        Database* const db)
{
    if (sm_detach(&db->sharedMemory)) {
        LOG_ADD0("Couldn't detach shared-memory");
        log_log(LOG_ERR);
    }
    else if (srwl_unlock(db->lock)) {
        LOG_ADD0("Couldn't unlock database");
        log_log(LOG_ERR);
    }
}

/**
 * Ensures that this module is initialized.
 */
static uldb_Status uldb_ensureModuleInitialized(
        void)
{
    int status;

    if (moduleInitialized) {
        status = ULDB_SUCCESS;
    }
    else {
        prodSpecAlignment = getAlignment(sizeof(EntryProdSpec));

        if (0 == prodSpecAlignment) {
            LOG_ADD0(
                    "Couldn't determine alignment of product-specification structure");
            status = ULDB_SYSTEM;
        }
        else {
            entryAlignment = getAlignment(sizeof(uldb_Entry));

            if (0 == entryAlignment) {
                LOG_ADD0("Couldn't determine alignment of entry structure");
                status = ULDB_SYSTEM;
            }
            else {
                mode_t um = umask(0);

                umask(um);

                read_only = 0444 & ~um;
                read_write = 0666 & ~um;
                moduleInitialized = 1;
                status = ULDB_SUCCESS;
            }
        }
    }

    return status;
}

/**
 * Returns the IPC key.
 *
 * @param key
 * @retval ULDB_SUCCESS Success
 * @retval ULDB_SYSTEM  System error. log_start() called.
 */
static uldb_Status uldb_getKey(
        key_t* const key)
{
    int status;
    key_t k = ftok(KEY_PATH, KEY_INDEX);

    if ((key_t) -1 == k) {
        LOG_SERROR2("Couldn't get IPC key for path \"%s\", index %d", KEY_PATH,
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
 * Creates the database.
 *
 * @param capacity          [in] Initial capacity of the database in bytes
 * @retval ULDB_SUCCESS     Success.
 * @retval ULDB_INIT        Database already open. log_start() called.
 * @retval ULDB_EXIST       Database already exists. log_start() called.
 * @retval ULDB_SYSTEM      System error. log_start() called.
 */
uldb_Status uldb_create(
        unsigned capacity)
{
    int status = uldb_ensureModuleInitialized();

    if (status) {
        LOG_ADD0("Couldn't initialize module");
    }
    else {
        if (status = db_verifyClosed(&database)) {
            LOG_START0("Database already open");
        }
        else {
            key_t key;

            if (status = uldb_getKey(&key)) {
                LOG_SERROR0("Couldn't get IPC key");
            }
            else if (status = sm_create(&database.sharedMemory, key,
                    capacity)) {
                LOG_ADD0("Couldn't create shared-memory component");
            }
            else if (srwl_create(key, &database.lock)) {
                LOG_ADD0("Couldn't create lock component");
                (void) sm_delete(&database.sharedMemory);
                status = ULDB_SYSTEM;
            }
            else {
                database.validString = VALID_STRING;
            }
        } /* database not open */
    }

    return status;
}

/**
 * Opens the existing database.
 *
 * @retval ULDB_SUCCESS  Success.
 * @retval ULDB_INIT     Already initialized. log_start() called.
 * @retval ULDB_EXIST    The database doesn't exist. log_start() called.
 * @retval ULDB_SYSTEM   System error. log_start() called.
 */
uldb_Status uldb_open(
        void)
{
    int status = uldb_ensureModuleInitialized();

    if (status) {
        LOG_ADD0("Couldn't initialize module");
    }
    else {
        if (status = db_verifyClosed(&database)) {
            LOG_ADD0("Database already open");
        }
        else {
            key_t key;

            if (status = uldb_getKey(&key)) {
                LOG_SERROR0("Couldn't get IPC key");
            }
            else {
                sm_init(&database.sharedMemory, key);

                if (srwl_get(key, &database.lock)) {
                    LOG_ADD0("Couldn't get existing read/write lock");
                    status = ULDB_SYSTEM;
                }
                else {
                    database.validString = VALID_STRING;
                }
            }
        } /* database not open */
    }

    return status;
}

/**
 * Closes the database, freeing any system resources. Upon successful return,
 * the database still exists but may not be accessed until uldb_open() is
 * called.
 *
 * @retval ULDB_SUCCESS  Success.
 * @retval ULDB_INIT     Database not open. log_start() called.
 * @retval ULDB_SYSTEM   System error. loc_start() called. Resulting module
 *                       state is unspecified.
 */
uldb_Status uldb_close(
        void)
{
    int status = uldb_ensureModuleInitialized();

    if (status) {
        LOG_ADD0("Couldn't initialize module");
    }
    else {
        int status = db_verifyOpen(&database);
        if (status) {
            LOG_ADD0("Database is not open");
        }
        else if (srwl_free(database.lock)) {
            LOG_ADD0("Couldn't free lock component");
            status = ULDB_SYSTEM;
        }
        else {
            database.lock = NULL;
            database.validString = NULL;
        }
    }

    return status;
}

/**
 * Unconditionally deletes the database.
 *
 * @retval ULDB_SUCCESS  Success
 * @retval ULDB_EXIST    The database didn't exist. log_start() called.
 * @retval ULDB_SYSTEM   System error. log_start() called. Resulting module
 *                       state is unspecified.
 */
uldb_Status uldb_delete(
        void)
{
    int status = uldb_ensureModuleInitialized();

    if (status) {
        LOG_ADD0("Couldn't initialize module");
    }
    else {
        key_t key;

        status = uldb_getKey(&key);

        if (status) {
            LOG_ADD0("Couldn't get IPC key for database");
        }
        else {
            status = sm_deleteByKey(key);

            if (status && ULDB_EXIST != status) {
                LOG_ADD0(
                        "Couldn't delete existing shared-memory database by IPC key");
            }
            else {
                int lockStatus = srwl_deleteByKey(key);

                if (status)
                    LOG_ADD0("Shared-memory database doesn't exist");

                if (lockStatus) {
                    if (RWL_EXIST != lockStatus) {
                        LOG_ADD0(
                                "Couldn't delete existing semaphore-based read/write lock by IPC key");

                        status = ULDB_SYSTEM;
                    }
                    else {
                        LOG_ADD0(
                                "Semaphore-based read/write lock doesn't exist");

                        if (ULDB_SUCCESS == status)
                            status = ULDB_EXIST;
                    }
                }
            }
        }
    }

    database.validString = NULL;

    return status;
}

/**
 * Returns the number of entries in the database.
 *
 * @param size          [out] Pointer to the size of the database
 * @retval ULDB_SUCCESS Success
 * @retval ULDB_INIT    The database is closed. log_start() called.
 * @retval ULDB_SYSTEM  System error. log_start() called.
 */
uldb_Status uldb_getSize(
        unsigned* const size)
{
    int status = uldb_ensureModuleInitialized();

    if (status) {
        LOG_ADD0("Couldn't initialize module");
    }
    else {
        status = db_verifyOpen(&database);

        if (status) {
            LOG_ADD0("Database not open");
        }
        else if (status = db_prepForReading(&database)) {
            LOG_ADD0("Couldn't prepare database for reading");
        }
        else {
            *size = sm_getSize(&database.sharedMemory);

            db_unprep(&database);
        }
    }

    return status;
}

/**
 * Adds to the database.
 *
 * @param pid           [in] PID of upstream LDM process
 * @param protoVers     [in] Protocol version number (e.g., 5 or 6)
 * @param isNotifier    [in] The type of the upstream LDM: notifier or feeder
 * @param sockAddr      [in] Socket Internet address of downstream LDM
 * @param prodClass     [in] The data-request by the downstream LDM
 * @retval ULDB_SUCCESS     Success.
 * @retval ULDB_INIT        Module not initialized. log_start() called.
 * @retval ULDB_ARG         Invalid PID. log_start() called.
 * @retval ULDB_EXIST       Entry for PID already exists. log_start() called.
 * @retval ULDB_DISALLOWED  The request is disallowed by policy. Database not
 *                          modified. log_start() called.
 * @retval ULDB_SYSTEM      System error. log_start() called.
 */
static uldb_Status uldb_add(
        const pid_t pid,
        const int protoVers,
        const int isNotifier,
        const struct sockaddr_in* const sockAddr,
        const prod_class* const prodClass)
{
    int status = uldb_ensureModuleInitialized();

    if (status) {
        LOG_ADD0("Couldn't initialize module");
    }
    else {
        status = db_prepForWriting(&database);

        if (ULDB_SUCCESS == status) {
            if (pid <= 0) {
                LOG_START1("Invalid PID: %ld", (long)pid);
                status = ULDB_ARG;
            }
            else {
                if (status = sm_add(&database.sharedMemory, pid, protoVers,
                        isNotifier, sockAddr, prodClass)) {
                    LOG_ADD0("Couldn't add to database");
                }
            }

            db_unprep(&database);
        }
    }

    return status;
}

/**
 * Adds an upstream LDM feeder to the database.
 *
 * @param pid           [in] PID of upstream LDM process
 * @param protoVers     [in] Protocol version number (e.g., 5 or 6)
 * @param sockAddr      [in] Socket Internet address of downstream LDM
 * @param prodClass     [in] The data-request by the downstream LDM
 * @retval ULDB_SUCCESS     Success.
 * @retval ULDB_INIT        Module not initialized. log_start() called.
 * @retval ULDB_ARG         Invalid PID. log_start() called.
 * @retval ULDB_EXIST       Entry for PID already exists. log_start() called.
 * @retval ULDB_DISALLOWED  The request is disallowed by policy. Database not
 *                          modified. log_start() called.
 * @retval ULDB_SYSTEM      System error. log_start() called.
 */
uldb_Status uldb_addFeeder(
        const pid_t pid,
        const int protoVers,
        const struct sockaddr_in* const sockAddr,
        const prod_class* const prodClass)
{
    return uldb_add(pid, protoVers, 0, sockAddr, prodClass);
}

/**
 * Adds an upstream LDM notifier to the database.
 *
 * @param pid           [in] PID of upstream LDM process
 * @param protoVers     [in] Protocol version number (e.g., 5 or 6)
 * @param sockAddr      [in] Socket Internet address of downstream LDM
 * @param prodClass     [in] The data-request by the downstream LDM
 * @retval ULDB_SUCCESS     Success.
 * @retval ULDB_INIT        Module not initialized. log_start() called.
 * @retval ULDB_ARG         Invalid PID. log_start() called.
 * @retval ULDB_EXIST       Entry for PID already exists. log_start() called.
 * @retval ULDB_DISALLOWED  The request is disallowed by policy. Database not
 *                          modified. log_start() called.
 * @retval ULDB_SYSTEM      System error. log_start() called.
 */
uldb_Status uldb_addNotifier(
        const pid_t pid,
        const int protoVers,
        const struct sockaddr_in* const sockAddr,
        const prod_class* const prodClass)
{
    return uldb_add(pid, protoVers, 1, sockAddr, prodClass);
}

/**
 * Removes an entry.
 *
 * @param pid                [in] PID of upstream LDM process
 * @retval ULDB_SUCCESS      Success. Corresponding entry found and removed.
 * @retval ULDB_INIT         Module not initialized. log_start() called.
 * @retval ULDB_ARG          Invalid PID. log_start() called.
 * @retval ULDB_EXIST        No corresponding entry found. log_start() called.
 * @retval ULDB_SYSTEM       System error. See "errno". log_start() called.
 */
uldb_Status uldb_remove(
        const pid_t pid)
{
    int status = uldb_ensureModuleInitialized();

    if (status) {
        LOG_ADD0("Couldn't initialize module");
    }
    else {
        status = db_prepForWriting(&database);

        if (ULDB_SUCCESS == status) {
            if (pid <= 0) {
                LOG_START1("Invalid PID: %ld", (long)pid);
                status = ULDB_ARG;
            }
            else {
                if (status = sm_remove(&database.sharedMemory, pid)) {
                    LOG_ADD0("Couldn't remove process from database");
                }
            }

            db_unprep(&database);
        }
    }

    return status;
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
 * @retval ULDB_INIT    The database is not open. log_start() called.
 * @retval ULDB_SYSTEM  System error. log_start() called.
 *
 */
uldb_Status uldb_getIterator(
        uldb_Iter** const iterator)
{
    int status = uldb_ensureModuleInitialized();

    if (status) {
        LOG_ADD0("Couldn't initialize module");
    }
    else {
        size_t nbytes = sizeof(uldb_Iter);
        uldb_Iter* iter = (uldb_Iter*) malloc(nbytes);

        if (NULL == iter) {
            LOG_SERROR1("Couldn't allocate %lu-bytes for iterator", nbytes);
            status = ULDB_SYSTEM;
        }
        else {
            status = db_prepForReading(&database);

            if (ULDB_SUCCESS == status) {
                if (status = seg_copy(database.sharedMemory.segment,
                        &iter->segment)) {
                    LOG_ADD0("Couldn't copy database");
                    free(iter);
                }
                else {
                    iter->entry = NULL;
                    *iterator = iter;
                }

                db_unprep(&database);
            }
        }
    }

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
 * @retval ULDB_SYSTEM  System error. log_start() called.
 */
uldb_Status uldb_entry_getProdClass(
        const uldb_Entry* const entry,
        prod_class** const prodClass)
{
    return entry_getProdClass(entry, prodClass);
}
