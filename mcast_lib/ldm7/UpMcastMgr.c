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
#include "ChildCommand.h"
#include "CidrAddr.h"
#include "fmtp.h"
#include "InetSockAddr.h"
#include "globals.h"
#include "ldmprint.h"
#include "log.h"
#include "mcast_info.h"
#include "MldmRpc.h"
#include "mldm_sender_map.h"
#include "priv.h"
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
 * Proxy for multicast LDM sender process:
 ******************************************************************************/

/// Pid of multicast LDM sender process
static pid_t     childPid;
/// Port number of FMTP server of multicast LDM sender process
static in_port_t fmtpSrvrPort;
/// Port number of RPC command server of multicast LDM sender process
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
 * @retval    0              Success
 * @retval    LDM7_SYSTEM    Failure. `log_add()` called.
 */
static Ldm7Status
mldm_exec(
    const struct in_addr               mcastIface,
    const SepMcastInfo* const restrict info,
    const unsigned short               ttl,
    const CidrAddr* const restrict     fmtpSubnet,
    const float                        retxTimeout,
    const char* const restrict         pqPathname,
    const int                          pipe)
{
    int   status = 0;
    char* args[23]; // Keep sufficiently capacious (search for `i\+\+`)
    int   i = 0;

    args[i++] = "mldm_sender";

    const char* logDestArg = log_get_destination();
    if (strcmp(log_get_default_destination(), logDestArg)) {
        if (logDestArg != NULL) {
            args[i++] = "-l";
            args[i++] = (char*)logDestArg; // Safe cast
        }
    } // Non-default logging destination

    if (log_is_enabled_info)
        args[i++] = "-v";
    if (log_is_enabled_debug)
        args[i++] = "-x";

    char feedtypeBuf[256];
    if (smi_getFeed(info) != EXP) {
        (void)sprint_feedtypet(feedtypeBuf, sizeof(feedtypeBuf),
                smi_getFeed(info));
        args[i++] = "-f";
        args[i++] = feedtypeBuf; // multicast group identifier
    } // Non-default LDM7 feed

    char mcastIfaceArg[INET_ADDRSTRLEN];
    if (mcastIface.s_addr) {
        inet_ntop(AF_INET, &mcastIface, mcastIfaceArg, sizeof(mcastIfaceArg));
        args[i++] = "-i";
        args[i++] = mcastIfaceArg;
    } // Non-default multicast interface

    char* fmtpSubnetArg = NULL;
    if (status == 0 && cidrAddr_getNumHostAddrs(fmtpSubnet)) {
        fmtpSubnetArg = cidrAddr_format(fmtpSubnet);
        if (fmtpSubnetArg == NULL) {
            log_add("Couldn't create FMTP subnet option-argument");
            status = LDM7_SYSTEM;
        }
        else {
            args[i++] = "-n";
            args[i++] = fmtpSubnetArg;
        }
    } // Non-default FMTP VLAN subnet

    char* retxTimeoutArg = NULL;
    if (status == 0 && retxTimeout >= 0) {
        retxTimeoutArg = ldm_format(4, "%f", retxTimeout);
        if (retxTimeoutArg == NULL) {
            log_add("Couldn't create FMTP retransmission timeout "
                    "option-argument");
            status = LDM7_SYSTEM;
        }
        else {
            args[i++] = "-r";
            args[i++] = retxTimeoutArg;
        }
    } // Non-default FMTP retransmission timeout

    if (status == 0 && strcmp(getDefaultQueuePath(), pqPathname)) {
        args[i++] = "-q";
        args[i++] = (char*)pqPathname; // safe cast
    }

    if (status == 0) {
        const char* fmtpSrvrArg = isa_getInetAddrStr(smi_getFmtpSrvr(info));
        if (fmtpSrvrArg == NULL) {
            log_add("Couldn't get FMTP server address");
            status = LDM7_SYSTEM;
        }
        else if (strcmp(fmtpSrvrArg, "0.0.0.0")) {
            args[i++] = "-s";
            args[i++] = (char*)fmtpSrvrArg; // `execvp()`=>Safe cast
        }
    } // Non-default FMTP server address

    char ttlOptArg[4];
    if (status == 0 && ttl != 1) {
        ssize_t nbytes = snprintf(ttlOptArg, sizeof(ttlOptArg), "%hu", ttl);
        if (nbytes < 0 || nbytes >= sizeof(ttlOptArg)) {
            log_add("Couldn't create time-to-live option-argument \"%hu\"",
                    ttl);
            status = LDM7_SYSTEM;
        }
        else {
            args[i++] = "-t";
            args[i++] = ttlOptArg;
        }
    } // Non-default time-to-live argument

    if (status == 0) {
        const char* mcastGrpArg = isa_toString(smi_getMcastGrp(info));

        if (mcastGrpArg == NULL) {
            log_add("Couldn't get multicast group argument");
            status = LDM7_SYSTEM;
        }
        else {
            args[i++] = (char*)mcastGrpArg; // Safe cast because of `execvp()`
            args[i++] = NULL;

            if (status == 0 && dup2(pipe, STDOUT_FILENO) < 0) {
                log_add("Couldn't redirect standard output stream to pipe");
                status = LDM7_SYSTEM;
            }
            else {
                StrBuf* command = catenateArgs((const char**)args); // Safe cast
                log_info("Executing multicast LDM sender: %s",
                        sbString(command));
                sbFree(command);

                execvp(args[0], args);

                log_add_syserr("Couldn't execute multicast LDM sender \"%s\"; "
                        "PATH=%s", args[0], getenv("PATH"));
            } // Standard output redirected to pipe
        } // Got multicast group argument
    } // `status == 0`

    free(retxTimeoutArg);
    free(fmtpSubnetArg);

    return status;
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
        const SepMcastInfo* const restrict info,
        const int* restrict                fds)
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
    const struct in_addr               mcastIface,
    const SepMcastInfo* const restrict info,
    const unsigned short               ttl,
    const CidrAddr* const restrict     fmtpSubnet,
    const float                        retxTimeout,
    const char* const restrict         pqPathname)
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
        const struct in_addr               mcastIface,
        const SepMcastInfo* const restrict info,
        const unsigned short               ttl,
        const CidrAddr* const restrict     fmtpSubnet,
        const float                        retxTimeout,
        const char* const restrict         pqPathname)
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
 * Obtains an IP address for a client FMTP component.
 *
 * @param[out] downFmtpAddr  IP address for client FMTP component in network
 *                           byte order
 * @retval LDM7_OK           Success. `*downFmtpAddr` is set.
 * @retval LDM7_SYSTEM       System failure. `log_add()` called.
 */
