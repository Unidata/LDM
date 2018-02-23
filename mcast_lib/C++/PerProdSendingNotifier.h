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
#include "SendProxy.h"

#include <sys/types.h>

#include "Authorizer.h"

class PerProdSendingNotifier: public SendProxy {
    /// Function to call when the FMTP layer is done with a product.
    void     (*eop_func)(FmtpProdIndex prodIndex);

    /// Authorization database
    Authorizer authorizer;

public:
    /**
     * Constructs from the notification functions.
     *
     * @param[in] eop_func              Function to call when the FMTP layer is
     *                                  finished with a product.
     * @throws    std::invalid_argument if `eop_func == NULL`.
     */
    PerProdSendingNotifier(
            void      (*eop_func)(FmtpProdIndex prodIndex),
            Authorizer& authDb);

    ~PerProdSendingNotifier() {}
    void notify_of_eop(FmtpProdIndex prodIndex);

    /**
     * Requests the application to verify an incoming connection request,
     * and to decide whether to accept or to reject the connection. This
     * method is thread-safe.
     * @return    true: receiver accepted; false: receiver rejected.
     */
    bool verify_new_recv(int newsock);
};

#endif /* PER_PROD_SENDING_NOTIFIER_H_ */
