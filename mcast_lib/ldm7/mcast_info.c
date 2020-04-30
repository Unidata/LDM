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

#include "InetSockAddr.h"
#include "inetutil.h"
#include "ldm.h"
#include "ldmprint.h"
#include "log.h"
#include "mcast_info.h"

#include <stdlib.h>
#include <string.h>
#include <xdr.h>

/*
 * IPv4 multicast address categories:
 *     224.0.0.0 - 224.0.0.255     Reserved for local purposes
 *     224.0.1.0 - 238.255.255.255 User-defined multicast addresses
 *     239.0.0.0 - 239.255.255.255 Reserved for administrative scoping
 *
 * Time-to-live of outgoing packets:
 *      0           Restricted to same host. Won't be output by any interface.
 *      1           Restricted to the same subnet. Won't be forwarded by a
 *                  router.
 *      2<=ttl<32   Restricted to the same site, organization or department.
 *     32<=ttl<64   Restricted to the same region.
 *     64<=ttl<128  Restricted to the same continent.
 *    128<=ttl<255  Unrestricted in scope. Global.
 */

/**
 * Initializes a multicast information object.
 *
 * @param[out] info       The multicast information object.
 * @param[in]  feed       The feedtype of the multicast group.
 * @param[in]  mcast      The Internet address of the multicast group. The
 *                        caller may free.
 * @param[in]  ucast      The Internet address of the unicast service for blocks
 *                        and files that are missed by the multicast receiver.
 *                        The caller may free.
 * @retval     true       Success. `info` is set.
 * @retval     false      Failure. \c log_add() called. The state of `info` is
 *                        indeterminate.
 */
bool
mi_init(
    McastInfo* const restrict  info,
    const feedtypet            feed,
    const char* const restrict mcast,
    const char* const restrict ucast)
{
    if ((info->group = strdup(mcast)) == NULL) {
        log_add("Couldn't copy multicast address");
        return false;
    }

    if ((info->server = strdup(ucast)) == NULL) {
        log_add("Couldn't copy unicast address");
        xdr_free(xdr_ServiceAddr, (char*)&info->group);
        return false;
    }

    info->feed = feed;

    return true; // success
}

/******************************************************************************
 * Public API:
 ******************************************************************************/

/**
 * Returns a new multicast information object.
 *
 * @param[out] mcastInfo  Initialized multicast information object. The caller
 *                        should call `mi_free(*mcastInfo)` when it's no longer
 *                        needed.
 * @param[in]  feed       The feedtype of the multicast group.
 * @param[in]  mcast      The Internet address of the multicast group. The
 *                        caller may free.
 * @param[in]  ucast      The Internet address of the unicast service for blocks
 *                        and files that are missed by the multicast receiver.
 *                        The caller may free.
 * @retval     0          Success. `*info` is set.
 * @retval     ENOMEM     Out-of-memory error. `log_add()` called.
 */
int
mi_new(
    McastInfo** const          mcastInfo,
    const feedtypet            feed,
    const char* const restrict mcast,
    const char* const restrict ucast)
{
    int              status;
    McastInfo* const info = log_malloc(sizeof(McastInfo),
            "multicast information");

    if (NULL == info) {
        status = ENOMEM;
    }
    else {
        if (!mi_init(info, feed, mcast, ucast)) {
            free(info);
            status = ENOMEM;
        }
        else {
            *mcastInfo = info;
            status = 0;
        }
    }   // `info` allocated

    return status;
}

/**
 * Destroys a multicast information object.
 *
 * @param[in] info  The multicast information object.
 */
void
mi_destroy(
    McastInfo* const info)
{
    free(info->group);
    free(info->server);
}

/**
 * Frees multicast information.
 *
 * @param[in,out] mcastInfo  Pointer to multicast information to be freed or
 *                           NULL. If non-NULL, then it must have been returned
 *                           by `mi_new()`.
 */
void
mi_free(
    McastInfo* const mcastInfo)
{
    if (mcastInfo) {
        mi_destroy(mcastInfo);
        free(mcastInfo);
    }
}

/**
 * Copies multicast information. Performs a deep copy. Destination fields are
 * not freed prior to copying.
 *
 * @param[out] to           Destination.
 * @param[in]  from         Source. The caller may free.
 * @retval     0            Success.
 * @retval     LDM7_SYSTEM  System error. \c log_add() called.
 */
Ldm7Status
mi_copy(
    McastInfo* const restrict       to,
    const McastInfo* const restrict from)
{
    return mi_init(to, from->feed, from->group, from->server)
            ? 0 : LDM7_SYSTEM;
}