static Ldm7Status
mldm_getFmtpClntAddr(in_addr_t* const restrict downFmtpAddr)
{
    Ldm7Status  status;
    void* const mldmClnt = mldmClnt_new(mldmCmdPort);

    if (mldmClnt == NULL) {
        log_add("Couldn't create new multicast LDM RPC client");
        status = LDM7_SYSTEM;
    }
    else {
        status = mldmClnt_reserve(mldmClnt, downFmtpAddr);

        if (status) {
            log_add("Couldn't obtain IP address for remote FMTP layer");
        }
        else {
            char buf[80];
            log_info("Allocated IP address %s for remote FMTP",
                    inet_ntop(AF_INET, downFmtpAddr, buf, sizeof(buf)));
        }

        mldmClnt_free(mldmClnt);
    }

    return status;
}

/**
 * Explicitly allows the IP address of an FMTP client to connect to the
 * multicast LDM FMTP server.
 *
 * @param[in] fmtpClntAddr  IP address of FMTP client
 * @retval    0             Success
 * @retval    LDM7_SYSTEM   System failure. `log_add()` called.
 */
static Ldm7Status
mldm_allow(const in_addr_t fmtpClntAddr)
{
    Ldm7Status  status;
    void* const mldmClnt = mldmClnt_new(mldmCmdPort);

    if (mldmClnt == NULL) {
        log_add("mldmClnt_new() failure");
        status = LDM7_SYSTEM;
    }
    else {
        status = mldmClnt_allow(mldmClnt, fmtpClntAddr);

        if (status) {
            log_add("mldmClnt_allow() failure");
        }
        else {
            char ipStr[INET_ADDRSTRLEN];
            log_debug("Address %s allowed", inet_ntop(AF_INET, &fmtpClntAddr,
                    ipStr, sizeof(ipStr)));
        }

        mldmClnt_free(mldmClnt);
    } // Have multicast LDM command-client

    return status;
}

