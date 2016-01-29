/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: mldm_sender.c
 * @author: Steven R. Emmerson
 *
 * This file implements the multicast LDM sender, which is a program for
 * multicasting LDM data-products from the LDM product-queue to a multicast
 * group using VCMTP.
 */

#include "config.h"

#include "atofeedt.h"
#include "prod_index_map.h"
#include "globals.h"
#include "inetutil.h"
#include "ldm.h"
#include "ldmprint.h"
#include "log.h"
#include "mcast.h"
#include "mcast_info.h"
#include "OffsetMap.h"
#include "pq.h"
#include "prod_class.h"
#include "StrBuf.h"
#include "timestamp.h"

#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <limits.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef _XOPEN_PATH_MAX
/* For some reason, the following isn't defined by gcc(1) 4.8.3 on Fedora 19 */
#   define _XOPEN_PATH_MAX 1024 // value mandated by XPG6; includes NUL
#endif

#define NELT(a) (sizeof(a)/sizeof((a)[0]))

/**
 * C-callable multicast sender.
 */
static McastSender*          mcastSender;
/**
 * Information on the multicast group.
 */
static McastInfo             mcastInfo;
/**
 * Termination signals.
 */
static const int             termSigs[] = {SIGHUP, SIGINT, SIGTERM};
/**
 * Signal-set for termination signals.
 */
static sigset_t              termSigSet;
/**
 * VCMTP product-index to product-queue offset map.
 */
static OffMap*               offMap;

/**
 * Blocks termination signals for the current thread.
 */
static inline void
blockTermSigs(void)
{
    (void)pthread_sigmask(SIG_BLOCK, &termSigSet, NULL);
}

/**
 * Unblocks termination signals for the current thread.
 */
static inline void
unblockTermSigs(void)
{
    (void)pthread_sigmask(SIG_UNBLOCK, &termSigSet, NULL);
}

/**
 * Appends a usage message to the pending log messages.
 */
static void
mls_usage(void)
{
    log_add("\
Usage: %s [options] groupId:groupPort\n\
Options:\n\
    -f feedExpr       Feedtype expression specifying data to send. Default\n\
                      is EXP.\n\
    -l logfile        Log file pathname or '-' for standard error stream.\n\
                      Default depends on standard error stream:\n\
                          is tty     => use standard error stream\n\
                          is not tty => use system logging daemon.\n\
    -m mcastIf        IP address of interface to use to send multicast\n\
                      packets. Default is the system's default multicast\n\
                      interface.\n\
    -p serverPort     Port number for VCMTP TCP server. Default is chosen by\n\
                      operating system.\n\
    -q prodQueue      Pathname of product-queue. Default is \"%s\".\n\
    -s serverIface    IP Address of interface on which VCMTP TCP server will\n\
                      listen. Default is all interfaces.\n\
    -t ttl            Time-to-live of outgoing packets (default is 1):\n\
                           0  Restricted to same host. Won't be output by\n\
                              any interface.\n\
                           1  Restricted to same subnet. Won't be\n\
                              forwarded by a router (default).\n\
                         <32  Restricted to same site, organization or\n\
                              department.\n\
                         <64  Restricted to same region.\n\
                        <128  Restricted to same continent.\n\
                        <255  Unrestricted in scope. Global.\n\
    -v                Verbose logging: log INFO level messages.\n\
    -x                Debug logging: log DEBUG level messages.\n\
    -y                Log using microsecond resolution.\n\
    -z                Log using ISO 8601 timestamps.\n\
Operands:\n\
    groupId:groupPort Internet service address of multicast group, where\n\
                      <groupId> is either group-name or dotted-decimal IPv4\n\
                      address and <groupPort> is port number.",
            log_get_id(), getDefaultQueuePath());
}