/**
 * Clones a multicast information object.
 *
 * @param[in] info  Multicast information object to be cloned.
 * @retval    NULL  Failure. `log_add()` called.
 * @return          Clone of multicast information object. Caller should call
 *                  `mi_free()` when the clone is no longer needed.
 */
McastInfo*
mi_clone(
    const McastInfo* const info)
{
    McastInfo* clone;

    return mi_new(&clone, info->feed, info->group, info->server)
            ? NULL
            : clone;
}

/**
 * Replaces the Internet identifier of the TCP server. The previous identifier
 * is freed.
 *
 * @param[in,out] info         Multicast information to be modified.
 * @param[in]     id           Replacement Internet identifier.
 * @retval        0            Success. `info->server.inetId` was freed and now
 *                             points to a copy of `id`.
 * @retval        LDM7_SYSTEM  System failure. `log_add()` called.
 */
Ldm7Status
mi_replaceServerId(
        McastInfo* const restrict  info,
        const char* const restrict id)
{
    char* const dup = strdup(id);

    if (NULL == dup) {
        log_add_syserr("Couldn't duplicate Internet identifier of TCP server");
        return LDM7_SYSTEM;
    }

    free(info->server);
    info->server = dup;

    return 0;
}

/**
 * Returns the feedtype of a multicast information object.
 *
 * @param[in] info  Multicast information object.
 * @return          Feedtype of the object.
 */
feedtypet
mi_getFeedtype(
        const McastInfo* const info)
{
    return info->feed;
}

/**
 * Compares the server information of two multicast information objects. Returns
 * a value less than, equal to, or greater than zero as the server information
 * in the first object is considered less than, equal to, or greater than the
 * server information in the second object, respectively. Server informations
 * are considered equal if their TCP server Internet identifiers and port
 * numbers are equal.
 *
 * @param[in] info1  First multicast information object.
 * @param[in] info2  Second multicast information object.
 * @retval    -1     First object is less than second.
 * @retval     0     Objects are equal.
 * @retval    +1     First object is greater than second.
 */
int
mi_compareServers(
    const McastInfo* const restrict info1,
    const McastInfo* const restrict info2)
{
    return strcmp(info1->server, info2->server);
}

/**
 * Compares the multicast group information of two multicast information
 * objects. Returns a value less than, equal to, or greater than zero as the
 * server information in the first object is considered less than, equal to, or
 * greater than the server information in the second object, respectively.
 * Multicast group informations are considered equal if their Internet
 * identifiers and port numbers are equal.
 *
 * @param[in] info1  First multicast group information object.
 * @param[in] info2  Second multicast group information object.
 * @retval    -1     First object is less than second.
 * @retval     0     Objects are equal.
 * @retval    +1     First object is greater than second.
 */
int
mi_compareGroups(
    const McastInfo* const restrict info1,
    const McastInfo* const restrict info2)
{
    return strcmp(info1->group, info2->group);
}

/**
 * Returns a formatted representation of a multicast information object that's
 * suitable for use as a filename.
 *
 * @param[in] info  The multicast information object.
 * @retval    NULL  Failure. `log_add()` called.
 * @return          A filename representation of `info`. Caller should free when
 *                  it's no longer needed.
 */
char*
mi_asFilename(
    const McastInfo* const info)
{
    const char* feedStr = s_feedtypet(info->feed);
    char*       toString = ldm_format(256, "%s_%s_%s", feedStr, info->group,
            info->server);

    return toString;
}

/**
 * Returns a formatted representation of a multicast information object.
 *
 * @param[in] info  The multicast information object.
 * @retval    NULL  Failure. `log_add()` called.
 * @return          A string representation of `info`. Caller should `free()`
 *                  it when it's no longer needed.
 */
char*
mi_format(
    const McastInfo* const info)
{
    char* string;
    char  feedStr[130];
    int   nbytes = ft_format(info->feed, feedStr, sizeof(feedStr));

    if (nbytes < 0 || nbytes >= sizeof(feedStr)) {
        log_add("Couldn't format feedtype %0x", info->feed);
        string = NULL;
    }
    else {
        string = ldm_format(256, "{feed=%s, group=%s, server=%s}",
                feedStr, info->group, info->server);
    }

    return string;
}

/******************************************************************************
 * Separated-out multicast information:
 ******************************************************************************/

struct sepMcastInfo {
    feedtypet     feed;
    InetSockAddr* mcastGrp;
    InetSockAddr* fmtpSrvr;
};

