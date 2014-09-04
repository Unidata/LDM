/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: file_id_map.h
 * @author: Steven R. Emmerson
 *
 * This file declares the API for the file-identifier map.
 */

#ifndef FILE_ID_MAP_H_
#define FILE_ID_MAP_H_

#include "ldm.h"

#ifdef __cplusplus
    extern "C" {
#endif

/**
 * Initializes this module for read and write access to a feedtype-specific
 * file-identifier map contained in a file. Creates the file if necessary.
 *
 * @param[in] pathname     Pathname of the file. Caller may free.
 * @param[in] maxSigs      Maximum number of data-product signatures.
 * @retval    0            Success.
 * @retval    LDM7_SYSTEM  System error. `log_add()` called. The state of the
 *                         file is unspecified.
 */
Ldm7Status
fim_openForWriting(
        const char* const pathname,
        const size_t      maxSigs);

/**
 * Opens the file-identifier map for reading. A process should call this
 * function at most once.
 *
 * @param[in] pathname     Pathname of the file. Caller may free.
 * @retval    0            Success.
 * @retval    LDM7_INVAL   Maximum number of signatures isn't positive.
 *                         `log_add()` called. The file wasn't opened or
 *                         created.
 * @retval    LDM7_SYSTEM  System error. `log_add()` called. The state of the
 *                         file is unspecified.
 */
Ldm7Status
fim_openForReading(
        const char* const pathname);

/**
 * Closes the file-identifier map.
 *
 * @retval 0            Success.
 * @retval LDM7_SYSTEM  SYSTEM error. `log_add()` called. The state of the map
 *                      is unspecified.
 */
Ldm7Status
fim_close(void);

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
        const signaturet* const sig);

/**
 * Returns the data-product signature to which a file-identifier maps.
 *
 * @param[in]  fileId       File-identifier.
 * @param[out] sig          Data-product signature mapped-to by `fileId`.
 * @return     0            Success.
 * @retval     LDM7_NOENT   File-identifier is unknown.
 * @retval     LDM7_SYSTEM  System error. `log_add()` called.
 */
Ldm7Status
fim_get(
        const McastFileId fileId,
        signaturet* const sig);

#ifdef __cplusplus
    }
#endif

#endif /* FILE_ID_MAP_H_ */
