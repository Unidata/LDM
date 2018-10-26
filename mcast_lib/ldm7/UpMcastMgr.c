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

#include "log.h"
#include "inetutil.h"
//#include "AuthClient.h"
#include "CidrAddr.h"
#include "fmtp.h"
#include "InetSockAddr.h"
#include "globals.h"
#include "ldmprint.h"
#include "log.h"
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
#include <sys/wait.h>
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

/// Feed being multicast
static feedtypet feed;
/// Pid of multicast LDM sender child process
static pid_t     childPid;
/// Port number of FMTP server in multicast LDM sender process
static in_port_t fmtpSrvrPort;
/// Port number of RPC command server in multicast LDM sender process
static in_port_t mldmCmdPort;

static void
mldm_reset(void)
{
    childPid = 0;
    fmtpSrvrPort = 0;
    mldmCmdPort = 0;
}

/**
 * Gets the port number of the multicast FMTP TCP server and the multicast LDM
 * RPC server from a multicast LDM sender process that writes them to a pipe.
 * Doesn't close the pipe.
 *
 * @param[in]  pipe          Pipe for reading from the multicast LDM sender
 *                           process
 * @retval     0             Success. `fmtpSrvrPort` and `mldmCmdPort` are set.
 * @retval     LDM7_SYSTEM   System failure. `log_add()` called.
 * @retval     LDM7_LOGIC    Logic failure. `log_add()` called.
 */
