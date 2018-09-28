/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: mldm_receiver_memory.c
 * @author: Steven R. Emmerson
 *
 * This file implements the persistent, session-to-session memory for the
 * receiving side of a multicast-capable LDM.
 */

#include "config.h"

#include "fmtp.h"
#include "globals.h"
#include "inetutil.h"
#include "ldmprint.h"
#include "log.h"
#include "mldm_receiver_memory.h"
#include "prod_index_queue.h"

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <yaml.h>

/**
 * The data structure of a multicast receiver memory:
 */
struct McastReceiverMemory {
    /**
     * Pointer to magic object indicating a valid structure.
     */
    const void*      magic;
    /**
     * Path of the canonical multicast-receiver memory-file:
     */
    char*            path;
    /**
     * Path of the temporary multicast-receiver memory-file:
     */
    char*            tmpPath;
    /**
     * Signature of the last data-product received via multicast:
     */
    signaturet       lastMcastProd;
    /**
     * Whether or not `lastMcastProd` is set:
     */
    bool             sigSet;
    /**
     * Queue of missed-but-not-yet-requested data-products:
     */
    ProdIndexQueue*  missedQ;
    /**
     * Queue of requested-but-not-yet-received data-products:
     */
    ProdIndexQueue*  requestedQ;
    /**
     * Whether or not the multicast receiver memory has been modified by the
     * user.
     */
    bool             modified;
    /**
     * Concurrent access mutex.
     */
    pthread_mutex_t  mutex;
};

/**
 * Magic object to point at indicating a valid multicast receiver memory object.
 */
static const char MAGIC;
/**
 * The key for the YAML mapping to the signature of the last data-product
 * received by the multicast receiver:
 */
static const char* const LAST_MCAST_PROD_KEY = "Last Multicast Product Signature";
/**
 * The key for the YAML mapping to the list of identifiers of files that were
 * missed by the multicast receiver:
 */
static const char* const MISSED_MCAST_FILES_KEY = "Missed Multicast File Identifiers";

/**
 * Vets a multicast receiver memory object.
 *
 * @param[in] mrm    The object to be vetted.
 *
 */
static void
vetMrm(
        const McastReceiverMemory* const mrm)
{
    log_assert(mrm != NULL && mrm->magic == &MAGIC);
}

/**
 * Returns the pathname of the memory-file corresponding to a server and a
 * multicast group. This function is reentrant.
 *
 * @param[in] servAddr  The address of the server associated with the multicast
 *                      group.
 * @param[in] feedtype  Feedtype of multicast group.
 * @retval    NULL      Failure. `log_add()` called.
 * @return              The path of the corresponding memory-file. The caller
 *                      should free when it's no longer needed.
 */
static char*
getSessionPath(
    const ServiceAddr* const servAddr,
    const feedtypet          feedtype)
{
    char* path;
    char  ftBuf[256];

    if (sprint_feedtypet(ftBuf, sizeof(ftBuf), feedtype) < 0) {
        log_add("sprint_feedtypet() failure");
        path = NULL;
    }
    else {
        path = ldm_format(256, "%s/%s_%s.yaml", getLdmLogDir(),
                servAddr->inetId, ftBuf);
    }

    return path;
}

/**
 * Returns the path of the temporary memory-file corresponding to the path of a
 * canonical memory-file. This function is thread-safe.
 *
 * @param[in] path  The path of a canonical memory-file. The caller may free
 *                  when it's no longer needed.
 * @retval    NULL  Failure. `log_add()` called.
 * @return          The path of the corresponding temporary memory-file. The
 *                  caller should free when it's no longer needed.
 */
static char*
makeTempPath(
    char* const restrict path)
{
    char* const tmpPath = ldm_format(256, "%s%s", path, ".new");

    if (tmpPath == NULL)
        log_add("Couldn't create path of temporary memory-file");

    return tmpPath;
}

static inline const char*
yamlParserErrMsg(
    const yaml_parser_t* const parser)
{
    return parser->problem;
}

static inline unsigned long
yamlParserLine(
    const yaml_parser_t* const parser)
{
    return parser->problem_mark.line;
}

static inline unsigned long
yamlParserColumn(
    const yaml_parser_t* const parser)
{
    return parser->problem_mark.column;
}

/**
 * Returns the value-node of a document that corresponds to the value associated
 * with a mapping key. This function is reentrant.
 *
 * @param[in] document  The document.
 * @param[in] start     The first mapping pair.
 * @param[in] end       The last mapping pair.
 * @param[in] keyStr    The key whose value-node is to be returned.
 * @retval    NULL      The value-node doesn't exist.
 * @return              The value-node associated with the given key.
 */
static yaml_node_t*
getValueNode(
    yaml_document_t* const restrict    document,
    yaml_node_pair_t* const            start,
    yaml_node_pair_t* const            end,
    const char* const restrict         keyStr)
{
    /*
     * ASSUMPTIONS:
     *   1) `start` is NULL for an empty mapping;
     *   2) `end` points to the last, valid entry.
     */
    for (yaml_node_pair_t* pair = start; pair && pair <= end; pair++) {
        yaml_node_t* const keyNode = yaml_document_get_node(document,
                pair->key);

        if (keyNode != NULL && keyNode->type == YAML_SCALAR_NODE &&
                strcmp(keyNode->data.scalar.value, keyStr) == 0)
            return yaml_document_get_node(document, pair->value);
    }

    return NULL;
}

