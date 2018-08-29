/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * @file RecvProxy.h
 *
 * This file declares the API for classes that notify a receiving application
 * about files.
 *
 * @author: Steven R. Emmerson
 */

#ifndef FMTP_RECEIVER_RECVPROXY_H_
#define FMTP_RECEIVER_RECVPROXY_H_


#include <stdint.h>
#include <sys/types.h>


/**
 * This base class notifies a receiving application about events.
 */
class RecvProxy {
public:
    virtual ~RecvProxy() {};        // definition must exist

    /**
     * Notifies the receiving application about the beginning of a product. This
     * method is thread-safe.
     *
     * @param[in]  iProd     FMTP product-index.
     * @param[in]  prodSize  Size of the product in bytes.
     * @param[in]  metadata  Application-level product metadata.
     * @param[in]  metaSize  Size of the metadata in bytes.
     * @param[out] data      Pointer to where FMTP should write subsequent
     *                       data. If `*data == nullptr`, then the data-product
     *                       should be ignored.
     */
    virtual void notify_of_bop(const uint32_t iProd, size_t prodSize,
            void* metadata, unsigned metaSize, void** data) = 0;

    /**
     * Notifies the receiving application about the complete reception of the
     * previous product. This method is thread-safe.
     */
    virtual void notify_of_eop(uint32_t iProd) = 0;

    /**
     * Notifies the receiving application about a product that the FMTP layer
     * missed. This method is thread-safe.
     *
     * @param[in] prodIndex  Index of the missed product.
     */
    virtual void notify_of_missed_prod(uint32_t prodIndex) = 0;
};


#endif /* FMTP_RECEIVER_RECVPROXY_H_ */
