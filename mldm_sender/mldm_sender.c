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
#include "pq.h"
#include "prod_class.h"
#include "StrBuf.h"
#include "timestamp.h"

#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define NELT(a) (sizeof(a)/sizeof((a)[0]))

/**
 * C-callable multicast sender.
 */
static void*       mcastSender;
/**
 * Information on the multicast group.
 */
static McastInfo   mcastInfo;
/**
 * Termination signals.
 */
static const int   termSigs[] = {SIGINT, SIGTERM};
/**
 * Signal-set for termination signals.
 */
static sigset_t    termSigSet;

/**
 * Appends a usage message to the pending log messages.
 */
static void
mls_usage(void)
{
    log_add(
"Usage: %s [options] feed groupId:groupPort\n"
"Options:\n"
"    -I serverIface    Interface on which VCMTP TCP server will listen.\n"
"                      Default is all interfaces.\n"
"    -P serverPort     Port number for VCMTP TCP server. Default is chosen by\n"
"                      operating system.\n"
"    -l logfile        Log to file <logfile> ('-' => standard error stream).\n"
"                      Default depends on standard error stream:\n"
"                          is tty     => use standard error stream\n"
"                          is not tty => use system logging daemon.\n"
"    -q queue          Use product-queue <queue>. Default is \"%s\".\n"
"    -t ttl            Time-to-live of outgoing packets (default is 1):\n"
"                           0  Restricted to same host. Won't be output by\n"
"                              any interface.\n"
"                           1  Restricted to same subnet. Won't be\n"
"                              forwarded by a router (default).\n"
"                         <32  Restricted to same site, organization or\n"
"                              department.\n"
"                         <64  Restricted to same region.\n"
"                        <128  Restricted to same continent.\n"
"                        <255  Unrestricted in scope. Global.\n"
"    -v                Verbose logging: log INFO level messages.\n"
"    -x                Debug logging: log DEBUG level messages.\n"
"Operands:\n"
"    feed              Identifier of multicast group in form of feedtype\n"
"                      expression\n"
"    groupId:groupPort Internet service address of multicast group, where\n"
"                      <groupId> is either group-name or dotted-decimal IPv4\n"
"                      address and <groupPort> is port number.",
            getulogident(), getQueuePath());
}

/**
 * Decodes the options of the command-line.
 *
 * @pre                     {`openulog()` has already been called.}
 * @param[in]  argc         Number of arguments.
 * @param[in]  argv         Arguments.
 * @param[out] serverIface  Interface on which VCMTP TCP server should listen.
 *                          Caller must not free.
 * @param[out] serverPort   Port number for VCMTP TCP server.
 * @param[out] ttl          Time-to-live of outgoing packets.
 *                               0  Restricted to same host. Won't be output by
 *                                  any interface.
 *                               1  Restricted to the same subnet. Won't be
 *                                  forwarded by a router (default).
 *                             <32  Restricted to the same site, organization or
 *                                  department.
 *                             <64  Restricted to the same region.
 *                            <128  Restricted to the same continent.
 *                            <255  Unrestricted in scope. Global.
 * @retval     0            Success. `*serverIface` or `*ttl` might not have
 *                          been set.
 * @retval     1            Invalid options. `log_start()` called.
 */