static void
lock(
    McastReceiverMemory* const mrm)
{
    int status = pthread_mutex_lock(&mrm->mutex);

    if (status) {
        log_errno_q(status, "Couldn't lock mutex");
    }
}

static void
unlock(
    McastReceiverMemory* const mrm)
{
    int status = pthread_mutex_unlock(&mrm->mutex);

    if (status) {
        log_errno_q(status, "Couldn't unlock mutex");
    }
}

/**
 * Initializes the last, multicast data-product signature in a multicast
 * receiver memory from a YAML mapping-node. This function is reentrant.
 *
 * @param[in] mrm       The multicast receiver memory to initialize.
 * @param[in] document  The YAML document to use.
 * @param[in] start     The first node-pair of the mapping.
 * @param[in] end       The last node-pair of the mapping.
 * @retval    true      Success or the parameter doesn't exist.
 * @retval    false     Error. `log_add()` called.
 */
static bool
initLastMcastProd(
    McastReceiverMemory* const restrict mrm,
    yaml_document_t* const restrict     document,
    yaml_node_pair_t* const restrict    start,
    yaml_node_pair_t* const restrict    end)
{
    yaml_node_t* const valueNode = getValueNode(document, start, end,
            LAST_MCAST_PROD_KEY);

    if (valueNode == NULL)
        return true;

    if (valueNode->type != YAML_SCALAR_NODE) {
        log_add("Unexpected node-type for value associated with key \"%s\"",
                LAST_MCAST_PROD_KEY);
        return false;
    }

    const char* sigStr = valueNode->data.scalar.value;

    if (sigParse(sigStr, &mrm->lastMcastProd) == -1) {
        log_add("Unable to parse last multicast data-product signature \"%s\"",
                sigStr);
        return false;
    }

    mrm->sigSet = true;

    return true;
}

/**
 * Initializes from a YAML sequence a multicast receiver memory's list of files
 * that were missed by the multicast receiver during the previous session.
 *
 * @param[in] mrm       The multicast receiver memory to initialize.
 * @param[in] document  The YAML document to use.
 * @param[in] start     The first item of the sequence.
 * @param[in] end       The last item of the sequence.
 * @retval    true      Success.
 * @retval    false     Error. `log_add()` called.
 */
static bool
initMissedFilesFromSequence(
    McastReceiverMemory* const restrict    mrm,
    yaml_document_t* const restrict        document,
    const yaml_node_item_t* const restrict start,
    const yaml_node_item_t* const restrict end)
{
    for (const yaml_node_item_t* item = start; item && item <= end; item++) {
        yaml_node_t* itemNode = yaml_document_get_node(document, *item);

        if (itemNode == NULL) {
            log_add("yaml_document_get_node() failure");
            return false;
        }
        if (itemNode->type != YAML_SCALAR_NODE) {
            log_add("Unexpected node-type for missed-file item");
            return false;
        }
        unsigned long fileId;
        int           nbytes;
        if (sscanf(itemNode->data.scalar.value, "%80lu %n", &fileId, &nbytes)
                != 1 || itemNode->data.scalar.value[nbytes] != 0) {
            log_add_syserr("Couldn't decode missed-file identifier \"%s\"",
                    itemNode->data.scalar.value);
            return false;
        }
        if (piq_add(mrm->missedQ, fileId) != 0)
            return false;
    }

    return true;
}

/**
 * Initializes a multicast receiver memory's list of files that were missed by
 * the multicast receiver during the previous session from a YAML mapping-node.
 *
 * @param[in] mrm       The multicast receiver memory to initialize.
 * @param[in] document  The YAML document to use.
 * @param[in] start     The first node-pair of the mapping.
 * @param[in] end       The last node-pair of the mapping.
 * @retval    true      Success or the information doesn't exist.
 * @retval    false     Error. `log_add()` called.
 */
static bool
initMissedFiles(
    McastReceiverMemory* const restrict mrm,
    yaml_document_t* const restrict     document,
    yaml_node_pair_t* const restrict    start,
    yaml_node_pair_t* const restrict    end)
{
    yaml_node_t* const valueNode = getValueNode(document, start, end,
            MISSED_MCAST_FILES_KEY);

    if (valueNode == NULL)
        return true;

    if (valueNode->type != YAML_SEQUENCE_NODE) {
        log_add("Unexpected node-type for value associated with key \"%s\"",
                MISSED_MCAST_FILES_KEY);
        return false;
    }

    return initMissedFilesFromSequence(mrm, document,
            valueNode->data.sequence.items.start,
            valueNode->data.sequence.items.end);
}

/**
 * Initializes a multicast receiver memory from a YAML node. This function is
 * reentrant.
 *
 * @param[in] mrm       The multicast receiver memory to initialize.
 * @param[in] document  The YAML document to use.
 * @param[in] node      The YAML node to use.
 * @retval    true      Success.
 * @retval    false     Failure. `log_add()` called.
 */