/**
 * Releases for reuse the IP address of an FMTP client.
 *
 * @param[in]     fmtpClntAddr  IP address of FMTP client
 * @retval        LDM7_OK       Success
 * @retval        LDM7_NOENT    `fmtpClntAddr` wasn't previously reserved.
 *                              `log_add()` called.
 * @retval        LDM7_SYSTEM   System failure. `log_add()` called.
 */
static Ldm7Status
mldm_release(const in_addr_t fmtpClntAddr)
{
    Ldm7Status  status;
    void* const mldmClnt = mldmClnt_new(mldmCmdPort);

    if (mldmClnt == NULL) {
        log_add("mldmClnt_new() failure");
        status = LDM7_SYSTEM;
    }
    else {
        status = mldmClnt_release(mldmClnt, fmtpClntAddr);

        if (status) {
            log_add("mldmClnt_release() failure");
        }
        else {
            char ipStr[INET_ADDRSTRLEN];
            log_debug("Address %s released", inet_ntop(AF_INET, &fmtpClntAddr,
                    ipStr, sizeof(ipStr)));
        }

        mldmClnt_free(mldmClnt);
    }

    return status;
}

/******************************************************************************
 * OESS-based submodule for creating an AL2S virtual circuit
 ******************************************************************************/

static const char    python[] = "python"; ///< Name of python executable

/**
 * Creates an AL2S virtual circuit between two end-points.
 *
 * @param[in]  wrkGrpName   Name of the AL2S workgroup
 * @param[in]  desc         Description of virtual circuit
 * @param[in]  end1         One end of the virtual circuit. If the endpoint
 *                          isn't valid, then the circuit will not be created.
 * @param[in]  end2         Other end of the virtual circuit. If the endpoint
 *                          isn't valid, then the circuit will not be created.
 * @param[out] circuitId    Identifier of created virtual-circuit. Caller should
 *                          call `free(*circuitId)` when the identifier is no
 *                          longer needed.
 * @retval     0            Success or an endpoint isn't valid
 * @retval     LDM7_INVAL   Invalid argument. `log_add()` called.
 * @retval     LDM7_SYSTEM  Failure. `log_add()` called.
 */
static int
oess_provision(
        const char* const restrict       wrkGrpName,
        const char* const restrict       desc,
        const VcEndPoint* const restrict end1,
        const VcEndPoint* const restrict end2,
        char** const restrict            circuitId)
{
    int  status;

    if (wrkGrpName == NULL || desc == NULL || end1 == NULL ||
            end2 == NULL || circuitId == NULL) {
        char* end1Id = end1 ? vcEndPoint_format(end1) : NULL;
        char* end2Id = end2 ? vcEndPoint_format(end2) : NULL;
        log_add("NULL argument: wrkGrpName=%s, desc=%s, end1=%s, end2=%s, "
                "circuitId=%p", wrkGrpName, desc, end1Id, end2Id, circuitId);
        free(end1Id);
        free(end2Id);
        status = LDM7_INVAL;
    }
    else {
        char vlanId1[12]; // More than sufficient for 12-bit VLAN ID
        char vlanId2[12];

        (void)snprintf(vlanId1, sizeof(vlanId1), "%hu", end1->vlanId);
        (void)snprintf(vlanId2, sizeof(vlanId2), "%hu", end2->vlanId);

        const char* const cmdVec[] = {python, "provision.py", wrkGrpName,
                end1->switchId, end1->portId, vlanId1,
                end2->switchId, end2->portId, vlanId2, NULL};

        rootpriv();
            ChildCmd* cmd = childCmd_execvp(cmdVec[0], cmdVec);
        unpriv();

        if (cmd == NULL) {
            status = LDM7_SYSTEM;
        }
        else {
            char*   line = NULL;
            size_t  size = 0;
            ssize_t nbytes = childCmd_getline(cmd, &line, &size);
            int     circuitIdStatus;

            if (nbytes <= 0) {
                log_add("Couldn't get AL2S virtual-circuit ID");

                circuitIdStatus = LDM7_SYSTEM;
            }
            else {
                circuitIdStatus = 0;

                if (line[nbytes-1] == '\n')
                    line[nbytes-1] = 0;
            }

            int childExitStatus;

            status = childCmd_reap(cmd, &childExitStatus);

            if (status) {
                status = LDM7_SYSTEM;
            }
            else {
                if (childExitStatus) {
                    log_add("OESS provisioning process terminated with status "
                            "%d", childExitStatus);

                    status = LDM7_SYSTEM;
                }
                else {
                    if (circuitIdStatus) {
                        status = circuitIdStatus;
                    }
                    else {
                        *circuitId = line;
                    }
                } // Child process terminated unsuccessfully
            } // Child-command was reaped
        } // Couldn't execute child-command
    } // Valid arguments and actual provisioning

    return status;
}

