/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * @file PerProdNotifier.h
 *
 * This file declares the API for a class that notifies the receiving
 * application of events on a per-file basis.
 *
 * @author: Steven R. Emmerson
 */

#ifndef PER_PROD_NOTIFIER_H_
#define PER_PROD_NOTIFIER_H_

#include "mcast.h"
#include "mldm_receiver.h"
#include "pq.h"

#include <sys/types.h>

typedef int     (*BopFunc)(Mlr* mlr, size_t prodSize, const void* metadata,
                        unsigned metaSize, void** data, pqe_index* pqeIndex);
typedef int     (*EopFunc)(Mlr* mlr, void* prod, size_t prodSize,
                        pqe_index* pqeIndex);
typedef void    (*MissedProdFunc)(void* obj, const VcmtpProdIndex iProd);

#ifdef __cplusplus

#include "RecvAppNotifier.h"
#include <unordered_map>

class PerProdNotifier: public RecvAppNotifier {
public:
    /**
     * Constructs from the notification functions.
     *
     * @param[in] bof_func              Function to call when the beginning of
     *                                  a product has been seen by the VCMTP
     *                                  layer.
     * @param[in] eof_func              Function to call when a product has been
     *                                  completely received by the VCMTP layer.
     * @param[in] missed_prod_func      Function to call when a product is
     *                                  missed by the VCMTP layer.
     * @param[in] mlr                   Associated multicast LDM receiver.
     * @throws    std::invalid_argument if @code{!bof_func || !eof_func ||
     *                                  !missed_prod_func}
     */
    PerProdNotifier(
            BopFunc         bof_func,
            EopFunc         eof_func,
            MissedProdFunc  missed_prod_func,
            Mlr*            mlr);

    ~PerProdNotifier() {}
    void notify_of_bop(const VcmtpProdIndex iProd, size_t prodSize, void*
            metadata, unsigned metaSize, void** data);
    void notify_of_eop(VcmtpProdIndex prodIndex);
    void notify_of_missed_prod(VcmtpProdIndex prodIndex);

private:
    /**
     * C function to call when a beginning-of-product has been seen by the VCMTP
     * layer.
     */
    BopFunc             bop_func;
    /**
     * C function to call when a product has been completely received by the
     * VCMTP layer.
     */
    EopFunc             eop_func;
    /**
     * C function to call when a product is missed by the VCMTP layer.
     */
    MissedProdFunc      missed_prod_func;
    /**
     * Associated multicast LDM receiver.
     */
    Mlr*               mlr;

    // Map from product-index to useful information.
    typedef struct {
        void*     start; /**< Pointer to start of XDR-encoded product in
                              product-queue */
        size_t    size;  /**< Size of XDR-encoded product in bytes */
        pqe_index index; /**< Reference to allocated space in product-queue */
    } ProdInfo;
    std::unordered_map<VcmtpProdIndex, ProdInfo> prodInfos;
};

#endif

#ifdef __cplusplus
    extern "C" {
#endif

int ppn_new(
        void**            ppn,
        BopFunc           bop_func,
        EopFunc           eop_func,
        MissedProdFunc    missed_prod_func,
        Mlr*              obj);

void ppn_free(
        void* ppn);

#ifdef __cplusplus
}
#endif

#endif /* PER_PROD_NOTIFIER_H_ */