static bool
initFromNode(
    McastReceiverMemory* const restrict mrm,
    yaml_document_t* const restrict     document,
    yaml_node_t* const restrict         node)
{
    if (node->type != YAML_MAPPING_NODE) {
        log_add("Unexpected YAML node: %d", node->type);
        return false;
    }

    yaml_node_pair_t* const start = node->data.mapping.pairs.start;
    yaml_node_pair_t* const end = node->data.mapping.pairs.end;

    return initLastMcastProd(mrm, document, start, end) &&
            initMissedFiles(mrm, document, start, end);
}

/**
 * Initializes a multicast receiver memory from a YAML document. This function
 * is reentrant.
 *
 * @param[in] mrm       The multicast receiver memory to initialize.
 * @param[in] document  The YAML document to use.
 * @retval    true      Success.
 * @retval    false     Failure. `log_add()` called.
 */
static bool
initFromDocument(
    McastReceiverMemory* const restrict mrm,
    yaml_document_t* const restrict     document)
{
    yaml_node_t* rootNode = yaml_document_get_root_node(document);

    if (rootNode == NULL) {
        log_add("YAML document is empty");
        return false;
    }

    return initFromNode(mrm, document, rootNode);
}

/**
 * Initializes a multicast receiver memory from a YAML stream. Only the first
 * document is used. This function is reentrant.
 *
 * @param[in] mrm     The multicast receiver memory to be initialized.
 * @param[in] parser  The YAML parser.
 * @retval    true    Success.
 * @retval    false   Error. `log_add()` called.
 */
static bool
initFromStream(
    McastReceiverMemory* const restrict mrm,
    yaml_parser_t* const restrict       parser)
{
    yaml_document_t document;

    (void)memset(&document, 0, sizeof(document));

    if (!yaml_parser_load(parser, &document)) {
        log_add("YAML parser failure at line=%lu, column=%lu: %s:",
                yamlParserLine(parser), yamlParserColumn(parser),
                yamlParserErrMsg(parser));
        return false;
    }

    bool success = initFromDocument(mrm, &document);

    yaml_document_delete(&document);

    return success;
}

/**
 * Initializes a multicast receiver memory from a YAML file. This function is
 * reentrant.
 *
 * @param[in] mrm   The multicast receiver memory to initialize.
 * @param[in] file  The YAML file to parse.
 * @retval    0     Success.
 * @retval    1     System error. `log_add()` called.
 * @retval    2     Parse error. `log_add()` called.
 */
static int
initFromYamlFile(
    McastReceiverMemory* const restrict mrm,
    FILE* const restrict                file)
{
    int           status;
    yaml_parser_t parser;

    if (!yaml_parser_initialize(&parser)) {
        log_add_syserr("Couldn't initialize YAML parser");
        status = 1;
    }
    else {
        yaml_parser_set_input_file(&parser, file);

        if (!initFromStream(mrm, &parser)) {
            log_add("Error parsing memory-file. Delete or correct it.");
            status = 2;
        }
        else {
            status = 0;
        }

        yaml_parser_delete(&parser);
    }

    return status;
}

/**
 * Initializes a multicast receiver memory from a memory-file. This function is
 * reentrant.
 *
 * @param[in] mrm           The multicast receiver memory to initialize.
 * @param[in] path          The path of the memory-file. Caller must not modify
 *                          or free.
 * @retval    0             Success.
 * @retval    1             System error. `log_add()` called.
 * @retval    2             Memory-file doesn't exist.
 */
static int
initFromFile(
    McastReceiverMemory* const restrict mrm,
    char* const restrict                path)
{
    int   status;
    FILE* file = fopen(path, "r");


    if (file == NULL) {
        if (errno == ENOENT) {
            status = 2;
        }
        else {
            log_add_syserr("Couldn't open memory-file \"%s\"", path);
            status = 1;
        }
    }
    else {
        status = initFromYamlFile(mrm, file);

        if (status)
            log_add("Couldn't initialize multicast-memory from file \"%s\"",
                    path);

        (void)fclose(file); // don't care because open for reading only
    } // `file` open

    return status;
}

/**
 * Initializes the mutex of a multicast receiver memory.
 *
 * @param[in] mrm    The multicast receiver memory.
 * @retval    true   Success.
 * @retval    false  Failure. `log_add()` called.
 */
static bool
initMutex(
    McastReceiverMemory* const mrm)
{
    pthread_mutexattr_t mutexAttr;
    int                 status = pthread_mutexattr_init(&mutexAttr);

    if (status) {
        log_errno_q(status, "Couldn't initialize mutex attributes");
    }
    else {
        // At most one lock per thread.
        (void)pthread_mutexattr_settype(&mutexAttr, PTHREAD_MUTEX_ERRORCHECK);
        // Prevent priority inversion
        (void)pthread_mutexattr_setprotocol(&mutexAttr, PTHREAD_PRIO_INHERIT);

        status = pthread_mutex_init(&mrm->mutex, &mutexAttr);

        if (status)
            log_errno_q(status, "Couldn't initialize mutex");

        (void)pthread_mutexattr_destroy(&mutexAttr);
    } // `mutexAttr` initialized

    return 0 == status;
}

