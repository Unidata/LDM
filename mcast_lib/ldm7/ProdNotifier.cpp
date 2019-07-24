/**
 * Notifies the receiving application about FMTP events on a per-product basis.
 *
 * Copyright 2019 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * @file ProdNotifier.cpp
 *
 * @author: Steven R. Emmerson
 */

#include "config.h"

#include "MldmRcvr.h"
#include "ProdNotifier.h"
#include "fmtp.h"
#include "ldmprint.h"
#include "log.h"

#include <stdlib.h>
#include <stdexcept>
#include <strings.h>

ProdNotifier::ProdInfo::ProdInfo()
:
    pqRegion(nullptr),
    size(0),
    index(),
    startTime{}
{}

ProdNotifier::ProdInfo::~ProdInfo()
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
        *ppn = new ProdNotifier(bop_func, eop_func, missed_prod_func, mlr);
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
    delete static_cast<ProdNotifier*>(ppn);
}

ProdNotifier::ProdNotifier(
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
 * @param[out]  pqRegion              The start location for writing the
 *                                    product or `NULL`, in which case the
 *                                    product should be ignored
 * @throws      std::runtime_error    if the receiving application indicates
 *                                    an error.
 */
void ProdNotifier::startProd(
        const struct timespec& startTime,
        const FmtpProdIndex    iProd,
        const size_t           prodSize,
        void* const            metadata,
        const unsigned         metaSize,
        void** const           pqRegion)
{
    try {
        if (log_is_enabled_debug) {
            char      sigStr[2*sizeof(signaturet)+1];
            (void)sprint_signaturet(sigStr, sizeof(sigStr),
                    (const unsigned char*)metadata);
            log_debug("Entered: prodIndex=%lu, prodSize=%zu, metaSize=%u, "
                    "metadata=%s", (unsigned long)iProd, prodSize, metaSize,
                    sigStr);
        }

        bool      found;
        ProdInfo* prodInfo;
        {
            std::unique_lock<std::mutex> lock(mutex);
            auto                         iter = prodInfos.find(iProd);

            found = iter != prodInfos.end();
            prodInfo = found ? &iter->second : &prodInfos[iProd];
        }

        if (found) {
            if (    prodInfo->startTime.tv_sec != startTime.tv_sec ||
                    prodInfo->startTime.tv_nsec != startTime.tv_nsec ||
                    prodInfo->size != prodSize ||
                    ::memcmp(prodInfo->index.signature, metadata,
                            sizeof(signaturet))) {
                throw std::runtime_error("ProdNotifier::startProd() "
                        "Product " + std::to_string(iProd) +
                        " BOP doesn't match previous BOP");
            }
        }
        else {
            pqe_index pqeIndex;
            int       status = bop_func(mlr, prodSize, metadata, metaSize,
                    pqRegion, &pqeIndex);

            if (status) {
                log_add("bop_func() failure on {iProd: %lu, prodSize: %zu, "
                		"metaSize: %u}", (unsigned long)iProd, prodSize,
						metaSize);

                if (status == E2BIG || status == EEXIST) {
                    log_flush_warning();
                    *pqRegion = nullptr; // Ignore this product
                }
                else {
                    log_flush_error();
                    throw std::runtime_error("ProdNotifier::startProd() Error "
                            "notifying LDM7 of beginning-of-product");
                }
            }
            else {
                prodInfo->pqRegion = *pqRegion; // Can't be `nullptr`
                prodInfo->startTime = startTime;
                prodInfo->size = prodSize;
                prodInfo->index = pqeIndex;
            } // `bop_func()` was successful
        } // No previous BOP for this product
    }
    catch (const std::exception& e) {
        log_free(); // to prevent memory leak by FMTP thread
        throw;
    }

    log_debug("Returning");
    log_free(); // to prevent memory leak by FMTP thread
}

/**
 * @param[in] stopTime         When end-of-product message arrived
 * @param[in] prodIndex        The FMTP index of the product.
 * @param[in] numRetrans       Number of FMTP data-block retransmissions
 * @throws std::runtime_error  Receiving application error.
 */
void ProdNotifier::endProd(
        const struct timespec& stopTime,
        const FmtpProdIndex    prodIndex,
        const uint32_t         numRetrans)
{
    log_debug("Entered: prodIndex=%lu", (unsigned long)prodIndex);

    try {
        ProdInfo prodInfo;
        {
            std::unique_lock<std::mutex> lock(mutex);
            prodInfo = prodInfos.at(prodIndex);
            (void)prodInfos.erase(prodIndex);
        }
        double   duration =
                (stopTime.tv_sec - prodInfo.startTime.tv_sec) +
                (stopTime.tv_nsec - prodInfo.startTime.tv_nsec)/1e9;

        if (eop_func(mlr, prodIndex, prodInfo.pqRegion, prodInfo.size,
                &prodInfo.index, duration, numRetrans)) {
            log_flush_error();
            log_free(); // to prevent memory leak by FMTP thread
            throw std::runtime_error(
                    "Error notifying receiving application about end-of-product");
        }
    }
    catch (const std::out_of_range& e) {
        log_add("Unknown product-index: %lu", (unsigned long)prodIndex);
        log_flush_error();
        log_free(); // to prevent memory leak by FMTP thread
        throw;
    }

    log_debug("Returning");
    log_free(); // to prevent memory leak by FMTP thread
}

/**
 * @param[in] prodIndex       The FMTP product index.
 * @throws std::out_of_range  `prodIndex` is unknown.
 */
void ProdNotifier::missedProd(const FmtpProdIndex prodIndex)
{
    log_debug("Entered: prodIndex=%lu", (unsigned long)prodIndex);

    try {
        void*     prodStart;
        pqe_index pqeIndex;
        bool      found;
        {
            std::lock_guard<std::mutex> lock(mutex);
            auto                        iter = prodInfos.find(prodIndex);

            found = iter != prodInfos.end();

            if (found) {
                prodStart = iter->second.pqRegion;
                pqeIndex = iter->second.index;
                prodInfos.erase(iter);
            }
        }

        log_info("Missed product: prodIndex=%lu, prodStart=%p",
                (unsigned long)prodIndex, prodStart);
        missed_prod_func(mlr, prodIndex, found ? &pqeIndex : nullptr);
    }
    catch (const std::exception& e) {
    	log_add(e.what());
    	log_flush_error();
        log_free(); // to prevent memory leak by FMTP thread
        throw;
    }

    log_flush_error(); // Just in case
    log_debug("Returning");
    log_free(); // to prevent memory leak by FMTP thread
}
