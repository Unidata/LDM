/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * @file LdmBofResponse.cpp
 *
 * This file defines the API for the response by the LDM to a beginning-of-file
 * notice from the FMTP layer.
 *
 * @author: Steven R. Emmerson
 */

#include "LdmBofResponse.h"
#include "log.h"
#include "BofResponse.h"
#include "pq.h"

/**
 * The class corresponding to a response from the LDM to a beginning-of-file
 * notice from the FMTP layer.
 */
class LdmBofResponse : public MemoryBofResponse {
public:
    LdmBofResponse(char* buf, size_t size, const pqe_index* ndx)
        : MemoryBofResponse(buf, size, true), regionIndex(*ndx) {}
    const pqe_index* getIndex() const {return &regionIndex;}
private:
    const pqe_index regionIndex;
};

/**
 * Returns a new LDM BOF response.
 *
 * @param[in] buf    The buffer into which to write the FMTP file.
 * @param[in] size   The size of the buffer in bytes.
 * @param[in] index  Information on the region in the LDM product-queue into
 *                   which the FMTP file will be written. The client may free.
 * @return           Pointer to a corresponding LDM BOF response. The client
 *                   should \c free() it when it is no longer needed.
 */
void*
ldmBofResponse_new(
    char* const            buf,
    const size_t           size,
    const pqe_index* const index)
{
    return new LdmBofResponse(buf, size, index);
}

/**
 * Returns information on the region of the product-queue associated with the
 * FMTP file.
 *
 * @param[in] ldmBofResponse  Pointer to an LDM BOF response.
 * @return                    Pointer to the associated region information.
 */
const pqe_index*
ldmBofResponse_getIndex(
    const void* ldmBofResponse)
{
    return ((LdmBofResponse*)ldmBofResponse)->getIndex();
}

/**
 * Returns the buffer associated with the FMTP file.
 *
 * @param[in] ldmBofResponse  Pointer to an LDM BOF response.
 * @return                    Pointer to the associated buffer.
 */
const char*
ldmBofResponse_getBuf(
    const void* ldmBofResponse)
{
    return ((const MemoryBofResponse*)ldmBofResponse)->getBuf();
}
