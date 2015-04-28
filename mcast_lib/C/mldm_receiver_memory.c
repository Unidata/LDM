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

#include "prod_index_queue.h"
#include "globals.h"
#include "inetutil.h"
#include "mcast.h"
#include "mldm_receiver_memory.h"
#include "ldmprint.h"
#include "log.h"

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <yaml.h>

/**
 * The data structure of a multicast session memory:
 */
struct McastSessionMemory {
    /**
     * Path of the canonical multicast-session memory-file:
     */
    char*            path;
    /**
     * Path of the temporary multicast-session memory-file:
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
     * Whether or not the multicast session memory has been modified by the
     * user.
     */
    bool             modified;
    /**
     * Concurrent access mutex.
     */
    pthread_mutex_t  mutex;
};

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
 * Returns the path of the memory-file corresponding to a server and a multicast
 * group. This function is reentrant.
 *
 * @param[in] servAddr  The address of the server associated with the multicast
 *                      group.
 * @param[in] feedtype  Feedtype of multicast group.
 * @retval    NULL      Failure. `log_start()` called.
 * @return              The path of the corresponding memory-file. The caller
 *                      should free when it's no longer needed.
 */
static char*
getSessionPath(
    const ServiceAddr* const servAddr,
    const feedtypet          feedtype)
{
    char*       path;
    char* const servAddrStr = sa_format(servAddr);

    if (NULL == servAddrStr) {
        path = NULL;
    }
    else {
        char ftBuf[256];

        if (sprint_feedtypet(ftBuf, sizeof(ftBuf), feedtype) < 0) {
            LOG_START0("sprint_feedtypet() failure");
            path = NULL;
        }
        else {
            path = ldm_format(256, "%s/%s_%s.yaml", getLdmLogDir(), servAddrStr,
                    ftBuf);
            free(servAddrStr);
        }
    }

    return path;
}

/**
 * Returns the path of the temporary memory-file corresponding to the path of a
 * canonical memory-file. This function is thread-safe.
 *
 * @param[in] path  The path of a canonical memory-file. The caller may free
 *                  when it's no longer needed.
 * @retval    NULL  Failure. `log_start()` called.
 * @return          The path of the corresponding temporary memory-file. The
 *                  caller should free when it's no longer needed.
 */
static char*
makeTempPath(
    char* const restrict path)
{
    char* const tmpPath = ldm_format(256, "%s%s", path, ".new");

    if (tmpPath == NULL)
        LOG_ADD0("Couldn't create path of temporary memory-file");

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
    McastSessionMemory* const msm)
{
    int status = pthread_mutex_lock(&msm->mutex);

    if (status) {
        LOG_ERRNUM0(status, "Couldn't lock mutex");
        log_log(LOG_ERR);
    }
}

static void
unlock(
    McastSessionMemory* const msm)
{
    int status = pthread_mutex_unlock(&msm->mutex);

    if (status) {
        LOG_ERRNUM0(status, "Couldn't unlock mutex");
        log_log(LOG_ERR);
    }
}

/**
 * Initializes the last, multicast data-product signature in a multicast session
 * memory from a YAML mapping-node. This function is reentrant.
 *
 * @param[in] msm       The multicast session memory to initialize.
 * @param[in] document  The YAML document to use.
 * @param[in] start     The first node-pair of the mapping.
 * @param[in] end       The last node-pair of the mapping.
 * @retval    true      Success or the parameter doesn't exist.
 * @retval    false     Error. `log_start()` called.
 */
static bool
initLastMcastProd(
    McastSessionMemory* const restrict msm,
    yaml_document_t* const restrict    document,
    yaml_node_pair_t* const restrict   start,
    yaml_node_pair_t* const restrict   end)
{
    yaml_node_t* const valueNode = getValueNode(document, start, end,
            LAST_MCAST_PROD_KEY);

    if (valueNode == NULL)
        return true;

    if (valueNode->type != YAML_SCALAR_NODE) {
        LOG_START1("Unexpected node-type for value associated with key \"%s\"",
                LAST_MCAST_PROD_KEY);
        return false;
    }

    const char* sigStr = valueNode->data.scalar.value;

    if (sigParse(sigStr, &msm->lastMcastProd) == -1) {
        LOG_ADD1("Unable to parse last multicast data-product signature \"%s\"",
                sigStr);
        return false;
    }

    msm->sigSet = true;

    return true;
}