/**
 * Destroys an Al2S virtual circuit.
 *
 * @param[in] wrkGrpName   Name of the AL2S workgroup
 * @param[in] circuitId    Virtual-circuit identifier
 */
static void
oess_remove(
        const char* const restrict wrkGrpName,
        const char* const restrict circuitId)
{
    int               status;
    const char* const cmdVec[] = {python, "remove.py", wrkGrpName, circuitId,
            NULL};
    ChildCmd*         cmd = childCmd_execvp(cmdVec[0], cmdVec);

    if (cmd == NULL) {
        status = errno;
    }
    else {
        int exitStatus;

        status = childCmd_reap(cmd, &exitStatus);

        if (status == 0 && exitStatus)
            log_add("Child-process terminated with status %d", exitStatus);
    } // Child-command executing

    if (status) {
        log_add_errno(status, "Couldn't destroy AL2S virtual-circuit");
        log_flush_error();
    }
}

/******************************************************************************
 * Multicast Entry:
 ******************************************************************************/

typedef struct {
    struct in_addr mcastIface;
    SepMcastInfo*  info;
    char*          circuitId;
    char*          pqPathname;
    VcEndPoint*    vcEnd;
    CidrAddr       fmtpSubnet;
    unsigned short ttl;
} McastEntry;

/**
 * Initializes a multicast entry.
 *
 * @param[out] entry       Entry to be initialized.
 * @param[in]  mcastIface  IPv4 address of interface to use for multicasting.
 *                         "0.0.0.0" obtains the system's default multicast
 *                         interface.
 * @param[in]  mcastInfo   Multicast information. Caller may free.
 * @param[in]  ttl         Time-to-live for multicast packets.
 * @param[in]  vcEnd       Local virtual-circuit endpoint or `NULL`. Caller may
 *                         free.
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
        const SepMcastInfo* const        mcastInfo,
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
        //char* const str = smi_toString(mcastInfo);
        //log_notice("me_init(): entry->info=%s", str);
        //free(str);

        entry->vcEnd = vcEndPoint_clone(vcEnd);

        if (entry->vcEnd == NULL) {
            log_add("vcEndPoint_clone() failure");
            status = LDM7_SYSTEM;
        }
        else {
            entry->info = smi_clone(mcastInfo);

            if (entry->info == NULL) {
                log_add("smi_clone() failure");
                status = LDM7_SYSTEM;
            }
            else {
                entry->pqPathname = strdup(pqPathname);

                if (NULL == entry->pqPathname) {
                    log_add_syserr("Couldn't copy pathname of product-queue");
                    status = LDM7_SYSTEM;
                }
                else {
                    cidrAddr_copy(&entry->fmtpSubnet, fmtpSubnet);

                    entry->mcastIface = mcastIface;
                    entry->ttl = ttl;
                    entry->circuitId = NULL;
                    status = 0;
                } // `entry->pqPathname` allocated

                if (status)
                    smi_free(entry->info);
            } // `entry->info` allocated

            if (status)
                vcEndPoint_free(entry->vcEnd);
        } // `entry->vcEnd` allocated
    } // `ttl` is valid

    return status;
}

/**
 * Destroys a multicast entry.
 *
 * @param[in,out] entry  The multicast entry to be destroyed.
 */
static void
me_destroy(McastEntry* const entry)
{
    free(entry->circuitId);
    cidrAddr_destroy(&entry->fmtpSubnet);
    free(entry->pqPathname);
    smi_free(entry->info);
    vcEndPoint_free(entry->vcEnd);
}