/**
 * Initializes a multicast receiver memory from scratch.
 *
 * @param[in] mrm    The multicast receiver memory to initialize.
 * @param[in] path   The path of the canonical memory-file. Caller must not
 *                   modify or free.
 * @param[in] path   The path of the temporary memory-file. Caller must not
 *                   modify or free.
 * @retval    true   Success.
 * @retval    false  Failure. `log_add()` called.
 */
static bool
initFromScratch(
    McastReceiverMemory* const restrict mrm,
    char* const restrict                path)
{
    bool        success = false;
    char* const tmpPath = makeTempPath(path);

    if (tmpPath != NULL) {
        if ((mrm->missedQ = piq_new()) == NULL) {
            log_add("Couldn't create queue of missed data-products");
        }
        else {
            if ((mrm->requestedQ = piq_new()) == NULL) {
                log_add("Couldn't create queue of requested data-products");
            }
            else {
                if (initMutex(mrm)) {
                    mrm->path = path;
                    mrm->tmpPath = tmpPath;
                    mrm->sigSet = false;
                    mrm->modified = false;
                    success = true;
                }
                else {
                    piq_free(mrm->requestedQ);
                }
            } // `mrm->requestedQ` allocated

            if (!success)
                piq_free(mrm->missedQ);
        } // `mrm->missedQ` allocated

        if (!success)
            free(tmpPath);
    } // `tmpPath` allocated

    return success;
}

/**
 * Initializes a multicast receiver memory from a pre-existing memory-file or
 * from scratch if the memory-file doesn't exist. This function is reentrant.
 *
 * @param[in] mrm       The multicast receiver memory to initialize.
 * @param[in] path      The path of the canonical memory-file. Caller must not
 *                      modify or free.
 * @retval    true      Success.
 * @retval    false     Failure. `log_add()` called.
 */
static bool
initFromScratchOrFile(
    McastReceiverMemory* const restrict mrm,
    char* const restrict                path)
{
    if (!initFromScratch(mrm, path))
        return false;

    int status = initFromFile(mrm, path);

    return (status == 0) || (status == 2); // success or file doesn't exist
}

/**
 * Initializes a multicast receiver memory. This function is reentrant.
 *
 * @param[in] mrm       The muticast receiver memory to initialize.
 * @param[in] servAddr  Address of the server.
 * @param[in] feedtype  Feedtype of the multicast group.
 * @retval    true      Success.
 * @retval    false     Failure. `log_add()` called.
 */
static bool
init(
    McastReceiverMemory* const restrict mrm,
    const ServiceAddr* const restrict   servAddr,
    const feedtypet                     feedtype)
{
    bool        success;
    char* const path = getSessionPath(servAddr, feedtype);

    if (path == NULL) {
        success = false;
    }
    else {
        success = initFromScratchOrFile(mrm, path);
        if (!success) {
            free(path);
        }
        else {
            mrm->magic = &MAGIC;
        }
    } // `path` allocated

    return success;
}

/**
 * Opens a memory-file of a multicast receiver memory.
 *
 * @param[in] mrm    The multicast receiver memory.
 * @retval    NULL   Failure. `log_add()` called.
 * @return           A memory-file open for writing.
 */
static FILE*
openTempMemoryFile(
    McastReceiverMemory* const mrm)
{
    FILE* file = fopen(mrm->tmpPath, "w");

    if (file == NULL)
        log_add_syserr("Couldn't open temporary memory-file \"%s\"", mrm->tmpPath);

    return file;
}

/**
 * Appends to a YAML sequence-node the list of files contained in a file-
 * identifier queue.
 *
 * @param[in] document  The YAML document.
 * @param[in] seq       The identifier of the YAML sequence-node.
 * @param[in] fiq       The product-index queue.
 * @retval    true      Success.
 * @retval    false     Failure. `log_add()` called.
 */
static bool
appendFileIds(
    yaml_document_t* const restrict document,
    const int                       seq,
    ProdIndexQueue* const restrict     fiq)
{
    FmtpProdIndex iProd;

    while (piq_removeNoWait(fiq, &iProd) == 0) {
        char          buf[sizeof(iProd)*4+1]; // overly capacious

        (void)snprintf(buf, sizeof(buf), "%lu", (unsigned long)iProd);

        int  scalarNode = yaml_document_add_scalar(document, NULL, buf, -1,
                YAML_PLAIN_SCALAR_STYLE);

        if (!scalarNode) {
            log_add("yaml_document_add_scalar() failure");
            return false;
        }
        else {
            if (!yaml_document_append_sequence_item(document, seq, scalarNode)) {
                log_add("yaml_document_append_sequence_item() failure");
                return false;
            }
        }
    }
    
    return true;
}

/**
 * Appends to a YAML sequence-node the current list of files that were missed by
 * the multicast receiver associated with a multicast receiver memory. Both the
 * "missed" and "requested" queues are used.
 *
 * @param[in] mrm       The multicast receiver memory.
 * @param[in] document  The YAML document.
 * @param[in] seq       The identifier of the YAML sequence-node.
 * @retval    true      Success.
 * @retval    false     Failure. `log_add()` called.
 */
