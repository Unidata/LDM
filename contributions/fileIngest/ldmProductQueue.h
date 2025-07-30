/*
 *   Copyright 2011, University Corporation for Atmospheric Research.
 *   See file COPYRIGHT for copying and redistribution conditions.
 */

#ifndef LDM_PRODUCT_QUEUE_H
#define LDM_PRODUCT_QUEUE_H

#include "ldm.h"

typedef struct LdmProductQueue  LdmProductQueue;

/**
 * Returns the pathname of the LDM product-queue.
 *
 * @return Pathname of the LDM product-queue.
 */
const char* lpqGetQueuePath(void);

/**
 * Returns the LDM product-queue that corresponds to a pathname.
 *
 * This function is thread-safe.
 *
 * @retval 0    Success.
 * @retval 1    Precondition failure. \link log_start() \endlink called.
 * @retval 2    O/S failure. \link log_start() \endlink called.
 * @retval 3    Couldn't open product-queue. \link log_start() \endlink called.
 */
int lpqGet(
    const char*             pathname,   /**< [in] LDM product-queue pathname or
                                          *  NULL to obtain the default queue */
    LdmProductQueue** const lpq)        /**< [out] Pointer to pointer to be set
                                         *  to address of corresponding LDM
                                         *  product-queue. */;

/**
 * Inserts a data-product into an LDM product-queue.
 *
 * This function is thread-safe.
 *
 * @retval 0    Success. Product inserted into queue.
 * @retval 1    Precondition failure. \link log_start() \endlink called.
 * @retval 2    O/S error. \link log_start() \endlink called.
 * @retval 3    Product already in queue.
 * @retval 4    Product-queue error. \link log_start() \endlink called.
 */
int lpqInsert(
    LdmProductQueue* const  lpq,    /**< LDM product-queue to insert data-
                                     *   product into. */
    const product* const    prod)   /**< LDM data-product to be inserted */;

/**
 * Closes an LDM product-queue.
 *
 * This function is thread-safe.
 *
 * @retval 0    Success. Product inserted into queue.
 * @retval 1    Precondition failure. \link log_start() \endlink called.
 * @retval 2    O/S error. \link log_start() \endlink called.
 */
int lpqClose(
    LdmProductQueue* const  lpq)    /**< LDM product-queue */;

#endif
