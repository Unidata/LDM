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

#include "ChildCmd.h"
#include "CidrAddr.h"
#include "fmtp.h"
#include "globals.h"
#include "InetSockAddr.h"
#include "inetutil.h"
#include "ldmprint.h"
#include "log.h"
#include "mcast_info.h"
#include "MldmRpc.h"
#include "mldm_sender_map.h"
#include "priv.h"
#include "registry.h"
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
/**
 * Port number of FMTP server of multicast LDM sender process in host byte-order
 */
static in_port_t fmtpSrvrPort;
/**
 * Port number of RPC command server of multicast LDM sender process in host
 * byte-order
 */
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
mldm_getSrvrPorts(const int pipe)
{
#if 1
    char    buf[100];
    ssize_t nbytes = read(pipe, buf, sizeof(buf));

    int     status;
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
 * @param[in] subnetLen      Number of bits in the network prefix of the private
 *                           AL2S network.
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
    const SepMcastInfo* const restrict info,
    const unsigned short               ttl,
    const unsigned short               subnetLen,
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
        args[i++] = "-l";
        args[i++] = (char*)logDestArg; // Safe cast
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

    char subnetLenArg[3];
    if (status == 0 && subnetLen) {
        if (snprintf(subnetLenArg, sizeof(subnetLenArg), "%u",
                subnetLen) >= sizeof(subnetLenArg)) {
            log_add("Invalid subnet-length parameter %u", subnetLen);
            status = LDM7_INVAL;
        }
        else {
            args[i++] = "-n";
            args[i++] = subnetLenArg;
        }
    } // Non-default FMTP subnet length

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
mldm_stopSndr()
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
 * Executes a multicast LDM sender as a child process. Doesn't block. Sets
 * `childPid`, `fmtpSrvrPort` and `mldmCmdPort`.
 *
 * @param[in,out] info           Information on the multicast group.
 * @param[in]     ttl            Time-to-live of multicast packets.
 * @param[in]     subnetLen      Number of bits in the network prefix of the
 *                               private AL2S network.
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
    const SepMcastInfo* const restrict info,
    const unsigned short               ttl,
    const unsigned short               subnetLen,
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
            mldm_exec(info, ttl, subnetLen, retxTimeout, pqPathname, fds[1]);
            log_flush_error();
            exit(1);
        }
        else {
            /* Parent process */
            childPid = pid;

            // Sets `fmtpSrvrPort` and `mldmCmdPort`
            status = mldm_getSrvrPorts(fds[0]);

            if (status) {
                char* const id = smi_toString(info);
                log_add("Couldn't get port numbers from multicast LDM sender "
                        "%s", id);
                free(id);

                (void)mldm_stopSndr(); // Uses `childPid`
                childPid = 0;
            }
        } // Parent process

        (void)close(fds[0]);
        (void)close(fds[1]);
    } // Pipe created

    return status;
}

/**
 * Ensures that a multicast LDM sender process is running.
 *
 * @param[in] info         LDM7 multicast information
 * @param[in] ttl          Time-to-live of multicast packets
 * @param[in] subnetLen    Number of bits in the network prefix of the private
 *                         AL2S network.
 * @param[in] retxTimeout  FMTP retransmission timeout in minutes. A negative
 *                         value obtains the FMTP default.
 * @param[in] pqPathname   Pathname of product-queue
 * @retval    0            Success. The multicast LDM sender associated with the
 *                         given multicast group was already running or was
 *                         successfully started. `mldm_getFmtpSrvrPort()` will
 *                         return the port number of the FMTP server of the
 *                         multicast LDM sender process.
 * @retval    LDM7_LOGIC   Logic error. `log_add()` called.
 * @retval    LDM7_SYSTEM  System error. `log_add()` called.
 */