/**
 * Decodes the options of the command-line.
 *
 * @pre                      {`openulog()` has already been called.}
 * @param[in]  argc          Number of arguments.
 * @param[in]  argv          Arguments.
 * @param[out] feed          Feedtypes of data to be sent.
 * @param[out] serverIface   Interface on which VCMTP TCP server should listen.
 *                           Caller must not free.
 * @param[out] serverPort    Port number for VCMTP TCP server.
 * @param[out] ttl           Time-to-live of outgoing packets.
 *                                0  Restricted to same host. Won't be output by
 *                                   any interface.
 *                                1  Restricted to the same subnet. Won't be
 *                                   forwarded by a router (default).
 *                              <32  Restricted to the same site, organization
 *                                   or department.
 *                              <64  Restricted to the same region.
 *                             <128  Restricted to the same continent.
 *                             <255  Unrestricted in scope. Global.
 * @param[out] ifaceAddr     IP address of the interface to use to send
 *                           multicast packets.
 * @param[out] timeoutFactor Ratio of the duration that a data-product will
 *                           be held by the VCMTP layer before being released
 *                           after being multicast to the duration to
 *                           multicast the product. If negative, then the
 *                           default timeout factor is used.
 * @retval     0             Success. `*serverIface` or `*ttl` might not have
 *                           been set.
 * @retval     1             Invalid options. `log_add()` called.
 */
static int
mls_decodeOptions(
        int                            argc,
        char* const* const restrict    argv,
        feedtypet* const restrict      feed,
        const char** const restrict    serverIface,
        unsigned short* const restrict serverPort,
        unsigned* const restrict       ttl,
        const char** const restrict    ifaceAddr,
        float* const                   timeoutFactor)
{
    int          ch;
    extern int   opterr;
    extern int   optopt;
    extern char* optarg;

    opterr = 1; // prevent getopt(3) from trying to print error messages

    while ((ch = getopt(argc, argv, ":F:f:l:m:p:q:s:t:vxyz")) != EOF)
        switch (ch) {
        case 'f': {
            if (strfeedtypet(optarg, feed)) {
                log_add("Invalid feed expression: \"%s\"", optarg);
                return 1;
            }
            break;
        }
        case 'l': {
            (void)log_set_destination(optarg);
            break;
        }
        case 'm': {
            *ifaceAddr = optarg;
            break;
        }
        case 'p': {
            unsigned short port;
            int            nbytes;

            if (1 != sscanf(optarg, "%5hu %n", &port, &nbytes) ||
                    0 != optarg[nbytes]) {
                log_add("Couldn't decode TCP-server port-number option-argument "
                        "\"%s\"", optarg);
                return 1;
            }
            *serverPort = port;
            break;
        }
        case 'q': {
            setQueuePath(optarg);
            break;
        }
        case 's': {
            *serverIface = optarg;
            break;
        }
        case 't': {
            unsigned t;
            int      nbytes;
            if (1 != sscanf(optarg, "%3u %n", &t, &nbytes) ||
                    0 != optarg[nbytes]) {
                log_add("Couldn't decode time-to-live option-argument \"%s\"",
                        optarg);
                return 1;
            }
            if (t >= 255) {
                log_add("Invalid time-to-live option-argument \"%s\"",
                        optarg);
                return 1;
            }
            *ttl = t;
            break;
        }
        case 'v': {
            if (!log_is_enabled_info)
                (void)log_set_level(LOG_LEVEL_INFO);
            break;
        }
        case 'x': {
            (void)log_set_level(LOG_LEVEL_DEBUG);
            break;
        }
        case 'y': {
            log_set_options(LOG_MICROSEC);
            break;
        }
        case 'z': {
            log_set_options(LOG_ISO_8601);
            break;
        }
        case ':': {
            log_add("Option \"%c\" requires an argument", optopt);
            return 1;
        }
        default: {
            log_add("Unknown option: \"%c\"", optopt);
            return 1;
        }
        }

    return 0;
}

/**
 * Sets a service address.
 *
 * @param[in]  id           The Internet identifier. Either a name or a
 *                          formatted IP address.
 * @param[in]  port         The port number.
 * @param[out] serviceAddr  The Internet service address to be set.
 * @retval     0            Success. `*serviceAddr` is set.
 * @retval     1            Invalid argument. `log_add()` called.
 * @retval     2            System failure. `log_add()` called.
 */
static int
mls_setServiceAddr(
        const char* const    id,
        const unsigned short port,
        ServiceAddr** const  serviceAddr)
{
    int status = sa_new(serviceAddr, id, port);

    return (0 == status)
            ? 0
            : (EINVAL == status)
              ? 1
              : 2;
}