static bool
addMissedFiles(
    const McastReceiverMemory* const restrict mrm,
    yaml_document_t* const restrict           document,
    const int                                 seq)
{
    return appendFileIds(document, seq, mrm->requestedQ) &&
            appendFileIds(document, seq, mrm->missedQ);
}

/**
 * Returns the node-identifier of a YAML sequence of files that were missed by
 * the multicast receiver according to a multicast receiver memory.
 *
 * @param[in] mrm       The multicast receiver memory.
 * @param[in] document  The YAML document.
 * @retval    0         Failure. `log_add()` called.
 * @return              The identifier of the YAML sequence-node.
 */
static int
getMissedFileSequence(
    const McastReceiverMemory* const restrict mrm,
    yaml_document_t* const restrict           document)
{
    int seq = yaml_document_add_sequence(document, NULL,
            YAML_FLOW_SEQUENCE_STYLE);

    if (!seq) {
        log_add("yaml_document_add_sequence() failure");
    }
    else {
        if (!addMissedFiles(mrm, document, seq))
            seq = 0;
    }

    return seq;
}

/**
 * Adds to a map-node of a YAML document the list of files that were missed
 * by the multicast receiver associated with a multicast receiver memory.
 *
 * @param[in] mrm       The multicast receiver memory.
 * @param[in] document  The YAML document.
 * @param[in] map       The identifier of the YAML map-node.
 * @retval    true      Success.
 * @retval    false     Failure. `log_add()` called.
 */
static bool
addMissedMcastFiles(
    const McastReceiverMemory* const restrict mrm,
    yaml_document_t* const restrict           document,
    const int                                 map)
{
    bool success = false;
    int  seq = getMissedFileSequence(mrm, document);

    if (seq) {
        // ASSUMPTION: The 3rd argument isn't modified
        int  key = yaml_document_add_scalar(document, NULL,
                (char*)MISSED_MCAST_FILES_KEY, -1, YAML_PLAIN_SCALAR_STYLE);

        if (key == 0) {
            log_add("yaml_document_add_scalar() failure");
        }
        else {
            if (!yaml_document_append_mapping_pair(document, map, key, seq)) {
                log_add("yaml_document_append_mapping_pair() failure");
            }
            else {
                success = true;
            }
        }
    }

    return success;
}

/**
 * Appends a mapping from a string to a string to a YAML mapping.
 *
 * @param[in] document  The YAML document.
 * @param[in] map       The YAML mapping in the document to which to append.
 * @param[in] keyStr    The key string.
 * @param[in] valueStr  The value string.
 * @retval    true      Success.
 * @retval    false     Failure. `log_add()` called.
 */
static bool
appendStringMapping(
    yaml_document_t* const restrict document,
    const int                       map,
    const char* const restrict      keyStr,
    const char* const restrict      valueStr)
{
    bool success = false;
    // ASSUMPTION: The 3rd argument isn't modified
    int  key = yaml_document_add_scalar(document, NULL, (char*)keyStr, -1,
            YAML_PLAIN_SCALAR_STYLE);

    if (key == 0) {
        log_add("yaml_document_add_scalar() failure");
    }
    else {
        // ASSUMPTION: The 3rd argument isn't modified
        int value = yaml_document_add_scalar(document, NULL, (char*)valueStr, -1,
                YAML_PLAIN_SCALAR_STYLE);

        if (value == 0) {
            log_add("yaml_document_add_scalar() failure");
        }
        else {
            if (!yaml_document_append_mapping_pair(document, map, key, value)) {
                log_add("yaml_document_append_mapping_pair() failure");
            }
            else {
                success = true;
            }
        }
    }

    return success;
}

/**
 * Adds to a map-node of a YAML document the last data-product received in
 * a multicast receiver memory.
 *
 * @param[in] mrm       The multicast receiver memory.
 * @param[in] document  The YAML document.
 * @param[in] map       The identifier of the YAML map-node.
 * @retval    true      Success.
 * @retval    false     Failure. `log_add()` called.
 */
static bool
addLastMcastProd(
    const McastReceiverMemory* const restrict mrm,
    yaml_document_t* const restrict           document,
    const int                                 map)
{
    char sigStr[sizeof(signaturet)*2+1];

    (void)sprint_signaturet(sigStr, sizeof(sigStr), mrm->lastMcastProd);
    return appendStringMapping(document, map, LAST_MCAST_PROD_KEY, sigStr);
}

/**
 * Copies the information in a multicast receiver memory to the root node of a
 * YAML document.
 *
 * @param[in] mrm       The multicast receiver memory.
 * @param[in] document  The YAML document.
 * @retval    true      Success.
 * @retval    false     Failure. `log_add()` called.
 */
static bool
addData(
    const McastReceiverMemory* const restrict mrm,
    yaml_document_t* const restrict           document)
{
    bool success;
    int  root = yaml_document_add_mapping(document, NULL,
            YAML_BLOCK_MAPPING_STYLE);

    if (root == 0) {
        log_add("yaml_document_add_mapping() failure");
        success = false;
    }
    else {
        success = (!mrm->sigSet || addLastMcastProd(mrm, document, root)) &&
                ((piq_count(mrm->requestedQ) == 0 &&
                        piq_count(mrm->missedQ) == 0) ||
                addMissedMcastFiles(mrm, document, root));
    }

    return success;
}

