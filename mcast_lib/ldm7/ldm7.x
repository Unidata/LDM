/*
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: ldm7.x
 * @author: Steven R. Emmerson
 *
 * This file declares the RPC/XDR interface of LDM-7.
 */


%#include <ldm.h>


#if defined(RPC_HDR) || defined(RPC_XDR)
%
%/*
% * Multicast subscription request:
% */
#endif
typedef string McastGroupName<>;


#if defined(RPC_HDR) || defined(RPC_XDR)
%
%/*
% * FMTP file identifier:
% */
#endif
typedef uint32_t FmtpFileId;


#if defined(RPC_HDR) || defined(RPC_XDR)
%
%/*
% * Successful multicast subscription return value:
% */
#endif
struct McastInfo {
    /*
     * Multicast group name:
     */
    McastGroupName  mcastName;
    /*
     * Hostname or formatted IP address of associated LDM-7 server (for missed
     * data-products).
     */
    string         serverAddr<>;
    /*
     * Port number of associated LDM-7 server in local byte order:
     */
    unsigned short serverPort;
    /*
     * Hostname or formatted IP address of associated multicast group.
     */
    string         groupAddr<>;
    /*
     * Port number of associated multicast group in local byte order:
     */
    unsigned short groupPort;
};


#if defined(RPC_HDR) || defined(RPC_XDR)
%
%/*
% * Discriminant for multicast subscription reply:
% */
#endif
enum SubscriptionStatus {
    LDM7_OK = 0, /* Success */
    LDM7_INVAL,  /* Invalid argument */
    LDM7_UNAUTH  /* Unauthorized */
};


#if defined(RPC_HDR) || defined(RPC_XDR)
%
%/*
% * Multicast subscription return values:
% */
#endif
union SubscriptionReply switch (SubscriptionStatus status) {
    case LDM7_OK:
        McastInfo groupInfo;
    case LDM7_INVAL:
        void;
    case LDM7_UNAUTH:
        void;
};


program LDMPROG {
    version SEVEN {
        /*
         * Downstream to upstream RPC messages:
         */
        SubscriptionReply SUBSCRIBE(McastGroupName) = 1;
        void              REQUEST_PRODUCT(FmtpFileId) = 2;
        /*
         * Upstream to downstream RPC messages:
         */
        void              DELIVER_PRODUCT(product) = 3;
    } = 7;
} = LDM_PROG; /* LDM = 300029, use 0x2ffffffe for experiments */
