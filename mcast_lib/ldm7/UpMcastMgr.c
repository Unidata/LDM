/**
 * This file implements the manager for multicasting from the upstream site.
 * The manager is designed to be populated by the LDM configuration-file parser
 * and then accessed by the individual upstream LDM7 processes. Populating the
 * manager causes the Internet Address Manager (inam_*) to be initialized.
 *
 * Copyright 2018 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: UpMcastMgr.c
 * @author: Steven R. Emmerson
 *
 * The functions in this file are thread-compatible but not thread-safe.
 */

#include "config.h"

#include "AuthClient.h"
#include "CidrAddr.h"
#include "globals.h"
#include "ldmprint.h"
#include "log.h"
#include "mcast.h"
#include "mcast_info.h"
#include "MldmRpc.h"
#include "mldm_sender_map.h"
#include "StrBuf.h"
#include "UpMcastMgr.h"

#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <search.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

/**
 * Concatenates arguments; inserts a single space between arguments.
 *
 * @param[in] args  The arguments to be concatenated.
 * @return          A new string buffer. The caller should pass it to `sbFree()`
 *                  when it's no longer needed.
 */
static StrBuf*
catenateArgs(
        const char** args) {
    StrBuf* buf = sbNew();

    while (*args)
        sbCatL(buf, *args++, " ", NULL);

    return sbTrim(buf);
}

/**
 * Allows certain signals to be received by the current thread. Idempotent.
 */
static void
allowSigs(void)
{
    sigset_t sigset;

    (void)sigemptyset(&sigset);
    (void)sigaddset(&sigset, SIGINT);  // for termination
    (void)sigaddset(&sigset, SIGTERM); // for termination
    (void)pthread_sigmask(SIG_UNBLOCK, &sigset, NULL);
}

/******************************************************************************
 * Multicast LDM process:
 ******************************************************************************/

static volatile pid_t childPid;
static feedtypet      feed;

static void
mldm_killChild(void)
{
    if (childPid) {
        (void)kill(childPid, SIGTERM);
        childPid = 0;
        feed = NONE;
    }
}

static int
mldm_ensureCleanup(const pid_t pid)
{
    if (childPid)
        return 0;

    int status = atexit(mldm_killChild);
    if (status) {
        log_add_syserr("Couldn't register cleanup routine");
        status = LDM7_SYSTEM;
    }
    else {
        childPid = pid;
    }
    return status;
}

/**
 * Indicates if a particular multicast LDM sender is running.
 *
 * @pre                      Multicast LDM sender PID map is locked for writing.
 * @param[in]  feedtype      Feed-type of multicast group.
 * @param[out] port          Port number of the FMTP TCP server.
 * @param[out] mldmSrvrPort  Port number of multicast LDM RPC server
 * @retval     0             The multicast LDM sender associated with the given
 *                           multicast group is running. `*pid` and `*port` are
 *                           set.
 * @retval     LDM7_NOENT    No such process.
 * @retval     LDM7_SYSTEM   System error. `log_add()` called.
 */
static Ldm7Status
mldm_isRunning(
        const feedtypet                feedtype,
        unsigned short* const restrict port,
        unsigned short* const restrict mldmSrvrPort)
{
    pid_t          msmPid;
    unsigned short msmPort;
    unsigned short msmMldmSrvrPort;
    int   status = msm_get(feedtype, &msmPid, &msmPort, &msmMldmSrvrPort);

    if (status == 0) {
        if (kill(msmPid, 0) == 0) {
            /* Can signal the process */
            *port = msmPort;
            *mldmSrvrPort = msmMldmSrvrPort;
        }
        else {
            /* Can't signal the process */
            log_warning("According to my information, the PID of the multicast LDM "
                    "sender associated with feed-type %s is %d -- but that "
                    "process can't be signaled by this process. I'll assume "
                    "the relevant multicast LDM sender is not running.",
                    s_feedtypet(feedtype), msmPid);
            (void)msm_remove(msmPid);   // don't care if it exists or not
            status = LDM7_NOENT;
        }
    }

    return status;
}