/**
 * Decodes the Internet service address of the multicast group.
 *
 * @param[in]  arg        Relevant operand.
 * @param[out] groupAddr  Internet service address of the multicast group.
 *                        Caller should free when it's no longer needed.
 * @retval     0          Success. `*groupAddr` is set.
 * @retval     1          Invalid operand. `log_add()` called.
 * @retval     2          System failure. `log_add()` called.
 */
static int
mls_decodeGroupAddr(
        char* const restrict         arg,
        ServiceAddr** const restrict groupAddr)
{
    int          status = sa_parse(groupAddr, arg);

    if (ENOMEM == status) {
        status = 2;
    }
    else if (status) {
        log_add("Invalid multicast group specification");
    }

    return status;
}

/**
 * Decodes the operands of the command-line.
 *
 * @param[in]  argc         Number of operands.
 * @param[in]  argv         Operands.
 * @param[out] groupAddr    Internet service address of the multicast group.
 *                          Caller should free when it's no longer needed.
 * @retval     0            Success. `*groupAddr`, `*serverAddr`, and `*feed`
 *                          are set.
 * @retval     1            Invalid operands. `log_add()` called.
 * @retval     2            System failure. `log_add()` called.
 */
static int
mls_decodeOperands(
        int                          argc,
        char* const* restrict        argv,
        ServiceAddr** const restrict groupAddr)
{
    int status;

    if (argc < 1) {
        log_add("Multicast group not specified");
        status = 1;
    }
    else {
        status = mls_decodeGroupAddr(*argv, groupAddr);
    }

    return status;
}

/**
 * Sets the multicast group information from command-line arguments.
 *
 * @param[in]  serverIface  Interface on which VCMTP TCP server should listen.
 *                          Caller must not free.
 * @param[in]  serverPort   Port number for VCMTP TCP server.
 * @param[in]  feed         Feedtype of multicast group.
 * @param[in]  groupAddr    Internet service address of multicast group.
 *                          Caller should free when it's no longer needed.
 * @param[out] mcastInfo    Information on multicast group.
 * @retval     0            Success. `*mcastInfo` is set.
 * @retval     1            Invalid argument. `log_add()` called.
 * @retval     2            System failure. `log_add()` called.
 */
static int
mls_setMcastGroupInfo(
        const char* const restrict        serverIface,
        const unsigned short              serverPort,
        const feedtypet                   feed,
        const ServiceAddr* const restrict groupAddr,
        McastInfo** const restrict        mcastInfo)
{
    ServiceAddr* serverAddr;
    int          status = mls_setServiceAddr(serverIface, serverPort,
            &serverAddr);

    if (0 == status) {
        status = mi_new(mcastInfo, feed, groupAddr, serverAddr) ? 2 : 0;
        sa_free(serverAddr);
    }

    return status;
}

/**
 * Decodes the command line.
 *
 * @param[in]  argc          Number of arguments.
 * @param[in]  argv          Arguments.
 * @param[out] mcastInfo     Multicast group information.
 * @param[out] ttl           Time-to-live of outgoing packets.
 *                                 0  Restricted to same host. Won't be output
 *                                    by any interface.
 *                                 1  Restricted to the same subnet. Won't be
 *                                    forwarded by a router (default).
 *                               <32  Restricted to the same site, organization
 *                                    or department.
 *                               <64  Restricted to the same region.
 *                              <128  Restricted to the same continent.
 *                              <255  Unrestricted in scope. Global.
 * @param[out] ifaceAddr     IP address of the interface from which multicast
 *                           packets should be sent or NULL to have them sent
 *                           from the system's default multicast interface.
 * @param[out] timeoutFactor Ratio of the duration that a data-product will
 *                           be held by the VCMTP layer before being released
 *                           after being multicast to the duration to
 *                           multicast the product. If negative, then the
 *                           default timeout factor is used.
 * @retval     0             Success. `*mcastInfo` is set. `*ttl` might be set.
 * @retval     1             Invalid command line. `log_add()` called.
 * @retval     2             System failure. `log_add()` called.
 */
