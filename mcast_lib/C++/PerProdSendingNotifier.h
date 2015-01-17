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

#ifndef PER_PROD_SENDING_NOTIFIER_H_
#define PER_PROD_SENDING_NOTIFIER_H_

#include "mcast.h"
#include "SendAppNotifier.h"

#include <sys/types.h>

class PerProdSendingNotifier: public SendAppNotifier {
public:
    /**
     * Constructs from the notification functions.
     *
     * @param[in] eop_func              Function to call when the VCMTP layer is
     *                                  finished with a product.
     * @throws    std::invalid_argument if `eop_func == NULL`.
     */
    PerProdSendingNotifier(
            void (*eop_func)(VcmtpProdIndex prodIndex));

    ~PerProdSendingNotifier() {}
    void notify_of_eop(VcmtpProdIndex prodIndex);

private:
    /**
     * Function to call when the VCMTP layer is done with a product.
     */
    void   (*eop_func)(VcmtpProdIndex prodIndex);
};

#endif /* PER_PROD_SENDING_NOTIFIER_H_ */