/**
 * Gets the port number of the multicast FMTP TCP server and the multicast LDM
 * RPC server from a multicast LDM sender process that writes them to a pipe.
 *
 * @param[in]  pipe          Pipe for reading from the multicast LDM sender
 *                           process
 * @param[out] fmtpSrvrPort  Port number of FMTP TCP server in host byte-order
 * @param[out] mldmSrvrPort  Port number of multicast LDM RPC server in host
 *                           byte-order
 * @retval     0             Success. `*fmtpSrvrPort` and `*mldmSrvrPort` are
 *                           set
 * @retval     LDM7_SYSTEM   System failure. `log_add()` called.
 * @retval     LDM7_LOGIC    Logic failure. `log_add()` called.
 */
static Ldm7Status
mldm_getServerPorts(
    const int                      pipe,
    unsigned short* const restrict fmtpSrvrPort,
    unsigned short* const restrict mldmSrvrPort)
{
    int   status;
    FILE* stream = fdopen(pipe, "r");
    if (stream == NULL) {
        log_add_syserr("Couldn't associate stream with pipe");
        status = LDM7_SYSTEM;
    }
    else {
        if (fscanf(stream, "%hu %hu ", fmtpSrvrPort, mldmSrvrPort) != 2) {
            log_add("Couldn't decode port numbers for multicast FMTP server "
                    "and RPC server");
            status = LDM7_LOGIC;
        }
        else {
            status = LDM7_OK;
        }
        fclose(stream);
    }
    return status;
}

/**
 * Executes the multicast LDM sender program. If this function returns, then an
 * error occurred and `log_add()` was called. The multicast LDM sender process
 * inherits the following from this process:
 *     - The LDM log;
 *     - The logging level; and
 *     - The LDM product-queue;
 *
 * @param[in] info           Information on the multicast group.
 * @param[in] ttl            Time-to-live for the multicast packets:
 *                                0  Restricted to same host. Won't be output by
 *                                   any interface.
 *                                1  Restricted to same subnet. Won't be
 *                                   forwarded by a router.
 *                              <32  Restricted to same site, organization or
 *                                   department.
 *                              <64  Restricted to same region.
 *                             <128  Restricted to same continent.
 *                             <255  Unrestricted in scope. Global.
 * @param[in] fmtpSubnet     Subnet for client FMTP TCP connections
 * @param[in] pqPathname     Pathname of product-queue. Caller may free.
 * @param[in] pipe           Pipe for writing to parent process.
 */
static void
mldm_exec(
    const McastInfo* const restrict info,
    const unsigned short            ttl,
    const CidrAddr* const restrict  fmtpSubnet,
    const char* const restrict      pqPathname,
    const int const                 pipe)
{
    char* args[23]; // Keep sufficiently capacious (search for `i\+\+`)
    int   i = 0;

    args[i++] = "mldm_sender";

    char feedtypeBuf[256];
    if (info->feed != EXP) {
        (void)sprint_feedtypet(feedtypeBuf, sizeof(feedtypeBuf), info->feed);
        args[i++] = "-f";
        args[i++] = feedtypeBuf; // multicast group identifier
    }

    char* arg = (char*)log_get_destination(); // safe cast
    if (arg != NULL) {
        args[i++] = "-l";
        args[i++] = arg;
    }

    int logOptions = log_get_options();

    char serverPortOptArg[6];
    if (info->server.port != 0) {
        ssize_t nbytes = snprintf(serverPortOptArg, sizeof(serverPortOptArg),
                "%hu", info->server.port);
        if (nbytes < 0 || nbytes >= sizeof(serverPortOptArg)) {
            log_add("Couldn't create server-port option-argument \"%hu\"",
                    info->server.port);
            goto failure;
        }
        else {
            args[i++] = "-p";
            args[i++] = serverPortOptArg;
        }
    }

    args[i++] = "-q";
    args[i++] = (char*)pqPathname; // safe cast

    if (info->server.inetId && strcmp(info->server.inetId, "0.0.0.0")) {
        args[i++] = "-s";
        args[i++] = info->server.inetId;
    }

    char ttlOptArg[4];
    if (ttl != 1) {
        ssize_t nbytes = snprintf(ttlOptArg, sizeof(ttlOptArg), "%hu", ttl);
        if (nbytes < 0 || nbytes >= sizeof(ttlOptArg)) {
            log_add("Couldn't create time-to-live option-argument \"%hu\"",
                    ttl);
            goto failure;
        }
        args[i++] = "-t";
        args[i++] = ttlOptArg;
    }

    if (log_is_enabled_info)
        args[i++] = "-v";
    if (log_is_enabled_debug)
        args[i++] = "-x";

    char* mcastGroupOperand = ldm_format(128, "%s:%hu", info->group.inetId,
            info->group.port);
    if (mcastGroupOperand == NULL) {
        log_add("Couldn't create multicast-group operand");
        goto failure;
    }
    args[i++] = mcastGroupOperand;

    char* fmtpSubnetArg = cidrAddr_format(fmtpSubnet);
    if (fmtpSubnetArg == NULL) {
        goto fmtpNetFailure;
    }
    else {
        args[i++] = fmtpSubnetArg;
    }

    args[i++] = NULL;

    StrBuf* command = catenateArgs((const char**)args); // Safe cast
    log_notice("Executing multicast LDM sender: %s", sbString(command));
    sbFree(command);

    (void)dup2(pipe, 1);
    execvp(args[0], args);

    log_add_syserr("Couldn't execute multicast LDM sender \"%s\"; PATH=%s",
            args[0], getenv("PATH"));
    free(fmtpSubnetArg);
fmtpNetFailure:
    free(mcastGroupOperand);
failure:
    return;
}

