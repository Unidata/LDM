/*
 *   Copyright 2015, University Corporation for Atmospheric Research
 *   See file ../COPYRIGHT for copying and redistribution conditions.
 */

/* 
 * Miscellaneous functions to make dealing with Internet addresses easier.
 */

#ifndef _INETUTIL_H_
#define _INETUTIL_H_

#include "config.h"

#include "error.h"
#include "ldm.h"
#include "rpc/types.h"
#include "rpc/xdr.h"

#include <stdbool.h>
#include <netinet/in.h>
#include <sys/socket.h>

#ifdef IPPROTO_IP /* we included netinet/in.h, so struct sockaddr_in is */
extern const char*    hostbyaddr(
    const struct sockaddr_in* const paddr);
extern int            addrbyhost(
    const char* const               id,
    struct sockaddr_in* const       paddr);
extern ErrorObj*      hostHasIpAddress(
    const char* const	            hostname,
    const in_addr_t	            targetAddr,
    int* const		            hasAddress);
extern char*          s_sockaddr_in(struct sockaddr_in *paddr);
extern int            gethostaddr_in(struct sockaddr_in *paddr);
#endif
extern int            getservport(const char *servicename, const char *proto);
extern char*          ghostname(void);
extern int            udpopen(const char *hostname, const char *servicename);
extern int            isMe(const char *remote);
extern int            local_sockaddr_in(struct sockaddr_in* addr);
extern int            sockbind(const char *type, unsigned short port);
/**
 * Returns the IPv4 dotted-decimal form of an Internet identifier.
 *
 * @param[in]  inetId  The Internet identifier. May be a name or a formatted
 *                     IPv4 address in dotted-decimal format.
 * @param[out] out     The corresponding form of the Internet identifier in
 *                     dotted-decimal format. It's the caller's responsibility
 *                     to ensure that the buffer can hold at least
 *                     `INET_ADDRSTRLEN` (from `<netinet/in.h>`) bytes.
 * @retval     0       Success. `out` is set.
 * @retval     EAGAIN  A necessary resource is temporarily unavailable.
 *                     `log_add()` called.
 * @retval     EINVAL  The identifier cannot be converted to an IPv4
 *                     dotted-decimal format. `log_add()` called.
 * @retval     ENOENT  No IPv4 address corresponds to the given Internet
 *                     identifier.
 * @retval     ENOMEM  Out-of-memory. `log_add()` called.
 * @retval     ENOSYS  A non-recoverable error occurred when attempting to
 *                     resolve the identifier. `log_add()` called.
 */
int
getDottedDecimal(
    const char* const    inetId,
    char* const restrict out);

/**
 * Initializes an IPv4 address from a string specification.
 *
 * @param[out] addr  The IPv4 address in network byte order.
 * @param[in]  spec  The IPv4 address in Internet standard dot notation or NULL
 *                   to obtain INADDR_ANY.
 * @retval     0     Success. `*addr` is set.
 * @retval     1     Usage error. `log_add()` called.
 */
int
addr_init(
        in_addr_t* const restrict  addr,
        const char* const restrict spec);

/**
 * Vets a multicast IPv4 address.
 *
 * @param[in] addr   The IPv4 address to be vetted in network byte order.
 * @retval    true   The IPv4 address is a valid multicast address.
 * @retval    false  The IPv4 address is not a valid multicast address.
 */
bool
mcastAddr_isValid(
        const in_addr_t addr);

/**
 * Initializes an IPv4 address from an IPv4 address specification.
 *
 * @param[out] inetAddr   The IPv4 address.
 * @param[in]  inetSpec   The IPv4 address specification. May be `NULL` to
 *                        obtain `INADDR_ANY`.
 * @retval     0          Success. `*inetAddr` is set.
 * @retval     1          Usage error. `log_add()` called.
 */
int
inetAddr_init(
        struct in_addr* const restrict inetAddr,
        const char* const restrict     spec);

/**
 * Initializes an IPv4 socket address.
 *
 * @param[out] sockAddr  The IPv4 socket address to be initialized.
 * @param[in]  addr      The IPv4 address in network byte order.
 * @param[in]  port      The port number in host byte order.
 * @retval     0         Success. `*sockAddr` is set.
 * @retval     1         Usage error. `log_add()` called.
 */