/**
 * Emits the native, internal data of a multicast receiver memory to a YAML
 * document.
 *
 * @param[in] mrm      The multicast receiver memory to be written.
 * @param[in] emitter  The YAML emitter.
 * @retval    true     Success.
 * @retval    false    Failure. `log_add()` called.
 */
static bool
emitDocument(
    const McastReceiverMemory* const restrict mrm,
    yaml_emitter_t* const restrict            emitter)
{
    bool            success = false;
    yaml_document_t document;

    (void)memset(&document, 0, sizeof(document));

    if (!yaml_document_initialize(&document, NULL, NULL, NULL, 0, 0)) {
        log_add("yaml_document_initialize() failure");
    }
    else {
        if (!addData(mrm, &document)) {
            yaml_document_delete(&document);
        }
        else {
            success = yaml_emitter_dump(emitter, &document); // deletes document
        }

    } // `document` initialized

    return success;
}

/**
 * Emits the native, internal data of a multicast receiver memory to a YAML
 * stream.
 *
 * @param[in] mrm      The multicast receiver memory.
 * @param[in] emitter  The YAML emitter.
 * @retval    true     Success.
 * @retval    false    Failure. `log_add()` called.
 */
static bool
emitStream(
    const McastReceiverMemory* const restrict mrm,
    yaml_emitter_t* const restrict            emitter)
{
    bool success = false;

    if (!yaml_emitter_open(emitter)) { // emit STREAM-START event
        log_add("yaml_emitter_open() failure");
    }
    else {
        success = emitDocument(mrm, emitter);

        if (!yaml_emitter_close(emitter)) { // emit STREAM-STOP event?
            log_add("yaml_emitter_close() failure");
            success = false;
        }
    } // `emitter` opened

    return success;
}

/**
 * Dumps the native, internal data of a multicast receiver memory to its
 * memory-file.
 *
 * @param[in] mrm    The multicast receiver memory.
 * @param[in] file   The file into which to dump the memory.
 * @retval    true   Success.
 * @retval    false  Failure. `log_add()` called.
 */
static bool
dumpMemory(
    const McastReceiverMemory* const mrm,
    FILE* const                      file)
{
    bool           success = false;
    yaml_emitter_t emitter;

    (void)memset(&emitter, 0, sizeof(emitter));

    if (!yaml_emitter_initialize(&emitter)) {
        log_add("yaml_emitter_initialize() failure");
    }
    else {
        yaml_emitter_set_output_file(&emitter, file);
        yaml_emitter_set_canonical(&emitter, 0);
        yaml_emitter_set_unicode(&emitter, 1);

        success = emitStream(mrm, &emitter);

        yaml_emitter_delete(&emitter);
    } // `emitter` initialized

    return success;
}

/**
 * Closes the temporary memory-file of a multicast receiver memory and renames
 * it to the canonical memory-file.
 *
 * @param[in] mrm       The multicast receiver memory.
 * @param[in] goodDump  Was the dump of memory successful?
 * @retval    true      Success.
 * @retval    false     Failure. `log_add()` called.
 */
static bool
closeAndRenameTempMemoryFile(
    McastReceiverMemory* const mrm,
    FILE* const                file,
    const bool                 goodDump)
{
    if (fclose(file)) {
        log_add_syserr("Couldn't close temporary memory-file \"%s\"", mrm->tmpPath);
        return false;
    }
    if (rename(mrm->tmpPath, mrm->path)) {
        log_add_syserr("Couldn't rename file \"%s\" to \"%s\"", mrm->tmpPath,
                mrm->path);
        return false;
    }

    return true;
}

/**
 * Dumps the native, internal representation of a multicast receiver memory to
 * its associated memory-file.
 *
 * @param[in] mrm    The multicast memory receiver to be dumped.
 * @retval    true   Success.
 * @retval    false  Failure. `log_add()` called. The associated memory-file,
 *                   if it exists, is unmodified.
 */
static bool
dump(
    McastReceiverMemory* const mrm)
{
    bool  success = false;
    FILE* file = openTempMemoryFile(mrm);

    if (file) {
        if (dumpMemory(mrm, file))
            success = true;

        if (!closeAndRenameTempMemoryFile(mrm, file, success))
            success = false;
    }

    return success;
}

/**
 * Adds an index of a product that was missed by the multicast receiver to one
 * of the queues of a multicast receiver memory.
 *
 * @pre              The multicast receiver memory is unlocked.
 * @param[in] mrm    The multicast receiver memory.
 * @param[in] fiq    The queue to use.
 * @param[in] id     The product index to add.
 * @retval    true   Success.
 * @retval    false  Error. `log_add()` called.
 * @post             The multicast receiver memory is unlocked.
 */
static bool
addFile(
    McastReceiverMemory* const restrict mrm,
    ProdIndexQueue* const restrict      fiq,
    const FmtpProdIndex                iProd)
{
    lock(mrm);
    bool success = piq_add(fiq, iProd) == 0;
    if (success)
        mrm->modified = true;
    unlock(mrm);

    return success;
}

/******************************************************************************
 * Public API
 ******************************************************************************/

