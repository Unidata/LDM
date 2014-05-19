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

#include "ldm.h"
#include "log.h"
#include "mcast_info.h"

#include <stdlib.h>
#include <string.h>
#include <xdr.h>

/*
 * Multicast address categories:
 *     224.0.0.0 - 224.0.0.255     Reserved for local purposes
 *     224.0.1.0 - 238.255.255.255 User-defined multicast addresses
 *     239.0.0.0 - 239.255.255.255 Reserved for administrative scoping
 */

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

/**
 * Copies multicast information.
 *
 * @param[out] to           Destination.
 * @param[in]  from         Source.
 * @retval     0            Success.
 * @retval     LDM7_SYSTEM  System error. \c log_add() called.
 */
int
mcastInfo_copy(
    McastGroupInfo* const restrict       to,
    const McastGroupInfo* const restrict from)
{
    char* const restrict mcastName = strdup(from->mcastName);

    if (mcastName == NULL) {
        LOG_ADD1("Couldn't copy multicast group name \"%s\"", from->mcastName);
    }
    else {
        char* const restrict groupAddr = strdup(from->groupAddr);

        if (groupAddr == NULL) {
            LOG_ADD1("Couldn't copy multicast group address \"%s\"",
                    from->groupAddr);
        }
        else {
            to->mcastName = mcastName;
            to->groupAddr = groupAddr;
            to->groupPort = from->groupPort;
            return 0;
        } /* "groupAddr" allocated */

        free(mcastName);
    } /* "mcastName allocated */

    return LDM7_SYSTEM;
}
