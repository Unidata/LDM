/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * @file PerProdSendingNotifier.cpp
 *
 * This file defines a class that notifies the sending application about
 * events on a per-product basis.
 *
 * @author: Steven R. Emmerson
 */

#include "PerProdSendingNotifier.h"
#include "mcast.h"
#include "mylog.h"

#include <stdlib.h>
#include <stdexcept>
#include <strings.h>

PerProdSendingNotifier::PerProdSendingNotifier(
    void (*eop_func)(VcmtpProdIndex iProd))
:
    eop_func(eop_func)
{
    if (!eop_func)
        throw std::invalid_argument("Null argument: eop_func");
}

/**
 * Notifies the sending application when the VCMTP layer is done with a product.
 *
 * @param[in,out] prodIndex             Index of the product.
 */
void PerProdSendingNotifier::notify_of_eop(
        const VcmtpProdIndex prodIndex)
{
    eop_func(prodIndex);
}