/**
 * Executes a multicast LDM sender as a child process. Doesn't block.
 *
 * @param[in,out] info           Information on the multicast group.
 * @param[out]    mldmSrvrPort   Port number of the multicast LDM RPC server
 * @param[in]     ttl            Time-to-live of multicast packets.
 * @param[in]     fmtpSubnet     Subnet for client FMTP TCP connections
 * @param[in]     pqPathname     Pathname of product-queue. Caller may free.
 * @param[out]    pid            Process ID of the multicast LDM sender.
 * @retval        0              Success. `*pid` and `info->server.port` are
 *                               set.
 * @retval        LDM7_SYSTEM    System error. `log_add()` called.
 */
static Ldm7Status
mldm_spawn(
    McastInfo* const restrict       info,
    unsigned short* restrict        mldmSrvrPort,
    const unsigned short            ttl,
    const CidrAddr* const restrict  fmtpSubnet,
    const char* const restrict      pqPathname,
    pid_t* const restrict           pid)
{
    int   fds[2];
    int   status = pipe(fds);

    if (status) {
        log_add_syserr("Couldn't create pipe for multicast LDM sender process");
        status = LDM7_SYSTEM;
    }
    else {
        pid_t child = fork();

        if (child == -1) {
            char* const id = mi_format(info);

            log_add_syserr("Couldn't fork() multicast LDM sender for \"%s\"", id);
            free(id);
            status = LDM7_SYSTEM;
        }
        else if (child == 0) {
            /* Child process */
            (void)close(fds[0]); // read end of pipe unneeded
            allowSigs(); // so process will terminate and process products
            // The following statement shouldn't return
            mldm_exec(info, ttl, fmtpSubnet, pqPathname, fds[1]);
            log_flush_error();
            exit(1);
        }
        else {
            /* Parent process */
            (void)close(fds[1]);                // write end of pipe unneeded
            status = mldm_getServerPorts(fds[0], &info->server.port,
                    mldmSrvrPort);
            (void)close(fds[0]);                // no longer needed

            if (status) {
                log_add("Couldn't get port numbers from multicast LDM sender "
                        "process. Terminating that process.");
                (void)kill(child, SIGTERM);
            }
            else {
                *pid = child;
            }
        }
    }

    return status;
}

/**
 * Executes the multicast LDM sender for a particular multicast group as a child
 * process. Doesn't block.
 * @pre                          Multicast LDM sender PID map is locked.
 * @pre                          Relevant multicast LDM sender isn't running.
 * @param[in,out] info           Information on the multicast group.
 * @param[out]    mldmSrvrPort   Port number of the multicast LDM RPC server
 * @param[in]     ttl            Time-to-live of multicast packets.
 * @param[in]     fmtpSubnet     Subnet for client FMTP TCP connections
 * @param[in]     pqPathname     Pathname of product-queue. Caller may free.
 * @retval        0              Success. Multicast LDM sender spawned. `*pid`
 *                               and `info->server.port` are set.
 * @retval        LDM7_SYSTEM    System error. `log_add()` called.
 * @threadsafety                 Hostile
 */