/**
 * Returns a new multicast entry.
 *
 * @param[out] entry       New, initialized entry.
 * @param[in]  mcastIface  IPv4 address of interface to use for multicasting.
 *                         "0.0.0.0" obtains the system's default multicast
 *                         interface.
 * @param[in]  mcastInfo   Multicast information. Caller may free.
 * @param[in]  ttl         Time-to-live for multicast packets.
 * @param[in]  vcEnd       Local virtual-circuit endpoint or `NULL`. Caller may
 *                         free.
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
        const SepMcastInfo* const        mcastInfo,
        unsigned short                   ttl,
        const VcEndPoint* const restrict vcEnd,
        const CidrAddr* const restrict   fmtpSubnet,
        const char* const restrict       pqPathname)
{
    int         status;
    McastEntry* ent = log_malloc(sizeof(McastEntry), "multicast entry");

    if (ent == NULL) {
        status = LDM7_SYSTEM;
    }
    else {
        //char* const str = smi_toString(mcastInfo);
        //log_notice("me_new(): info=%s", str);
        //free(str);

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
 * Creates an AL2S virtual-circuit between two end-points for a given LDM feed.
 *
 * @param[in,out] entry       Multicast entry
 * @param[in]     wrkGrpName  Name of AL2S workgroup or `NULL`
 * @param[in]     feed        LDM feed
 * @param[in]     rmtVcEnd    Remote end of virtual circuit
 * @retval        0           Success
 * @retval        LDM7_SYSTEM Failure. `log_add()` called.
 */
static Ldm7Status
me_createVirtCirc(
        McastEntry* const restrict       entry,
        const char* const restrict       wrkGrpName,
        const VcEndPoint* const restrict rmtVcEnd)
{
    int  status;
    char feedStr[128];

    (void)ft_format(smi_getFeed(entry->info), feedStr, sizeof(feedStr));
    feedStr[sizeof(feedStr)-1] = 0;

    char* const desc = ldm_format(128, "%s feed", feedStr);

    if (desc == NULL) {
        log_add("Couldn't format description for AL2S virtual-circuit for "
                "feed %s", feedStr);
        status = LDM7_SYSTEM;
    }
    else {
        status = oess_provision(wrkGrpName, desc, entry->vcEnd, rmtVcEnd,
                &entry->circuitId);

        if (status)
            log_add("Couldn't add host to AL2S virtual circuit for feed %s",
                    feedStr);

        free(desc);
    } // `desc` allocated

    return status;
}

/**
 * Destroys the virtual circuit of a multicast entry.
 *
 * @param[in,out] entry       Multicast entry
 * @param[in]     wrkGrpName  Name of AL2S workgroup
 */
static void
me_destroyVirtCirc(
        McastEntry* const restrict entry,
        const char* const restrict wrkGrpName)
{
    if (entry->circuitId) {
        oess_remove(wrkGrpName, entry->circuitId);
        free(entry->circuitId);
        entry->circuitId = NULL;
    }
}

/**
 * Indicates if the multicast LDM sender of a multicast entry multicasts on an
 * AL2S multipoint VLAN.
 *
 * @param[in] entry    Multicast entry
 * @retval    `true`   The associated multicast LDM sender does use a VLAN
 * @retval    `false`  The associated multicast LDM sender does not use a VLAN
 */
static inline bool
me_usesVlan(McastEntry* const entry)
{
    return vcEndPoint_isValid(entry->vcEnd);
}

/**
 * Subscribes to an LDM7 multicast:
 *   - Adds the FMTP client to the multipoint VLAN if appropriate
 *   - Starts the multicast LDM process if necessary
 *   - Returns information on the multicast group
 *   - Returns the CIDR address for the FMTP client if appropriate
 *
 * @param[in]  entry        Multicast entry
 * @param[in]  wrkGrpName   Name of AL2S workgroup
 * @param[in]  clntAddr     Address of client in network byte order
 * @param[in]  rmtVcEnd     Remote virtual-circuit endpoint or `NULL`. Caller
 *                          may free.
 * @param[in]  retxTimeout  FMTP retransmission timeout in minutes. A negative
 *                          value obtains the FMTP default.
 * @param[out] smi          Separated-out multicast information. Set only on
 *                          success.
 * @param[out] fmtpClntCidr CIDR address for the FMTP client. Set only on
 *                          success.
 * @retval 0                Success
 * @retval LDM7_MCAST       All addresses have been reserved
 * @retval LDM7_SYSTEM      System error. `log_add()` called.
 */
