/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * @file PerProdNotifier.cpp
 *
 * This file defines a class that notifies the receiving application about
 * events on a per-product basis.
 *
 * @author: Steven R. Emmerson
 */

#include "PerProdNotifier.h"
#include "mcast.h"
#include "log.h"

#include <stdlib.h>
#include <stdexcept>
#include <strings.h>

PerProdNotifier::PerProdNotifier(
    BopFunc             bop_func,
    EopFunc             eop_func,
    MissedProdFunc      missed_prod_func,
    void*               obj)
:
    bop_func(bop_func),
    eop_func(eop_func),
    missed_prod_func(missed_prod_func),
    obj(obj)
{
    if (!bop_func)
        throw std::invalid_argument("Null argument: bof_func");
    if (!eop_func)
        throw std::invalid_argument("Null argument: eof_func");
    if (!missed_prod_func)
        throw std::invalid_argument("Null argument: missed_prod_func");
}

/**
 * Notifies the receiving application about a product that is about to be
 * received by the VCMTP layer.
 *
 * @param[in,out] prod_entry            The VCMTP product-entry.
 * @retval        0                     Success.
 * @throws        std::runtime_error    if the receiving application indicates
 *                                      an error.
 */
void PerProdNotifier::notify_of_bop(
        const size_t   prodSize,
        void* const    metadata,
        const unsigned metaSize,
        void** const   data)
{
    if (bop_func(obj, prodSize, metadata, metaSize, data)) {
        throw std::runtime_error(std::string(
                "Error notifying receiving application of beginning of product"));
    }
}

void PerProdNotifier::notify_of_eop()
{
    if (eop_func(obj)) {
        throw std::runtime_error(std::string(
                "Error notifying receiving application of end of product"));
    }
}

void PerProdNotifier::notify_of_missed_prod(const McastProdIndex iProd)
{
    missed_prod_func(obj, iProd);
}