static int
mls_decodeCommandLine(
        int                         argc,
        char* const* restrict       argv,
        McastInfo** const restrict  mcastInfo,
        unsigned* const restrict    ttl,
        const char** const restrict ifaceAddr,
        float* const                timeoutFactor)
{
    feedtypet      feed = EXP;
    const char*    serverIface = "0.0.0.0";     // default: all interfaces
    unsigned short serverPort = 0;              // default: chosen by O/S
    const char*    mcastIf = "0.0.0.0";         // default mcast interface
    int            status = mls_decodeOptions(argc, argv, &feed, &serverIface,
            &serverPort, ttl, &mcastIf, timeoutFactor);
    extern int     optind;

    if (0 == status) {
        ServiceAddr* groupAddr;

        argc -= optind;
        argv += optind;
        status = mls_decodeOperands(argc, argv, &groupAddr);

        if (0 == status) {
            status = mls_setMcastGroupInfo(serverIface, serverPort, feed,
                    groupAddr, mcastInfo);
            sa_free(groupAddr);

            if (0 == status)
                *ifaceAddr = mcastIf;
        }
    } // options decoded

    return status;
}

/**
 * Handles a signal by rotating the logging level.
 *
 * @param[in] sig  Signal to be handled. Ignored.
 */
static void
mls_rotateLoggingLevel(
        const int sig)
{
    log_roll_level();
}

/**
 * Handles a signal by setting the `done` flag.
 *
 * @param[in] sig  Signal to be handled.
 */
static void
mls_setDoneFlag(
        const int sig)
{
    if (sig == SIGTERM) {
        log_debug("SIGTERM");
    }
    else if (sig == SIGINT) {
        log_debug("SIGINT");
    }
    else {
        log_debug("Signal %d", sig);
    }
    done = 1;
}

/**
 * Sets signal handling.
 */
static void
mls_setSignalHandling(void)
{
    /*
     * Initialize signal-set for termination signals.
     */
    (void)sigemptyset(&termSigSet);
    for (int i = 0; i < NELT(termSigs); i++)
        (void)sigaddset(&termSigSet, termSigs[i]);

    /*
     * Establish signal handlers.
     */
    struct sigaction sigact;

    (void)sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;

    /*
     * Register termination signal-handler.
     */
    sigact.sa_handler = mls_setDoneFlag;
    (void)sigaction(SIGINT, &sigact, NULL);
    (void)sigaction(SIGTERM, &sigact, NULL);

    /*
     * Register logging-level signal-handler. Ensure that it only affects
     * logging by restarting any interrupted system call.
     */
    sigact.sa_flags |= SA_RESTART;
    sigact.sa_handler = mls_rotateLoggingLevel;
    (void)sigaction(SIGUSR2, &sigact, NULL);
}

/**
 * Returns the dotted-decimal IPv4 address of an Internet identifier.
 *
 * @param[in]  inetId       The Internet identifier. May be a name or an IPv4
 *                          address in dotted-decimal form.
 * @param[in]  desc         The description of the entity associated with the
 *                          identifier appropriate for the phrase "Couldn't get
 *                          address of ...".
 * @param[out] buf          The dotted-decimal IPv4 address corresponding to the
 *                          identifier. It's the caller's responsibility to
 *                          ensure that the buffer can hold at least
 *                          INET_ADDRSTRLEN bytes.
 * @retval     0            Success. `buf` is set.
 * @retval     LDM7_INVAL   The identifier cannot be converted to an IPv4
 *                          address because it's invalid or unknown.
 *                          `log_add()` called.
 * @retval     LDM7_SYSTEM  The identifier cannot be converted to an IPv4
 *                          address due to a system error. `log_add()` called.
 */
static Ldm7Status
mls_getIpv4Addr(
        const char* const restrict inetId,
        const char* const restrict desc,
        char* const restrict       buf)
{
    int status = getDottedDecimal(inetId, buf);

    if (status == 0)
        return 0;

    log_add("Couldn't get address of %s", desc);

    return (status == EINVAL || status == ENOENT)
            ? LDM7_INVAL
            : LDM7_SYSTEM;
}

/**
 * Opens the product-index map for updating. Creates the associated file if it
 * doesn't exist. The parent directory of the associated file is the parent
 * directory of the LDM product-queue.
 *
 * @param[in] feed         Feedtype to be multicast.
 * @param[in] maxSigs      Maximum number of signatures that the product-index
 *                         must contain.
 * @retval    0            Success.
 * @retval    LDM7_INVAL   Maximum number of signatures isn't positive.
 *                         `log_add()` called. The file wasn't opened or
 *                         created.
 * @retval    LDM7_SYSTEM  System error. `log_add()` called.
 */
