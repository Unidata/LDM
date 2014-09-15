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
 * group.
 */

#include "config.h"

#include "atofeedt.h"
#include "file_id_map.h"
#include "globals.h"
#include "inetutil.h"
#include "ldm.h"
#include "ldmprint.h"
#include "log.h"
#include "mcast.h"
#include "mcast_info.h"
#include "mldm_sender_memory.h"
#include "pq.h"
#include "prod_class.h"
#include "StrBuf.h"
#include "timestamp.h"

#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct {
    void*                 mcastSender;
    McastInfo             mcastInfo;
    pthread_t             mcastThread;
    McastFileId           fileId;
    volatile sig_atomic_t done;
} McastLdmSender;

static sigset_t termSigSet;
static sigset_t pqSigSet;

/**
 * Returns the logging options appropriate to a log-file specification.
 *
 * @param[in] logFileSpec  Log-file specification:
 *                             NULL  Use syslog(3)
 *                             ""    Use syslog(3)
 *                             "-"   Log to `stderr`
 *                             else  Pathname of log-file
 * @return                 Logging options appropriate to the log-file
 *                         specification.
 */
static unsigned
mls_getLogOpts(
    const char* const logFileSpec)
{
    return (logFileSpec && 0 == strcmp(logFileSpec, "-"))
        /*
         * Interactive invocation. Use ID, timestamp, UTC, no PID, and no
         * console.
         */
        ? LOG_IDENT
        /*
         * Non-interactive invocation. Use ID, timestamp, UTC, PID, and the
         * console as a last resort.
         */
        : LOG_IDENT | LOG_PID | LOG_CONS;
}

/**
 * Initializes logging. This should be called before the command-line is
 * decoded.
 *
 * @param[in] progName  Name of the program.
 */
static void
mls_initLogging(
    const char* const progName)
{
    char* logFileSpec;

    if (isatty(fileno(stderr))) {
        /* Interactive execution. */
        logFileSpec = "-"; // log to `stderr`
    }
    else {
        logFileSpec = NULL; // use syslog(3)
    }

    (void)setulogmask(LOG_UPTO(LOG_NOTICE));
    (void)openulog(progName, mls_getLogOpts(logFileSpec), LOG_LDM, logFileSpec);
}

/**
 * Initializes the sets of signals that are used to ensure that only certain
 * threads receive certain signals.
 */
static void
mls_initSignalSets(void)
{
    (void)sigemptyset(&termSigSet);
    (void)sigaddset(&termSigSet, SIGTERM);
    (void)sigaddset(&termSigSet, SIGINT);

    (void)sigemptyset(&pqSigSet);
    (void)sigaddset(&pqSigSet, SIGCONT);
    (void)sigaddset(&pqSigSet, SIGALRM);
}

/**
 * Logs a usage message.
 */
static void
mls_usage(void)
{
    log_add(
"Usage: %s [options] feed groupId:groupPort serverPort\n"
"Options:\n"
"    -I serverIface    Interface on which the TCP server will listen. Default\n"
"                      is all interfaces.\n"
"    -l logfile        Log to file <logfile> ('-' => standard error stream).\n"
"                      Defaults are standard error stream if interactive and\n"
"                      system logging daemon if not.\n"
"    -q queue          Use product-queue <queue>. Default is \"%s\".\n"
"    -t ttl            Time-to-live of outgoing packets (default is 1):\n"
"                           0  Restricted to same host. Won't be output by\n"
"                              any interface.\n"
"                           1  Restricted to the same subnet. Won't be\n"
"                              forwarded by a router (default).\n"
"                         <32  Restricted to the same site, organization or\n"
"                              department.\n"
"                         <64  Restricted to the same region.\n"
"                        <128  Restricted to the same continent.\n"
"                        <255  Unrestricted in scope. Global.\n"
"    -v                Verbose logging: log INFO level messages.\n"
"    -x                Debug logging: log DEBUG level messages.\n"
"Operands:\n"
"    feed              Identifier of the multicast group in the form of a\n"
"                      feedtype expression\n"
"    groupId:groupPort Internet service address of multicast group, where\n"
"                      <groupId> is either a group-name or a dotted-decimal\n"
"                      IPv4 address and <groupPort> is the port number.\n"
"    serverPort        Port number for VCMTP TCP server.",
            getulogident(), getQueuePath());
}

