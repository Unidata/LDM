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

#ifdef __cplusplus
    extern "C" {
#endif

/******************************************************************************
 * C API:
 ******************************************************************************/

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
 * @retval Ldm7_SYSTEM  System failure. `log_add()` called.
 */
Ldm7Status mldmClnt_reserve(
        void*           mldmClnt,
        struct in_addr* fmtpAddr);

/**
 *
 * @param mldmClnt
 * @param fmtpAddr
 * @retval Ldm7_SYSTEM  System failure. `log_add()` called.
 */
Ldm7Status mldmClnt_release(
        void*                 mldmClnt,
        const struct in_addr* fmtpAddr);

/**
 * Destroys an allocated multicast LDM RPC client and deallocates it.
 * @param[in] mldmClnt  Multicast LDM RPC client
 * @see mldmClnt_new()
 */
void mldmClnt_delete(void* mldmClnt);

void* mldmSrvr_new(void* authorizer);

in_port_t mldmSrvr_getPort(void* mldmSrvr);

/**
 * Starts the multicast LDM RPC server. Doesn't return unless an error occurs.
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

    struct in_addr reserve() const;

    void release(const struct in_addr& fmtpAddr) const;
};

/**
 * Multicast LDM RPC server.
 */
class MldmSrvr final
{
    class                 Impl;
    std::shared_ptr<Impl> pImpl;

public:
    MldmSrvr(Authorizer& authDb);

    in_port_t getPort() const noexcept;

    /**
     * Runs the server. Doesn't return unless an exception is thrown.
     */
    void operator()() const;
};

#endif // `#ifdef __cplusplus`

#endif /* MCAST_LIB_C___MLDMRPC_H_ */