static Ldm7Status
mls_openProdIndexMap(
        const feedtypet feed,
        const size_t    maxSigs)
{
    char pathname[_XOPEN_PATH_MAX];
    (void)strncpy(pathname, getQueuePath(), sizeof(pathname));
    return pim_openForWriting(dirname(pathname), feed, maxSigs);
}

/**
 * Accepts notification that the multicast layer is finished with a data-product
 * and releases associated resources.
 *
 * @param[in] prodIndex  Index of the product.
 */
static void
mls_doneWithProduct(
    const VcmtpProdIndex prodIndex)
{
    off_t offset;
    int   status = om_get(offMap, prodIndex, &offset);
    if (status) {
        log_error("Couldn't get file-offset corresponding to product-index %lu",
                (unsigned long)prodIndex);
    }
    else {
        status = pq_release(pq, offset);
        if (status) {
            log_error("Couldn't release data-product in product-queue "
                    "corresponding to file-offset %ld, product-index %lu",
                    (long)offset, (unsigned long)prodIndex);
        }
    }
}

/**
 * Initializes the resources of this module. Sets `mcastInfo`; in particular,
 * sets `mcastInfo.server.port` to the actual port number used by the VCMTP
 * TCP server (in case the number was chosen by the operating-system).
 *
 * @param[in] info           Information on the multicast group.
 * @param[in] ttl            Time-to-live of outgoing packets.
 *                                0  Restricted to same host. Won't be output
 *                                   by any interface.
 *                                1  Restricted to the same subnet. Won't be
 *                                   forwarded by a router (default).
 *                              <32  Restricted to the same site, organization
 *                                   or department.
 *                              <64  Restricted to the same region.
 *                             <128  Restricted to the same continent.
 *                             <255  Unrestricted in scope. Global.
 * @param[in] ifaceAddr      IP address of the interface to use to send
 *                           multicast packets. "0.0.0.0" obtains the default
 *                           multicast interface. Caller may free.
 * @param[in] timeoutFactor  Ratio of the duration that a data-product will
 *                           be held by the VCMTP layer before being released
 *                           after being multicast to the duration to
 *                           multicast the product. If negative, then the
 *                           default timeout factor is used.
 * @param[in] pqPathname     Pathname of product queue from which to obtain
 *                           data-products.
 * @retval    0              Success. `*sender` is set.
 * @retval    LDM7_INVAL     An Internet identifier couldn't be converted to an
 *                           IPv4 address because it's invalid or unknown.
 *                           `log_add()` called.
 * @retval    LDM7_MCAST     Failure in multicast system. `log_add()` called.
 * @retval    LDM7_SYSTEM    System error. `log_add()` called.
 */
static Ldm7Status
mls_init(
    const McastInfo* const restrict info,
    const unsigned                  ttl,
    const char*                     ifaceAddr,
    const float                     timeoutFactor,
    const char* const restrict      pqPathname)
{
    char serverInetAddr[INET_ADDRSTRLEN];
    int  status = mls_getIpv4Addr(info->server.inetId, "server",
            serverInetAddr);

    if (status)
        goto return_status;

    char groupInetAddr[INET_ADDRSTRLEN];
    if ((status = mls_getIpv4Addr(info->group.inetId, "multicast-group",
            groupInetAddr)))
        goto return_status;

    offMap = om_new();
    if (offMap == NULL) {
        log_add("Couldn't create prodIndex-to-offset map");
        status = LDM7_SYSTEM;
        goto return_status;
    }

    /*
     * Thread-safe because `mls_tryMulticast()` and `mls_doneWithProduct()`
     * might be executed on different threads.
     */
    if (pq_open(pqPathname, PQ_READONLY | PQ_THREADSAFE, &pq)) {
        log_add("Couldn't open product-queue \"%s\"", pqPathname);
        status = LDM7_SYSTEM;
        goto free_offMap;
    }

    if ((status = mls_openProdIndexMap(info->feed, pq_getSlotCount(pq))))
        goto close_pq;

    VcmtpProdIndex iProd;
    if ((status = pim_getNextProdIndex(&iProd)))
        goto close_prod_index_map;

    if (mi_copy(&mcastInfo, info)) {
        status = LDM7_SYSTEM;
        goto close_prod_index_map;
    }

    if ((status = mcastSender_spawn(&mcastSender, serverInetAddr,
            &mcastInfo.server.port, groupInetAddr, mcastInfo.group.port,
            ifaceAddr, ttl, iProd, timeoutFactor, mls_doneWithProduct))) {
        status = (status == 1)
                ? LDM7_INVAL
                : (status == 2)
                  ? LDM7_MCAST
                  : LDM7_SYSTEM;
        goto free_mcastInfo;
    }

    done = 0;

    return 0;

free_mcastInfo:
    xdr_free(xdr_McastInfo, (char*)&mcastInfo);
close_prod_index_map:
    (void)pim_close();
close_pq:
    (void)pq_close(pq);
free_offMap:
    om_free(offMap);
return_status:
    return status;
}