static Ldm7Status
mldm_execute(
    McastInfo* const restrict       info,
    unsigned short* const restrict  mldmSrvrPort,
    const unsigned short            ttl,
    const CidrAddr* const restrict  fmtpSubnet,
    const char* const restrict      pqPathname)
{
    int status;

    if (childPid) {
        log_add("Can execute only one multicast sender child process");
        status = LDM7_SYSTEM;
    }
    else {
        const feedtypet feedtype = mi_getFeedtype(info);
        pid_t           procId;

        // The following will set `info->server.port` and `mldmSrvrPort`
        status = mldm_spawn(info, mldmSrvrPort, ttl, fmtpSubnet, pqPathname,
                &procId);
        if (0 == status) {
            status = mldm_ensureCleanup(procId);
            if (status) {
                (void)kill(procId, SIGTERM);
            }
            else {
                status = msm_put(feedtype, procId, info->server.port,
                        *mldmSrvrPort);
                if (status) {
                    // preconditions => LDM7_DUP can't be returned
                    char* const id = mi_format(info);

                    log_add("Terminating just-started multicast LDM sender for "
                            "\"%s\"", id);
                    free(id);
                    (void)kill(procId, SIGTERM);
                }
                else {
                    childPid = procId;
                    feed = feedtype;
                }
            }
        }
    }

    return status;
}

/******************************************************************************
 * Multicast Entry:
 ******************************************************************************/

typedef struct {
    McastInfo      info;
    /// Local virtual-circuit endpoint
    VcEndPoint*    vcEnd;
    char*          pqPathname;
    CidrAddr       fmtpSubnet;
    unsigned short mldmSrvrPort;
    unsigned short ttl;
} McastEntry;

/**
 * Initializes a multicast entry.
 *
 * @param[out] entry       Entry to be initialized.
 * @param[in]  info        Multicast information. Caller may free.
 * @param[in]  ttl         Time-to-live for multicast packets.
 * @param[in]  vcEnd       Local virtual-circuit endpoint
 * @param[in]  fmtpSubnet  Subnet for client FMTP TCP connections
 * @param[in]  pqPathname  Pathname of product-queue. Caller may free.
 * @retval     0           Success. `*entry` is initialized. Caller should call
 *                         `me_destroy(entry)` when it's no longer needed.
 * @retval     LDM7_INVAL  `ttl` is too large. `log_add()` called.
 * @retval     LDM7_SYSTEM System error. `log_add()` called. The state of
 *                         `*entry` is unspecified.
 */
static Ldm7Status
me_init(
        McastEntry* const restrict       entry,
        const McastInfo* const           info,
        unsigned short                   ttl,
        const VcEndPoint* const restrict vcEnd,
        const CidrAddr* const restrict   fmtpSubnet,
        const char* const restrict       pqPathname)
{
    int status;

    if (ttl >= 255) {
        log_add("Time-to-live is too large: %hu >= 255", ttl);
        status = LDM7_INVAL;
    }
    else if (mi_copy(&entry->info, info)) {
        status = LDM7_SYSTEM;
    }
    else {
        entry->ttl = ttl;
        entry->pqPathname = strdup(pqPathname);

        if (NULL == entry->pqPathname) {
            log_add_syserr("Couldn't copy pathname of product-queue");
            status = LDM7_SYSTEM;
        }
        else {
            entry->vcEnd = vcEndPoint_clone(vcEnd);

            if (NULL == entry->vcEnd) {
                log_add_syserr("Couldn't clone virtual-circuit endpoint");
                status = LDM7_SYSTEM;
            }
            else {
                cidrAddr_copy(&entry->fmtpSubnet, fmtpSubnet);
                status = 0;
            } // `entry->vcEnd` allocated
            if (status)
                free(entry->pqPathname);
        } // `entry->pqPathname` allocated

        if (status)
            mi_destroy(&entry->info);
    } // `entry->info` allocated

    return status;
}

/**
 * Destroys a multicast entry.
 *
 * @param[in] entry  The multicast entry to be destroyed.
 */
static void
me_destroy(
        McastEntry* const entry)
{
    mi_destroy(&entry->info);
    vcEndPoint_delete(entry->vcEnd);
    free(entry->pqPathname);
}