/**
 * Decodes the options of the command-line.
 *
 * @pre                     {`openulog()` has already been called.}
 * @param[in]  argc         Number of arguments.
 * @param[in]  argv         Arguments.
 * @param[out] serverIface  Interface on which TCP server should listen. Caller
 *                          must not free.
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
    int                         argc,
    char* const* const restrict argv,
    const char** const restrict serverIface,
    unsigned* const restrict    ttl)
{
    int          ch;
    extern int   opterr;
    extern int   optopt;
    extern char* optarg;

    opterr = 1; // prevent getopt(3) from trying to print error messages

    while ((ch = getopt(argc, argv, ":I:l:q:t:vx")) != EOF)
        switch (ch) {
        case 'I': {
            *serverIface = optarg;
            break;
        }
        case 'l': {
            (void)openulog(NULL, mls_getLogOpts(optarg), LOG_LDM, optarg);
            break;
        }
        case 'q': {
            setQueuePath(optarg);
            break;
        }
        case 't': {
            unsigned t;
            int      nbytes;
            if (1 != sscanf(optarg, "%u %n", &t, &nbytes) ||
                    0 != optarg[nbytes]) {
                log_start("Couldn't decode time-to-live option-argument \"%s\"",
                        optarg);
                return 1;
            }
            if (t >= 255) {
                log_start("Invalid time-to-live option-argument \"%s\"", optarg);
                return 1;
            }
            *ttl = t;
            break;
        }
        case 'v': {
            (void)setulogmask(setulogmask(0) | LOG_MASK(LOG_INFO));
            break;
        }
        case 'x': {
            (void)setulogmask(setulogmask(0) | LOG_MASK(LOG_DEBUG));
            break;
        }
        case ':': {
            LOG_START1("Option \"%c\" requires an argument", optopt);
            mls_usage();
            return 1;
        }
        default: {
            LOG_ADD1("Unknown option: \"%c\"", optopt);
            mls_usage();
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
 * @retval     2            System failure. `log_start()` called.
 */
static int
mls_setServiceAddr(
    const char* const    id,
    const unsigned short port,
    ServiceAddr** const  serviceAddr)
{
    ServiceAddr* sa = sa_new(id, port);

    if (sa == NULL)
        return 2;

    *serviceAddr = sa;

    return 0;
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
    ServiceAddr*   sa;
    int            status = sa_parse(&sa, arg);

    if (ENOMEM == status) {
        status = 2;
    }
    else if (status) {
        log_add("Invalid multicast group specification");
    }
    else {
        *groupAddr = sa;
    }

    return status;
}

/**
 * Decodes the Internet service address of the TCP server.
 *
 * @param[in]  arg          Relevant operand.
 * @param[in]  serverIface  Interface on which the TCP server should listen. May
 *                          be a name or a formatted IP address. Caller may
 *                          free.
 * @param[out] serverAddr   Internet service address of the TCP server. Caller
 *                          should free when it's no longer needed.
 * @retval     0            Success. `*serverAddr` is set.
 * @retval     1            `arg == NULL` or invalid operand. `log_start()`
 *                          called.
 * @retval     2            System failure. `log_start()` called.
 */
static int
mls_decodeServerAddr(
    char* const restrict          arg,
    const char* const restrict    serverIface,
    ServiceAddr** const restrict  serverAddr)
{
    int            status;

    if (arg == NULL) {
        log_start("NULL argument");
        status = 1;
    }
    else {
        unsigned short port;
        int            nbytes;

        if (1 != sscanf(arg, "%hu %n", &port, &nbytes) || 0 != arg[nbytes]) {
            log_start("Couldn't decode port number \"%s\"", arg);
            status = 1;
        }
        else {
            status = mls_setServiceAddr(serverIface, port, serverAddr);
        }
    }

    return status;
}

