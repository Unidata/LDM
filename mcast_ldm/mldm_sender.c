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

#include "globals.h"
#include "ldm.h"
#include "log.h"
#include "mcast.h"
#include "mcast_info.h"
#include "mldm_sender_memory.h"
#include "pq.h"
#include "StrBuf.h"

#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

static void
usage(void)
{
    log_start(
"Usage: %s [options] groupId:groupPort serverPort\n"
"Options:\n"
"    -I serverIface    Interface on which the TCP server will listen. Default\n"
"                      is all interfaces.\n"
"    -l logfile        Log to file <logfile> ("-" => standard error stream).\n"
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
"    groupId:groupPort Internet service address of multicast group, where\n"
"                      <groupId> is either a group-name or a dotted-decimal\n"
"                      IPv4 address and <groupPort> is the port number.\n"
"    serverPort        Port number of TCP server.",
            getulogident(), getQueuePath());
}

/**
 * Decodes the options of the command-line.
 *
 * @pre                     `openulog()` has already been called.
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
decodeOptions(
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

    while ((ch = getopt(argc, argv, ":l:q:t:vx")) != EOF)
        switch (ch) {
        case 'I': {
            *serverIface = optarg;
            break;
        }
        case 'l': {
            (void)openulog(NULL, ulog_get_options(), LOG_LDM, optarg);
            break;
        }
        case 'q': {
            setQueuePath(optarg);
            break;
        }
        case 't': {
            unsigned t;
            if (sscanf(optarg, "%u", &t) != 1) {
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
            usage();
            return 1;
        }
        default: {
            LOG_ADD1("Unknown option: \"%c\"", optopt);
            usage();
            return 1;
        }
        }

    return 0;
}

/**
 * Decodes an Internet service address. An Internet service address has the form
 * `[id:]port`, where `id` is the Internet identifier (either a name or a
 * dotted-decimal IPv4 address) and `port` is the port number.
 *
 * @param[in,out] arg    String containing the specification. A NUL character
 *                       will replace the colon if it exists. Caller must not
 *                       free until `*ident` is no longer needed.
 * @param[in]     desc   Description of the service address suitable for the
 *                       phrase "Couldn't decode Internet service address of
 *                       ...". Caller may free.
 * @param[out]    ident  Internet identifier of the Internet service address.
 *                       Will be set to NULL if and only if the argument doesn't
 *                       contain an identifier.
 * @param[out]    port   Port number of the Internet service address.
 * @retval        0      Success. `*ident` and `*port` are set.
 * @retval        1      Invalid specifier. `log_start()` called.
 */
