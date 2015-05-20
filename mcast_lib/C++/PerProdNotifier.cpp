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

#define restrict

#include "PerProdNotifier.h"
#include "mcast.h"
#include "mldm_receiver.h"
#include "log.h"

#include <stdlib.h>
#include <stdexcept>
#include <strings.h>

int ppn_new(
        void** const            ppn,
        BopFunc const           bop_func,
        EopFunc const           eop_func,
        MissedProdFunc const    missed_prod_func,
        Mlr* const              mlr)
{
    int status;
    try {
        *ppn = new PerProdNotifier(bop_func, eop_func, missed_prod_func, mlr);
        status = 0;
    }
    catch (...) {
        LOG_SERROR0("Couldn't allocate per-product-notifier");
        status = ENOMEM;
    }
    return status;
}

void ppn_free(
        void* const ppn)
{
    delete (PerProdNotifier*)ppn;
}

PerProdNotifier::PerProdNotifier(
    BopFunc             bop_func,
    EopFunc             eop_func,
    MissedProdFunc      missed_prod_func,
    Mlr*                mlr)
:
    bop_func(bop_func),
    eop_func(eop_func),
    missed_prod_func(missed_prod_func),
    mlr(mlr),
    prodInfos(16)
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
 * @param[in]   iProd                 VCMTP product-index.
 * @param[in]   prodSize              The size of the product in bytes.
 * @param[in]   metadata              The product's metadata. Ignored if
 *                                    `metaSize == 0`.
 * @param[in]   metaSize              The size of the product's metadata in
 *                                    bytes.
 * @param[out]  data                  The start location for writing the
 *                                    product.
 * @retval      0                     Success.
 * @throws      std::runtime_error    if the receiving application indicates
 *                                    an error.
 */
void PerProdNotifier::notify_of_bop(
        const VcmtpProdIndex iProd,
        const size_t         prodSize,
        void* const          metadata,
        const unsigned       metaSize,
        void** const         data)
{
    pqe_index pqeIndex;

    if (bop_func(mlr, prodSize, metadata, metaSize, data, &pqeIndex))
        throw std::runtime_error(
                "Error notifying receiving application of beginning of product");

    ProdInfo& prodInfo = prodInfos[iProd];
    prodInfo.start = *data;
    prodInfo.size = prodSize;
    prodInfo.index = pqeIndex;
}

/**
 * @param[in] prodIndex  The VCMTP index of the product.
 * @throws std::out_of_range  if there's no entry for `prodIndex`
 */
void PerProdNotifier::notify_of_eop(
        const VcmtpProdIndex prodIndex)
{
    ProdInfo& prodInfo = prodInfos.at(prodIndex);

    if (eop_func(mlr, prodInfo.start, prodInfo.size, &prodInfo.index))
        throw std::runtime_error(std::string(
                "Error notifying receiving application of end of product"));

    (void)prodInfos.erase(prodIndex);
}

void PerProdNotifier::notify_of_missed_prod(const VcmtpProdIndex iProd)
{
    missed_prod_func(mlr, iProd);
}