/**
 * Decodes the operands of the command-line.
 *
 * @param[in]  argc         Number of operands.
 * @param[in]  argv         Operands.
 * @param[in]  serverIface  Interface on which the TCP server should listen. May
 *                          be a name or a formatted IP address. Caller may
 *                          free.
 * @param[out] groupAddr    Internet service address of the multicast group.
 *                          Caller should free when it's no longer needed.
 * @param[out] serverAddr   Internet service address of the TCP server. Caller
 *                          should free when it's no longer needed.
 * @param[out] feed         Feedtype of the multicast group.
 * @retval     0            Success. `*groupAddr`, `*serverAddr`, and `*feed`
 *                          are set.
 * @retval     1            Invalid operands. `log_start()` called.
 * @retval     2            System failure. `log_start()` called.
 */
static int
mls_decodeOperands(
    int                           argc,
    char* const* restrict         argv,
    const char* const restrict    serverIface,
    ServiceAddr** const restrict  groupAddr,
    ServiceAddr** const restrict  serverAddr,
    feedtypet* const restrict     feed)
{
    int status;

    if (argc < 1) {
        log_start("Multicast group not specified");
        mls_usage();
        status = 1;
    }
    else {
        feedtypet ft;

        if (strfeedtypet(*argv, &ft)) {
            log_start("Invalid feed expression: \"%s\"", *argv);
        }
        else {
            argc--; argv++;

            ServiceAddr* grpAddr;
            ServiceAddr* srvrAddr;

            if ((status = mls_decodeGroupAddr(*argv, &grpAddr))) {
                mls_usage();
                status = 1;
            }
            else {
                argc--; argv++;

                if ((status = mls_decodeServerAddr(*argv, serverIface, &srvrAddr))) {
                    log_start("Port number of TCP server unspecified or invalid");
                    mls_usage();
                    status = 1;
                }
                else {
                    *feed = ft;
                    *groupAddr = grpAddr;
                    *serverAddr = srvrAddr;
                }
            }
        }
    }

    return status;
}

/**
 * Decodes the command line.
 *
 * @param[in]  argc        Number of arguments.
 * @param[in]  argv        Arguments.
 * @param[out] groupInfo   Multicast group information.
 * @param[out] ttl         Time-to-live of outgoing packets.
 *                               0  Restricted to same host. Won't be output by
 *                                  any interface.
 *                               1  Restricted to the same subnet. Won't be
 *                                  forwarded by a router (default).
 *                             <32  Restricted to the same site, organization or
 *                                  department.
 *                             <64  Restricted to the same region.
 *                            <128  Restricted to the same continent.
 *                            <255  Unrestricted in scope. Global.
 * @retval     0           Success. `*groupInfo` is set. `*ttl` might be set.
 * @retval     1           Invalid command line. `log_start()` called.
 * @retval     2           System failure. `log_start()` called.
 */
