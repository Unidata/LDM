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

#include "config.h"

#include "fmtp.h"
#include "ldmprint.h"
#include "log.h"
#include "mldm_receiver.h"
#include "PerProdNotifier.h"

#include <stdlib.h>
#include <stdexcept>
#include <strings.h>

PerProdNotifier::ProdInfo::ProdInfo()
:
    pqRegion(nullptr),
    size(0),
    index(),
    startTime{}
{}

PerProdNotifier::ProdInfo::~ProdInfo()
{
    pqRegion = nullptr;
}

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
        log_add_syserr("Couldn't allocate per-product-notifier");
        status = ENOMEM;
    }
    return status;
}

void ppn_free(
        void* ppn)
{
    delete static_cast<PerProdNotifier*>(ppn);
}

PerProdNotifier::PerProdNotifier(
    BopFunc             bop_func,
    EopFunc             eop_func,
    MissedProdFunc      missed_prod_func,
    Mlr*                mlr)
:
    mutex(),
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
 * received by the FMTP layer.
 *
 * @param[in]   start                 Time of start-of-transmission
 * @param[in]   iProd                 FMTP product-index.
 * @param[in]   prodSize              The size of the product in bytes.
 * @param[in]   metadata              The product's metadata. Ignored if
 *                                    `metaSize == 0`.
 * @param[in]   metaSize              The size of the product's metadata in
 *                                    bytes.
 * @param[out]  prodStart             The start location for writing the
 *                                    product.
 * @retval      0                     Success.
 * @throws      std::runtime_error    if the receiving application indicates
 *                                    an error.
 */
void PerProdNotifier::notify_of_bop(
        const struct timespec& startTime,
        const FmtpProdIndex    iProd,
        const size_t           prodSize,
        void* const            metadata,
        const unsigned         metaSize,
        void** const           pqRegion)
{
    pqe_index pqeIndex;

    char sigStr[2*sizeof(signaturet)+1];
    (void)sprint_signaturet(sigStr, sizeof(sigStr),
            (const unsigned char*)metadata);
    log_debug("Entered: prodIndex=%lu, prodSize=%zu, metaSize=%u, "
            "metadata=%s", (unsigned long)iProd, prodSize, metaSize, sigStr);

    if (bop_func(mlr, prodSize, metadata, metaSize, pqRegion,
            &pqeIndex)) {
        log_free(); // to prevent memory leak by FMTP thread
        throw std::runtime_error(
                "Error notifying receiving application about beginning-of-product");
    }

    if (*pqRegion == nullptr) {
        log_info("Duplicate product: prodIndex=%lu, prodSize=%zu, "
                "metaSize=%u, metadata=%s", (unsigned long)iProd, prodSize,
                metaSize, sigStr);
    }
    else {
        std::unique_lock<std::mutex> lock(mutex);
        if (prodInfos.count(iProd)) {
            // Exists
            log_info("Duplicate BOP: prodIndex=%lu, prodSize=%u",
                    (unsigned long)iProd, prodSize);
        }
        else {
            // Doesn't exist
            ProdInfo& prodInfo = prodInfos[iProd];
            prodInfo.pqRegion = *pqRegion; // can't be nullptr
            prodInfo.startTime = startTime;
            prodInfo.size = prodSize;
            prodInfo.index = pqeIndex;
        }
    }

    log_free(); // to prevent memory leak by FMTP thread
}

/**
 * @param[in] stopTime         When end-of-product message arrived
 * @param[in] prodIndex        The FMTP index of the product.
 * @throws std::runtime_error  Receiving application error.
 */
void PerProdNotifier::notify_of_eop(
        const struct timespec& stopTime,
        const FmtpProdIndex    prodIndex)
{
    log_debug("Entered: prodIndex=%lu", (unsigned long)prodIndex);

    std::unique_lock<std::mutex> lock(mutex);
    try {
        ProdInfo& prodInfo = prodInfos.at(prodIndex);
        double    duration = (stopTime.tv_sec - prodInfo.startTime.tv_sec) +
                (stopTime.tv_nsec - prodInfo.startTime.tv_nsec)/1e9;
        if (eop_func(mlr, prodInfo.pqRegion, prodInfo.size, &prodInfo.index,
                duration)) {
            log_free(); // to prevent memory leak by FMTP thread
            throw std::runtime_error(
                    "Error notifying receiving application about end-of-product");
        }
        (void)prodInfos.erase(prodIndex);
    }
    catch (const std::out_of_range& e) {
        log_warning("Unknown product-index: %lu", (unsigned long)prodIndex);
    }

    log_free(); // to prevent memory leak by FMTP thread
}

/**
 * @param[in] prodIndex       The FMTP product index.
 * @throws std::out_of_range  `prodIndex` is unknown.
 */
void PerProdNotifier::notify_of_missed_prod(const FmtpProdIndex prodIndex)
{
    std::unique_lock<std::mutex> lock(mutex);
    void* const                  prodStart = prodInfos[prodIndex].pqRegion;

    log_info("Missed product: prodIndex=%lu, prodStart=%p",
            (unsigned long)prodIndex, prodStart);

    missed_prod_func(mlr, prodIndex,
            prodStart ? &prodInfos[prodIndex].index : nullptr);

    (void)prodInfos.erase(prodIndex);

    log_free(); // to prevent memory leak by FMTP thread
}