/**
 * Destroys the multicast LDM sender by stopping it and releasing its resources.
 *
 * @retval 0            Success.
 * @retval LDM7_MCAST   Multicast system failure. `log_add()` called.
 * @retval LDM7_SYSTEM  System failure. `log_add()` called.
 */
static inline int       // inlined because small and only called in one place
mls_destroy(void)
{
    int status = mcastSender_terminate(mcastSender);

    (void)xdr_free(xdr_McastInfo, (char*)&mcastInfo);
    (void)pim_close();
    (void)pq_close(pq);

    return (status == 0)
            ? 0
            : (status == 2)
              ? LDM7_MCAST
              : LDM7_SYSTEM;
}

/**
 * Multicasts a single data-product. Called by `pq_sequenceLock()`.
 *
 * @param[in] info         Pointer to the data-product's metadata.
 * @param[in] data         Pointer to the data-product's data.
 * @param[in] xprod        Pointer to an XDR-encoded version of the data-product
 *                         (data and metadata).
 * @param[in] size         Size, in bytes, of the XDR-encoded version.
 * @param[in] arg          Pointer to the `off_t` product-queue offset for the
 *                         data-product.
 * @retval    0            Success.
 * @retval    LDM7_MCAST   Multicast layer error. `log_add()` called.
 * @retval    LDM7_SYSTEM  System error. `log_add()` called.
 */
static int
mls_multicastProduct(
        const prod_info* const restrict info,
        const void* const restrict      data,
        void* const restrict            xprod,
        const size_t                    size,
        void* const restrict            arg)
{
    off_t          offset = *(off_t*)arg;
    VcmtpProdIndex iProd = mcastSender_getNextProdIndex(mcastSender);
    int            status = om_put(offMap, iProd, offset);
    if (status) {
        log_add("Couldn't add product %lu, offset %lu to map",
                (unsigned long)iProd, (unsigned long)offset);
    }
    else {
        /*
         * The signature is added to the product-index map before the product is
         * sent so that it can be found if the receiving LDM-7 immediately
         * requests it.
         */
        status = pim_put(iProd, (const signaturet*)&info->signature);
        if (status) {
            char buf[LDM_INFO_MAX];
            log_add("Couldn't add to product-index map: prodIndex=%lu, "
                    "prodInfo=%s", (unsigned long)iProd,
                    s_prod_info(buf, sizeof(buf), info, 1));
        }
        else {
            status = mcastSender_send(mcastSender, xprod, size,
                    (void*)info->signature, sizeof(signaturet), &iProd);

            if (status) {
                off_t off;
                (void)om_get(offMap, iProd, &off);
                status = LDM7_MCAST;
            }
            else {
                if (log_is_enabled_info) {
                    char buf[LDM_INFO_MAX];
                    log_info("Sent: prodIndex=%lu, prodInfo=\"%s\"",
                            (unsigned long)iProd,
                            s_prod_info(buf, sizeof(buf), info, 1));
                }
            }
        }
    }

    return status;
}

/**
 * Returns a new product-class for a multicast LDM sender for selecting
 * data-products from the sender's associated product-queue.
 *
 * @param[out] prodClass    Product-class for selecting data-products. Caller
 *                          should call `free_prod_class(*prodClass)` when it's
 *                          no longer needed.
 * @retval     0            Success. `*prodClass` is set.
 * @retval     LDM7_INVAL   Invalid parameter. `log_add()` called.
 * @retval     LDM7_SYSTEM  System error. `log_add()` called.
 */
