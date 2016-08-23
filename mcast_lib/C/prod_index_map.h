/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: prod_index_map.h
 * @author: Steven R. Emmerson
 *
 * This file declares the API for the product-index map.
 */

#ifndef FILE_ID_MAP_H_
#define FILE_ID_MAP_H_

#include "mcast.h"
#include "ldm.h"

#ifdef __cplusplus
    extern "C" {
#endif

/**
 * Initializes this module for read and write access to a feedtype-specific
 * product-index map contained in a file. Creates the file if necessary.
 *
 * @param[in] dirname      Pathname of the parent directory or NULL, in which
 *                         case the current working directory is used. Caller
 *                         may free.
 * @param[in] feedtype     Feedtype of the map.
 * @param[in] maxSigs      Maximum number of data-product signatures.
 * @retval    0            Success.
 * @retval    LDM7_SYSTEM  System error. `log_add()` called. The state of the
 *                         file is unspecified.
 */
Ldm7Status
pim_openForWriting(
        const char* const dirname,
        const feedtypet   feedtype,
        const size_t      maxSigs);

/**
 * Opens the product-index map for reading. A process should call this
 * function at most once.
 *
 * @param[in] dirname      Pathname of the parent directory or NULL, in which
 *                         case the current working directory is used. Caller
 *                         may free.
 * @param[in] feedtype     Feedtype of the map.
 * @retval    0            Success.
 * @retval    LDM7_INVAL   Maximum number of signatures isn't positive.
 *                         `log_add()` called. The file wasn't opened or
 *                         created.
 * @retval    LDM7_SYSTEM  System error. `log_add()` called. The state of the
 *                         file is unspecified.
 */
Ldm7Status
pim_openForReading(
        const char* const dirname,
        const feedtypet   feedtype);

/**
 * Closes the product-index map.
 *
 * @retval 0            Success.
 * @retval LDM7_SYSTEM  SYSTEM error. `log_add()` called. The state of the map
 *                      is unspecified.
 */
Ldm7Status
pim_close(void);

/**
 * Deletes the file associated with a product-index map. Any instance of this
 * module that has the file open will continue to work until its `pim_close()`
 * is called.
 *
 * @param[in] dirname    Pathname of the parent directory or NULL, in which case
 *                       the current workingi directory is used.
 * @param[in] feedtype   The feedtype.
 * @retval 0             Success. The associated file doesn't exist or has been
 *                       removed.
 * @retval LDM7_SYSTEM   System error. `log_add()` called.
 */
Ldm7Status
pim_delete(
        const char* const dirname,
        const feedtypet   feedtype);

/**
 * Adds a mapping from a product-index to a data-product signature to the
 * product-index map. Clears the map first if the given product-index is
 * not one greater than the previous product-index.
 *
 * @param[in] iProd        Product index.
 * @param[in] sig          Data-product signature.
 * @retval    0            Success.
 * @retval    LDM7_SYSTEM  System error. `log_add()` called.
 */
Ldm7Status
pim_put(
        const FmtpProdIndex    iProd,
        const signaturet* const sig);

/**
 * Returns the data-product signature to which a product-index maps.
 *
 * @param[in]  iProd        Product-index.
 * @param[out] sig          Data-product signature mapped-to by `fileId`.
 * @return     0            Success.
 * @retval     LDM7_NOENT   Product-index is unknown.
 * @retval     LDM7_SYSTEM  System error. `log_add()` called.
 */
Ldm7Status
pim_get(
        const FmtpProdIndex iProd,
        signaturet* const    sig);

/**
 * Returns the next product-index that should be put into the product-index
 * map. The product-index will be zero if the map is empty.
 *
 * @param[out] iProd        Next product-index.
 * @retval     0            Success. `*fileId` is set.
 * @retval     LDM7_SYSTEM  System error. `log_add()` called.
 */
Ldm7Status
pim_getNextProdIndex(
        FmtpProdIndex* const iProd);

#ifdef __cplusplus
    }
#endif

#endif /* FILE_ID_MAP_H_ */