/**
 * Returns a new multicast entry.
 *
 * @param[out] entry       New, initialized entry.
 * @param[in]  info        Multicast information. Caller may free.
 * @param[in]  ttl         Time-to-live for multicast packets.
 * @param[in]  vcEnd       Local virtual-circuit endpoint
 * @param[in]  fmtpSubnet  Subnet for client FMTP TCP connections
 * @param[in]  pqPathname  Pathname of product-queue. Caller may free.
 * @retval     0           Success. `*entry` is set. Caller should call
 *                         `me_free(*entry)` when it's no longer needed.
 * @retval     LDM7_INVAL  `info->server->port` is not zero. `log_add()`
 *                         called.
 * @retval     LDM7_INVAL  `ttl` is too large. `log_add()` called.
 */
static Ldm7Status
me_new(
        McastEntry** const restrict      entry,
        const McastInfo* const           info,
        unsigned short                   ttl,
        const VcEndPoint* const restrict vcEnd,
        const CidrAddr* const restrict   fmtpSubnet,
        const char* const restrict       pqPathname)
{
    int            status;
    unsigned short port = sa_getPort(&info->server);

#if 0
    if (port != 0) {
        log_add("Port number of FMTP TCP server isn't zero: %hu", port);
        status = LDM7_INVAL;
    }
    else {
#endif
        McastEntry* ent = log_malloc(sizeof(McastEntry), "multicast entry");

        if (ent == NULL) {
            status = LDM7_SYSTEM;
        }
        else {
            status = me_init(ent, info, ttl, vcEnd, fmtpSubnet, pqPathname);

            if (status) {
                free(ent);
            }
            else {
                *entry = ent;
            }
        }
#if 0
    }
#endif

    return status;
}

/**
 * Frees a multicast entry.
 *
 * @param[in]  The multicast entry to be freed or NULL.
 */
static void
me_free(
        McastEntry* const entry)
{
    if (entry) {
        me_destroy(entry);
        free(entry);
    }
}

/**
 * Indicates if two multicast entries conflict (e.g., have feed-types that
 * overlap, specify the same TCP server IP address and positive port number,
 * etc.).
 *
 * @param[in] info1  First multicast entry.
 * @param[in] info2  Second multicast entry.
 * @retval    true   The multicast entries do conflict.
 * @retval    false  The multicast entries do not conflict.
 */
static bool
me_doConflict(
        const McastInfo* const info1,
        const McastInfo* const info2)
{
    if (mi_getFeedtype(info1) & mi_getFeedtype(info2))
        return true;
    if (0 == mi_compareServers(info1, info2) && sa_getPort(&info1->server) != 0)
        return true;
    if (0 == mi_compareGroups(info1, info2))
        return true;
    return false;
}

/**
 * Compares two multicast entries and returns a value less than, equal to, or
 * greater than zero as the first entry is considered less than, equal to, or
 * greater than the second entry, respectively. Only the feed-types are
 * compared.
 *
 * @param[in] o1  First multicast entry object.
 * @param[in] o2  Second multicast entry object.
 * @retval    -1  First object is less than second object.
 * @retval     0  First object equals second object.
 * @retval    +1  First object is greater than second object.
 */
static int
me_compareFeedtypes(
        const void* o1,
        const void* o2)
{
    const feedtypet f1 = mi_getFeedtype(&((McastEntry*)o1)->info);
    const feedtypet f2 = mi_getFeedtype(&((McastEntry*)o2)->info);

    return (f1 < f2)
              ? -1
              : (f1 == f2)
                ? 0
                : 1;
}

/**
 * Compares two multicast entries and returns a value less than, equal to, or
 * greater than zero as the first entry is considered less than, equal to, or
 * greater than the second entry, respectively. The entries are considered equal
 * if they conflict (e.g., have feed-types that overlap, specify the same TCP
 * server IP address and port number, etc.).
 *
 * @param[in] o1  First multicast information object.
 * @param[in] o2  Second multicast information object.
 * @retval    -1  First object is less than second object.
 * @retval     0  First object equals second object or the objects conflict.
 * @retval    +1  First object is greater than second object.
 */