static int
decodeServiceAddr(
    char* restrict              arg,
    const char* const restrict  desc,
    const char** const restrict ident,
    unsigned short* restrict    port)
{
    int         status;
    char* const colon = strchr(arg, ':');
    char*       id;

    if (colon == NULL) {
        id = NULL;
    }
    else {
        *colon = 0;
        id = arg;
        arg = colon + 1;
    }

    if (sscanf(arg, "%hu", port) != 1) {
        log_start("Couldn't decode port number of %s", desc);
        status = 1;
    }
    else {
        *ident = id;
        status = 0;
    }

    return status;
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
setServiceAddr(
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
decodeGroupAddr(
    char* const restrict         arg,
    ServiceAddr** const restrict groupAddr)
{
    const char*    id;
    unsigned short port;

    int status = decodeServiceAddr(arg, "multicast group", &id, &port);

    if (status == 0) {
        if (id == NULL) {
            log_start("Internet identifier of multicast group not specified");
            status = 1;
        }
        else {
            status = setServiceAddr(id, port, groupAddr);
        }
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
 * @retval     1            Invalid operand. `log_start()` called.
 * @retval     2            System failure. `log_start()` called.
 */
static int
decodeServerAddr(
    char* const restrict          arg,
    const char* const restrict    serverIface,
    ServiceAddr** const restrict  serverAddr)
{
    int            status;
    unsigned short port;

    if (sscanf(arg, "%hu", port) != 1) {
        log_start("Couldn't decode port number \"%s\"", arg);
        status = 1;
    }
    else {
        status = setServiceAddr(serverIface, port, serverAddr);
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
 * @retval    0             Success.
 * @retval    1             Invalid operands. `log_start()` called.
 * @retval    2             System failure. `log_start()` called.
 */
static int
decodeOperands(
    int                           argc,
    char* const* restrict         argv,
    const char* const restrict    serverIface,
    ServiceAddr** const restrict  groupAddr,
    ServiceAddr** const restrict  serverAddr)
{
    int status;
    ServiceAddr* grpAddr;
    ServiceAddr* srvrAddr;

    if (argc < 1 || (status = decodeGroupAddr(*argv, &grpAddr))) {
        log_start("Unspecified or invalid Internet service address of "
                "multicast group");
        usage();
        status = 1;
    }
    else {
        argc--; argv++;

        if (argc < 1 || (status = decodeServerAddr(*argv, serverIface,
                &srvrAddr))) {
            log_start("Unspecified or invalid port number of TCP server");
            usage();
            status = 1;
        }
        else {
            *groupAddr = grpAddr;
            *serverAddr = srvrAddr;
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
decodeCommandLine(
    int                         argc,
    char* const* restrict       argv,
    McastInfo** const restrict  groupInfo,
    unsigned* const restrict    ttl)
{
    const char*  serverIface = "0.0.0.0"; // default: all interfaces
    int          status = decodeOptions(argc, argv, &serverIface, ttl);
    extern int   optind;

    if (status == 0) {
        argc -= optind;
        argv += optind;

        ServiceAddr* groupAddr;
        ServiceAddr* serverAddr;

        status = decodeOperands(argc, argv, serverIface, &groupAddr,
                &serverAddr);

        if (status == 0) {
            McastInfo* gi = mi_new("", groupAddr, serverAddr);

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
 * @param[in]  entity       The description of the entity associated with the
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
getIpv4Addr(
    const char* const restrict inetId,
    const char* const restrict entity,
    char* const restrict       buf)
{
    int status = getDottedDecimal(inetId, buf);

    if (status == 0)
        return 0;

    LOG_ADD1("Couldn't get address of %s", entity);

    return (status == EINVAL || status == ENOENT)
            ? LDM7_INVAL
            : LDM7_SYSTEM;
}

/**
 * Creates a multicast LDM sender.
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
 * @param[out] sender       The multicast LDM sender.
 * @retval     0            Success. `*sender` is set.
 * @retval     LDM7_INVAL   An Internet identifier couldn't be converted to an
 *                          IPv4 address because it's invalid or unknown.
 *                          `log_start()` called.
 * @retval     LDM7_SYSTEM  System error. `log_start()` called.
 */
static Ldm7Status
mls_createMulticastSender(
    const McastInfo* const restrict info,
    const unsigned                  ttl,
    McastSender** const restrict    sender)
{
    char         serverInetAddr[INET_ADDRSTRLEN];
    int          status = getIpv4Addr(info->server.inetId, "server",
            serverInetAddr);

    if (status == 0) {
        char groupInetAddr[INET_ADDRSTRLEN];

        status = getIpv4Addr(info->group.inetId, "multicast-group",
                groupInetAddr);

        if (status == 0) {
            status = mcastSender_new(sender, serverInetAddr, info->server.port,
                    groupInetAddr, info->group.port, ttl);

            if (status == -1)
                status = LDM7_SYSTEM;
        }
    }

    return status;
}

/**
 * Sends data-products to a multicast group.
 *
 * @param[in]  pq           The product-queue from which data-products are read.
 * @param[out] sender       The multicast LDM sender.
 * @retval     0            Termination was requested.
 * @retval     LDM7_SYSTEM  Failure. `log_start()` called.
 */
static Ldm7Status
mls_multicastProducts(
    pqueue* const restrict      pq,
    McastSender* const restrict sender)
{
    LOG_START0("Unimplemented");
    return LDM7_SYSTEM;
}

/**
 * Destroys a sender of data to a multicast group.
 *
 * @param[in] sender  The sender of data to a multicast group.
 */
static void
mls_destroyMulticastSender(
    McastSender* const restrict sender)
{
    mcastSender_free(sender);
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
    int status = pq_open(pqPathname, PQ_READONLY, &pq);

    if (status) {
        LOG_START1("Couldn't open product-queue \"%s\"", pqPathname);
        status = LDM7_SYSTEM;
    }
    else {
        McastSender* sender;

        status = mls_createMulticastSender(info, ttl, &sender);

        if (status == 0) {
            status = mls_multicastProducts(pq, sender);
            mls_destroyMulticastSender(sender);
        } // `sender` created

        (void)pq_close(pq);
    } // `pq` open

    return status;
}

int
main(
    const int    argc,
    char** const argv)
{
    /*
     * Initialize logging.
     */
    {
        unsigned logmask =  LOG_UPTO(LOG_NOTICE); // logging level
        unsigned logoptions = LOG_CONS | LOG_PID; // console last resort, PID

        if (isatty(fileno(stderr))) {
            /* Interactive execution. Modify logging defaults. */
            logfname = "-"; // log to `stderr`
            logoptions = 0; // timestamp, UTC, no console, no PID
        }
        (void)setulogmask(logmask);
        (void)openulog(basename(argv[0]), logoptions, LOG_LDM, logfname);
    }

    ServiceAddr* groupAddr;  // Internet service address of the multicast group:
    ServiceAddr* serverAddr; // Internet service address of the TCP server:
    McastInfo*   groupInfo;  // multicast group information
    unsigned     ttl = 1;    // Restricted to same subnet. Won't be forwarded.
    int          status = decodeCommandLine(argc, argv, &groupInfo, &ttl);

    if (status) {
        log_log(LOG_ERR);
    }
    else {
        status = mls_execute(groupInfo, ttl, getQueuePath());

        mi_free(groupInfo);
    }

    return status;
}