void
sockAddr_init(
        struct sockaddr_in* const restrict sockAddr,
        const in_addr_t                    addr,
        const unsigned short               port);

/**
 * Initializes a UDP socket from an IPv4 socket address.
 *
 * @param[out] sock      The socket.
 * @param[in]  sockAddr  The IPv4 socket address.
 * @retval     0         Success.
 * @retval     2         System failure. `log_add()` called.
 */
int
udpSock_init(
        int* const restrict                      sock,
        const struct sockaddr_in* const restrict sockAddr);

/**
 * Joins a socket to an IPv4 multicast group.
 *
 * @param[out] socket     The socket.
 * @param[in]  mcastAddr  IPv4 address of the multicast group.
 * @param[in]  ifaceAddr  IPv4 address of the interface on which to listen for
 *                        multicast UDP packets. May specify `INADDR_ANY`.
 * @retval     0          Success.
 * @retval     2          O/S failure. `log_add()` called.
 */
int
mcastRecvSock_joinGroup(
        const int                            socket,
        const struct in_addr* const restrict mcastAddr,
        const struct in_addr* const restrict ifaceAddr);

/**
 * Initializes an IPv4 multicast socket.
 *
 * @param[out] socket         The socket.
 * @param[in]  mcastSockAddr  IPv4 socket address of the multicast group to
 *                            join.
 * @param[in]  ifaceAddr      IPv4 address of the interface. May specify
 *                            `INADDR_ANY`.
 * @retval     0              Success.
 * @retval     1              Usage failure. `log_add()` called.
 * @retval     2              System failure. `log_add()` called.
 */
int
mcastRecvSock_init(
        int* const restrict                      socket,
        const struct sockaddr_in* const restrict mcastSockAddr,
        const struct in_addr* const restrict     ifaceAddr);

/**
 * Returns an identifier of the host referenced by a socket address.
 * @param[in]  sockAddr  Socket address to be examined
 * @param[out] id        Host identifier: either a hostname or IP address in
 *                       dotted-decimal form
 * @param[in]  size      Size of `id` in bytes. Should be at least
 *                       `_POSIX_HOST_NAME_MAX+1`.
 * @threadsafety         Safe
 */
void sockAddrIn_getHostId(
        const struct sockaddr_in* const restrict sockAddr,
        char* const restrict                     id,
        const size_t                             size);

#if WANT_MULTICAST
/**
 * Returns a new service address.
 *
 * @param[out] svcAddr  Service address. Caller should call
 *                      `sa_free(*serviceAddr)` when it's no longer needed.
 * @param[in]  addr     Identifier of the service. May be a name or formatted IP
 *                      address. Client may free upon return.
 * @param[in]  port     Port number of the service. Must be non-negative.
 * @retval     0        Success. `*svcAddr` is set.
 * @retval     EINVAL   Invalid Internet address or port number. `log_add()`
 *                      called.
 * @retval     ENOMEM   Out-of-memory. `log_add()` called.
 */
int
sa_new(
    ServiceAddr** const  svcAddr,
    const char* const    addr,
    const int            port);
extern void           sa_free(ServiceAddr* const sa);
/**
 * Copies a service address.
 *
 * @param[out] dest   The destination.
 * @param[in]  src    The source. The caller may free.
 * @retval     true   Success. `*dest` is set.
 * @retval     false  Failure. `log_add()` called.
 */
extern bool           sa_copy(
    ServiceAddr* const restrict       dest,
    const ServiceAddr* const restrict src);
extern void           sa_destroy(ServiceAddr* sa);
extern ServiceAddr*   sa_clone(const ServiceAddr* const sa);
extern const char*    sa_getInetId(const ServiceAddr* const sa);
extern unsigned short sa_getPort(const ServiceAddr* const sa);
extern int            sa_snprint(const ServiceAddr* restrict sa,
                          char* restrict buf, size_t len);