/**
 * Initializes a multicast session memory's list of files that were missed by
 * the multicast receiver during the previous session from a YAML sequence.
 *
 * @param[in] msm       The multicast session memory to initialize.
 * @param[in] document  The YAML document to use.
 * @param[in] start     The first item of the sequence.
 * @param[in] end       The last item of the sequence.
 * @retval    true      Success.
 * @retval    false     Error. `log_start()` called.
 */
static bool
initMissedFilesFromSequence(
    McastSessionMemory* const restrict     msm,
    yaml_document_t* const restrict        document,
    const yaml_node_item_t* const restrict start,
    const yaml_node_item_t* const restrict end)
{
    for (const yaml_node_item_t* item = start; item && item <= end; item++) {
        yaml_node_t* itemNode = yaml_document_get_node(document, *item);

        if (itemNode == NULL) {
            LOG_START0("yaml_document_get_node() failure");
            return false;
        }
        if (itemNode->type != YAML_SCALAR_NODE) {
            LOG_START0("Unexpected node-type for missed-file item");
            return false;
        }
        unsigned long fileId;
        if (sscanf(itemNode->data.scalar.value, "%80lu", &fileId) != 1) {
            LOG_SERROR1("Couldn't decode missed-file identifier \"%s\"",
                    itemNode->data.scalar.value);
            return false;
        }
        if (piq_add(msm->missedQ, fileId) != 0)
            return false;
    }

    return true;
}

/**
 * Initializes a multicast session memory's list of files that were missed by
 * the multicast receiver during the previous session from a YAML mapping-node.
 *
 * @param[in] msm       The multicast session memory to initialize.
 * @param[in] document  The YAML document to use.
 * @param[in] start     The first node-pair of the mapping.
 * @param[in] end       The last node-pair of the mapping.
 * @retval    true      Success or the information doesn't exist.
 * @retval    false     Error. `log_start()` called.
 */
static bool
initMissedFiles(
    McastSessionMemory* const restrict msm,
    yaml_document_t* const restrict    document,
    yaml_node_pair_t* const restrict   start,
    yaml_node_pair_t* const restrict   end)
{
    yaml_node_t* const valueNode = getValueNode(document, start, end,
            MISSED_MCAST_FILES_KEY);

    if (valueNode == NULL)
        return true;

    if (valueNode->type != YAML_SEQUENCE_NODE) {
        LOG_START1("Unexpected node-type for value associated with key \"%s\"",
                MISSED_MCAST_FILES_KEY);
        return false;
    }

    return initMissedFilesFromSequence(msm, document,
            valueNode->data.sequence.items.start,
            valueNode->data.sequence.items.end);
}

/**
 * Initializes a multicast session memory from a YAML node. This function is
 * reentrant.
 *
 * @param[in] msm       The multicast session memory to initialize.
 * @param[in] document  The YAML document to use.
 * @param[in] node      The YAML node to use.
 * @retval    true      Success.
 * @retval    false     Failure. `log_start()` called.
 */
static bool
initFromNode(
    McastSessionMemory* const restrict msm,
    yaml_document_t* const restrict    document,
    yaml_node_t* const restrict        node)
{
    if (node->type != YAML_MAPPING_NODE) {
        LOG_START1("Unexpected YAML node: %d", node->type);
        return false;
    }

    yaml_node_pair_t* const start = node->data.mapping.pairs.start;
    yaml_node_pair_t* const end = node->data.mapping.pairs.end;

    return initLastMcastProd(msm, document, start, end) &&
            initMissedFiles(msm, document, start, end);
}

/**
 * Initializes a multicast session memory from a YAML document. This function is
 * reentrant.
 *
 * @param[in] msm       The multicast session memory to initialize.
 * @param[in] document  The YAML document to use.
 * @retval    true      Success.
 * @retval    false     Failure. `log_start()` called.
 */
static bool
initFromDocument(
    McastSessionMemory* const restrict msm,
    yaml_document_t* const restrict    document)
{
    yaml_node_t* rootNode = yaml_document_get_root_node(document);

    if (rootNode == NULL) {
        LOG_START0("YAML document is empty");
        return false;
    }

    return initFromNode(msm, document, rootNode);
}

/**
 * Initializes a multicast session memory from a YAML stream. Only the first
 * document is used. This function is reentrant.
 *
 * @param[in] msm     The multicast session memory to be initialized.
 * @param[in] parser  The YAML parser.
 * @retval    true    Success.
 * @retval    false   Error. `log_start()` called.
 */
