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
#include "ReceivingApplicationNotifier.h"

#include <sys/types.h>

class PerProdNotifier: public ReceivingApplicationNotifier {
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
     * @param[in] obj                   Relevant object in the receiving
     *                                  application. May be NULL.
     * @throws    std::invalid_argument if @code{!bof_func || !eof_func ||
     *                                  !missed_prod_func}
     */
    PerProdNotifier(
            BopFunc         bof_func,
            EopFunc         eof_func,
            MissedProdFunc  missed_prod_func,
            void*           obj);

    ~PerProdNotifier() {}
    void notify_of_bop(size_t prodSize, void* metadata,
            unsigned metaSize, void** data);
    void notify_of_eop();
    void notify_of_missed_prod(uint32_t prodIndex);

private:
    /**
     * Function to call when a beginning-of-product has been seen by the VCMTP
     * layer.
     */
    BopFunc             bop_func;
    /**
     * Function to call when a product has been completely received by the VCMTP
     * layer.
     */
    EopFunc             eop_func;
    /**
     * Function to call when a product is missed by the VCMTP layer.
     */
    MissedProdFunc      missed_prod_func;
    /**
     * Extra argument passed to the above functions.
     */
    void*               obj;
};

#endif /* PER_PROD_NOTIFIER_H_ */