static int
mls_setProdClass(
        prod_class** const restrict prodClass)
{
    int         status;
    /* PQ_CLASS_ALL has feedtype=ANY, pattern=".*", from=BOT, to=EOT */
    prod_class* pc = dup_prod_class(PQ_CLASS_ALL);      // compiles ERE

    if (pc == NULL) {
        status = LDM7_SYSTEM;
    }
    else {
        (void)set_timestamp(&pc->from); // send products starting now
        pc->psa.psa_val->feedtype = mcastInfo.feed;
        *prodClass = pc;
        status = 0;
    } // `pc` allocated

    return status;
}

/**
 * Tries to multicast the next data-product from a multicast LDM sender's
 * product-queue. Will block for 30 seconds or until a SIGCONT is received if
 * the next data-product doesn't exist.
 *
 * @param[in] prodClass    Class of data-products to multicast.
 * @retval    0            Success.
 * @retval    LDM7_MCAST   Multicast layer error. `log_add()` called.
 * @retval    LDM7_PQ      Product-queue error. `log_add()` called.
 * @retval    LDM7_SYSTEM  System error. `log_add()` called.
 */
static int
mls_tryMulticast(
        prod_class* const restrict prodClass)
{
    // TODO: Keep product locked until VCMTP notification, then release

    off_t offset;
    int   status = pq_sequenceLock(pq, TV_GT, prodClass, mls_multicastProduct,
            &offset, &offset);

    if (PQUEUE_END == status) {
        /* No matching data-product. */
        /*
         * The following code ensures that a termination signal isn't delivered
         * between the time that the done flag is checked and the thread is
         * suspended.
         */
        blockTermSigs();

        if (!done) {
            /*
             * Block until a signal handler is called or the timeout occurs. NB:
             * `pq_suspend()` unblocks SIGCONT and SIGALRM.
             *
             * Keep timeout duration consistent with function description.
             */
            (void)pq_suspendAndUnblock(30, termSigs, NELT(termSigs));
        }

        status = 0;           // no problems here
        unblockTermSigs();
    }
    else if (status < 0) {
        log_errno(status, "Error in product-queue");
        status = LDM7_PQ;
    }

    return status;
}

/**
 * Blocks signals used by the product-queue for the current thread.
 */
static inline void      // inlined because small and only called in one place
mls_blockPqSignals(void)
{
    static sigset_t pqSigSet;

    (void)sigemptyset(&pqSigSet);
    (void)sigaddset(&pqSigSet, SIGCONT);
    (void)sigaddset(&pqSigSet, SIGALRM);

    (void)pthread_sigmask(SIG_BLOCK, &pqSigSet, NULL);
}

/**
 * Starts multicasting data-products.
 *
 * @pre                    `mls_init()` was called.
 * @retval    0            Success.
 * @retval    LDM7_PQ      Product-queue error. `log_add()` called.
 * @retval    LDM7_MCAST   Multicast layer error. `log_add()` called.
 * @retval    LDM7_SYSTEM  System error. `log_add()` called.
 */
static Ldm7Status
mls_startMulticasting(void)
{
    prod_class* prodClass;
    int         status = mls_setProdClass(&prodClass);

    if (status == 0) {
        pq_cset(pq, &prodClass->from);  // sets product-queue cursor

        /*
         * The `done` flag is checked before `mls_tryMulticast()` is called
         * because that function is potentially lengthy and a SIGTERM might
         * have already been received.
         */
        while (0 == status && !done)
            status = mls_tryMulticast(prodClass);

        free_prod_class(prodClass);
    } // `prodClass` allocated

    return status;
}