static int
me_compareOrConflict(
        const void* o1,
        const void* o2)
{
    const McastInfo* const i1 = o1;
    const McastInfo* const i2 = o2;

    if (me_doConflict(i1, i2))
        return 0;

    return me_compareFeedtypes(o1, o2);
}

/**
 * Starts a multicast LDM sender process if necessary.
 *
 * @param[in,out]  entry        Multicast entry
 * @retval         0            Success. The multicast LDM sender associated
 *                              with the given multicast group is running or was
 *                              successfully started. `entry->info.server.port`
 *                              is set to the port number of the FMTP TCP
 *                              server.
 * @retval         LDM7_SYSTEM  System error. `log_add()` called.
 */
static Ldm7Status
me_startIfNecessary(McastEntry* const entry)
{
    /*
     * The Multicast-LDM Sender Map (MSM) is locked because it might be accessed
     * multiple times.
     */
    int status = msm_lock(true);
    if (status) {
        log_add("Couldn't lock multicast sender map");
    }
    else {
        // Accesses multicast sender map
        status = mldm_isRunning(entry->info.feed, &entry->info.server.port,
                &entry->mldmSrvrPort);
        if (status == LDM7_NOENT) {
            // The relevant multicast LDM sender isn't running
            childPid = 0; // because multicast process isn't running
            // Accesses multicast sender map
            status = mldm_execute(&entry->info, &entry->mldmSrvrPort,
                    entry->ttl, &entry->fmtpSubnet, entry->pqPathname);
        }
        (void)msm_unlock();
    } // Multicast sender map is locked

    return status;
}

/**
 * Reserves an IP address for a downstream FMTP layer.
 * @param[in]  entry         Multicast LDM entry
 * @param[out] downFmtpAddr  IP address for downstream FMTP layer in network
 *                           byte order
 * @retval LDM7_OK           Success. `*downFmtpAddr` is set.
 * @retval LDM7_SYSTEM       System failure. `log_add()` called.
 */
static Ldm7Status
me_reserve(
        const McastEntry* const restrict entry,
        in_addr_t* const restrict        downFmtpAddr)
{
    Ldm7Status  status;
    void* const mldmClnt = mldmClnt_new(entry->mldmSrvrPort);
    if (mldmClnt == NULL) {
        log_add("Couldn't create new multicast LDM RPC client");
        status = LDM7_SYSTEM;
    }
    else {
        status = mldmClnt_reserve(mldmClnt, downFmtpAddr);
        if (status)
            log_add("Couldn't reserve IP address for remote FMTP layer");
        mldmClnt_delete(mldmClnt);
    }
    return status;
}

/**
 * @retval LDM7_OK       Success. `fmtpAddr` is available for subsequent
 *                       reservation.
 * @retval LDM7_NOENT    `fmtpAddr` wasn't previously reserved. `log_add()`
 *                       called.
 * @retval LDM7_SYSTEM   System failure. `log_add()` called.
 */
static Ldm7Status
me_release(
        const McastEntry* const restrict entry,
        in_addr_t const                  fmtpAddr)
{
    Ldm7Status  status;
    void* const mldmClnt = mldmClnt_new(entry->mldmSrvrPort);
    if (mldmClnt == NULL) {
        log_add("Couldn't create new multicast LDM RPC client");
        status = LDM7_SYSTEM;
    }
    else {
        status = mldmClnt_release(mldmClnt, fmtpAddr);
        if (status)
            log_add("Couldn't release IP address for remote FMTP layer");
        mldmClnt_delete(mldmClnt);
    }
    return status;
}

/**
 * Sets the response to a subscription request.
 * @param[in]  entry    Multicast entry
 * @param[out] reply    Subscription reply. Caller should destroy when it's no
 *                      longer needed.
 * @retval 0            Success
 * @retval LDM7_NOENT   No associated Internet address-space
 * @retval LDM7_MCAST   All addresses have been reserved
 * @retval LDM7_SYSTEM  System error
 */