/**
 * Returns the formatted representation of a service address.
 *
 * This function is thread-safe.
 *
 * @param[in]  sa    Pointer to the service address.
 * @retval     NULL  Failure. `log_add()` called.
 * @return           Pointer to the formatted representation. The caller should
 *                   free when it's no longer needed.
 */
extern char*          sa_format(const ServiceAddr* const sa);
/**
 * Parses a formatted Internet service address. An Internet service address has
 * the general form `id:port`, where `id` is the Internet identifier (either a
 * name, a formatted IPv4 address, or a formatted IPv6 address enclosed in
 * square brackets) and `port` is the port number.
 *
 * @param[out] serviceAddr  Internet service address. Caller should call
 *                          `sa_free(*sa)` when it's no longer needed.
 * @param[in]  spec         String containing the specification.
 * @retval     0            Success. `*sa` is set.
 * @retval     EINVAL       Invalid specification. `log_add()` called.
 * @retval     ENOMEM       Out of memory. `log_add()` called.
 */
int
sa_parse(
    ServiceAddr** const restrict serviceAddr,
    const char* restrict         spec);
/**
 * Like `sa_parse()` but with default values for the Internet identifier and
 * port number. If a field in the specification doesn't exist, then the
 * corresponding default value is used, if possible.
 *
 * @param[out] svcAddr      Internet service address. Caller should call
 *                          `sa_free(*sa)` when it's no longer needed.
 * @param[in]  spec         String containing the specification. Caller may
 *                          free.
 * @param[in]  defId        Default Internet identifier or NULL. If NULL, then
 *                          Internet ID must exist in specification. Caller may
 *                          free.
 * @param[in]  defPort      Default port number. If negative, then port number
 *                          must exist in specification.
 * @retval     0            Success. `*sa` is set.
 * @retval     EINVAL       Invalid specification. `log_add()` called.
 * @retval     ENOMEM       Out of memory. `log_add()` called.
 */
int
sa_parseWithDefaults(
        ServiceAddr** const restrict svcAddr,
        const char* restrict         spec,
        const char* restrict         defId,
        const int                    defPort);
/**
 * Returns the Internet socket address that corresponds to a service address.
 *
 * @param[in]  servAddr      The service address.
 * @param[in]  family        Address family. One of AF_INET, AF_INET6, or
 *                           AF_UNSPEC, in which case the name in `servAddr`
 *                           should be unambiguous (i.e., not a hostname).
 * @param[in]  serverSide    Whether or not the returned socket address should be
 *                           suitable for a server's `bind()` operation.
 * @param[out] inetSockAddr  The corresponding Internet socket address. The
 *                           socket type will be `SOCK_STREAM` and the protocol
 *                           will be `IPPROTO_TCP`.
 * @param[out] sockLen       The size of the returned socket address in bytes.
 *                           Suitable for use in a `bind()` or `connect()` call.
 * @retval     0             Success.
 * @retval     EAGAIN        A necessary resource is temporarily unavailable.
 *                           `log_add()` called.
 * @retval     EINVAL        Invalid port number. `log_add()` called.
 * @retval     ENOENT        The service address doesn't resolve into an IP
 *                           address.
 * @retval     ENOMEM        Out-of-memory. `log_add()` called.
 * @retval     ENOSYS        A non-recoverable error occurred when attempting to
 *                           resolve the name. `log_add()` called.
 */
int
sa_getInetSockAddr(
    const ServiceAddr* const       servAddr,
    const int                      family,
    const bool                     serverSide,
    struct sockaddr_storage* const inetSockAddr,
    socklen_t* const               sockLen);

/**
 * Compares two service address objects. Returns a value less than, equal to, or
 * greater than zero as the first object is considered less than, equal to, or
 * greater than the second object, respectively. Service addresses are
 * considered equal if their Internet identifiers and port numbers are equal.
 *
 * @param[in] sa1  First service address object.
 * @param[in] sa2  Second service address object.
 * @retval    -1   First object is less than second.
 * @retval     0   Objects are equal.
 * @retval    +1   First object is greater than second.
 */
int
sa_compare(
    const ServiceAddr* const sa1,
    const ServiceAddr* const sa2);
#endif // WANT_MULTICAST

#endif /* !_INETUTIL_H_ */