static Ldm7Status
mldm_getServerPorts(const int pipe)
{
    int     status;
#if 1
    char    buf[100];
    ssize_t nbytes = read(pipe, buf, sizeof(buf));
    if (nbytes == -1) {
        log_add_syserr("Couldn't read from pipe to multicast FMTP process");
        status = LDM7_SYSTEM;
    }
    else if (nbytes == 0) {
        log_add("Couldn't read from pipe to multicast FMTP process due to EOF");
        status = LDM7_LOGIC;
    }
    else if (nbytes >= sizeof(buf)) {
        log_add("Read too many bytes from pipe to multicast FMTP process");
        status = LDM7_LOGIC;
    }
    else {
        buf[nbytes] = 0;
        if (sscanf(buf, "%hu %hu ", &fmtpSrvrPort, &mldmCmdPort) != 2) {
            log_add("Couldn't decode port numbers for multicast FMTP server "
                    "and RPC server in \"%s\"", buf);
            status = LDM7_LOGIC;
        }
        else {
            log_debug("Port numbers read from pipe");
            status = LDM7_OK;
        }
    }
#else
    FILE*   stream = fdopen(pipe, "r");
    if (stream == NULL) {
        log_add_syserr("Couldn't associate stream with pipe");
        status = LDM7_SYSTEM;
    }
    else {
        if (fscanf(stream, "%hu %hu ", &fmtpSrvrPort, &mldmCmdPort) != 2) {
            log_add("Couldn't read port numbers for multicast FMTP server "
                    "and RPC server from multicast LDM sender process");
            status = LDM7_LOGIC;
        }
        else {
            log_debug("Port numbers read from pipe");
            status = LDM7_OK;
        }
    }
#endif
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
 * @param[in] mcastIface     IPv4 address of interface to use for multicasting.
 *                           "0.0.0.0" obtains the system's default multicast
 *                           interface.
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
 * @param[in] retxTimeout    FMTP retransmission timeout in minutes. Duration
 *                           that a product will be held by the FMTP layer
 *                           before being released. If negative, then the
 *                           default timeout is used.
 * @param[in] pqPathname     Pathname of product-queue. Caller may free.
 * @param[in] pipe           Write end of pipe to parent process
 */
static void
mldm_exec(
    const struct in_addr            mcastIface,
    SepMcastInfo* const restrict   info,
    const unsigned short            ttl,
    const CidrAddr* const restrict  fmtpSubnet,
    const float                     retxTimeout,
    const char* const restrict      pqPathname,
    const int                       pipe)
{
    char* args[23]; // Keep sufficiently capacious (search for `i\+\+`)
    int   i = 0;

    args[i++] = "mldm_sender";

    char* arg = (char*)log_get_destination(); // safe cast
    if (arg != NULL) {
        args[i++] = "-l";
        args[i++] = arg;
    }

    if (log_is_enabled_info)
        args[i++] = "-v";
    if (log_is_enabled_debug)
        args[i++] = "-x";

    char mcastIfaceBuf[INET_ADDRSTRLEN];
    if (mcastIface.s_addr) {
        inet_ntop(AF_INET, &mcastIface, mcastIfaceBuf, sizeof(mcastIfaceBuf));
        args[i++] = "-m";
        args[i++] = mcastIfaceBuf;
    }

    char feedtypeBuf[256];
    if (smi_getFeed(info) != EXP) {
        (void)sprint_feedtypet(feedtypeBuf, sizeof(feedtypeBuf),
                smi_getFeed(info));
        args[i++] = "-f";
        args[i++] = feedtypeBuf; // multicast group identifier
    }

    char* retxTimeoutOptArg = NULL;
    if (retxTimeout >= 0) {
        retxTimeoutOptArg = ldm_format(4, "%f", retxTimeout);
        if (retxTimeoutOptArg == NULL) {
            log_add("Couldn't create FMTP retransmission timeout "
                    "option-argument");
            goto failure;
        }
        args[i++] = "-r";
        args[i++] = retxTimeoutOptArg;
    }

    args[i++] = "-q";
    args[i++] = (char*)pqPathname; // safe cast

    const char* const fmtpSrvrStr = isa_getInetAddrStr(smi_getFmtpSrvr(info));
    if (strcmp(fmtpSrvrStr, "0.0.0.0")) {
        args[i++] = "-s";
        // Safe cast because of `execvp()`
        args[i++] = (char*)fmtpSrvrStr;
    }

    char ttlOptArg[4];
    if (ttl != 1) {
        ssize_t nbytes = snprintf(ttlOptArg, sizeof(ttlOptArg), "%hu", ttl);
        if (nbytes < 0 || nbytes >= sizeof(ttlOptArg)) {
            log_add("Couldn't create time-to-live option-argument \"%hu\"",
                    ttl);
            goto free_retxTimeoutOptArg;
        }
        args[i++] = "-t";
        args[i++] = ttlOptArg;
    }

    // Safe cast because of `execvp()`
    args[i++] = (char*)isa_toString(smi_getMcastGrp(info));

    char* fmtpSubnetArg = cidrAddr_format(fmtpSubnet);
    if (fmtpSubnetArg == NULL) {
        goto free_retxTimeoutOptArg;
    }
    else {
        args[i++] = fmtpSubnetArg;
    }

    args[i++] = NULL;

    if (dup2(pipe, STDOUT_FILENO) < 0) {
        log_add("Couldn't redirect standard output stream to pipe");
        goto free_fmtpSubnetArg;
    }

    //StrBuf* command = catenateArgs((const char**)args); // Safe cast
    //log_info("Executing multicast LDM sender: %s", sbString(command));
    //sbFree(command);

    execvp(args[0], args);

    log_add_syserr("Couldn't execute multicast LDM sender \"%s\"; PATH=%s",
            args[0], getenv("PATH"));
free_fmtpSubnetArg:
    free(fmtpSubnetArg);
free_retxTimeoutOptArg:
    free(retxTimeoutOptArg);
failure:
    return;
}

/**
 * Terminates the multicast LDM sender process and waits for it to terminate.
 *
 * Idempotent.
 *
 * @pre                 `childPid` is set
 * @retval LDM7_OK      Success
 * @retval LDM7_SYSTEM  System failure. `log_add()` called.
 */
static Ldm7Status
mldm_terminateSenderAndReap()
{
    int status;

    if (childPid == 0) {
        status = LDM7_OK;
    }
    else {
        status = kill(childPid, SIGTERM);

        if (status) {
            log_add_syserr("Couldn't send SIGTERM to multicast LDM sender "
                    "process %d", childPid);
            status = LDM7_SYSTEM;
        }
        else {
            int procStatus;

            status = waitpid(childPid, &procStatus, 0);

            if (status == -1) {
                log_add_syserr("Couldn't wait for multicast LDM sender process "
                        "%d to terminate", childPid);
                status = LDM7_SYSTEM;
            }
            else {
                if (WIFEXITED(procStatus)) {
                    log_notice_q("Multicast LDM sender process %d terminated "
                            "normally with status %d", childPid,
                            WEXITSTATUS(procStatus));
                }
                else if (WIFSIGNALED(procStatus)) {
                    log_notice_q("Multicast LDM sender process %d terminated "
                            "abnormally due to signal %d", childPid,
                            WTERMSIG(procStatus));
                }

                mldm_reset();
                status = LDM7_OK;
            } // Couldn't reap multicast LDM sender process
        } // Multicast LDM sender was successfully signaled
    } // Multicast LDM sender process hasn't been started

    return status;
}

/**
 * Handles a just-executed multicast LDM child process.
 *
 * @pre                     `childPid` is set
 * @param[in] info          Multicast information
 * @param[in] fds           Pipe to child process
 * @retval    0             Success
 * @retval    LDM7_SYSTEM   System failure. `log_add()` called.
 * @retval    LDM7_LOGIC    Logic failure. `log_add()` called.
 */
static Ldm7Status
mldm_handleExecedChild(
        SepMcastInfo* const restrict info,
        const int* restrict       fds)
{
    int status;

    (void)close(fds[1]);                // write end of pipe unneeded

    // Sets `fmtpSrvrPort` and `mldmCmdPort`
    status = mldm_getServerPorts(fds[0]);
    (void)close(fds[0]);                // no longer needed

    if (status) {
        char* const id = smi_toString(info);
        log_add("Couldn't get port numbers from multicast LDM sender "
                "%s. Terminating that process.", id);
        free(id);
    }
    else {
        status = msm_put(smi_getFeed(info), childPid, fmtpSrvrPort,
                mldmCmdPort);

        if (status) {
            // preconditions => LDM7_DUP can't be returned
            char* const id = smi_toString(info);
            log_add("Couldn't save information on multicast LDM sender "
                    "%s. Terminating that process.", id);
            free(id);
        } // Information saved in multicast sender map
    } // FMTP server port and mldm_sender command port set

    return status;
}

/**
 * Executes a multicast LDM sender as a child process. Doesn't block. Sets
 * `childPid`, `fmtpSrvrPort` and `mldmCmdPort`.
 *
 * @param[in]     mcastIface     IPv4 address of interface to use for
 *                               multicasting. "0.0.0.0" obtains the system's
 *                               default multicast interface.
 * @param[in,out] info           Information on the multicast group.
 * @param[in]     ttl            Time-to-live of multicast packets.
 * @param[in]     fmtpSubnet     Subnet for client FMTP TCP connections
 * @param[in]     retxTimeout    FMTP retransmission timeout in minutes.
 *                               Duration that a product will be held by the
 *                               FMTP layer before being released. If negative,
 *                               then the default timeout is used.
 * @param[in]     pqPathname     Pathname of product-queue. Caller may free.
 * @retval        0              Success. `childPid`, `fmtpSrvrPort`, and
 *                               `mldmCmdPort` are set.
 * @retval        LDM7_LOGIC     Logic error. `log_add()` called.
 * @retval        LDM7_SYSTEM    System error. `log_add()` called.
 */
static Ldm7Status
mldm_spawn(
    const struct in_addr            mcastIface,
    SepMcastInfo* const restrict    info,
    const unsigned short            ttl,
    const CidrAddr* const restrict  fmtpSubnet,
    const float                     retxTimeout,
    const char* const restrict      pqPathname)
{
    int   fds[2];
    int   status = pipe(fds);

    if (status) {
        log_add_syserr("Couldn't create pipe for multicast LDM sender process");
        status = LDM7_SYSTEM;
    }
    else {
        const pid_t pid = fork();

        if (pid == -1) {
            char* const id = smi_toString(info);
            log_add_syserr("Couldn't fork() for multicast LDM sender %s", id);
            free(id);
            status = LDM7_SYSTEM;
        }
        else if (pid == 0) {
            /* Child process */
            (void)close(fds[0]); // read end of pipe unneeded
            allowSigs(); // so process can be terminated
            // The following statement shouldn't return
            mldm_exec(mcastIface, info, ttl, fmtpSubnet, retxTimeout,
                    pqPathname, fds[1]);
            log_flush_error();
            exit(1);
        }
        else {
            /* Parent process */
            childPid = pid;
            status = mldm_handleExecedChild(info, fds); // Uses `childPid`

            if (status) {
                (void)mldm_terminateSenderAndReap(); // Uses `childPid`
                childPid = 0;
            }
        } // Parent process
    } // Pipe created

    return status;
}

/**
 * Ensures that a multicast LDM sender process is running.
 *
 * @param[in] mcastIface    IPv4 address of interface to use for multicasting.
 *                          "0.0.0.0" obtains the system's default multicast
 *                          interface.
 * @param[in]  info         LDM7 multicast information
 * @param[in]  ttl          Time-to-live of multicast packets
 * @param[in]  fmtpSubnet   Subnet for client FMTP TCP connections
 * @param[in]  retxTimeout  FMTP retransmission timeout in minutes. A
 *                          negative value obtains the FMTP default.
 * @param[in]  pqPathname   Pathname of product-queue
 * @retval     0            Success. The multicast LDM sender associated
 *                          with the given multicast group was already running
 *                          or was successfully started.
 *                          `mldm_getFmtpSrvrPort()` will return the port number
 *                          of the FMTP server of the multicast LDM sender
 *                          process.
 * @retval     LDM7_LOGIC   Logic error. `log_add()` called.
 * @retval     LDM7_SYSTEM  System error. `log_add()` called.
 */
static Ldm7Status
mldm_ensureRunning(
        const struct in_addr            mcastIface,
        SepMcastInfo* const restrict    info,
        const unsigned short            ttl,
        const CidrAddr* const restrict  fmtpSubnet,
        const float                     retxTimeout,
        const char* const restrict      pqPathname)
{
    /*
     * The Multicast-LDM Sender Map (MSM) is locked because it might be accessed
     * multiple times.
     */
    int status = msm_lock(true); // Lock for writing

    if (status) {
        log_add("Couldn't lock multicast sender map");
    }
    else {
        if (childPid == 0) {
            if (msm_get(smi_getFeed(info), &childPid, &fmtpSrvrPort,
                    &mldmCmdPort) == 0) {
                if (kill(childPid, 0)) {
                    log_warning("Multicast LDM sender process %d should "
                            "exist but doesn't. Re-executing...", childPid);
                    childPid = 0;
                }
            }
        }
        if (childPid == 0) {
            /*
             * Sets `feed`, `childPid`, `fmtpSrvrPort`, `mldmCmdPort`; calls
             * `msm_put()`
             */
            status = mldm_spawn(mcastIface, info, ttl, fmtpSubnet, retxTimeout,
                    pqPathname);

            if (status)
                log_add("Couldn't spawn multicast LDM sender process");
        }

        (void)msm_unlock();
    } // Multicast sender map is locked

    return status;
}

/**
 * Returns the process identifier of the child multicast LDM sender process.
 * @retval 0  No such process exists
 * @return    Process identifier of child multicast LDM sender process
 */
inline static pid_t
mldm_getMldmSenderPid()
{
    return childPid;
}

/**
 * Returns the port number of the FMTP TCP server of the child multicast LDM
 * sender process.
 * @retval 0  No such process exists
 * @return    Port number of FMTP TCP server of child multicast LDM sender
 *            process.
 */
inline static in_port_t
mldm_getFmtpSrvrPort()
{
    return fmtpSrvrPort;
}

/**
 * Returns the port number of the RPC command-server of the child multicast LDM
 * sender process.
 * @retval 0  No such process exists
 * @return    Port number of RPC command-server of child multicast LDM sender
 *            process
 */
inline static in_port_t
mldm_getMldmCmdPort()
{
    return mldmCmdPort;
}

/******************************************************************************
 * Multicast Entry:
 ******************************************************************************/

typedef struct {
    struct in_addr mcastIface;
    SepMcastInfo*  info;
    /// Local virtual-circuit endpoint
    VcEndPoint*    vcEnd;
    char*          pqPathname;
    CidrAddr       fmtpSubnet;
    unsigned short ttl;
} McastEntry;

/**
 * Initializes a multicast entry.
 *
 * @param[out] entry       Entry to be initialized.
 * @param[in] mcastIface   IPv4 address of interface to use for multicasting.
 *                         "0.0.0.0" obtains the system's default multicast
 *                         interface.
 * @param[in]  mcastInfo   Multicast information. Freed by `me_destroy()`.
 * @param[in]  ttl         Time-to-live for multicast packets.
 * @param[in]  vcEnd       Local virtual-circuit endpoint
 * @param[in]  fmtpSubnet  Subnet for client FMTP TCP connections
 * @param[in]  pqPathname  Pathname of product-queue. Caller may free.
 * @retval     0           Success. `*entry` is initialized. Caller should call
 *                         `me_destroy(entry)` when it's no longer needed.
 * @retval     LDM7_INVAL  `ttl` is too large. `log_add()` called.
 * @retval     LDM7_SYSTEM System error. `log_add()` called. The state of
 *                         `*entry` is unspecified.
 * @see `me_destroy()`
 */
static Ldm7Status
me_init(
        McastEntry* const restrict       entry,
        const struct in_addr             mcastIface,
        SepMcastInfo* const              mcastInfo,
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
    else {
        char* const str = smi_toString(mcastInfo);
        //log_notice("me_init(): entry->info=%s", str);
        free(str);

        entry->info = mcastInfo;
        entry->mcastIface = mcastIface;
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
    smi_free(entry->info);
    vcEndPoint_free(entry->vcEnd);
    free(entry->pqPathname);
}

/**
 * Returns a new multicast entry.
 *
 * @param[out] entry       New, initialized entry.
 * @param[in]  mcastIface  IPv4 address of interface to use for multicasting.
 *                         "0.0.0.0" obtains the system's default multicast
 *                         interface.
 * @param[in]  mcastInfo   Multicast information. Freed by `me_free()`.
 * @param[in]  ttl         Time-to-live for multicast packets.
 * @param[in]  vcEnd       Local virtual-circuit endpoint
 * @param[in]  fmtpSubnet  Subnet for client FMTP TCP connections
 * @param[in]  pqPathname  Pathname of product-queue. Caller may free.
 * @retval     0           Success. `*entry` is set. Caller should call
 *                         `me_free(*entry)` when it's no longer needed.
 * @retval     LDM7_INVAL  `info->server->port` is not zero. `log_add()`
 *                         called.
 * @retval     LDM7_INVAL  `ttl` is too large. `log_add()` called.
 * @see `me_free()`
 */
static Ldm7Status
me_new(
        McastEntry** const restrict      entry,
        const struct in_addr             mcastIface,
        SepMcastInfo* const              mcastInfo,
        unsigned short                   ttl,
        const VcEndPoint* const restrict vcEnd,
        const CidrAddr* const restrict   fmtpSubnet,
        const char* const restrict       pqPathname)
{
    int            status;
        McastEntry* ent = log_malloc(sizeof(McastEntry), "multicast entry");

        if (ent == NULL) {
            status = LDM7_SYSTEM;
        }
        else {
        char* const str = smi_toString(mcastInfo);
        //log_notice("me_new(): info=%s", str);
        free(str);

        status = me_init(ent, mcastIface, mcastInfo, ttl, vcEnd, fmtpSubnet,
                    pqPathname);

            if (status) {
                free(ent);
            }
            else {
                *entry = ent;
            }
        }

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
    return ((mi_getFeedtype(info1) & mi_getFeedtype(info2)) || // Common feed
        (strcmp(info1->server, info2->server) == 0) ||         // Same server
        (strcmp(info1->group, info2->group) == 0));            // Same group
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
    const feedtypet f1 = smi_getFeed(((McastEntry*)o1)->info);
    const feedtypet f2 = smi_getFeed(((McastEntry*)o2)->info);

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
 * @param[in]      retxTimeout  FMTP retransmission timeout in minutes. A
 *                              negative value obtains the FMTP default.
 * @retval         0            Success. The multicast LDM sender associated
 *                              with the given multicast group is running or was
 *                              successfully started. `entry->info.server.port`
 *                              is set to the port number of the FMTP TCP
 *                              server.
 * @retval         LDM7_LOGIC   Logic error. `log_add()` called.
 * @retval         LDM7_SYSTEM  System error. `log_add()` called.
 */
static Ldm7Status
me_startIfNecessary(
        McastEntry* const entry,
        const float       retxTimeout)
{
    // Sets `mldmCmdPort`
    int status = mldm_ensureRunning(entry->mcastIface, entry->info,
            entry->ttl, &entry->fmtpSubnet, retxTimeout, entry->pqPathname);

    if (status == 0) {
        status = isa_setPort(smi_getFmtpSrvr(entry->info),
                mldm_getFmtpSrvrPort());
        log_assert(status == 0);
    }

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
    void* const mldmClnt = mldmClnt_new(mldm_getMldmCmdPort());

    if (mldmClnt == NULL) {
        log_add("Couldn't create new multicast LDM RPC client");
        status = LDM7_SYSTEM;
    }
    else {
        status = mldmClnt_reserve(mldmClnt, downFmtpAddr);

        if (status) {
            log_add("Couldn't reserve IP address for remote FMTP layer");
        }
        else {
            char buf[80];
            log_info("Reserved IP address %s for remote FMTP",
                    inet_ntop(AF_INET, downFmtpAddr, buf, sizeof(buf)));
        }

        mldmClnt_free(mldmClnt);
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
    void* const mldmClnt = mldmClnt_new(mldm_getMldmCmdPort());

    if (mldmClnt == NULL) {
        log_add("Couldn't create new command-client to multicast LDM sender");
        status = LDM7_SYSTEM;
    }
    else {
        status = mldmClnt_release(mldmClnt, fmtpAddr);

        if (status) {
            log_add("Couldn't release IP address for remote FMTP layer");
        }
        else {
            char ipStr[INET_ADDRSTRLEN];
            log_debug("Address %s released", inet_ntop(AF_INET, &fmtpAddr,
                    ipStr, sizeof(ipStr)));
        }

        mldmClnt_free(mldmClnt);
    }

    return status;
}

/**
 * Sets the response to a subscription request.
 * @param[in]  entry     Multicast entry
 * @param[out] reply     Subscription reply. Caller should destroy when it's no
 *                       longer needed.
 * @retval 0             Success
 * @retval LDM7_NOENT    No associated Internet address-space
 * @retval LDM7_MCAST    All addresses have been reserved
 * @retval LDM7_SYSTEM   System error
 */
static Ldm7Status
me_setSubscriptionReply(
        McastEntry* const restrict        entry,
        SubscriptionReply* const restrict reply)
{
    int               status;
    SubscriptionReply rep;

    //char* const str = smi_toString(entry->info);
    //log_notice("me_setSubscriptionReply(): entry->info=%s", str);
    //free(str);

    if (!mi_init(&rep.SubscriptionReply_u.info.mcastInfo,
            smi_getFeed(entry->info),
            isa_toString(smi_getMcastGrp(entry->info)),
            isa_toString(smi_getFmtpSrvr(entry->info)))) {
        log_add("Couldn't initialize multicast information");
        status = LDM7_SYSTEM;
    }
    else {
        SubnetLen prefixLen;

        status = me_reserve(entry, &downFmtpAddr);

        if (status == 0) {
            cidrAddr_init(&rep.SubscriptionReply_u.info.fmtpAddr,
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

/// Multicast entries
static void* mcastEntries;
/// Key for multicast entries
static McastEntry key;
/// FMTP retransmission timeout in minutes
static float retxTimeout = -1.0; // Negative => use FMTP default

/**
 * Returns the multicast entry corresponding to a particular feed.
 *
 * @param[in] feed  LDM feed
 * @retval    NULL  No entry corresponding to feed. `log_add()` called.
 * @return          Pointer to corresponding entry
 */
static McastEntry*
umm_getMcastEntry(const feedtypet feed)
{
    if (key.info == NULL)
        key.info = smi_newFromStr(0, "0.0.0.0", "0.0.0.0");

    smi_setFeed(key.info, feed);
    void* const node = tfind(&key, &mcastEntries, me_compareFeedtypes);
    if (NULL == node) {
        log_add("No multicast LDM sender is associated with feed-type %s",
                s_feedtypet(feed));
        return NULL;
    }
    return *(McastEntry**)node;
}

void
umm_setRetxTimeout(const float minutes)
{
    retxTimeout = minutes;
}

Ldm7Status
umm_addPotentialSender(
    const struct in_addr              mcastIface,
    SepMcastInfo* const restrict     mcastInfo,
    const unsigned short              ttl,
    const VcEndPoint* const restrict  vcEnd,
    const CidrAddr* const restrict    fmtpSubnet,
    const char* const restrict        pqPathname)
{
    McastEntry* entry;

    //char* const str = smi_toString(mcastInfo);
    //log_notice("umm_addPotentialSender(): info=%s", str);
    //free(str);

    int         status = me_new(&entry, mcastIface, mcastInfo, ttl, vcEnd,
            fmtpSubnet, pqPathname);

    if (0 == status) {
        const void* const node = tsearch(entry, &mcastEntries,
                me_compareOrConflict);

        if (NULL == node) {
            log_add_syserr("Couldn't add to multicast entries");
            status = LDM7_SYSTEM;
        }
        else if (*(McastEntry**)node != entry) {
            char* const mi1 = smi_toString(entry->info);
            char* const mi2 = smi_toString((*(McastEntry**)node)->info);

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
        log_add("No multicast entry corresponds to feed %s", s_feedtypet(feed));
        status = LDM7_NOENT;
    }
    else {
        //char* const str = smi_toString(entry->info);
        //log_notice("umm_subscribe(): entry->info=%s", str);
        //free(str);

            /*
         * Sets the port numbers of the FMTP server & RPC-command server of
         * the multicast LDM sender process
             */
            status = me_startIfNecessary(entry, retxTimeout);

            if (status) {
                log_add("Couldn't ensure running multicast sender");
            }
            else {
            //char* const str = smi_toString(entry->info);
            //log_notice("umm_subscribe(): entry->info=%s", str);
            //free(str);

            status = me_setSubscriptionReply(entry, reply);

                if (status)
                    log_add("Couldn't set subscription reply");
            }
    } // Desired feed maps to a possible multicast LDM sender

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
            childPid = 0; // No need to kill child because must have terminated
        (void)msm_unlock();
    }
    return status;
}

pid_t
umm_getMldmSenderPid(void)
{
    return mldm_getMldmSenderPid();
}

Ldm7Status
umm_unsubscribe(
        const feedtypet feed,
        const in_addr_t downFmtpAddr)
{
    int status;

    McastEntry* entry = umm_getMcastEntry(feed);

    if (entry == NULL) {
        status = LDM7_INVAL;
    }
    else  {
        status = me_release(entry, downFmtpAddr);

        if (status == 0) {
            char ipStr[INET_ADDRSTRLEN];
            log_debug("Address %s released", inet_ntop(AF_INET, &downFmtpAddr,
                    ipStr, sizeof(ipStr)));
        }
    }

    return status;
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

    if (key.info) {
        smi_free(key.info);
        key.info = NULL;
    }

    return status;
}