static int
mls_decodeOptions(
        int                            argc,
        char* const* const restrict    argv,
        const char** const restrict    serverIface,
        unsigned short* const restrict serverPort,
        unsigned* const restrict       ttl)
{
    int          ch;
    extern int   opterr;
    extern int   optopt;
    extern char* optarg;

    opterr = 1; // prevent getopt(3) from trying to print error messages

    while ((ch = getopt(argc, argv, ":I:l:P:q:t:vx")) != EOF)
        switch (ch) {
        case 'I': {
            *serverIface = optarg;
            break;
        }
        case 'l': {
            (void)openulog(getulogident(), log_getLogOpts(optarg),
                    getulogfacility(), optarg);
            break;
        }
        case 'P': {
            unsigned short port;
            int            nbytes;

            if (1 != sscanf(optarg, "%5hu %n", &port, &nbytes) ||
                    0 != optarg[nbytes]) {
                log_start("Couldn't decode TCP-server port-number option-argument "
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
        case 't': {
            unsigned t;
            int      nbytes;
            if (1 != sscanf(optarg, "%3u %n", &t, &nbytes) ||
                    0 != optarg[nbytes]) {
                log_start("Couldn't decode time-to-live option-argument \"%s\"",
                        optarg);
                return 1;
            }
            if (t >= 255) {
                log_start("Invalid time-to-live option-argument \"%s\"",
                        optarg);
                return 1;
            }
            *ttl = t;
            break;
        }
        case 'v': {
            (void)setulogmask(getulogmask() | LOG_MASK(LOG_INFO));
            break;
        }
        case 'x': {
            (void)setulogmask(getulogmask() | LOG_MASK(LOG_DEBUG));
            break;
        }
        case ':': {
            LOG_START1("Option \"%c\" requires an argument", optopt);
            return 1;
        }
        default: {
            LOG_ADD1("Unknown option: \"%c\"", optopt);
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
 * @retval     1            Invalid argument. `log_start()` called.
 * @retval     2            System failure. `log_start()` called.
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
 * @retval     1          Invalid operand. `log_start()` called.
 * @retval     2          System failure. `log_start()` called.
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
 * @param[out] feed         Feedtype of the multicast group.
 * @retval     0            Success. `*groupAddr`, `*serverAddr`, and `*feed`
 *                          are set.
 * @retval     1            Invalid operands. `log_start()` called.
 * @retval     2            System failure. `log_start()` called.
 */
static int
mls_decodeOperands(
        int                          argc,
        char* const* restrict        argv,
        ServiceAddr** const restrict groupAddr,
        feedtypet* const restrict    feed)
{
    int status;

    if (argc < 1) {
        log_start("Multicast group not specified");
        status = 1;
    }
    else {
        feedtypet ft;

        if (strfeedtypet(*argv, &ft)) {
            log_start("Invalid feed expression: \"%s\"", *argv);
        }
        else {
            argc--; argv++;

            if ((status = mls_decodeGroupAddr(*argv, groupAddr)))
                status = 1;
        }
    }

    return status;
}

/**
 * Sets runtime parameters from command-line arguments.
 *
 * @param[in]  serverIface  Interface on which VCMTP TCP server should listen.
 *                          Caller must not free.
 * @param[in]  serverPort   Port number for VCMTP TCP server.
 * @param[in]  feed         Feedtype of multicast group.
 * @param[in]  groupAddr    Internet service address of multicast group.
 *                          Caller should free when it's no longer needed.
 * @param[out] mcastInfo    Information on multicast group.
 * @retval     0            Success. `*mcastInfo` is set.
 * @retval     1            Invalid argument. `log_start()` called.
 * @retval     2            System failure. `log_start()` called.
 */
static int
mls_setRuntimeParameters(
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
 * @param[in]  argc       Number of arguments.
 * @param[in]  argv       Arguments.
 * @param[out] mcastInfo  Multicast group information.
 * @param[out] ttl        Time-to-live of outgoing packets.
 *                              0  Restricted to same host. Won't be output by
 *                                 any interface.
 *                              1  Restricted to the same subnet. Won't be
 *                                 forwarded by a router (default).
 *                            <32  Restricted to the same site, organization or
 *                                 department.
 *                            <64  Restricted to the same region.
 *                           <128  Restricted to the same continent.
 *                           <255  Unrestricted in scope. Global.
 * @retval    0           Success. `*mcastInfo` is set. `*ttl` might be set.
 * @retval    1           Invalid command line. `log_start()` called.
 * @retval    2           System failure. `log_start()` called.
 */
static int
mls_decodeCommandLine(
        int                        argc,
        char* const* restrict      argv,
        McastInfo** const restrict mcastInfo,
        unsigned* const restrict   ttl)
{
    const char*    serverIface = "0.0.0.0";     // default: all interfaces
    unsigned short serverPort = 0;              // default: chosen by O/S
    int            status = mls_decodeOptions(argc, argv, &serverIface,
            &serverPort, ttl);
    extern int     optind;

    if (0 == status) {
        ServiceAddr* groupAddr;
        feedtypet    feed;

        argc -= optind;
        argv += optind;
        status = mls_decodeOperands(argc, argv, &groupAddr, &feed);

        if (0 == status) {
            status = mls_setRuntimeParameters(serverIface, serverPort, feed,
                    groupAddr, mcastInfo);
            sa_free(groupAddr);
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
    rollulogpri();
}

/**
 * Handles a signal by setting the `done` flag.
 *
 * @param[in] sig  Signal to be handled. Ignored.
 */
static void
mls_setDoneFlag(
        const int sig)
{
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
 *                          `log_start()` called.
 * @retval     LDM7_SYSTEM  The identifier cannot be converted to an IPv4
 *                          address due to a system error. `log_start()` called.
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

    LOG_ADD1("Couldn't get address of %s", desc);

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
    /* Queue pathname is cloned because `dirname()` may alter its argument */
    char* const queuePathname = strdup(getQueuePath());
    int         status;

    if (NULL == queuePathname) {
        LOG_SERROR0("Couldn't duplicate product-queue pathname");
        status = LDM7_SYSTEM;
    }
    else {
        char* const mapPathname = ldm_format(256, "%s/%s.map",
                dirname(queuePathname), s_feedtypet(feed));

        if (NULL == mapPathname) {
            LOG_ADD0("Couldn't construct pathname of product-index map");
            status = LDM7_SYSTEM;
        }
        else {
            status = pim_openForWriting(mapPathname, maxSigs);
            free(mapPathname);
        } // `mapPathname` allocated

        free(queuePathname);
    } // `queuePathname` allocated

    return status;
}

/**
 * Accepts notification that the multicast layer is finished with a
 * data-product and releases associated resources.
 *
 * @param[in] prodIndex  Index of the product.
 */
static void
mls_doneWithProduct(
    const VcmtpProdIndex prodIndex)
{
    // TODO: Nothing is done yet because a product should reside in the queue
    // much longer than it takes VCMTP to send it and the queue API will have to
    // be modified to support holding a lock on a product until it's explicitly
    // unlocked.
}

/**
 * Initializes the resources of this module. Sets `mcastInfo`; in particular,
 * sets `mcastInfo.server.port` to the actual port number used by the VCMTP
 * TCP server (in case the number was chosen by the operating-system).
 *
 * @param[in]  info         Information on the multicast group.
 * @param[in]  ttl          Time-to-live of outgoing packets.
 *                               0  Restricted to same host. Won't be output by
 *                                  any interface.
 *                               1  Restricted to the same subnet. Won't be
 *                                  forwarded by a router (default).
 *                             <32  Restricted to the same site, organization or
 *                                  department.
 *                             <64  Restricted to the same region.
 *                            <128  Restricted to the same continent.
 *                            <255  Unrestricted in scope. Global.
 * @param[in]  pqPathname   Pathname of product queue from which to obtain
 *                          data-products.
 * @retval     0            Success. `*sender` is set.
 * @retval     LDM7_INVAL   An Internet identifier couldn't be converted to an
 *                          IPv4 address because it's invalid or unknown.
 *                          `log_start()` called.
 * @retval     LDM7_SYSTEM  System error. `log_start()` called.
 */
static Ldm7Status
mls_init(
    const McastInfo* const restrict info,
    const unsigned                  ttl,
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

    if (pq_open(pqPathname, PQ_READONLY, &pq)) {
        LOG_START1("Couldn't open product-queue \"%s\"", pqPathname);
        status = LDM7_SYSTEM;
        goto return_status;
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

    if ((status = mcastSender_new(&mcastSender, serverInetAddr,
            &mcastInfo.server.port, groupInetAddr, mcastInfo.group.port, ttl,
            iProd, mls_doneWithProduct))) {
        status = (status == EINVAL) ? LDM7_INVAL : LDM7_SYSTEM;
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
return_status:
    return status;
}

/**
 * Ensures that the resources of this module are released.
 */
static inline void      // inlined because small and only called in one place
mls_destroy(void)
{
    (void)xdr_free(xdr_McastInfo, (char*)&mcastInfo);
    (void)pim_close();
    (void)pq_close(pq);
    mcastSender_free(mcastSender);
}

/**
 * Multicasts a single data-product. Called by `pq_sequence()`.
 *
 * @param[in] info   Pointer to the data-product's metadata.
 * @param[in] data   Pointer to the data-product's data.
 * @param[in] xprod  Pointer to an XDR-encoded version of the data-product (data
 *                   and metadata).
 * @param[in] size   Size, in bytes, of the XDR-encoded version.
 * @param[in] arg    Optional last `pq_sequence()` argument. Ignored.
 * @retval    0      Success.
 * @retval    EIO    I/O failure. `log_start()` called.
 */
static int
mls_multicastProduct(
        const prod_info* const restrict info,
        const void* const restrict      data,
        void* const restrict            xprod,
        const size_t                    size,
        void* const restrict            arg)
{
    VcmtpProdIndex iProd;
    int            status = mcastSender_send(mcastSender, xprod, size,
            (void*)info->signature, sizeof(signaturet), &iProd);

    if (0 == status) {
        prod_info info;
        XDR       xdrs;

        xdrmem_create(&xdrs, xprod, size, XDR_DECODE);
        (void)memset(&info, 0, sizeof(info)); // necessary for `xdr_prod_info()`

        if (!xdr_prod_info(&xdrs, &info)) {
            LOG_SERROR0("Couldn't decode LDM product-metadata");
            status = EIO;
        }
        else {
            status = pim_put(iProd, (const signaturet*)&info.signature);

            if (ulogIsVerbose())
                uinfo("Sent: prodIndex=%lu, prodInfo=\"%s\"",
                        (unsigned long)iProd,
                        s_prod_info(NULL, 0, &info, ulogIsDebug()));

            xdr_free(xdr_prod_info, (char*)&info);
        }

        xdr_destroy(&xdrs);
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
 * @retval     LDM7_INVAL   Invalid parameter. `log_start()` called.
 * @retval     LDM7_SYSTEM  System error. `log_start()` called.
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
 * Tries to multicast the next data-product from a multicast LDM sender's
 * product-queue. Will block for 30 seconds or until a SIGCONT is received if
 * the next data-product doesn't exist.
 *
 * @param[in] prodClass  Class of data-products to multicast.
 * @retval    0          Success.
 * @return               `<errno.h>` error-code from `pq_sequence()`.
 * @return               Return-code from `mls_multicastProduct()`.
 */
static int
mls_tryMulticast(
        prod_class* const restrict prodClass)
{
    // TODO: Keep product locked until VCMTP notification, then release

    int status = pq_sequence(pq, TV_GT, prodClass, mls_multicastProduct, NULL);

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
    else if (status) {
        LOG_ERRNUM0(status, "Bad return from pq_sequence()");
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
 * @pre                     {`mls_init()` was called.}
 * @retval     0            Success.
 * @retval     LDM7_SYSTEM  Error. `log_start()` called.
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
 * @param[in]  info         Information on the multicast group.
 * @param[out] ttl          Time-to-live of outgoing packets.
 *                               0  Restricted to same host. Won't be output by
 *                                  any interface.
 *                               1  Restricted to the same subnet. Won't be
 *                                  forwarded by a router (default).
 *                             <32  Restricted to the same site, organization or
 *                                  department.
 *                             <64  Restricted to the same region.
 *                            <128  Restricted to the same continent.
 *                            <255  Unrestricted in scope. Global.
 * @param[in]  pqPathname   Pathname of the product-queue.
 * @retval     0            Success. Termination was requested.
 * @retval     LDM7_SYSTEM  System failure. `log_start()` called.
 */
static Ldm7Status
mls_execute(
        const McastInfo* const restrict info,
        const unsigned                  ttl,
        const char* const restrict      pqPathname)
{
    int status = mls_init(info, ttl, pqPathname); // sets `mcastInfo`

    if (status) {
        LOG_ADD0("Couldn't initialize multicast LDM sender");
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
         * Block signals used by `pq_sequence()` so that they will only be
         * received by a thread that's accessing the product queue.
         */
        mls_blockPqSignals();

        /*
         * Data-products are multicast on the current (main) thread so that the
         * process will automatically terminate if something goes wrong.
         */
        char* miStr = mi_format(&mcastInfo);
        unotice("Starting up: mcastInfo=%s, ttl=%u", miStr, ttl);
        mi_free(miStr);
        status = mls_startMulticasting();

        mls_destroy();
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
 * @retval    2     System failure. ERROR-level message logged.
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
    log_initLogging(basename(argv[0]), LOG_NOTICE, LOG_LDM);

    /*
     * Decode the command-line.
     */
    McastInfo* groupInfo;  // multicast group information
    unsigned   ttl = 1;    // Won't be forwarded by any router.
    int        status = mls_decodeCommandLine(argc, argv, &groupInfo, &ttl);

    if (status) {
        log_add("Couldn't decode command-line");
        if (1 == status)
            mls_usage();
        log_log(LOG_ERR);
    }
    else {
        mls_setSignalHandling();

        if (mls_execute(groupInfo, ttl, getQueuePath())) {
            log_log(LOG_ERR);
            status = done ? 0 : 2;
        }

        unotice("Terminating");
        mi_free(groupInfo);
    } // `groupInfo` allocated

    return status;
}