static Ldm7Status
smi_init(
        SepMcastInfo* const restrict       smi,
        const feedtypet                    feed,
        const InetSockAddr* const restrict mcastGrp,
        const InetSockAddr* const restrict fmtpSrvr)
{
    int status = LDM7_SYSTEM;

    smi->feed = feed;
    smi->fmtpSrvr = isa_clone(fmtpSrvr);

    if (smi->fmtpSrvr == NULL) {
        log_add("isa_clone() failure");
    }
    else {
        smi->mcastGrp = isa_clone(mcastGrp);

        if (smi->mcastGrp == NULL) {
            log_add("isa_clone() failure");
            isa_free(smi->fmtpSrvr);
        }
        else {
            status = 0;
        }
    } // `smi->fmtpSrvr` allocated

    return status;
}

SepMcastInfo*
smi_new(const feedtypet                    feed,
        const InetSockAddr* const restrict mcastGrp,
        const InetSockAddr* const restrict fmtpSrvr)
{
    SepMcastInfo* smi = log_malloc(sizeof(SepMcastInfo),
            "separated-out multicast information object");

    if (smi) {
        if (smi_init(smi, feed, mcastGrp, fmtpSrvr)) {
            free(smi);
            smi = NULL;
        }
    }

    return smi;
}

/**
 * Initializes a separated-out multicast information object.
 *
 * @param[in,out] smi
 * @param[in]     feed         LDM7 feed
 * @param[in]     mcastGrpStr  String representation of the multicast group
 *                             address in the form
 *                               - <name>[:<port>]
 *                               - <nnn.nnn.nnn.nnn>[:<port>]
 *                             The default port number is `LDM_PORT`. May be
 *                             freed.
 * @param[in]     fmtpSrvrStr  String representation of the FMTP server address
 *                             in the form
 *                               - <name>[:<port>]
 *                               - <nnn.nnn.nnn.nnn>[:<port>]
 *                             The default port number is 0. May be freed.
 * @retval         0           Success
 * @retval         -1          Failure. `log_add()` called.
 */
static int
smi_initFromStr(
        SepMcastInfo* const smi,
        const feedtypet     feed,
        const char* const   mcastGrpStr,
        const char* const   fmtpSrvrStr)
{
    int                 status = -1;
    InetSockAddr* const mcastGrp = isa_newFromId(mcastGrpStr, LDM_PORT);

    if (mcastGrp == NULL) {
        log_add("isa_newFromId() failure");
    }
    else {
        InetSockAddr* const fmtpSrvr = isa_newFromId(fmtpSrvrStr, 0);

        if (fmtpSrvr == NULL) {
            log_add("isa_newFromId() failure");
        }
        else {
            smi->feed = feed;
            smi->mcastGrp = mcastGrp;
            smi->fmtpSrvr = fmtpSrvr;
            status = 0;
        }

        if (status)
            isa_free(mcastGrp);
    } // `mcastGrp` allocated

    return status;
}

SepMcastInfo*
smi_newFromStr(
        const feedtypet     feed,
        const char* const   mcastGrpStr,
        const char* const   fmtpSrvrStr)
{
    SepMcastInfo* smi = log_malloc(sizeof(SepMcastInfo),
            "separated-out multicast information object");

    if (smi && smi_initFromStr(smi, feed, mcastGrpStr, fmtpSrvrStr)) {
        log_add("smi_initFromStr() failure");
        free(smi);
        smi = NULL;
    }

    return smi;
}

SepMcastInfo*
smi_clone(
        const SepMcastInfo* const smi)
{
    return smi_new(smi->feed, smi->mcastGrp, smi->fmtpSrvr);
}

void
smi_free(SepMcastInfo* const smi)
{
    if (smi) {
        isa_free(smi->fmtpSrvr);
        isa_free(smi->mcastGrp);
        free(smi);
    }
}

char*
smi_toString(const SepMcastInfo* const smi)
{
    char* const       feedStr = feedtypet_format(smi->feed);
    const char* const mcastGrpStr = isa_toString(smi->mcastGrp);
    const char* const fmtpSrvrStr = isa_toString(smi->fmtpSrvr);
    char* const       smiStr = ldm_format(256, "{feed=%s, mcastGrp=%s, "
            "fmtpSrvr=%s}", feedStr, mcastGrpStr, fmtpSrvrStr);

    free(feedStr);

    return smiStr;
}

void
smi_setFeed(
        SepMcastInfo* const smi,
        const feedtypet     feed)
{
    smi->feed = feed;
}

feedtypet
smi_getFeed(const SepMcastInfo* const smi)
{
    return smi->feed;
}

InetSockAddr*
smi_getMcastGrp(const SepMcastInfo* const smi)
{
    return smi->mcastGrp;
}

InetSockAddr*
smi_getFmtpSrvr(const SepMcastInfo* const smi)
{
    return smi->fmtpSrvr;
}