/**
 * Deletes a multicast-receiver memory-file. This function is reentrant.
 *
 * @param[in] servAddr  Address of the server.
 * @param[in] feedtype  Feedtype of the multicast group.
 * @retval    true      Success or the file doesn't exist.
 * @retval    false     Error. `log_add()` called.
 */
bool
mrm_delete(
    const ServiceAddr* const servAddr,
    const feedtypet          feedtype)
{
    bool        success;
    char* const path = getSessionPath(servAddr, feedtype);

    if (!path) {
        success = false;
    }
    else {
        if (unlink(path)) {
            if (errno == ENOENT) {
                success = true;
            }
            else {
                log_add_syserr("Couldn't remove multicast-receiver memory-file \"%s\"",
                        path);
                success = false;
            }
        }
        else {
            success = true;
        }

        free(path);
    } // `path` allocated

    return success;
}

/**
 * Opens a multicast receiver memory. This function is reentrant.
 *
 * @param[in] servAddr  Address of the server.
 * @param[in] feedtype  Feedtype of the multicast group.
 * @retval    NULL      Error. `log_add()` called.
 * @return              Pointer to a multicast receiver memory object.
 */
McastReceiverMemory*
mrm_open(
    const ServiceAddr* const servAddr,
    const feedtypet          feedtype)
{
    McastReceiverMemory* mrm = log_malloc(sizeof(McastReceiverMemory),
            "multicast receiver memory");

    if (mrm) {
        if (!init(mrm, servAddr, feedtype)) {
            free(mrm);
            mrm = NULL;
        }
    }

    return mrm;
}

/**
 * Closes a multicast receiver memory. Upon successful return, the multicast
 * receiver memory of a subsequent identical `mrm_open()` will comprise that of
 * the previous `mrm_open()` as subsequently modified prior to calling this
 * function. This function is thread-compatible but not thread-safe for the
 * same argument.
 *
 * @param[in] mrm    The multicast receiver memory, returned by `mrm_open()`, to
 *                   be closed. Use of this object upon successful return from
 *                   this function results in undefined behavior.
 * @retval    true   Success.
 * @retval    false  Failure. `log_add()` called. `mrm` is unmodified.
 */
bool
mrm_close(
    McastReceiverMemory* const mrm)
{
    vetMrm(mrm);

    if (mrm->modified && !dump(mrm))
        return false;

    mrm->magic = NULL;

    (void)pthread_mutex_destroy(&mrm->mutex);
    piq_free(mrm->requestedQ);
    piq_free(mrm->missedQ);
    free(mrm->path);
    free(mrm->tmpPath);
    free(mrm);

    return true;
}

/**
 * Sets the signature of the last data-product received via multicast in a
 * multicast receiver memory. It is unspecified when the memory-file is
 * modified.
 *
 * @param[in] mrm    The multicast receiver memory.
 * @param[in] sig    Signature of the last data-product received via multicast.
 * @retval    true   Success.
 * @retval    false  Failure. `log_add()` called. The multicast receiver
 *                   memory is unmodified. Thread-safe.
 */
bool
mrm_setLastMcastProd(
    McastReceiverMemory* const restrict mrm,
    const signaturet                    sig)
{
    vetMrm(mrm);
    lock(mrm);
    (void)memcpy(&mrm->lastMcastProd, sig, sizeof(signaturet));
    mrm->sigSet = true;
    mrm->modified = true;
    unlock(mrm);
    return true;
}

/**
 * Returns the signature of the last data-product received via multicast of a
 * multicast receiver memory. Thread-safe.
 *
 * @param[in]  mrm    The multicast receiver memory.
 * @param[out] sig    Signature of the last data-product received via multicast.
 * @retval     true   Success. `sig` is set.
 * @retval     false  The signature doesn't exist. `sig` is unaltered.
 */
bool
mrm_getLastMcastProd(
    McastReceiverMemory* const restrict mrm,
    signaturet                          sig)
{
    vetMrm(mrm);
    lock(mrm);
        bool sigSet = mrm->sigSet;
        if (sigSet)
            (void)memcpy(sig, &mrm->lastMcastProd, sizeof(signaturet));
    unlock(mrm);

    return sigSet;
}

/**
 * Clears the list of files in a multicast receiver memory that were missed by
 * the multicast receiver: both the missed-but-not-requested and
 * requested-but-not-received queues are cleared. Idempotent. Thread-safe.
 *
 * @param[in] mrm  The multicast receiver memory.
 */
void
mrm_clearAllMissedFiles(
    McastReceiverMemory* const restrict mrm)
{
    vetMrm(mrm);
    lock(mrm);
    mrm->modified = piq_clear(mrm->requestedQ) != 0 ||
            piq_clear(mrm->missedQ) != 0;
    unlock(mrm);
}

/**
 * Removes and returns the index of a product that has not been received by
 * the multicast receiver associated with a multicast receiver memory. The
 * requested-but-not-received queue is tried first; then the
 * missed-but-not-requested queue. Thread-safe.
 *
 * @param[in] mrm     The multicast receiver memory.
 * @param[in] iProd   The index of the missed product.
 * @retval    true    Such an index exists. `*iProd` is set.
 * @retval    false   No such index (the queues are empty).
 */