/**
 * Executes a multicast upstream LDM. Blocks until termination is requested or
 * an error occurs.
 *
 * @param[in]  info          Information on the multicast group.
 * @param[out] ttl           Time-to-live of outgoing packets.
 *                                0  Restricted to same host. Won't be output
 *                                   by any interface.
 *                                1  Restricted to the same subnet. Won't be
 *                                   forwarded by a router (default).
 *                              <32  Restricted to the same site, organization
 *                                   or department.
 *                              <64  Restricted to the same region.
 *                             <128  Restricted to the same continent.
 *                             <255  Unrestricted in scope. Global.
 * @param[in]  ifaceAddr     IP address of the interface to use to send
 *                           multicast packets. "0.0.0.0" obtains the default
 *                           multicast interface. Caller may free.
 * @param[in]  timeoutFactor Ratio of the duration that a data-product will
 *                           be held by the VCMTP layer before being released
 *                           after being multicast to the duration to
 *                           multicast the product. If negative, then the
 *                           default timeout factor is used.
 * @param[in]  pqPathname    Pathname of the product-queue.
 * @retval     0             Success. Termination was requested.
 * @retval     LDM7_INVAL.   Invalid argument. `log_add()` called.
 * @retval     LDM7_MCAST    Multicast sender failure. `log_add()` called.
 * @retval     LDM7_PQ       Product-queue error. `log_add()` called.
 * @retval     LDM7_SYSTEM   System failure. `log_add()` called.
 */
static Ldm7Status
mls_execute(
        const McastInfo* const restrict info,
        const unsigned                  ttl,
        const char* const restrict      ifaceAddr,
        const float                     timeoutFactor,
        const char* const restrict      pqPathname)
{

    /*
     * Block signals used by `pq_sequence()` so that they will only be
     * received by a thread that's accessing the product queue. (The product-
     * queue ensures signal reception.)
     */
    mls_blockPqSignals();

    /*
     * Prevent the multicast sender from receiving a term signal because
     * this thread manages the multicast sender.
     */
    blockTermSigs();
    // Sets `mcastInfo`
    int status = mls_init(info, ttl, ifaceAddr, timeoutFactor, pqPathname);
    unblockTermSigs();

    if (status) {
        log_add("Couldn't initialize multicast LDM sender");
    }
    else {
        /*
         * Print the port number of the TCP server to the standard output
         * stream in case it wasn't specified by the user and was, instead,
         * chosen by the operating system.
         */
        (void)printf("%hu\n", mcastInfo.server.port);
        (void)fflush(stdout);

        /*
         * Data-products are multicast on the current (main) thread so that
         * the process will automatically terminate if something goes wrong.
         */
        char* miStr = mi_format(&mcastInfo);
        log_notice("Starting up: iface=%s, mcastInfo=%s, ttl=%u, pq=\"%s\"",
                ifaceAddr, miStr, ttl, pqPathname);
        free(miStr);
        status = mls_startMulticasting();

        int msStatus = mls_destroy();
        if (status == 0)
            status = msStatus;
    } // module initialized

    return status;
}

/**
 * Multicasts data-products to a multicast group.
 *
 * @param[in] argc  Number of arguments.
 * @param[in] argv  Arguments. See [mls_usage()](@ref mls_usage)
 * @retval    0     Success.
 * @retval    1     Invalid command line. ERROR-level message logged.
 * @retval    2     System error. ERROR-level message logged.
 * @retval    3     Product-queue error. ERROR-level message logged.
 * @retval    4     Multicast layer error. ERROR-level message logged.
 */
int
main(
        const int    argc,
        char** const argv)
{
    /*
     * Initialize logging. Done first in case something happens that needs to
     * be reported.
     */
    log_init(argv[0]);

    /*
     * Decode the command-line.
     */
    McastInfo*  groupInfo;         // multicast group information
    unsigned    ttl = 1;           // Won't be forwarded by any router.
    const char* ifaceAddr;         // IP address of multicast interface
    // Ratio of product-hold duration to multicast duration
    float       timeoutFactor = -1; // Use default
    int         status = mls_decodeCommandLine(argc, argv, &groupInfo, &ttl,
            &ifaceAddr, &timeoutFactor);

    if (status) {
        log_add("Couldn't decode command-line");
        if (1 == status)
            mls_usage();
        log_flush_error();
    }
    else {
        mls_setSignalHandling();

        status = mls_execute(groupInfo, ttl, ifaceAddr, timeoutFactor,
                getQueuePath());
        if (status) {
            log_flush_error();
            switch (status) {
                case LDM7_INVAL: status = 1; break;
                case LDM7_PQ:    status = 3; break;
                case LDM7_MCAST: status = 4; break;
                default:         status = 2; break;
            }
        }

        log_notice("Terminating");
        mi_free(groupInfo);
    } // `groupInfo` allocated

    return status;
}
