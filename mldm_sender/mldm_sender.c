/**
 * Multicast LDM data-products from the queue to a multicast group.
 *
 *   @file: mldm_sender.c
 * @author: Steven R. Emmerson
 *
 * Copyright 2018 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 */

#include "config.h"

#include "atofeedt.h"
#include "AuthServer.h"
#include "CidrAddr.h"
#include "fmtp.h"
#include "globals.h"
#include "InetSockAddr.h"
#include "inetutil.h"
#include "ldm.h"
#include "LdmConfFile.h"
#include "ldmprint.h"
#include "log.h"
#include "mcast_info.h"
#include "MldmRpc.h"
#include "OffsetMap.h"
#include "pq.h"
#include "prod_class.h"
#include "prod_index_map.h"
#include "StrBuf.h"
#include "timestamp.h"

#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <libgen.h>
#include <limits.h>
#include <fcntl.h>
#include <pthread.h>
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
 * C-callable FMTP sender.
 */
static FmtpSender*          fmtpSender;
/**
 * Time-to-live of the multicast packets:
 *   -    0  Restricted to same host. Won't be output by any interface.
 *   -    1  Restricted to the same subnet. Won't be forwarded by a router
 *           (default).
 *   -  <32  Restricted to the same site, organization or department.
 *   -  <64  Restricted to the same region.
 *   - <128  Restricted to the same continent.
 *   - <255  Unrestricted in scope. Global.
 */
static unsigned              ttl = 1; // Won't be forwarded by any router.
/**
 * Ratio of product-hold duration to multicast duration. If negative, then the
 * default timeout is used.
 */
static float                 retxTimeout = -1;  // Use FMTP module's default
/**
 * Termination signals.
 */
static const int             termSigs[] = {SIGINT, SIGTERM};
/**
 * Signal-set for termination signals.
 */
static sigset_t              termSigSet;
/**
 * FMTP product-index to product-queue offset map.
 */
static OffMap*               indexToOffsetMap;
/**
 * Collection of IP addresses for FMTP clients:
 */
static void*                 fmtpClntAddrs;
/**
 * Authorizer of remote clients:
 */
static void*                 authorizer;
/**
 * Multicast information:
 */
static SepMcastInfo*         mcastInfo;
/**
 * Bit-length of the FMTP subnet prefix:
 */
static unsigned short        subnetLen;
/**
 * Multicast LDM RPC command-server:
 */
static void*                 mldmCmdSrvr;
/**
 * Multicast LDM RPC command-server thread:
 */
static pthread_t             mldmCmdSrvrThrd;

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
    -l dest           Log to `dest`. One of: \"\" (system logging daemon),\n\
                      \"-\" (standard error), or file `dest`. Default is\n\
                      \"%s\".\n\
    -n subnetLen      Bit-length of FMTP subnet prefix. Default is 0.\n\
    -q prodQueue      Pathname of product-queue. Default is \"%s\".\n\
    -r retxTimeout    FMTP retransmission timeout in minutes. Duration that a\n\
                      product will be held by the FMTP layer before being\n\
                      released. If negative, then the default FMTP timeout is\n\
                      used.\n\
    -s serverAddr     IPv4 socket address for FMTP server in the form\n\
                      <nnn.nnn.nnn.nnn>[:<port>]. Default is all interfaces\n\
                      and O/S-assigned port number.\n\
    -t ttl            Time-to-live for outgoing multicast packets:\n\
                           0  Restricted to same host. Won't be output by\n\
                              any interface.\n\
                           1  Restricted to same subnet. Won't be\n\
                              forwarded by a router. This is the default.\n\
                         <32  Restricted to same site, organization or\n\
                              department.\n\
                         <64  Restricted to same region.\n\
                        <128  Restricted to same continent.\n\
                        <255  Unrestricted in scope. Global.\n\
                      The default is 1.\n\
    -v                Verbose logging: log INFO level messages.\n\
    -x                Debug logging: log DEBUG level messages.\n\
Operands:\n\
    groupId:groupPort Internet service address of multicast group, where\n\
                      <groupId> is either group-name or dotted-decimal IPv4\n\
                      address and <groupPort> is port number.\n",
            log_get_id(), log_get_default_destination(), getDefaultQueuePath());
}