static Ldm7Status
mldm_ensureExec(
        const SepMcastInfo* const restrict info,
        const unsigned short               ttl,
        const unsigned short               subnetLen,
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
        status = msm_get(smi_getFeed(info), &childPid, &fmtpSrvrPort,
                &mldmCmdPort);

        if (status) {
            if (status == LDM7_NOENT) {
                log_debug("No multicast sender for feed %s",
                        s_feedtypet(smi_getFeed(info)));
                childPid = 0;
                status = 0;
            }
            else {
                log_add("msm_get() failure");
            }
        }
        else {
            if (kill(childPid, 0)) {
                log_warning("Multicast LDM sender process %d should "
                        "exist but doesn't. Re-executing...", childPid);
                status = msm_remove(childPid);
                log_assert(status == 0);
                childPid = 0;
            }
        }

        if (status == 0 && childPid == 0) {
            /*
             * Sets `childPid`, `fmtpSrvrPort`, and `mldmCmdPort`
             */
            status = mldm_spawn(info, ttl, subnetLen, retxTimeout, pqPathname);

            if (status) {
                log_add("Couldn't spawn multicast LDM sender process");
            }
            else {
                status = msm_put(smi_getFeed(info), childPid, fmtpSrvrPort,
                        mldmCmdPort);

                if (status) {
                    char* const id = smi_toString(info);
                    log_add("Couldn't save information on multicast LDM sender "
                            "%s. Terminating that process.", id);
                    free(id);

                    (void)mldm_stopSndr(); // Uses `childPid`
                    childPid = 0;
                }
            } // Multicast LDM sender spawned
        } // Multicast LDM sender should be spawned

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
            log_add("Couldn't obtain IP address for remote FMTP client");
        }
        else {
            char buf[80];
            log_info("Allocated IP address %s for remote FMTP client",
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
            log_debug("Address %s is allowed", inet_ntop(AF_INET, &fmtpClntAddr,
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

static const char defaultOessPathname[] = LDMHOME "/etc/OESS-account.yaml";
static char*      oessPathname;

static void
oess_init(void)
{
    if (oessPathname == NULL) {
        if (reg_getString(REG_OESS_PATHNAME, &oessPathname))
            oessPathname = strdup(defaultOessPathname);
    }
}

static void
oess_destroy(void)
{
    free(oessPathname);

    oessPathname = NULL;
}

/**
 * Creates an AL2S virtual circuit between two end-points.
 *
 * @param[in]  wrkGrpName   Name of the AL2S workgroup (e.g., "UCAR-LDM")
 * @param[in]  desc         Description of virtual circuit
 * @param[in]  sendEnd      Sending (local) end of the virtual circuit. If the
 *                          endpoint isn't valid, then the circuit will not be
 *                          created.
 * @param[in]  recvEnd      Receiving (remote) end of the virtual circuit. If
 *                          the endpoint isn't valid, then the circuit will not
 *                          be created.
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
        const VcEndPoint* const restrict sendEnd,
        const VcEndPoint* const restrict recvEnd,
        char** const restrict            circuitId)
{
    int  status;

    if (wrkGrpName == NULL || desc == NULL || sendEnd == NULL ||
            recvEnd == NULL || circuitId == NULL) {
        char* sendEndId = sendEnd ? vcEndPoint_format(sendEnd) : NULL;
        char* recvEndId = recvEnd ? vcEndPoint_format(recvEnd) : NULL;
        log_add("NULL argument: wrkGrpName=%s, desc=%s, sendEnd=%s,"
                "recvEnd=%s, circuitId=%p", wrkGrpName, desc, sendEndId,
                recvEndId, circuitId);
        free(sendEndId);
        free(recvEndId);
        status = LDM7_INVAL;
    }
    else {
        char  sendVlanTag[12]; // More than sufficient for 12-bit VLAN tag
        char  recvVlanTag[12];

        (void)snprintf(sendVlanTag, sizeof(sendVlanTag), "%hu", sendEnd->vlanId);
        (void)snprintf(recvVlanTag, sizeof(recvVlanTag), "%hu", recvEnd->vlanId);

        const char* const cmdVec[] = {"provision.py",
                wrkGrpName, oessPathname, desc,
                recvEnd->switchId, recvEnd->portId, recvVlanTag,
                sendEnd->switchId, sendEnd->portId, sendVlanTag,
                NULL};

        ChildCmd* cmd = childCmd_execvp(cmdVec[0], cmdVec);

        if (cmd == NULL) {
            log_add("Couldn't execute %s", childCmd_getCmd(cmd));
            status = LDM7_SYSTEM;
        }
        else {
            char*   line = NULL;
            size_t  size = 0;
            ssize_t nbytes = childCmd_getline(cmd, &line, &size);
            int     circuitIdStatus;

            if (nbytes <= 0) {
                log_add(nbytes
                            ? "childCmd_getline() failure"
                            : "childCmd_getline() EOF");
                log_add("Couldn't get AL2S virtual-circuit ID from command "
                        "\"%s\"", childCmd_getCmd(cmd));

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
                log_add("Couldn't reap %s process", cmdVec[0]);
                status = LDM7_SYSTEM;
            }
            else {
                if (childExitStatus) {
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
 * @param[in] wrkGrpName   Name of the AL2S workgroup (e.g., "UCAR-LDM")
 * @param[in] desc         Description of circuit (e.g., "NEXRAD2 feed")
 * @param[in] recvEnd      Receiving (remote) end of the virtual circuit
 */
static void
oess_remove(
        const char* const restrict       wrkGrpName,
        const char* const restrict       desc,
        const VcEndPoint* const restrict recvEnd)
{
    if (wrkGrpName == NULL || desc == NULL || recvEnd == NULL) {
        char* recvEndId = recvEnd ? vcEndPoint_format(recvEnd) : NULL;
        log_add("NULL argument: wrkGrpName=%s, desc=%s, recvEnd=%s",
                wrkGrpName, desc, recvEndId);
        free(recvEndId);
    }
    else {
        char recvVlanTag[12];

        (void)snprintf(recvVlanTag, sizeof(recvVlanTag), "%hu",
                recvEnd->vlanId);

        const char* const cmdVec[] = {"remove.py",
                wrkGrpName, oessPathname, desc,
                recvEnd->switchId, recvEnd->portId, recvVlanTag,
                NULL};
        ChildCmd*         cmd = childCmd_execvp(cmdVec[0], cmdVec);

        if (cmd == NULL) {
            log_add("Couldn't execute %s", cmdVec[0]);
        }
        else {
            int exitStatus;
            int status = childCmd_reap(cmd, &exitStatus);

            if (status)
                log_add("Couldn't reap %s process", cmdVec[0]);
        } // Child-command executing
    }

    log_flush_error();
}

/******************************************************************************
 * Multicast Entry:
 ******************************************************************************/

typedef struct {
    SepMcastInfo*  info;
    char*          circuitId;
    char*          pqPathname;
    VcEndPoint*    vcEnd; ///< Local (sending) virtual-circuit endpoint
    unsigned short subnetLen;
    CidrAddr       fmtpSubnet;
    unsigned short ttl;
} McastEntry;

/**
 * Initializes a multicast entry.
 *
 * @param[out] entry       Entry to be initialized.
 * @param[in]  mcastInfo   Multicast information. Caller may free.
 * @param[in]  ttl         Time-to-live for multicast packets.
 * @param[in]  subnetLen   Number of bits in the network prefix of the private
 *                         AL2S network.
 * @param[in]  vcEnd       Sending (local) virtual-circuit endpoint or `NULL`.
 *                         Caller may free.
 * @param[in]  pqPathname  Pathname of product-queue. Caller may free.
 * @retval     0           Success. `*entry` is initialized. Caller should call
 *                         `me_destroy(entry)` when it's no longer needed.
 * @retval     LDM7_INVAL  `ttl` is too large. `log_add()` called.
 * @retval     LDM7_SYSTEM System error. `log_add()` called. The state of
 *                         `*entry` is unspecified.
 * @see `me_destroy()`
 */
static Ldm7Status
me_init(McastEntry* const restrict       entry,
        const SepMcastInfo* const        mcastInfo,
        const unsigned short             ttl,
        const unsigned short             subnetLen,
        const VcEndPoint* const restrict vcEnd,
        const char* const restrict       pqPathname)
{
    int status;

    if (ttl >= 255) {
        log_add("Time-to-live is too large: %u >= 255", ttl);
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
                    entry->subnetLen = subnetLen;
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
    free(entry->pqPathname);
    smi_free(entry->info);
    vcEndPoint_free(entry->vcEnd);
}

/**
 * Returns a new multicast entry.
 *
 * @param[out] entry       New, initialized entry.
 * @param[in]  mcastInfo   Multicast information. Caller may free.
 * @param[in]  ttl         Time-to-live for multicast packets.
 * @param[in]  subnetLen   Number of bits in the network prefix of the private
 *                         AL2S network.
 * @param[in]  vcEnd       Local virtual-circuit endpoint or `NULL`. Caller may
 *                         free.
 * @param[in]  pqPathname  Pathname of product-queue. Caller may free.
 * @retval     0           Success. `*entry` is set. Caller should call
 *                         `me_free(*entry)` when it's no longer needed.
 * @retval     LDM7_INVAL  `info->server->port` is not zero. `log_add()`
 *                         called.
 * @retval     LDM7_INVAL  `ttl` is too large. `log_add()` called.
 * @see `me_free()`
 */
static Ldm7Status
me_new( McastEntry** const restrict      entry,
        const SepMcastInfo* const        mcastInfo,
        unsigned short                   ttl,
        const unsigned short             subnetLen,
        const VcEndPoint* const restrict vcEnd,
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

        status = me_init(ent, mcastInfo, ttl, subnetLen, vcEnd, pqPathname);

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
 * Indicates if two multicast entries conflict (e.g., specify the same
 * multicast group, the same FMTP server with positive port numbers, etc.).
 *
 * @param[in] entry1  First multicast entry.
 * @param[in] entry1  Second multicast entry.
 * @retval    true    The multicast entries do conflict.
 * @retval    false   The multicast entries do not conflict.
 */
static bool
me_doConflict(
        const McastEntry* const entry1,
        const McastEntry* const entry2)
{
    bool conflict;

    if (entry1 == entry2) {
        conflict = false; // Same multicast entry
    }
    else {
        const SepMcastInfo* const info1 = entry1->info;
        const SepMcastInfo* const info2 = entry2->info;
        const InetSockAddr* const mcastGrp1 = smi_getMcastGrp(info1);
        const InetSockAddr* const mcastGrp2 = smi_getMcastGrp(info2);
        const InetSockAddr* const fmtpSrvr1 = smi_getFmtpSrvr(info1);
        const InetSockAddr* const fmtpSrvr2 = smi_getFmtpSrvr(info2);

        if (isa_compare(mcastGrp1, mcastGrp2) == 0) {
            // Source-specific multicasting won't work in the source is the same
            conflict = isa_compare(fmtpSrvr1, fmtpSrvr2) == 0;
        } // Same multicast group addresses
        else {
            const in_port_t port1 = isa_getPort(fmtpSrvr1);
            const in_port_t port2 = isa_getPort(fmtpSrvr2);

            // Different multicast groups can't have the same source
            if (port1 || port2) {
                conflict = isa_compare(fmtpSrvr1, fmtpSrvr2) == 0;
            } // An FMTP server port was explicitly specified
            else {
                const InetId* const hostId1 = isa_getInetId(fmtpSrvr1);
                const InetId* const hostId2 = isa_getInetId(fmtpSrvr2);

                conflict = inetId_compare(hostId1, hostId2) == 0;
            } // Both FMTP server ports will be chosen by O/S
        } // Different multicast group addresses
    } // Not the same entry

    return conflict;
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
me_compareFeeds(
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
    const McastEntry* const entry1 = o1;
    const McastEntry* const entry2 = o2;

    if (me_doConflict(entry1, entry2))
        return 0;

    return me_compareFeeds(entry1, entry2);
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
me_startIfNot(
        McastEntry* const entry,
        const float       retxTimeout)
{
    // Sets `mldmCmdPort`
    int status = mldm_ensureExec(entry->info,
            entry->ttl, entry->subnetLen, retxTimeout, entry->pqPathname);

    if (status == 0) {
        status = isa_setPort(smi_getFmtpSrvr(entry->info),
                mldm_getFmtpSrvrPort());
        log_assert(status == 0);
    }

    return status;
}

/**
 * Returns the description of an AL2S virtual-circuit for an entry.
 *
 * @param[in] entry  Multicast entry
 * @retval    NULL   Failure. `log_add()` called.
 * @return           Description of AL2S virtual-circuit. Caller should free
 *                   when it's no longer needed.
 */
static char*
me_newDesc(const McastEntry* const restrict entry)
{
    char feedStr[128];

    (void)ft_format(smi_getFeed(entry->info), feedStr, sizeof(feedStr));
    feedStr[sizeof(feedStr)-1] = 0;

    char* const desc = ldm_format(128, "%s feed", feedStr);

    if (desc == NULL)
        log_add("Couldn't format description for AL2S virtual-circuit for "
                "feed %s", feedStr);

    return desc;
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
    int         status;
    char* const desc = me_newDesc(entry);

    if (desc == NULL) {
        log_add("Couldn't get description of AL2S virtual-circuit");
        status = LDM7_SYSTEM;
    }
    else {
        status = oess_provision(wrkGrpName, desc, entry->vcEnd, rmtVcEnd,
                &entry->circuitId);

        if (status)
            log_add("Couldn't add host to AL2S virtual circuit");

        free(desc);
    } // `desc` allocated

    return status;
}

/**
 * Destroys the virtual circuit of a multicast entry.
 *
 * @param[in,out] entry       Multicast entry
 * @param[in]     wrkGrpName  Name of AL2S workgroup (e.g., "UCAR-LDM")
 * @param[in]     recvEnd     Receiving (remote) virtual-circuit endpoint
 */
static void
me_destroyVirtCirc(
        McastEntry* const restrict       entry,
        const char* const restrict       wrkGrpName,
        const VcEndPoint* const restrict recvEnd)
{
    if (entry->circuitId) {
        char* const desc = me_newDesc(entry);

        if (desc == NULL) {
            log_add("Couldn't get description of AL2S virtual-circuit");
        }
        else {
            oess_remove(wrkGrpName, desc, recvEnd);
            free(desc);
            free(entry->circuitId);
            entry->circuitId = NULL;
        }
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
        status = me_startIfNot(entry, retxTimeout);

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
                    cidrAddr_init(fmtpClntCidr, fmtpClntAddr, entry->subnetLen);
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
            me_destroyVirtCirc(entry, wrkGrpName, rmtVcEnd);
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
 * @param[in]     recvEnd       Receiving (remote) virtual-circuit endpoint
 * @retval        LDM7_OK       Success. `fmtpClntAddr` is available for subsequent
 *                              reservation.
 * @retval        LDM7_NOENT    `fmtpAddr` wasn't previously reserved.
 *                              `log_add()` called.
 * @retval        LDM7_SYSTEM   System failure. `log_add()` called.
 */
static Ldm7Status
me_unsubscribe(
        McastEntry* const restrict       entry,
        const in_addr_t                  fmtpClntAddr,
        const char* const restrict       wrkGrpName,
        const VcEndPoint* const restrict recvEnd)
{
    int status = 0;

    if (me_usesVlan(entry)) {
        status = mldm_release(fmtpClntAddr);

        if (status) {
            struct in_addr addr = {fmtpClntAddr};
            log_add("Couldn't release client FMTP address %s for reuse",
                    inet_ntoa(addr));
        }

        me_destroyVirtCirc(entry, wrkGrpName, recvEnd);
    } // Associated multicast LDM sender uses a multipoint VLAN

    return status;
}


/******************************************************************************
 * Upstream Multicast Manager:
 ******************************************************************************/

/// Module is initialized?
static bool        initialized = false;

/// Multicast entries
static void*       mcastEntries;

/// Key for multicast entries
static McastEntry  key;

/// FMTP retransmission timeout in minutes
static float       retxTimeout = -1.0; // Negative => use FMTP default

/// Name of AL2S Workgroup
//static const char* wrkGrpName = "UCAR-LDM"; // Default value
static const char* wrkGrpName = "Virginia"; // Default value

/// Receiving (remote) virtual-circuit endpoint
static VcEndPoint* recvEnd = NULL;

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
    McastEntry* entry;

    if (key.info == NULL)
        key.info = smi_newFromStr(0, "0.0.0.0", "0.0.0.0");

    smi_setFeed(key.info, feed);
    void* const node = tfind(&key, &mcastEntries, me_compareFeeds);

    if (NULL == node) {
        log_add("No multicast LDM sender is associated with feed-type %s",
                s_feedtypet(feed));
        entry = NULL;
    }
    else {
        entry = *(McastEntry**)node;
    }

    return entry;
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
umm_addSndr(
    const SepMcastInfo* const restrict mcastInfo,
    const unsigned short               ttl,
    const unsigned short               subnetLen,
    const VcEndPoint* const restrict   vcEnd,
    const char* const restrict         pqPathname)
{
    int status;

    if (!initialized) {
        log_add("Upstream multicast manager is not initialized");
        status = LDM7_LOGIC;
    }
    else {
        //char* const str = smi_toString(mcastInfo);
        //log_notice("umm_addPotentialSender(): info=%s", str);
        //free(str);

        McastEntry* entry;

        status = me_new(&entry, mcastInfo, ttl, subnetLen, vcEnd, pqPathname);

        if (0 == status) {
            const void* const node = tsearch(entry, &mcastEntries,
                    me_compareOrConflict);

            if (NULL == node) {
                log_add_syserr("Couldn't add to multicast entries");
                me_free(entry);

                status = LDM7_SYSTEM;
            }
        } // `entry` allocated
    }

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

    if (!initialized) {
        log_add("Upstream multicast manager is not initialized");
        status = LDM7_LOGIC;
    }
    else {
        McastEntry* entry = umm_getMcastEntry(feed);

        if (NULL == entry) {
            log_add("No multicast entry corresponds to feed %s",
                    s_feedtypet(feed));
            status = LDM7_NOENT;
        }
        else {
            //char* const str = smi_toString(entry->info);
            //log_notice("umm_subscribe(): entry->info=%s", str);
            //free(str);

            recvEnd = vcEndPoint_clone(rmtVcEnd);

            if (recvEnd == NULL) {
                log_add("Couldn't clone remote virtual-circuit endpoint");
                status = LDM7_SYSTEM;
            }
            else {
                /*
                 * Sets the port numbers of the FMTP server & RPC-command server
                 * of the multicast LDM sender process if appropriate
                 */
                status = me_subscribe(entry, wrkGrpName, clntAddr, recvEnd,
                        retxTimeout, smi, fmtpClntCidr);

                if (status) {
                    log_add("me_subscribe() failure");
                    vcEndPoint_free(recvEnd);
                    recvEnd = NULL;
                }
           } // Remote endpoint cloned
        } // Desired feed maps to a possible multicast LDM sender
    } // Module is initialized

    return status;
}

Ldm7Status
umm_terminated(
        const pid_t pid)
{
    int status;

    if (!initialized) {
        log_add("Upstream multicast manager is not initialized");
        status = LDM7_LOGIC;
    }
    else {
        status = msm_lock(true);

        if (status) {
            log_add("Couldn't lock multicast sender map");
        }
        else {
            status = msm_remove(pid);

            if (pid == childPid)
                childPid = 0; // No need to kill child because must have terminated

            (void)msm_unlock();
        } // Multicast sender map is locked
    } // This module is initialized

    return status;
}

pid_t
umm_getSndrPid(void)
{
    return mldm_getMldmSenderPid();
}

Ldm7Status
umm_unsubscribe(
        const feedtypet feed,
        const in_addr_t fmtpClntAddr)
{
    int status;

    if (!initialized) {
        log_add("Upstream multicast manager is not initialized");
        status = LDM7_LOGIC;
    }
    else {
        McastEntry* entry = umm_getMcastEntry(feed);

        if (entry == NULL) {
            log_add("No multicast LDM sender corresponds to feed %s",
                    s_feedtypet(feed));
            status = LDM7_INVAL;
        }
        else  {
            status = me_unsubscribe(entry, fmtpClntAddr, wrkGrpName, recvEnd);

            if (status)
                log_add("me_unsubscribe() failure");
        } // Corresponding entry found
    }

    return status;
}

void
umm_clear(void)
{
    while (mcastEntries) {
        McastEntry* const entry = *(McastEntry**)mcastEntries;

        (void)tdelete(entry, &mcastEntries, me_compareOrConflict);
        me_free(entry);
    }

    if (key.info) {
        smi_free(key.info);
        key.info = NULL;
    }
}

Ldm7Status
umm_init(void)
{
    int status;

    if (initialized) {
        log_add("Upstream multicast manager is already initialized");
        status = LDM7_LOGIC;
    }
    else {
        status = msm_init();

        if (status) {
            log_add("Couldn't initialize the multicast sender map");
        }
        else {
            oess_init();

            initialized = true;
        }
    }

    return status;
}

void
umm_destroy(const bool final)
{
    if (!initialized) {
        log_warning("Upstream multicast manager is not initialized");
    }
    else {
        oess_destroy();
        umm_clear();
        vcEndPoint_free(recvEnd);
        recvEnd = NULL;
        msm_destroy(final);
        initialized = false;
    }
}

bool
umm_isInited(void)
{
    return initialized;
}

Ldm7Status
umm_remove(const pid_t pid)
{
    int status;

    if (!initialized) {
        log_add("Upstream multicast manager is not initialized");
        status = LDM7_LOGIC;
    }
    else {
        status = msm_lock(true);

        if (status) {
            log_add("msm_lock() failure");
        }
        else {
            status = msm_remove(pid);
        }

        (void)msm_unlock();
    }

    return status;
}