static Ldm7Status
me_subscribe(
        McastEntry* const restrict          entry,
        const char* const restrict          wrkGrpName,
        const in_addr_t                     clntAddr,
        const VcEndPoint* const restrict    rmtVcEnd,
        const float                         retxTimeout,
        const SepMcastInfo** const restrict smi,
        CidrAddr* const restrict            fmtpClntCidr)
{
    int status = me_usesVlan(entry)
            ? me_createVirtCirc(entry, wrkGrpName, rmtVcEnd)
            : 0;

    if (status == 0) {
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
            //log_notice("me_setSubscriptionReply(): entry->info=%s", str);
            //free(str);

            if (me_usesVlan(entry)) {
                in_addr_t fmtpClntAddr;

                status = mldm_getFmtpClntAddr(&fmtpClntAddr);

                if (status == 0)
                    cidrAddr_init(fmtpClntCidr, fmtpClntAddr,
                            cidrAddr_getPrefixLen(&entry->fmtpSubnet));
            } // Sending FMTP multicasts on multipoint VLAN
            else {
                status = mldm_allow(clntAddr);

                if (status) {
                    log_add("mldm_allow() failure");
                }
                else {
                    cidrAddr_init(fmtpClntCidr, clntAddr, 32);
                }
            } // Sending FMTP doesn't multicast on multipoint VLAN

            if (status == 0)
                *smi = entry->info;
        } // Multicast LDM sender is running

        if (status && me_usesVlan(entry))
            me_destroyVirtCirc(entry, wrkGrpName);
    } // Virtual circuit to FMTP client created if appropriate

    return status;
}

/**
 * Unsubscribes an FMTP client from the multicast LDM sender associated with
 * a multicast entry.
 *
 * @param[in,out] entry         Multicast entry
 * @param[in]     fmtpClntAddr  Address of FMTP client
 * @param[in]     wrkGrpName    Name of AL2S workgroup. Only necessary if the
 *                              multicast LDM sender associated with `entry`
 *                              communicates with the FMTP client using an AL2S
 *                              multipoint VLAN.
 * @retval        LDM7_OK       Success. `fmtpClntAddr` is available for subsequent
 *                              reservation.
 * @retval        LDM7_NOENT    `fmtpAddr` wasn't previously reserved.
 *                              `log_add()` called.
 * @retval        LDM7_SYSTEM   System failure. `log_add()` called.
 */
static Ldm7Status
me_unsubscribe(
        McastEntry* const restrict entry,
        const in_addr_t            fmtpClntAddr,
        const char* const restrict wrkGrpName)
{
    int status;

    if (me_usesVlan(entry)) {
        status = mldm_release(fmtpClntAddr);

        if (status) {
            struct in_addr addr = {fmtpClntAddr};
            log_add("Couldn't release client FMTP address %s for reuse",
                    inet_ntoa(addr));
        }

        me_destroyVirtCirc(entry, wrkGrpName);
    } // Associated multicast LDM sender uses an AL2S VLAN

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
/// Name of AL2S Workgroup
static const char* wrkGrpName;

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

void
umm_setWrkGrpName(const char* const name)
{
    wrkGrpName = name;
}

Ldm7Status
umm_addPotentialSender(
    const struct in_addr               mcastIface,
    const SepMcastInfo* const restrict mcastInfo,
    const unsigned short               ttl,
    const VcEndPoint* const restrict   vcEnd,
    const CidrAddr* const restrict     fmtpSubnet,
    const char* const restrict         pqPathname)
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
        const feedtypet                     feed,
        const in_addr_t                     clntAddr,
        const VcEndPoint* const restrict    rmtVcEnd,
        const SepMcastInfo** const restrict smi,
        CidrAddr* const restrict            fmtpClntCidr)
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
         * the multicast LDM sender process if appropriate
         */
        status = me_subscribe(entry, wrkGrpName, clntAddr, rmtVcEnd,
                retxTimeout, smi, fmtpClntCidr);

        if (status)
            log_add("me_subscribe() failure");
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
        const in_addr_t fmtpClntAddr)
{
    int status;

    McastEntry* entry = umm_getMcastEntry(feed);

    if (entry == NULL) {
        log_add("No multicast LDM sender corresponds to feed %s",
                s_feedtypet(feed));
        status = LDM7_INVAL;
    }
    else  {
        status = me_unsubscribe(entry, fmtpClntAddr, wrkGrpName);

        if (status)
            log_add("me_unsubscribe() failure");
    } // Corresponding entry found

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