static Ldm7Status
me_setSubscriptionReply(
        const McastEntry* const restrict  entry,
        SubscriptionReply* const restrict reply)
{
    SubscriptionReply rep;
    int               status = mi_copy(&rep.SubscriptionReply_u.info.mcastInfo,
            &entry->info);
    if (status == 0) {
        in_addr_t downFmtpAddr;
        status = me_reserve(entry, &downFmtpAddr);
        if (status == 0) {
            cidrAddr_construct(&rep.SubscriptionReply_u.info.fmtpAddr,
                    downFmtpAddr, cidrAddr_getPrefixLen(&entry->fmtpSubnet));
            *reply = rep;
            status = LDM7_OK;
        } // `rep->SubscriptionReply_u.info.clntAddr` set
        if (status)
            mi_destroy(&rep.SubscriptionReply_u.info.mcastInfo);
    } // `rep->SubscriptionReply_u.info.mcastInfo` allocated
    reply->status = status;
    return status;
}


/******************************************************************************
 * Upstream Multicast Manager:
 ******************************************************************************/

static void* mcastEntries;

/**
 * Returns the multicast entry corresponding to a particular feed.
 * @param[in] feed  LDM feed
 * @retval    NULL  No entry corresponding to feed. `log_add()` called.
 * @return          Pointer to corresponding entry
 */
static McastEntry*
umm_getMcastEntry(const feedtypet feed)
{
    McastEntry key;
    key.info.feed = feed;
    void* const node = tfind(&key, &mcastEntries, me_compareFeedtypes);
    if (NULL == node) {
        log_add("No multicast LDM sender is associated with feed-type %s",
                s_feedtypet(feed));
        return NULL;
    }
    return *(McastEntry**)node;
}

Ldm7Status
umm_addPotentialSender(
    const McastInfo* const restrict   info,
    const unsigned short              ttl,
    const VcEndPoint* const restrict  vcEnd,
    const CidrAddr* const restrict    fmtpSubnet,
    const char* const restrict        pqPathname)
{
    McastEntry* entry;
    int         status = me_new(&entry, info, ttl, vcEnd, fmtpSubnet,
            pqPathname);

    if (0 == status) {
        const void* const node = tsearch(entry, &mcastEntries,
                me_compareOrConflict);

        if (NULL == node) {
            log_add_syserr("Couldn't add to multicast entries");
            status = LDM7_SYSTEM;
        }
        else if (*(McastEntry**)node != entry) {
            char* const mi1 = mi_format(&entry->info);
            char* const mi2 = mi_format(&(*(McastEntry**)node)->info);
            log_add("Multicast information \"%s\" "
                    "conflicts with earlier addition \"%s\"", mi1, mi2);
            free(mi1);
            free(mi2);
            status = LDM7_DUP;
        }
        if (status)
            me_free(entry);
    } // `entry` allocated

    return status;
}

Ldm7Status
umm_subscribe(
        const feedtypet          feed,
        SubscriptionReply* const reply)
{
    int         status;
    McastEntry* entry = umm_getMcastEntry(feed);
    if (NULL == entry) {
        status = LDM7_NOENT;
    }
    else {
            McastInfo* const info = &entry->info;
            // Sets port numbers of FMTP & RPC servers of multicast LDM sender
            status = me_startIfNecessary(entry);
            if (0 == status)
                status = me_setSubscriptionReply(entry, reply);
    } // Feed maps to possible multicast LDM sender
    return status;
}

Ldm7Status
umm_terminated(
        const pid_t pid)
{
    int status = msm_lock(true);
    if (status) {
        log_add("Couldn't lock multicast sender map");
    }
    else {
        status = msm_remove(pid);
        if (pid == childPid)
            childPid = 0; // No need to kill child
        (void)msm_unlock();
    }
    return status;
}

pid_t
umm_getMldmSenderPid(void)
{
    return childPid;
}

Ldm7Status
umm_unsubscribe(
        const feedtypet feed,
        const in_addr_t downFmtpAddr)
{
    McastEntry* entry = umm_getMcastEntry(feed);
    return (entry == NULL)
        ? LDM7_INVAL
        : me_release(entry, downFmtpAddr);
}

Ldm7Status
umm_clear(void)
{
    int status = msm_lock(true);
    if (status) {
        log_add("Couldn't lock multicast sender map");
    }
    else {
        while (mcastEntries) {
            McastEntry* entry = *(McastEntry**)mcastEntries;
            (void)tdelete(entry, &mcastEntries, me_compareOrConflict);
            me_free(entry);
        }
        msm_clear();
        (void)msm_unlock();
    }
    return status;
}