bool
mrm_getAnyMissedFileNoWait(
    McastReceiverMemory* const restrict mrm,
    FmtpProdIndex* const restrict      iProd)
{
    vetMrm(mrm);
    lock(mrm);
    bool exists = piq_removeNoWait(mrm->requestedQ, iProd) == 0 ||
            piq_removeNoWait(mrm->missedQ, iProd) == 0;
    unlock(mrm);

    return exists;
}

/**
 * Adds an index of a product that was missed by the multicast receiver but
 * has not yet been requested to the current list of such files in a multicast
 * receiver memory. Thread-safe.
 *
 * @param[in] mrm    The multicast receiver memory.
 * @param[in] id     The product-index to add.
 * @retval    true   Success.
 * @retval    false  Error. `log_add()` called.
 */
bool
mrm_addMissedFile(
    McastReceiverMemory* const restrict mrm,
    const FmtpProdIndex                iProd)
{
    vetMrm(mrm);
    return addFile(mrm, mrm->missedQ, iProd); // locks and unlocks `mrm`
}

/**
 * Adds an index of a product that was missed by the multicast receiver and
 * has been requested from the upstream LDM-7 to the current list of such
 * products in a multicast receiver memory. Thread-safe.
 *
 * @param[in] mrm    The multicast receiver memory.
 * @param[in] id     The product-index to add.
 * @retval    true   Success.
 * @retval    false  Error. `log_add()` called.
 */
bool
mrm_addRequestedFile(
    McastReceiverMemory* const restrict mrm,
    const FmtpProdIndex                iProd)
{
    vetMrm(mrm);
    return addFile(mrm, mrm->requestedQ, iProd); // locks and unlocks `mrm`
}

/**
 * Returns (but doesn't remove) the next product-index from the
 * missed-but-not-requested queue of a multicast receiver memory. Blocks until
 * such a file is available. Thread-safe.
 *
 * @param[in] mrm     The multicast receiver memory.
 * @param[in] iProd   The product-index.
 * @retval    true    Success. `*iProd` is set.
 * @retval    false   The queue has been shutdown.
 */
bool
mrm_peekMissedFileWait(
    McastReceiverMemory* const restrict mrm,
    FmtpProdIndex* const restrict      iProd)
{
    vetMrm(mrm);
    return piq_peekWait(mrm->missedQ, iProd) == 0;
}

/**
 * Returns (but doesn't remove) the next product-index from the
 * missed-but-not-requested queue of a multicast receiver memory. Thread-safe.
 *
 * @param[in] mrm     The multicast receiver memory.
 * @param[in] iProd   The product-index.
 * @retval    true    The index exists. `*iProd` is set.
 * @retval    false   No such index (the queue is empty).
 */
bool
mrm_peekMissedFileNoWait(
    McastReceiverMemory* const restrict mrm,
    FmtpProdIndex* const restrict      iProd)
{
    vetMrm(mrm);
    return piq_peekNoWait(mrm->missedQ, iProd) == 0;
}

/**
 * Removes and returns the next product-index from the
 * missed-but-not-requested queue of a multicast receiver memory. Thread-safe.
 *
 * @param[in] mrm     The multicast receiver memory.
 * @param[in] iProd   The product-index.
 * @retval    true    The index exists. `*iProd` is set.
 * @retval    false   No such index (the queue is empty).
 */
bool
mrm_removeMissedFileNoWait(
    McastReceiverMemory* const restrict mrm,
    FmtpProdIndex* const restrict      iProd)
{
    vetMrm(mrm);
    return piq_removeNoWait(mrm->missedQ, iProd) == 0;
}

/**
 * Returns (but doesn't remove) the next product-index from the
 * requested-but-not-received queue of a multicast receiver memory. Doesn't
 * block. Thread-safe.
 *
 * @param[in] mrm     The multicast receiver memory.
 * @param[in] iProd   The product-index.
 * @retval    true    Success. `*iProd` is set.
 * @retval    false   No such identifier (the queue is empty).
 */
bool
mrm_peekRequestedFileNoWait(
    McastReceiverMemory* const restrict mrm,
    FmtpProdIndex* const restrict      iProd)
{
    vetMrm(mrm);
    return piq_peekNoWait(mrm->requestedQ, iProd) == 0;
}

/**
 * Removes and returns the next product-index from the
 * requested-but-not-received queue of a multicast receiver memory. Thread-safe.
 *
 * @param[in] mrm     The multicast receiver memory.
 * @param[in] iProd   The product-index.
 * @retval    true    The index exists. `*iProd` is set.
 * @retval    false   No such index (the queue is empty).
 */
bool
mrm_removeRequestedFileNoWait(
    McastReceiverMemory* const restrict mrm,
    FmtpProdIndex* const restrict      iProd)
{
    vetMrm(mrm);
    return piq_removeNoWait(mrm->requestedQ, iProd) == 0;
}

/**
 * Shuts down the queue of missed-but-not-requested files in a multicast
 * receiver memory. Idempotent and thread-safe.
 *
 * @param[in] mrm     The multicast receiver memory.
 */
void
mrm_shutDownMissedFiles(
    McastReceiverMemory* const restrict mrm)
{
    vetMrm(mrm);
    (void)piq_cancel(mrm->missedQ);
}