static bool
initFromStream(
    McastSessionMemory* const restrict msm,
    yaml_parser_t* const restrict      parser)
{
    yaml_document_t document;

    (void)memset(&document, 0, sizeof(document));

    if (!yaml_parser_load(parser, &document)) {
        LOG_START3("YAML parser failure at line=%lu, column=%lu: %s:",
                yamlParserLine(parser), yamlParserColumn(parser),
                yamlParserErrMsg(parser));
        return false;
    }

    bool success = initFromDocument(msm, &document);

    yaml_document_delete(&document);

    return success;
}

/**
 * Initializes a multicast session memory from a YAML file. This function is
 * reentrant.
 *
 * @param[in] msm   The multicast session memory to initialize.
 * @param[in] file  The YAML file to parse.
 * @retval    0     Success.
 * @retval    1     System error. `log_start()` called.
 * @retval    2     Parse error. `log_start()` called.
 */
static int
initFromYamlFile(
    McastSessionMemory* const restrict msm,
    FILE* const restrict               file)
{
    int           status;
    yaml_parser_t parser;

    if (!yaml_parser_initialize(&parser)) {
        LOG_SERROR0("Couldn't initialize YAML parser");
        status = 1;
    }
    else {
        yaml_parser_set_input_file(&parser, file);

        if (!initFromStream(msm, &parser)) {
            LOG_ADD0("Error parsing memory-file. Delete or correct it.");
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
 * Initializes a multicast session memory from a memory-file. This function is
 * reentrant.
 *
 * @param[in] msm           The multicast session memory to initialize.
 * @param[in] path          The path of the memory-file. Caller must not modify
 *                          or free.
 * @retval    0             Success.
 * @retval    1             System error. `log_start()` called.
 * @retval    2             Memory-file doesn't exist.
 */
static int
initFromFile(
    McastSessionMemory* const restrict msm,
    char* const restrict               path)
{
    int   status;
    FILE* file = fopen(path, "r");


    if (file == NULL) {
        if (errno == ENOENT) {
            status = 2;
        }
        else {
            LOG_SERROR1("Couldn't open memory-file \"%s\"", path);
            status = 1;
        }
    }
    else {
        status = initFromYamlFile(msm, file);

        if (status)
            LOG_ADD1("Couldn't initialize multicast-memory from file \"%s\"",
                    path);

        (void)fclose(file); // don't care because open for reading only
    } // `file` open

    return status;
}

/**
 * Initializes the mutex of a multicast session memory.
 *
 * @param[in] msm    The multicast session memory.
 * @retval    true   Success.
 * @retval    false  Failure. `log_start()` called.
 */
static bool
initMutex(
    McastSessionMemory* const msm)
{
    pthread_mutexattr_t mutexAttr;
    int                 status = pthread_mutexattr_init(&mutexAttr);

    if (status) {
        LOG_ERRNUM0(status, "Couldn't initialize mutex attributes");
    }
    else {
        // At most one lock per thread.
        (void)pthread_mutexattr_settype(&mutexAttr, PTHREAD_MUTEX_ERRORCHECK);
        // Prevent priority inversion
        (void)pthread_mutexattr_setprotocol(&mutexAttr, PTHREAD_PRIO_INHERIT);

        status = pthread_mutex_init(&msm->mutex, &mutexAttr);

        if (status)
            LOG_ERRNUM0(status, "Couldn't initialize mutex");

        (void)pthread_mutexattr_destroy(&mutexAttr);
    } // `mutexAttr` initialized

    return 0 == status;
}

/**
 * Initializes a multicast session memory from scratch.
 *
 * @param[in] msm    The multicast session memory to initialize.
 * @param[in] path   The path of the canonical memory-file. Caller must not
 *                   modify or free.
 * @param[in] path   The path of the temporary memory-file. Caller must not
 *                   modify or free.
 * @retval    true   Success.
 * @retval    false  Failure. `log_start()` called.
 */
static bool
initFromScratch(
    McastSessionMemory* const restrict msm,
    char* const restrict               path)
{
    bool        success = false;
    char* const tmpPath = makeTempPath(path);

    if (tmpPath != NULL) {
        if ((msm->missedQ = piq_new()) == NULL) {
            LOG_ADD0("Couldn't create queue of missed data-products");
        }
        else {
            if ((msm->requestedQ = piq_new()) == NULL) {
                LOG_ADD0("Couldn't create queue of requested data-products");
            }
            else {
                if (initMutex(msm)) {
                    msm->path = path;
                    msm->tmpPath = tmpPath;
                    msm->sigSet = false;
                    msm->modified = false;
                    success = true;
                }
                else {
                    piq_free(msm->requestedQ);
                }
            } // `msm->requestedQ` allocated

            if (!success)
                piq_free(msm->missedQ);
        } // `msm->missedQ` allocated

        if (!success)
            free(tmpPath);
    } // `tmpPath` allocated

    return success;
}

/**
 * Initializes a multicast session memory from a pre-existing memory-file or
 * from scratch if the memory-file doesn't exist. This function is reentrant.
 *
 * @param[in] msm       The multicast session memory to initialize.
 * @param[in] path      The path of the canonical memory-file. Caller must not
 *                      modify or free.
 * @retval    true      Success.
 * @retval    false     Failure. `log_add()` called.
 */
static bool
initFromScratchOrFile(
    McastSessionMemory* const restrict msm,
    char* const restrict               path)
{
    if (!initFromScratch(msm, path))
        return false;

    int status = initFromFile(msm, path);

    return (status == 0) || (status == 2); // success or file doesn't exist
}

/**
 * Initializes a multicast session memory. This function is reentrant.
 *
 * @param[in] msm       The muticast session memory to initialize.
 * @param[in] servAddr  Address of the server.
 * @param[in] feedtype  Feedtype of the multicast group.
 * @retval    true      Success.
 * @retval    false     Failure. `log_add()` called.
 */
static bool
init(
    McastSessionMemory* const restrict msm,
    const ServiceAddr* const restrict  servAddr,
    const feedtypet                    feedtype)
{
    bool        success;
    char* const path = getSessionPath(servAddr, feedtype);

    if (path == NULL) {
        success = false;
    }
    else {
        if (!(success = initFromScratchOrFile(msm, path)))
            free(path);
    } // `path` allocated

    return success;
}

/**
 * Opens a memory-file of a multicast session memory.
 *
 * @param[in] msm    The multicast session memory.
 * @retval    NULL   Failure. `log_start()` called.
 * @return           A memory-file open for writing.
 */
static FILE*
openTempMemoryFile(
    McastSessionMemory* const msm)
{
    FILE* file = fopen(msm->tmpPath, "w");

    if (file == NULL)
        LOG_SERROR1("Couldn't open temporary memory-file \"%s\"", msm->tmpPath);

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
 * @retval    false     Failure. `log_start()` called.
 */
static bool
appendFileIds(
    yaml_document_t* const restrict document,
    const int                       seq,
    ProdIndexQueue* const restrict     fiq)
{
    VcmtpProdIndex iProd;

    while (piq_removeNoWait(fiq, &iProd) == 0) {
        char          buf[sizeof(iProd)*4+1]; // overly capacious

        (void)snprintf(buf, sizeof(buf), "%lu", (unsigned long)iProd);

        int  scalarNode = yaml_document_add_scalar(document, NULL, buf, -1,
                YAML_PLAIN_SCALAR_STYLE);

        if (!scalarNode) {
            LOG_START0("yaml_document_add_scalar() failure");
            return false;
        }
        else {
            if (!yaml_document_append_sequence_item(document, seq, scalarNode)) {
                LOG_START0("yaml_document_append_sequence_item() failure");
                return false;
            }
        }
    }
    
    return true;
}

/**
 * Appends to a YAML sequence-node the current list of files that were missed by
 * the multicast receiver associated with a multicast session memory. Both the
 * "missed" and "requested" queues are used.
 *
 * @param[in] msm       The multicast session memory.
 * @param[in] document  The YAML document.
 * @param[in] seq       The identifier of the YAML sequence-node.
 * @retval    true      Success.
 * @retval    false     Failure. `log_start()` called.
 */
static bool
addMissedFiles(
    const McastSessionMemory* const restrict msm,
    yaml_document_t* const restrict          document,
    const int                                seq)
{
    return appendFileIds(document, seq, msm->requestedQ) &&
            appendFileIds(document, seq, msm->missedQ);
}

/**
 * Returns the node-identifier of a YAML sequence of files that were missed by
 * the multicast receiver according to a multicast session memory.
 *
 * @param[in] msm       The multicast session memory.
 * @param[in] document  The YAML document.
 * @retval    0         Failure. `log_start()` called.
 * @return              The identifier of the YAML sequence-node.
 */
static int
getMissedFileSequence(
    const McastSessionMemory* const restrict msm,
    yaml_document_t* const restrict          document)
{
    int seq = yaml_document_add_sequence(document, NULL,
            YAML_FLOW_SEQUENCE_STYLE);

    if (!seq) {
        LOG_START0("yaml_document_add_sequence() failure");
    }
    else {
        if (!addMissedFiles(msm, document, seq))
            seq = 0;
    }

    return seq;
}

/**
 * Adds to a map-node of a YAML document the list of files that were missed
 * by the multicast receiver associated with a multicast session memory.
 *
 * @param[in] msm       The multicast session memory.
 * @param[in] document  The YAML document.
 * @param[in] map       The identifier of the YAML map-node.
 * @retval    true      Success.
 * @retval    false     Failure. `log_start()` called.
 */
static bool
addMissedMcastFiles(
    const McastSessionMemory* const restrict msm,
    yaml_document_t* const restrict          document,
    const int                                map)
{
    bool success = false;
    int  seq = getMissedFileSequence(msm, document);

    if (seq) {
        // ASSUMPTION: The 3rd argument isn't modified
        int  key = yaml_document_add_scalar(document, NULL,
                (char*)MISSED_MCAST_FILES_KEY, -1, YAML_PLAIN_SCALAR_STYLE);

        if (key == 0) {
            LOG_START0("yaml_document_add_scalar() failure");
        }
        else {
            if (!yaml_document_append_mapping_pair(document, map, key, seq)) {
                LOG_START0("yaml_document_append_mapping_pair() failure");
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
 * @retval    false     Failure. `log_start()` called.
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
        LOG_START0("yaml_document_add_scalar() failure");
    }
    else {
        // ASSUMPTION: The 3rd argument isn't modified
        int value = yaml_document_add_scalar(document, NULL, (char*)valueStr, -1,
                YAML_PLAIN_SCALAR_STYLE);

        if (value == 0) {
            LOG_START0("yaml_document_add_scalar() failure");
        }
        else {
            if (!yaml_document_append_mapping_pair(document, map, key, value)) {
                LOG_START0("yaml_document_append_mapping_pair() failure");
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
 * a multicast session memory.
 *
 * @param[in] msm       The multicast session memory.
 * @param[in] document  The YAML document.
 * @param[in] map       The identifier of the YAML map-node.
 * @retval    true      Success.
 * @retval    false     Failure. `log_start()` called.
 */
static bool
addLastMcastProd(
    const McastSessionMemory* const restrict msm,
    yaml_document_t* const restrict          document,
    const int                                map)
{
    char sigStr[sizeof(signaturet)*2+1];

    (void)sprint_signaturet(sigStr, sizeof(sigStr), msm->lastMcastProd);
    return appendStringMapping(document, map, LAST_MCAST_PROD_KEY, sigStr);
}

/**
 * Copies the information in a multicast session memory to the root node of a
 * YAML document.
 *
 * @param[in] msm       The multicast session memory.
 * @param[in] document  The YAML document.
 * @retval    true      Success.
 * @retval    false     Failure. `log_start()` called.
 */
static bool
addData(
    const McastSessionMemory* const restrict msm,
    yaml_document_t* const restrict          document)
{
    bool success;
    int  root = yaml_document_add_mapping(document, NULL,
            YAML_BLOCK_MAPPING_STYLE);

    if (root == 0) {
        LOG_START0("yaml_document_add_mapping() failure");
        success = false;
    }
    else {
        success = (!msm->sigSet || addLastMcastProd(msm, document, root)) &&
                ((piq_count(msm->requestedQ) == 0 &&
                        piq_count(msm->missedQ) == 0) ||
                addMissedMcastFiles(msm, document, root));
    }

    return success;
}

/**
 * Emits the native, internal data of a multicast session memory to a YAML
 * document.
 *
 * @param[in] msm      The multicast session memory to be written.
 * @param[in] emitter  The YAML emitter.
 * @retval    true     Success.
 * @retval    false    Failure. `log_start()` called.
 */
static bool
emitDocument(
    const McastSessionMemory* const restrict msm,
    yaml_emitter_t* const restrict           emitter)
{
    bool            success = false;
    yaml_document_t document;

    (void)memset(&document, 0, sizeof(document));

    if (!yaml_document_initialize(&document, NULL, NULL, NULL, 0, 0)) {
        LOG_START0("yaml_document_initialize() failure");
    }
    else {
        if (!addData(msm, &document)) {
            yaml_document_delete(&document);
        }
        else {
            success = yaml_emitter_dump(emitter, &document); // deletes document
        }

    } // `document` initialized

    return success;
}

/**
 * Emits the native, internal data of a multicast session memory to a YAML
 * stream.
 *
 * @param[in] msm      The multicast session memory.
 * @param[in] emitter  The YAML emitter.
 * @retval    true     Success.
 * @retval    false    Failure. `log_start()` called.
 */
static bool
emitStream(
    const McastSessionMemory* const restrict msm,
    yaml_emitter_t* const restrict           emitter)
{
    bool success = false;

    if (!yaml_emitter_open(emitter)) { // emit STREAM-START event
        LOG_START0("yaml_emitter_open() failure");
    }
    else {
        success = emitDocument(msm, emitter);

        if (!yaml_emitter_close(emitter)) { // emit STREAM-STOP event?
            LOG_START0("yaml_emitter_close() failure");
            success = false;
        }
    } // `emitter` opened

    return success;
}

/**
 * Dumps the native, internal data of a multicast session memory to its
 * memory-file.
 *
 * @param[in] msm    The multicast session memory.
 * @param[in] file   The file into which to dump the memory.
 * @retval    true   Success.
 * @retval    false  Failure. `log_start()` called.
 */
static bool
dumpMemory(
    const McastSessionMemory* const msm,
    FILE* const                     file)
{
    bool           success = false;
    yaml_emitter_t emitter;

    (void)memset(&emitter, 0, sizeof(emitter));

    if (!yaml_emitter_initialize(&emitter)) {
        LOG_START0("yaml_emitter_initialize() failure");
    }
    else {
        yaml_emitter_set_output_file(&emitter, file);
        yaml_emitter_set_canonical(&emitter, 0);
        yaml_emitter_set_unicode(&emitter, 1);

        success = emitStream(msm, &emitter);

        yaml_emitter_delete(&emitter);
    } // `emitter` initialized

    return success;
}

/**
 * Closes the temporary memory-file of a multicast session memory and renames
 * it to the canonical memory-file.
 *
 * @param[in] msm       The multicast session memory.
 * @param[in] goodDump  Was the dump of memory successful?
 * @retval    true      Success.
 * @retval    false     Failure. `log_start()` called.
 */
static bool
closeAndRenameTempMemoryFile(
    McastSessionMemory* const msm,
    FILE* const               file,
    const bool                goodDump)
{
    if (fclose(file)) {
        LOG_SERROR1("Couldn't close temporary memory-file \"%s\"", msm->tmpPath);
        return false;
    }
    if (rename(msm->tmpPath, msm->path)) {
        LOG_SERROR2("Couldn't rename file \"%s\" to \"%s\"", msm->tmpPath,
                msm->path);
        return false;
    }

    return true;
}

/**
 * Dumps the native, internal representation of a multicast session memory to
 * its associated memory-file.
 *
 * @param[in] msm    The multicast memory session to be dumped.
 * @retval    true   Success.
 * @retval    false  Failure. `log_start()` called. The associated memory-file,
 *                   if it exists, is unmodified.
 */
static bool
dump(
    McastSessionMemory* const msm)
{
    bool  success = false;
    FILE* file = openTempMemoryFile(msm);

    if (file) {
        if (dumpMemory(msm, file))
            success = true;

        if (!closeAndRenameTempMemoryFile(msm, file, success))
            success = false;
    }

    return success;
}

/**
 * Adds an index of a product that was missed by the multicast receiver to one
 * of the queues of a multicast session memory.
 *
 * @pre              The multicast session memory is unlocked.
 * @param[in] msm    The multicast session memory.
 * @param[in] fiq    The queue to use.
 * @param[in] id     The product index to add.
 * @retval    true   Success.
 * @retval    false  Error. `log_start()` called.
 * @post             The multicast session memory is unlocked.
 */
static bool
addFile(
    McastSessionMemory* const restrict msm,
    ProdIndexQueue* const restrict     fiq,
    const VcmtpProdIndex               iProd)
{
    lock(msm);
    bool success = piq_add(fiq, iProd) == 0;
    if (success)
        msm->modified = true;
    unlock(msm);

    return success;
}

/******************************************************************************
 * Public API
 ******************************************************************************/

/**
 * Deletes a multicast-session memory-file. This function is reentrant.
 *
 * @param[in] servAddr  Address of the server.
 * @param[in] feedtype  Feedtype of the multicast group.
 * @retval    true      Success or the file doesn't exist.
 * @retval    false     Error. `log_start()` called.
 */
bool
msm_delete(
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
                LOG_SERROR1("Couldn't remove multicast-session memory-file \"%s\"",
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
 * Opens a multicast session memory. This function is reentrant.
 *
 * @param[in] servAddr  Address of the server.
 * @param[in] feedtype  Feedtype of the multicast group.
 * @retval    NULL      Error. `log_add()` called.
 * @return              Pointer to a multicast session memory object.
 */
McastSessionMemory*
msm_open(
    const ServiceAddr* const servAddr,
    const feedtypet          feedtype)
{
    McastSessionMemory* msm = LOG_MALLOC(sizeof(McastSessionMemory),
            "multicast session memory");

    if (msm) {
        if (!init(msm, servAddr, feedtype)) {
            free(msm);
            msm = NULL;
        }
    }

    return msm;
}

/**
 * Closes a multicast session memory. Upon successful return, the multicast
 * session memory of a subsequent identical `msm_open()` will comprise that of
 * the previous `msm_open()` as subsequently modified prior to calling this
 * function. This function is thread-compatible but not thread-safe for the
 * same argument.
 *
 * @param[in] msm    The multicast session memory, returned by `msm_open()`, to
 *                   be closed. Use of this object upon successful return from
 *                   this function results in undefined behavior.
 * @retval    true   Success.
 * @retval    false  Failure. `log_start()` called. `msm` is unmodified.
 */
bool
msm_close(
    McastSessionMemory* const msm)
{
    if (msm->modified && !dump(msm))
        return false;

    (void)pthread_mutex_destroy(&msm->mutex);
    piq_free(msm->requestedQ);
    piq_free(msm->missedQ);
    free(msm->path);
    free(msm->tmpPath);
    free(msm);

    return true;
}

/**
 * Sets the signature of the last data-product received via multicast in a
 * multicast session memory. It is unspecified when the memory-file is
 * modified.
 *
 * @param[in] msm    The multicast session memory.
 * @param[in] sig    Signature of the last data-product received via multicast.
 * @retval    true   Success.
 * @retval    false  Failure. `log_start()` called. The multicast session memory
 *                   is unmodified. Thread-safe.
 */
bool
msm_setLastMcastProd(
    McastSessionMemory* const restrict msm,
    const signaturet                   sig)
{
    lock(msm);
    (void)memcpy(&msm->lastMcastProd, sig, sizeof(signaturet));
    msm->sigSet = true;
    msm->modified = true;
    unlock(msm);
    return true;
}

/**
 * Returns the signature of the last data-product received via multicast of a
 * multicast session memory. Thread-safe.
 *
 * @param[in]  msm    The multicast session memory.
 * @param[out] sig    Signature of the last data-product received via multicast.
 * @retval     true   Success. `sig` is set.
 * @retval     false  The signature doesn't exist. `sig` is unaltered.
 */
bool
msm_getLastMcastProd(
    McastSessionMemory* const restrict msm,
    signaturet                         sig)
{
    lock(msm);
    bool sigSet = msm->sigSet;
    if (sigSet)
        (void)memcpy(sig, &msm->lastMcastProd, sizeof(signaturet));
    unlock(msm);

    return sigSet;
}

/**
 * Clears the list of files in a multicast session memory that were missed by
 * the multicast receiver: both the missed-but-not-requested and
 * requested-but-not-received queues are cleared. Idempotent. Thread-safe.
 *
 * @param[in] msm  The multicast session memory.
 */
void
msm_clearAllMissedFiles(
    McastSessionMemory* const restrict msm)
{
    lock(msm);
    msm->modified = piq_clear(msm->requestedQ) != 0 ||
            piq_clear(msm->missedQ) != 0;
    unlock(msm);
}

/**
 * Removes and returns the index of a product that has not been received by
 * the multicast receiver associated with a multicast session memory. The
 * requested-but-not-received queue is tried first; then the
 * missed-but-not-requested queue. Thread-safe.
 *
 * @param[in] msm     The multicast session memory.
 * @param[in] iProd   The index of the missed product.
 * @retval    true    Such an index exists. `*iProd` is set.
 * @retval    false   No such index (the queues are empty).
 */
bool
msm_getAnyMissedFileNoWait(
    McastSessionMemory* const restrict msm,
    VcmtpProdIndex* const restrict     iProd)
{
    lock(msm);
    bool exists = piq_removeNoWait(msm->requestedQ, iProd) == 0 ||
            piq_removeNoWait(msm->missedQ, iProd) == 0;
    unlock(msm);

    return exists;
}

/**
 * Adds an index of a product that was missed by the multicast receiver but
 * has not yet been requested to the current list of such files in a multicast
 * session memory. Thread-safe.
 *
 * @param[in] msm    The multicast session memory.
 * @param[in] id     The product-index to add.
 * @retval    true   Success.
 * @retval    false  Error. `log_start()` called.
 */
bool
msm_addMissedFile(
    McastSessionMemory* const restrict msm,
    const VcmtpProdIndex               iProd)
{
    return addFile(msm, msm->missedQ, iProd); // locks and unlocks `msm`
}

/**
 * Adds an index of a product that was missed by the multicast receiver and
 * has been requested from the upstream LDM-7 to the current list of such
 * products in a multicast session memory. Thread-safe.
 *
 * @param[in] msm    The multicast session memory.
 * @param[in] id     The product-index to add.
 * @retval    true   Success.
 * @retval    false  Error. `log_start()` called.
 */
bool
msm_addRequestedFile(
    McastSessionMemory* const restrict msm,
    const VcmtpProdIndex               iProd)
{
    return addFile(msm, msm->requestedQ, iProd); // locks and unlocks `msm`
}

/**
 * Returns (but doesn't remove) the next product-index from the
 * missed-but-not-requested queue of a multicast session memory. Blocks until
 * such a file is available. Thread-safe.
 *
 * @param[in] msm     The multicast session memory.
 * @param[in] iProd   The product-index.
 * @retval    true    Success. `*iProd` is set.
 * @retval    false   The queue has been shutdown.
 */
bool
msm_peekMissedFileWait(
    McastSessionMemory* const restrict msm,
    VcmtpProdIndex* const restrict     iProd)
{
    return piq_peekWait(msm->missedQ, iProd) == 0;
}

/**
 * Returns (but doesn't remove) the next product-index from the
 * missed-but-not-requested queue of a multicast session memory. Thread-safe.
 *
 * @param[in] msm     The multicast session memory.
 * @param[in] iProd   The product-index.
 * @retval    true    The index exists. `*iProd` is set.
 * @retval    false   No such index (the queue is empty).
 */
bool
msm_peekMissedFileNoWait(
    McastSessionMemory* const restrict msm,
    VcmtpProdIndex* const restrict     iProd)
{
    return piq_peekNoWait(msm->missedQ, iProd) == 0;
}

/**
 * Removes and returns the next product-index from the
 * missed-but-not-requested queue of a multicast session memory. Thread-safe.
 *
 * @param[in] msm     The multicast session memory.
 * @param[in] iProd   The product-index.
 * @retval    true    The index exists. `*iProd` is set.
 * @retval    false   No such index (the queue is empty).
 */
bool
msm_removeMissedFileNoWait(
    McastSessionMemory* const restrict msm,
    VcmtpProdIndex* const restrict     iProd)
{
    return piq_removeNoWait(msm->missedQ, iProd) == 0;
}

/**
 * Returns (but doesn't remove) the next product-index from the
 * requested-but-not-received queue of a multicast session memory. Doesn't
 * block. Thread-safe.
 *
 * @param[in] msm     The multicast session memory.
 * @param[in] iProd   The product-index.
 * @retval    true    Success. `*iProd` is set.
 * @retval    false   No such identifier (the queue is empty).
 */
bool
msm_peekRequestedFileNoWait(
    McastSessionMemory* const restrict msm,
    VcmtpProdIndex* const restrict     iProd)
{
    return piq_peekNoWait(msm->requestedQ, iProd) == 0;
}

/**
 * Removes and returns the next product-index from the
 * requested-but-not-received queue of a multicast session memory. Thread-safe.
 *
 * @param[in] msm     The multicast session memory.
 * @param[in] iProd   The product-index.
 * @retval    true    The index exists. `*iProd` is set.
 * @retval    false   No such index (the queue is empty).
 */
bool
msm_removeRequestedFileNoWait(
    McastSessionMemory* const restrict msm,
    VcmtpProdIndex* const restrict     iProd)
{
    return piq_removeNoWait(msm->requestedQ, iProd) == 0;
}

/**
 * Shuts down the queue of missed-but-not-requested files in a multicast session
 * memory. Idempotent and thread-safe.
 *
 * @param[in] msm     The multicast session memory.
 */
void
msm_shutDownMissedFiles(
    McastSessionMemory* const restrict msm)
{
    (void)piq_cancel(msm->missedQ);
}