/**
 * Decodes the options of the command-line.
 *
 * @param[in]  argc      Number of arguments.
 * @param[in]  argv      Arguments.
 * @param[out] feed      Feedtypes of data to be sent.
 * @param[out] serverId  FMTP server address. Caller must not free.
 * @retval     0         Success. `subnetLen` or `*ttl` might not have been set.
 * @retval     1         Invalid options. `log_add()` called.
 */
static int
mls_decodeOptions(
        int                            argc,
        char* const* const restrict    argv,
        feedtypet* const restrict      feed,
        const char** const restrict    serverId)
{
    int          ch;
    extern int   opterr;
    extern int   optopt;
    extern char* optarg;

    opterr = 1; // prevent getopt(3) from trying to print error messages

    while ((ch = getopt(argc, argv, ":F:f:l:n:q:r:s:t:vx")) != EOF)
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
            case 'n': {
                int nbytes;
                if (sscanf(optarg, "%hu %n", &subnetLen, &nbytes) != 1 ||
                        optarg[nbytes] != 0) {
                    log_add("Invalid FMTP subnet-length argument, \"%s\"",
                            optarg);
                    return 1;
                }
                break;
            }
            case 'q': {
                setQueuePath(optarg);
                break;
            }
            case 'r': {
                int   nbytes;
                if (1 != sscanf(optarg, "%f %n", &retxTimeout, &nbytes) ||
                        0 != optarg[nbytes]) {
                    log_add("Couldn't decode FMTP retransmission timeout "
                            "option-argument \"%s\"", optarg);
                    return 1;
                }
                break;
            }
            case 's': {
                *serverId = optarg;
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
                ttl = t;
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
 * Decodes the operands of the command-line.
 *
 * @param[in]  argc          Number of operands.
 * @param[in]  argv          Operands.
 * @param[out] groupAddr     Internet service address of the multicast group.
 *                           Caller should free when it's no longer needed.
 * @retval     0             Success. `*groupAddr`, `*serverAddr`, `*feed`, and
 *                           `msgQName` are set.
 * @retval     1             Invalid operands. `log_add()` called.
 */
static int
mls_decodeOperands(
        int                         argc,
        char* const* restrict       argv,
        const char** const restrict groupAddr)
{
    int status = 1;

    if (argc-- < 1) {
        log_add("Multicast group not specified");
    }
    else {
        const char* mcastAddr = *argv++;

        if (argc > 0) {
            log_add("Too many operands");
        }
        else {
            *groupAddr = mcastAddr;
            status = 0;
        }
    }

    return status;
}

/**
 * Decodes the command line.
 *
 * @param[in]  argc          Number of arguments.
 * @param[in]  argv          Arguments.
 * @retval     0             Success. `*mcastGrpInfo` is set. `*ttl` might be
 *                           set.
 * @retval     1             Invalid command line. `log_add()` called.
 * @retval     2             System failure. `log_add()` called.
 */
static int
mls_decodeCommandLine(
        int                           argc,
        char* const* restrict         argv)
{
    int status;

    feedtypet   feed = EXP;
    const char* serverId = "0.0.0.0:0"; // default: all interfaces

    subnetLen = 0;

    status = mls_decodeOptions(argc, argv, &feed, &serverId);

    if (0 == status) {
        extern int  optind;
        const char* groupId;

        argc -= optind;
        argv += optind;
        status = mls_decodeOperands(argc, argv, &groupId);

        if (0 == status) {
            SepMcastInfo* const info = smi_newFromStr(feed, groupId,
                    serverId);

            if (info == NULL) {
                status = 2;
            }
            else {
                mcastInfo = info;
            }
        }
    } // options decoded

    return status;
}

/**
 * Handles a signal.
 *
 * @param[in] sig  Signal to be handled.
 */
static void
mls_handleSignal(
        const int sig)
{
    switch (sig) {
        case SIGTERM:
            done = 1;
            break;
        case SIGINT:
            done = 1;
            break;
        case SIGUSR1:
            log_refresh();
            break;
        case SIGUSR2:
            log_roll_level();
            break;
    }
    return;
}

/**
 * Sets signal handling.
 */
static void
mls_setSignalHandling(void)
{
    // Initialize signal-set for termination signals.
    (void)sigemptyset(&termSigSet);
    for (int i = 0; i < NELT(termSigs); i++)
        (void)sigaddset(&termSigSet, termSigs[i]);

    struct sigaction sigact;
    (void)sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;

    // Handle the following
    sigact.sa_handler = mls_handleSignal;

    // Don't restart the following
    (void)sigaction(SIGINT, &sigact, NULL);
    (void)sigaction(SIGTERM, &sigact, NULL);

    // Restart the following
    sigact.sa_flags |= SA_RESTART;
    (void)sigaction(SIGUSR1, &sigact, NULL);
    (void)sigaction(SIGUSR2, &sigact, NULL);
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
    pathname[sizeof(pathname)-1] = 0;

    return pim_openForWriting(dirname(pathname), feed, maxSigs);
}

/**
 * Accepts notification that the FMTP layer is finished with a data-product
 * and releases associated resources.
 *
 * @param[in] prodIndex  Index of the product.
 */
static void
mls_doneWithProduct(
    const FmtpProdIndex prodIndex)
{
    off_t offset;
    int   status = om_get(indexToOffsetMap, prodIndex, &offset);
    if (status) {
        log_error_q("Couldn't get file-offset corresponding to product-index %lu",
                (unsigned long)prodIndex);
    }
    else {
        status = pq_release(pq, offset);
        if (status) {
            log_error_q("Couldn't release data-product in product-queue "
                    "corresponding to file-offset %ld, product-index %lu",
                    (long)offset, (unsigned long)prodIndex);
        }
    }
}

/**
 * Initializes the resources of this module. Sets `mcastGrpInfo`; in particular,
 * sets the actual port number used by the FMTP TCP server (in case the number
 * was chosen by the operating-system). Upon return, all FMTP threads have been
 * created -- in particular,  the FMTP TCP server is listening.
 *
 * @retval    0              Success. `*sender` is set.
 * @retval    LDM7_INVAL     An Internet identifier couldn't be converted to an
 *                           IPv4 address because it's invalid or unknown.
 *                           `log_add()` called.
 * @retval    LDM7_MCAST     Failure in FMTP system. `log_add()` called.
 * @retval    LDM7_SYSTEM    System error. `log_add()` called.
 */
static Ldm7Status
mls_init()
{
    int status;

    indexToOffsetMap = om_new();

    if (indexToOffsetMap == NULL) {
        log_add("Couldn't create prodIndex-to-prodQueueOffset map");
        status = LDM7_SYSTEM;
        goto return_status;
    }

    /*
     * Product-queue is opened thread-safe because `mls_tryMulticast()` and
     * `mls_doneWithProduct()` might be executed on different threads.
     */
    if (pq_open(getQueuePath(), PQ_READONLY | PQ_THREADSAFE, &pq)) {
        log_add("Couldn't open product-queue \"%s\"", getQueuePath());
        status = LDM7_SYSTEM;
        goto free_offMap;
    }

    if ((status = mls_openProdIndexMap(smi_getFeed(mcastInfo),
            pq_getSlotCount(pq))))
        goto close_pq;

    FmtpProdIndex iProd;
    if ((status = pim_getNextProdIndex(&iProd)))
        goto close_prod_index_map;

    InetSockAddr* const fmtpSrvrAddr = smi_getFmtpSrvr(mcastInfo);
    const char* const   fmtpSrvrInetAddr = isa_getInetAddrStr(fmtpSrvrAddr);
    in_port_t           fmtpSrvrPort = isa_getPort(fmtpSrvrAddr);

    InetSockAddr* const mcastGrpAddr = smi_getMcastGrp(mcastInfo);
    const char* const   mcastGrpInetAddr = isa_getInetAddrStr(mcastGrpAddr);
    const in_port_t     mcastGrpPort = isa_getPort(mcastGrpAddr);

    if ((status = fmtpSender_create(&fmtpSender, fmtpSrvrInetAddr,
            &fmtpSrvrPort, mcastGrpInetAddr, mcastGrpPort, ttl,
            iProd, retxTimeout, mls_doneWithProduct, authorizer))) {
        log_add("Couldn't create FMTP sender");
        status = (status == 1)
                ? LDM7_INVAL
                : (status == 2)
                  ? LDM7_MCAST
                  : LDM7_SYSTEM;
        goto close_prod_index_map;
    }

    isa_setPort(fmtpSrvrAddr, fmtpSrvrPort);

    done = 0;

    return 0;

close_prod_index_map:
    (void)pim_close();
close_pq:
    (void)pq_close(pq);
free_offMap:
    om_free(indexToOffsetMap);
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
    int status = fmtpSender_terminate(fmtpSender);

    smi_free(mcastInfo);
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
mls_mcastProd(
        const prod_info* const restrict info,
        const void* const restrict      data,
        void* const restrict            xprod,
        const size_t                    size,
        void* const restrict            arg)
{
    off_t          offset = *(off_t*)arg;
    FmtpProdIndex iProd = fmtpSender_getNextProdIndex(fmtpSender);
    int            status = om_put(indexToOffsetMap, iProd, offset);
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
            status = fmtpSender_send(fmtpSender, xprod, size,
                    (void*)info->signature, sizeof(signaturet), &iProd);

            if (status) {
                off_t off;
                (void)om_get(indexToOffsetMap, iProd, &off);
                status = LDM7_MCAST;
            }
            else {
                char buf[LDM_INFO_MAX];
                log_info_q("Sent: prodIndex=%lu, prodInfo=\"%s\"",
                        (unsigned long)iProd,
                        s_prod_info(buf, sizeof(buf), info,
                                log_is_enabled_debug));
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
        (void)set_timestamp(&pc->from); // Send products starting now
        pc->psa.psa_val->feedtype = smi_getFeed(mcastInfo);
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
    // TODO: Keep product locked until FMTP notification, then release

    off_t offset;
    int   status = pq_sequenceLock(pq, TV_GT, prodClass, mls_mcastProd,
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
        log_errno_q(status, "Error in product-queue");
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

static void*
runMldmSrvr(void* mldmSrvr)
{
    if (mldmSrvr_run(mldmSrvr)) {
        log_error("Multicast LDM RPC server returned");
        log_flush_error();
    }

    log_free(); // Because end of thread

    return NULL;
}

/**
 * Starts the FMTP client authorization process.
 *
 * @param[in] fmtpSrvrAddr  Address of FMTP server
 * @param[in] subnetLen     Bit-length of FMTP subnet prefix
 * @retval    0             Success
 * @retval    LDM7_SYSTEM   Failure. `log_add()` called.
 */
static Ldm7Status
startAuthorization(
        const struct in_addr* const fmtpSrvrAddr,
        const unsigned short        subnetLen)
{
    Ldm7Status status;
    CidrAddr   fmtpSrvrCidr;

    if (!cidrAddr_init(&fmtpSrvrCidr, fmtpSrvrAddr->s_addr, subnetLen)) {
        log_add("Couldn't create FMTP server CIDR address");
        status = LDM7_SYSTEM;
    }
    else {
        fmtpClntAddrs = fmtpClntAddrs_new(&fmtpSrvrCidr);

        if (fmtpClntAddrs == NULL) {
            log_add_syserr("Couldn't create pool of available IP addresses");
            status = LDM7_SYSTEM;
        }
        else {
            authorizer = auth_new(fmtpClntAddrs, smi_getFeed(mcastInfo));

            if (authorizer == NULL) {
                log_add_syserr("Couldn't create authorizer of remote clients");
            }
            else {
                mldmCmdSrvr = mldmSrvr_new(fmtpClntAddrs);

                if (mldmCmdSrvr == NULL) {
                    log_add_syserr("Couldn't create multicast LDM RPC "
                            "command-server");
                    status = LDM7_SYSTEM;
                }
                else {
                    status = pthread_create(&mldmCmdSrvrThrd, NULL, runMldmSrvr,
                            mldmCmdSrvr);

                    if (status) {
                        log_add_syserr("Couldn't create multicast LDM RPC "
                                "command-server thread");
                        mldmSrvr_free(mldmCmdSrvr);
                        mldmCmdSrvr = NULL;
                        status = LDM7_SYSTEM;
                    }
                    else {
                        status = LDM7_OK;
                    }
                } // `mldmSrvr` set

                if (status)
                    auth_delete(authorizer);
            } // `authorizer` set

            if (status)
                fmtpClntAddrs_free(fmtpClntAddrs);
        } // `inAddrPool` set
    }

    return status;
}

static void
stopAuthorization()
{
    int   status = pthread_cancel(mldmCmdSrvrThrd);
    if (status) {
        log_add_syserr("Couldn't cancel multicast LDM RPC command-server "
                "thread");
    }
    else {
        void* result;
        status = pthread_join(mldmCmdSrvrThrd, &result);
        if (status) {
            log_add_syserr("Couldn't join multicast LDM RPC command-server "
                    "thread");
        }
        else {
            mldmSrvr_free(mldmCmdSrvr);
            auth_delete(authorizer);
            fmtpClntAddrs_free(fmtpClntAddrs);
        }
    }
}

/**
 * Executes a multicast LDM. Blocks until termination is requested or
 * an error occurs.
 *
 * @retval     0             Success. Termination was requested.
 * @retval     LDM7_INVAL.   Invalid argument. `log_add()` called.
 * @retval     LDM7_MCAST    Multicast sender failure. `log_add()` called.
 * @retval     LDM7_PQ       Product-queue error. `log_add()` called.
 * @retval     LDM7_SYSTEM   System failure. `log_add()` called.
 */
static Ldm7Status
mls_execute(void)
{
    int status;

    /*
     * Block signals used by `pq_sequence()` so that they will only be
     * received by a thread that's accessing the product queue. (The product-
     * queue ensures signal reception when necessary.)
     */
    mls_blockPqSignals();

    /*
     * Prevent child threads from receiving a term signal because this thread
     * manages the child threads.
     */
    blockTermSigs();

    /*
     * Sets `inAddrPool, `authorizer`, `mldmSrvr`, `mldmSrvrThread`, and
     * `mldmSrvrPort`.
     */
    sa_family_t    family;
    struct in_addr fmtpSrvrAddr;
    socklen_t      size = sizeof(fmtpSrvrAddr);
    const InetId*  fmtpSrvrId = isa_getInetId(smi_getFmtpSrvr(mcastInfo));

    if (inetId_idIsName(fmtpSrvrId) ||
            inetId_getAddr((InetId*)isa_getInetId(smi_getFmtpSrvr(mcastInfo)),
                    &family, &fmtpSrvrAddr, &size) || family != AF_INET) {
        log_add("FMTP server specification, \"%s\", isn't IPv4 address",
                inetId_getId(fmtpSrvrId));
        status = LDM7_INVAL;
    }
    else {
        status = startAuthorization(&fmtpSrvrAddr, subnetLen);

        if (status) {
            log_add("Couldn't initialize authorization of remote clients");
        }
        else {
            status = mls_init();
            unblockTermSigs(); // Done creating child threads

            if (status) {
                log_add("Couldn't initialize multicast LDM sender");
            }
            else {
                /*
                 * Print, to the standard output stream,
                 * - The port number of the FMTP TCP server in case it wasn't
                 *   specified by the user and was, instead, chosen by the
                 *   operating system; and
                 * - The port number of the multicast LDM RPC command-server so that
                 *   upstream LDM processes running on the local host can
                 *   communicate with it to, for example, reserve IP addresses for
                 *   remote FMTP clients.
                 */
                if (printf("%" PRIu16 " %" PRIu16 "\n",
                        isa_getPort(smi_getFmtpSrvr(mcastInfo)),
                        mldmSrvr_getPort(mldmCmdSrvr)) < 0) {
                    log_add_syserr(
                            "Couldn't write port numbers to standard output");
                    status = LDM7_SYSTEM;
                }
                else {
                    status = fflush(stdout);
                    log_assert(status != EOF);
                    /*
                     * Data-products are multicast on the current (main) thread
                     * so that the process will automatically terminate if
                     * something goes wrong.
                     */
                    char* miStr = smi_toString(mcastInfo);
                    log_notice("Starting up: mcastGrpInfo=%s, ttl=%u, "
                            "fmtpSubnetLen=%u, pq=\"%s\", mldmCmdPort=%u",
                            miStr, ttl, subnetLen, getQueuePath(),
                            mldmSrvr_getPort(mldmCmdSrvr));
                    free(miStr);
                    status = mls_startMulticasting();

                    int msStatus = mls_destroy();
                    if (status == 0)
                        status = msStatus;
                } // Port numbers successfully written to standard output stream
            } // Multicast LDM sender initialized

            stopAuthorization();
        } // Multicast LDM RPC server started
    }

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
main(   const int    argc,
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
    int status = mls_decodeCommandLine(argc, argv);

    if (status) {
        log_add("Couldn't decode command-line");
        if (1 == status)
            mls_usage();
        log_flush_error();
    }
    else {
        mls_setSignalHandling();

        status = mls_execute();
        if (status) {
            log_error_q("Couldn't execute multicast LDM sender");
            switch (status) {
                case LDM7_INVAL: status = 1; break;
                case LDM7_PQ:    status = 3; break;
                case LDM7_MCAST: status = 4; break;
                default:         status = 2; break;
            }
        }

        log_notice("Terminating");

        if (status)
            smi_free(mcastInfo);
    } // `groupInfo` allocated

    log_fini();

    return status;
}
