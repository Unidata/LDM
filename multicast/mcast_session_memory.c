/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: down7.c
 * @author: Steven R. Emmerson
 *
 * This file implements a downstream LDM-7.
 */

#include "config.h"

#include "globals.h"
#include "inetutil.h"
#include "mcast_session_memory.h"
#include "ldmprint.h"
#include "log.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <yaml.h>

typedef struct MissedFileBlock {
    struct MissedFileBlock* next;
    VcmtpFileId*            fileIds;
    size_t                  count;
} MissedFileBlock;

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
    bool             sigSet; ///< whether or not `lastMcastProd` is set
    /**
     * The start of the list of identifiers of files that were missed by the
     * multicast receiver:
     */
    MissedFileBlock* head;
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
 * group.
 *
 * @param[in] servAddr  The address of the server associated with the multicast
 *                      group.
 * @param[in] mcastId   The multicast group.
 * @retval    NULL      Failure. `log_start()` called.
 * @return              The path of the corresponding memory-file.
 */
static char*
getSessionPath(
    const ServAddr* const servAddr,
    const char* const     mcastId)
{
    char*       path;
    char* const servAddrStr = sa_format(servAddr);

    if (servAddrStr) {
        path = ldm_format(256, "%s/%s_%s.yaml", getLdmLogDir(), servAddrStr,
                mcastId);
        free(servAddrStr);
    }

    return path;
}

/**
 * Initializes a multicast session memory.
 *
 * @param[in] msm       The muticast session memory to initialize.
 * @param[in] servAddr  Address of the server.
 * @param[in] mcastId   Identifier of the multicast group.
 * @retval    true      Success.
 * @retval    false     Failure. `log_add()` called.
 */
static bool
init(
    McastSessionMemory* const restrict msm,
    const ServAddr* const restrict     servAddr,
    const char* const restrict         mcastId)
{
    bool        success = false;
    char* const path = getSessionPath(servAddr, mcastId);

    if (path != NULL) {
        char* const tmpPath = ldm_format(256, "%s%s", path, ".new");

        if (tmpPath != NULL) {
            msm->path = path;
            msm->tmpPath = tmpPath;
            msm->sigSet = false;
            msm->head = NULL;
            success = true;
        }
    }

    // TODO: Load from existing memory-file

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
openMemoryFile(
    McastSessionMemory* const msm)
{
    FILE* file = fopen(msm->tmpPath, "w");

    if (file == NULL)
        LOG_SERROR1("Couldn't open temporary memory-file \"%s\"", msm->tmpPath);

    return file;
}

/**
 * Adds to a YAML sequence-node the list of files that were missed by the
 * multicast receiver associated with a multicast session memory.
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
    bool success = false;

    for (MissedFileBlock* block = msm->head; block; block = block->next) {
        for (VcmtpFileId* fileId = block->fileIds;
                fileId < block->fileIds + block->count; fileId++) {
            unsigned long id;
            char          buf[sizeof(id)*4+1]; // overly capacious

            (void)snprintf(buf, sizeof(buf), "%lu", id);

            int  item = yaml_document_add_scalar(document, NULL, buf, -1,
                    YAML_PLAIN_SCALAR_STYLE);

            if (!item) {
                LOG_START0("yaml_document_add_scalar() failure");
                return false;
            }
            else {
                if (!yaml_document_append_sequence_item(document, seq, item)) {
                    LOG_START0("yaml_document_append_sequence_item() failure");
                    return false;
                }
            }
        }
    }

    return true;
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
    bool success = false;
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
                (msm->head == NULL || addMissedMcastFiles(msm, document, root));
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
 * Closes the memory-file of a multicast session memory.
 *
 * @param[in] msm       The multicast session memory.
 * @param[in] goodDump  Was the dump of memory successful?
 * @retval    true      Success.
 * @retval    false     Failure. `log_start()` called.
 */
static bool
closeMemoryFile(
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
    FILE* file = openMemoryFile(msm);

    if (file) {
        if (dumpMemory(msm, file))
            success = true;

        if (!closeMemoryFile(msm, file, success))
            success = false;
    }

    return success;
}

/******************************************************************************
 * Public API
 ******************************************************************************/

/**
 * Opens a multicast session memory.
 *
 * @param[in] servAddr  Address of the server.
 * @param[in] mcastId   Identifier of the multicast group.
 * @retval    NULL      Error. `log_add()` called.
 * @return              Pointer to a multicast session memory object.
 */
McastSessionMemory*
msm_open(
    const ServAddr* const servAddr,
    const char* const     mcastId)
{
    McastSessionMemory* msm = LOG_MALLOC(sizeof(McastSessionMemory),
            "multicast session memory");

    if (msm) {
        if (!init(msm, servAddr, mcastId)) {
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
 * function.
 *
 * @param[in] msm    The multicast session memory to be closed. Use of this
 *                   object upon successful return from this function results in
 *                   undefined behavior.
 * @retval    true   Success.
 * @retval    false  Failure. `log_start()` called. `msm` is unmodified.
 */
bool
msm_close(
    McastSessionMemory* const msm)
{
    if (!dump(msm))
        return false;

    for (MissedFileBlock *next, *block = msm->head; block; block = next) {
        next = block->next;
        free(block->fileIds);
        free(block);
    }

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
 *                   is unmodified.
 */
bool
msm_setLastMcastProd(
    McastSessionMemory* const restrict msm,
    const signaturet                   sig)
{
    (void)memcpy(&msm->lastMcastProd, sig, sizeof(signaturet));
    msm->sigSet = true;
    return true;
}

/**
 * Returns the signature of the last data-product received via multicast of a
 * multicast session memory.
 *
 * @param[in]  msm    The multicast session memory.
 * @param[out] sig    Signature of the last data-product received via multicast.
 * @retval     true   Success. `sig` is set.
 * @retval     false  Failure. `log_start()` called. `sig` is unaltered.
 */
bool
msm_getLastMcastProd(
    McastSessionMemory* const restrict msm,
    signaturet                         sig)
{
    if (msm->sigSet) {
        (void)memcpy(sig, &msm->lastMcastProd, sizeof(signaturet));
        return true;
    }

    LOG_START1("Signature of last multicast product not set for \"%s\"",
            msm->path);
    return false;
}