static int
mls_decodeCommandLine(
    int                         argc,
    char* const* restrict       argv,
    McastInfo** const restrict  groupInfo,
    unsigned* const restrict    ttl)
{
    const char*  serverIface = "0.0.0.0"; // default: all interfaces
    int          status = mls_decodeOptions(argc, argv, &serverIface, ttl);
    extern int   optind;

    if (0 == status) {
        argc -= optind;
        argv += optind;

        ServiceAddr* groupAddr;
        ServiceAddr* serverAddr;
        feedtypet    feed;

        status = mls_decodeOperands(argc, argv, serverIface, &groupAddr,
                &serverAddr, &feed);

        if (status == 0) {
            McastInfo* gi = mi_new(feed, groupAddr, serverAddr);

            if (gi == NULL) {
                status = 2;
            }
            else {
                *groupInfo = gi;
                status  = 0;
            }

            sa_free(groupAddr);
            sa_free(serverAddr);
        }
    }

    return status;
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
 * Opens the file-identifier map for updating.
 *
 * @param[in] mls          Multicast LDM sender object.
 * @param[in] feed         Feedtype to be multicast.
 * @param[in] maxSigs      Maximum number of signatures that the file-identifier
 *                         must contain.
 * @retval    0            Success.
 * @retval    LDM7_INVAL   Maximum number of signatures isn't positive.
 *                         `log_add()` called. The file wasn't opened or
 *                         created.
 * @retval    LDM7_SYSTEM  System error. `log_add()` called.
 */
static Ldm7Status
mls_openFileIdMap(
        McastLdmSender* const restrict  mls,
        const feedtypet                 feed,
        const size_t                    maxSigs)
{
    int         status;
    /* Queue pathname is cloned because `dirname()` may alter its argument */
    char* const queuePathname = strdup(getQueuePath());

    if (NULL == queuePathname) {
        LOG_SERROR0("Couldn't duplicate product-queue pathname");
        status = LDM7_SYSTEM;
    }
    else {
        char* const mapPathname = ldm_format(256, "%s/%s.map",
                dirname(queuePathname), s_feedtypet(feed));

        if (NULL == mapPathname) {
            LOG_ADD0("Couldn't construct pathname of file-identifier map");
            status = LDM7_SYSTEM;
        }
        else {
            status = fim_openForWriting(mapPathname, maxSigs);
            free(mapPathname);
        } // `mapPathname` allocated

        free(queuePathname);
    } // `queuePathname` allocated

    return status;
}

/**
 * Initializes a multicast LDM sender.
 *
 * @param[in]  mls          The multicast LDM sender.
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
    McastLdmSender* const restrict  mls,
    const McastInfo* const restrict info,
    const unsigned                  ttl,
    const char* const restrict      pqPathname)
{
    char         serverInetAddr[INET_ADDRSTRLEN];
    int          status = mls_getIpv4Addr(info->server.inetId, "server",
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

    if ((status = mls_openFileIdMap(mls, info->feed, pq_getSlotCount(pq))))
        goto close_pq;

    if ((status = fim_getNextFileId(&mls->fileId)))
        goto close_fileId_map;

    if ((status = mcastSender_new(&mls->mcastSender, serverInetAddr,
            info->server.port, groupInetAddr, info->group.port, ttl,
            mls->fileId))) {
        status = (status == EINVAL) ? LDM7_INVAL : LDM7_SYSTEM;
        goto close_fileId_map;
    }

    if (mi_copy(&mls->mcastInfo, info)) {
        status = LDM7_SYSTEM;
        goto free_mcastSender;
    }

    mls->done = 0;

    return 0;

free_mcastSender:
    mcastSender_free(mls->mcastSender);
close_fileId_map:
    (void)fim_close();
close_pq:
    (void)pq_close(pq);
return_status:
    return status;
}

/**
 * Destroys a multicast LDM sender. Releases the resources associated with the
 * object's fields but doesn't release the object itself.
 *
 * @param[in]  mls          The multicast LDM sender.
 * @pre                     {`mls_startMulticasting(mls)` was never called or
 *                          `mls_stopMulticasting(mls)` was called.}
 */
static inline void
mls_destroy(
    McastLdmSender* const restrict  mls)
{
    /* Releasing fields but not the object is what `xdr_free()` does. */
    (void)xdr_free(xdr_McastInfo, (char*)&mls->mcastInfo);
    (void)pq_close(pq);
    mcastSender_free(mls->mcastSender);
}

/**
 * Multicasts a single data-product. Called by `pq_sequence()`.
 *
 * @param[in] info   Pointer to the data-product's metadata.
 * @param[in] data   Pointer to the data-product's data.
 * @param[in] xprod  Pointer to an XDR-encoded version of the data-product (data
 *                   and metadata).
 * @param[in] size   Size, in bytes, of the XDR-encoded version.
 * @param[in] arg    Pointer to associated multicast LDM sender object.
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
    McastLdmSender* const mls = arg;
    int                   status = mcastSender_send(mls->mcastSender, xprod,
            size);

    if (0 == status) {
        prod_info info;
        XDR       xdrs;

        xdrmem_create(&xdrs, xprod, size, XDR_DECODE);

        if (!xdr_prod_info(&xdrs, &info)) {
            LOG_SERROR0("Couldn't decode LDM product-metadata");
            status = EIO;
        }
        else {
            status = fim_put(mls->fileId++, &info.signature);
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
 * @param[in]  mls          Multicast LDM sender.
 * @param[out] prodClass    Product-class for selecting data-products. Caller
 *                          should call `free_prod_class(*prodClass)` when it's
 *                          no longer needed.
 * @retval     0            Success. `*prodClass` is set.
 * @retval     LDM7_INVAL   Invalid parameter. `log_start()` called.
 * @retval     LDM7_SYSTEM  System error. `log_start()` called.
 */
static int
mls_setProdClass(
    McastLdmSender* const restrict mls,
    prod_class** const restrict    prodClass)
{
    int         status;
    /* The following sets `pc->psa.psa_len` but all the patterns are NULL */
    prod_class* pc = new_prod_class(1);

    if (pc == NULL) {
        status = LDM7_SYSTEM;
    }
    else {
        (void)set_timestamp(&pc->from); // from now
        pc->to = TS_ENDT;               // until the end-of-time

        pc->psa.psa_val->feedtype = mls->mcastInfo.feed;
        pc->psa.psa_val->pattern = strdup(".*");

        if (pc->psa.psa_val->pattern == NULL) {
            LOG_SERROR0("Couldn't duplicate pattern \".*\"");
            status = LDM7_SYSTEM;
        }
        else {
            clss_regcomp(pc);
            *prodClass = pc;
            status = 0;
        } // `pc->psa.psa_val->pattern` allocated

        if (status)
            free_prod_class(pc); // won't free NULL patterns
    } // `pc` allocated

    return status;
}

/**
 * Tries to multicast the next data-product from a multicast LDM sender's
 * product-queue. Will block for a short time or until a SIGCONT is received if
 * the next data-product doesn't exist.
 *
 * @param[in] mls        Multicast LDM sender.
 * @param[in] prodClass  Class of data-products to multicast.
 * @retval    0          Success.
 * @return               `<errno.h>` error-code from `pq_sequence()`.
 * @return               Return-code from `mls_multicastProduct()`.
 */
static inline int
mls_tryMulticast(
    McastLdmSender* const restrict mls,
    prod_class* const restrict     prodClass)
{
    int status = pq_sequence(pq, TV_GT, prodClass, mls_multicastProduct, mls);

    if (status == PQUEUE_END) {
        /*
         * No matching data-product. Block for a short time or until a SIGCONT
         * is received by this thread. NB: `ps_suspend()` ensures that SIGCONT
         * is unblocked for it.
         */
        (void)pq_suspend(30);
        status = 0;           // no problems here
    }

    return status;
}

/**
 * Blocks external termination signals for the current thread.
 */
static inline void
mls_blockTermSignals(void)
{
    (void)pthread_sigmask(SIG_BLOCK, &termSigSet, NULL);
}

/**
 * Blocks signals used by the product-queue for the current thread.
 */
static inline void
mls_blockPqSignals(void)
{
    (void)pthread_sigmask(SIG_BLOCK, &pqSigSet, NULL);
}

/**
 * Waits for a termination signal and then stops a multicast LDM sender. Start
 * routine for a new thread.
 *
 * @param[in] arg   Multicast LDM sender.
 * @return    NULL  Always.
 * @pre             {`termSigSet` signals are blocked.}
 */
static void*
mls_waitForTermSig(
    void* const arg)
{
    McastLdmSender* const mls = arg;
    int                   sig;

    (void)sigwait(&termSigSet, &sig);
    mls->done = 1;
    /*
     * A SIGCONT is sent to the multicast thread in case it's blocked in
     * `pq_suspend()`.
     */
    (void)pthread_kill(mls->mcastThread, SIGCONT);

    return NULL;
}

/**
 * Starts a new thread that will stop a multicast LDM sender when it receives a
 * termination signal.
 *
 * @param[in] mls          Multicast LDM sender.
 * @retval    0            Success.
 * @retval    LDM7_SYSTEM  System failure. `log_start()` called.
 */
static Ldm7Status
mls_startTermSigWaiter(
    McastLdmSender* const mls)
{
    pthread_t thread;
    int       status = pthread_create(&thread, NULL, mls_waitForTermSig, mls);

    if (status) {
        LOG_ERRNUM0(status, "Couldn't start termination-waiting thread");
        status = LDM7_SYSTEM;
    }
    else {
        (void)pthread_detach(thread);
    }

    return status;
}

/**
 * Starts multicasting data-products.
 *
 * @pre                     {`mls_init(mls)` was called.}
 * @param[in]  mls          Multicast LDM sender.
 * @retval     0            Success. `mls->thread` is set.
 * @retval     LDM7_SYSTEM  Error. `log_start()` called.
 */
static Ldm7Status
mls_startMulticasting(
        McastLdmSender* const mls)
{
    prod_class* prodClass;
    int         status = mls_setProdClass(mls, &prodClass);

    if (status == 0) {
        pq_cset(pq, &prodClass->from);

        do {
            status = mls_tryMulticast(mls, prodClass);
        } while (0 == status && !mls->done);

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
    McastLdmSender mls;
    int            status = mls_init(&mls, info, ttl, pqPathname);

    if (status) {
        LOG_ADD0("Couldn't initialize multicast LDM sender");
    }
    else {
        /*
         * Block all external signals that would, otherwise, terminate this
         * process to ensure that only the proper thread receives that signal.
         * VCMTP uses multiple threads and which thread receives a signal isn't
         * deterministic -- so I think this is the way to go.
         */
        mls_blockTermSignals();
        /*
         * Block signals used by the product-queue so that they will only be
         * received by a thread that's accessing the product queue.
         */
        mls_blockPqSignals();

        /*
         * Data-products are multicast on the current (main) thread so that the
         * process will automatically terminate if something goes wrong.
         */

        mls.mcastThread = pthread_self(); // needed by termination-signal waiter
        status = mls_startTermSigWaiter(&mls);

        if (status == 0)
            status = mls_startMulticasting(&mls);

        mls_destroy(&mls);
    } // `mls` initialized

    return status;
}

/**
 * Multicasts data-products to a multicast group.
 *
 * @param[in] argc  Number of arguments.
 * @param[in] argv  Arguments. See [mls_usage()](@ref mls_usage)
 * @retval    0     Success.
 * @retval    1     Invalid command line. `log_log(LOG_ERR)` called.
 * @retval    2     System failure. `log_log(LOG_ERR)` called.
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
    mls_initLogging(basename(argv[0]));

    /* Initialize sets of signals that will be used later. */
    mls_initSignalSets();

    /* Decode the command-line. */
    McastInfo*   groupInfo;  // multicast group information
    unsigned     ttl = 1;    // Restrict to same subnet. Won't be forwarded.
    int          status = mls_decodeCommandLine(argc, argv, &groupInfo, &ttl);

    if (status) {
        log_log(LOG_ERR);
    }
    else {
        if (mls_execute(groupInfo, ttl, getQueuePath())) {
            log_log(LOG_ERR);
            status = 2;
        }

        mi_free(groupInfo);
    } // `groupInfo` allocated

    return status;
}
