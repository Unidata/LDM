/**
 * This file declares the remote-procedure-call API for the multicast LDM.
 *
 *        File: MldmRpc.h
 *  Created on: Feb 7, 2018
 *      Author: Steven R. Emmerson
 */

#ifndef MCAST_LIB_C___MLDMRPC_H_
#define MCAST_LIB_C___MLDMRPC_H_

#include "ldm.h"

#include <netinet/in.h>

/******************************************************************************
 * C API:
 ******************************************************************************/

#ifdef __cplusplus
    extern "C" {
#endif

/**
 * Returns a new multicast LDM RPC client.
 * @param[in] port  Port number of the multicast LDM RPC server in host
 *                  byte-order
 * @retval NULL     Failure
 * @return          Pointer to multicast LDM RPC client. Caller should call
 *                  `mldmClnt_delete()` when it's no longer needed.
 * @see mldmClnt_delete()
 */
void* mldmClnt_new(const in_port_t port);

/**
 *
 * @param mldmClnt
 * @param fmtpAddr
 * @retval LDM7_SYSTEM  System failure. `log_add()` called.
 */
Ldm7Status mldmClnt_reserve(
        void*      mldmClnt,
        in_addr_t* fmtpAddr);

/**
 *
 * @param mldmClnt
 * @param fmtpAddr
 * @retval LDM7_SYSTEM  System failure. `log_add()` called.
 */
Ldm7Status mldmClnt_release(
        void*           mldmClnt,
        const in_addr_t fmtpAddr);

/**
 * Destroys an allocated multicast LDM RPC client and deallocates it.
 * @param[in] mldmClnt  Multicast LDM RPC client
 * @see mldmClnt_new()
 */
void mldmClnt_delete(void* mldmClnt);

/**
 * Constructs. Creates a listening server-socket and a file that contains a
 * secret.
 * @param[in] networkPrefix  Prefix for IP addresses in network byte-order
 * @param[in] prefixLen      Number of bits in network prefix
 */
void* mldmSrvr_new(
        const in_addr_t networkPrefix,
        const unsigned  prefixLen);

in_port_t mldmSrvr_getPort(void* mldmSrvr);

/**
 * Starts the multicast LDM RPC server. Doesn't return unless a fatal error
 * occurs.
 * @param[in] mldmSrvr     Multicast LDM RPC server
 * @retval    LDM7_SYSTEM  System failure. `log_add()` called.
 * @threadsafety           Unsafe
 */
Ldm7Status mldmSrvr_run(void* mldmSrvr);

/**
 * Destroys an allocated multicast LDM RPC server and deallocates it.
 * @param[in] mldmClnt  Multicast LDM RPC server
 * @see mldmSrvr_new()
 */
void mldmSrvr_delete(void* mldmSrvr);

#ifdef __cplusplus
} // `extern "C"`

/******************************************************************************
 * C++ API:
 ******************************************************************************/

#include "Authorizer.h"

#include <memory>

typedef enum MldmRpcAct
{
    RESERVE_ADDR,
    RELEASE_ADDR
} MldmRpcAct;

/**
 * Multicast LDM RPC client.
 */
class MldmClnt final
{
    class                 Impl;
    std::shared_ptr<Impl> pImpl;

public:
    /**
     * Constructs.
     * @param[in] port  Port number of multicast LDM RPC server in host
     *                  byte-order
     */
    MldmClnt(const in_port_t port);

    in_addr_t reserve() const;

    void release(const in_addr_t fmtpAddr) const;
};

/**
 * Multicast LDM RPC server.
 */
class MldmSrvr final
{
    class                 Impl;
    std::shared_ptr<Impl> pImpl;

public:
    /**
     * Constructs. Creates a listening server-socket and a file that contains a
     * secret.
     * @param[in] networkPrefix  Prefix for IP addresses in network byte-order
     * @param[in] prefixLen      Number of bits in network prefix
     */
    MldmSrvr(
            const in_addr_t networkPrefix,
            const unsigned  prefixLen);

    in_port_t getPort() const noexcept;

    /**
     * Runs the server. Doesn't return unless an exception is thrown.
     */
    void operator()() const;
};

#endif // `#ifdef __cplusplus`

#endif /* MCAST_LIB_C___MLDMRPC_H_ */
