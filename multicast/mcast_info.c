/**
 * Copyright 2014 University Corporation for Atmospheric Research.
 * All rights reserved. See file COPYRIGHT in the top-level source-directory
 * for legal conditions.
 *
 *   @file multicast_info.c
 * @author Steven R. Emmerson
 *
 * This file defines the multicast information returned by a server.
 */

#include "config.h"

#include "ldm7.h"
#include "mcast_info.h"

#include <stdlib.h>
#include <xdr.h>

/**
 * Frees multicast information.
 *
 * @param[in,out] mcastInfo  Pointer to multicast information to be freed or
 *                           NULL.
 */
void mcastInfo_free(
    McastGroupInfo* const mcastInfo)
{
    if (mcastInfo) {
        (void)xdr_free(xdr_McastGroupInfo, (char*)mcastInfo);
        free(mcastInfo);
    }
}
