/**
 * Miscellaneous functions dealing with Internet addresses.
 *
 * Copyright 2019, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 */

#include <config.h>

#ifndef _BSD_SOURCE
    #define _BSD_SOURCE  // To get `struct ip_mreq` on GNU/Linux. Don't move!
#endif

#include "error.h"
#include "inetutil.h"
#include "ldmprint.h"
#include "log.h"
#include "registry.h"
#include "timestamp.h"
#include "rpc/xdr.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
/*
 * On FreeBSD 4.10-RELEASE-p2 the following order is necessary.
 */
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <netdb.h>
#include <string.h>
#include <stdlib.h>

/*
 * Host names are limited to 255 bytes by the The Single UNIX�
 * Specification, Version 2, for the function gethostname().  MAXHOSTNAMELEN
 * is not required by that specification to be defined.
 */
#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN 256
#endif

#ifndef h_errno /* AIX 4.3 */
extern int      h_errno;        /* error number for gethostby...() functions */
#endif

/*
 * Return a string indicating the problem with one of the gethostby...()
 * functions.
 */
static const char*
host_err_str(void)
{
    static char msgstr[200];

    switch (h_errno)
    {
    case 0:
        msgstr[0] = 0;
        break;
#ifdef HOST_NOT_FOUND
    case HOST_NOT_FOUND:
        (void) strcpy(msgstr, "no such host is known");
        break;
#endif
#ifdef TRY_AGAIN
    case TRY_AGAIN:
        (void) strcpy(msgstr,
            "local server did not receive authoritative response");
        break;
#endif
#ifdef NO_RECOVERY
    case NO_RECOVERY:
        (void) strcpy(msgstr, "nonrecoverable error");
        break;
#endif
#ifdef NO_ADDRESS
    case NO_ADDRESS:
        (void) strcpy(msgstr, "valid name has no IP address");
        break;
#endif
    default:
        (void)sprintf(msgstr, "h_errno = %d", h_errno);
    }

    return msgstr;
}

char*
ipv4Sock_getLocalString(const int sock)
{
    struct sockaddr_in addr;
    socklen_t          sockAddrLen = sizeof(addr);
    (void)getsockname(sock, (struct sockaddr*)&addr, &sockAddrLen);
    return sockAddrIn_format(&addr);
}

char*
ipv4Sock_getPeerString(const int sock)
{
    struct sockaddr_in addr;
    socklen_t          sockAddrLen = sizeof(addr);
    int                status = getpeername(sock, (struct sockaddr*)&addr,
            &sockAddrLen);

    return status ? NULL : sockAddrIn_format(&addr);
}

/**
 * Returns the name of the local host. Checks the registry first. Tries to make
 * the name fully-qualified.
 *
 * @return      Pointer to a static buffer containing the NUL-terminated name
 *              of the local host. The length of the string, excluding the
 *              terminating NUL, will not be greater than _POSIX_HOST_NAME_MAX.
 */
char *
ghostname(void)
{
    static char hostname[_POSIX_HOST_NAME_MAX+1];

    if (hostname[0] == 0) {
        /*
         * The registry is first checked for the hostname because the ldm
         * programs require fully-qualified hostnames in an internet
         * environment AND users often don't have control over the system admin
         * conventions,
         */
        char* cp;
        int   status = reg_getString(REG_HOSTNAME, &cp);
        if (status == 0) {
            (void)snprintf(hostname, sizeof(hostname), "%s", cp);
            hostname[sizeof(hostname)-1] = 0;
            free(cp);
        }
        else {
            log_info("Couldn't get name of local host from registry");
            if (gethostname(hostname, sizeof(hostname)) < 0) {
                (void)snprintf(hostname, sizeof(hostname), "localhost");
                hostname[sizeof(hostname)-1] = 0;
                log_warning("Couldn't get name of local host from "
                        "gethostname(). Using default: \"%s\"", hostname);
            }
        }

		if (strchr(hostname, '.') == NULL) {
			struct hostent *hp = gethostbyname(hostname);
			if (hp != NULL && hp->h_addrtype == AF_INET) {
				// Hopefully, `hp->h_name` is fully qualified
				(void)snprintf(hostname, sizeof(hostname), "%s", hp->h_name);
				hostname[sizeof(hostname)-1] = 0;
			}
		}

		if (strchr(hostname, '.') == NULL)
			log_warning("Local host name, \"%s\", isn't fully qualified",
					hostname);
    }
    return hostname;
}

void sockAddrIn_getHostId(
        const struct sockaddr_in* const restrict sockAddr,
        char* const restrict                     id,
        const size_t                             size)
{
    if (getnameinfo((struct sockaddr*)sockAddr, sizeof(*sockAddr), id, size,
            NULL, 0, 0)) {
        log_add_syserr("Couldn't resolve IP address %s to a hostname",
                inet_ntop(AF_INET, &sockAddr->sin_addr, id, size));
        log_flush_info();
    }
}

char* sockAddrIn_format(const struct sockaddr_in* const sockAddr)
{
    // <hostname> + ":" + <port> + NUL
    const size_t bufSize = _POSIX_HOST_NAME_MAX+1+5+1;
    char* buf = log_malloc(bufSize, "IPV4 socket address");
    if (buf) {
        sockAddrIn_getHostId(sockAddr, buf, bufSize);
        const size_t used = strlen(buf);
        snprintf(buf+used, bufSize-used, ":%u", ntohs(sockAddr->sin_port));
    }
    return buf;
}

/**
 * Returns a string identifying the Internet host referred to by an IPv4 socket
 * address. If the hostname lookup fails, then the "dotted decimal" form of the
 * address is returned. Non-reentrant.
 *
 * @param[in] paddr  Pointer to the IPv4 socket address structure.
 * @return           Pointer to static buffer containing the identifying string.
 */
const char*
hostbyaddr(
    const struct sockaddr_in* const     paddr)
{
    in_addr_t   inAddr = paddr->sin_addr.s_addr;
    const char* identifier;

    if (ntohl(inAddr) == 0) {
        identifier = "localhost";
    }
    else {
        timestampt start;
        timestampt stop;

        static char hostname[_POSIX_HOST_NAME_MAX+1];
        (void)set_timestamp(&start);
        int status = getnameinfo((struct sockaddr*)paddr, sizeof(*paddr),
                hostname, sizeof(hostname), NULL, 0, 0);
        (void)set_timestamp(&stop);

        const double elapsed = d_diff_timestamp(&stop, &start);
        identifier = inet_ntoa(paddr->sin_addr);

        if (status) {
            const char* reason =
                (status == EAI_NONAME)
                    ? "address doesn't resolve to a name"
                    : (status == EAI_AGAIN)
                        ? "couldn't resolve name at this time"
                        : (status == EAI_FAIL)
                          ? "Unrecoverable error"
                          : (status == EAI_FAMILY)
                            ? "invalid address family"
                            : (status == EAI_MEMORY)
                              ? "out-of-memory"
                              : (status == EAI_OVERFLOW)
                                ? "hostname buffer is too small"
                                : (status == EAI_SYSTEM)
                                  ? strerror(errno)
                                  : "unanticipated error";
            LOG_LOG(elapsed >= RESOLVER_TIME_THRESHOLD ?
                        LOG_LEVEL_WARNING : LOG_LEVEL_INFO,
                    "Couldn't resolve \"%s\" to a hostname in %g seconds: %s",
                    identifier, elapsed, reason);
        }
        else {
            LOG_LOG(elapsed >= RESOLVER_TIME_THRESHOLD ?
                        LOG_LEVEL_WARNING : LOG_LEVEL_INFO,
                    "Resolving %s to %s took %g seconds", identifier, hostname,
                    elapsed);
            identifier = hostname;
        }
    }

    return identifier;
} // `hostbyaddr()`



/*
 * Indicates if a host identifier has a given IP address.
 * Potentially lengthy operation.
 * Arguments:
 *      id              Name of the host or dotted-quad IP address.
 *      targetAddr      Target IP address.
 *      hasAddress      Pointer to result.  Set on and only on success. Set to 0
 *                      if and only if the given host has the given IP address.
 * Returns:
 *      NULL    Success.  See *hasAddress for the result.
 *      else    An error occurred.  *hasAddress is not set.  Error codes:
 *                      1       gethostbyname() failure
 *                      2       "id" isn't an Internet host identifier
 */
ErrorObj*
hostHasIpAddress(
    const char* const   id,
    const in_addr_t     targetAddr,
    int* const          hasAddress)
{
    ErrorObj*           error = NULL;   /* success */
    const in_addr_t     ipAddr = inet_addr(id);
    
    if (ipAddr != (in_addr_t)-1) {
        *hasAddress = targetAddr == ipAddr;
    }
    else {
        /*
         * The identifier is not a dotted-quad IP address.
         */
        timestampt              start;
        timestampt              stop;
        double                  elapsed;
        const struct hostent*   hp;

        (void)set_timestamp(&start);
        hp = gethostbyname(id);
        (void)set_timestamp(&stop);

        elapsed = d_diff_timestamp(&stop, &start);

        if (hp == NULL) {
            error = ERR_NEW2(1, 
                ERR_NEW(h_errno, NULL,
                    h_errno == HOST_NOT_FOUND   ? "host not found" :
                    h_errno == NO_DATA          ? "no data on host" :
                    h_errno == NO_RECOVERY      ? "unrecoverable server error" :
                    h_errno == TRY_AGAIN        ? "hostname lookup timeout" :
                                                  "unknown error"),
            "Couldn't resolve \"%s\" to an Internet address in %g seconds",
                id, elapsed);
        }
        else if (hp->h_addrtype != AF_INET) {
            error = ERR_NEW1(2, NULL,
                "\"%s\" isn't an Internet host identifier", id);
        } else {
            const struct in_addr* const*        in_addr_pp;

            for (in_addr_pp = (const struct in_addr* const*)hp->h_addr_list;
                     *in_addr_pp != NULL;
                     in_addr_pp++) {
                if ((*in_addr_pp)->s_addr == targetAddr)
                    break;
            }

            *hasAddress = *in_addr_pp != NULL;

            if (elapsed >= RESOLVER_TIME_THRESHOLD ||
                    log_is_enabled_info) {
                err_log_and_free(
                    ERR_NEW2(0, NULL,
                        "Resolving %s to an IP address took %g seconds",
                        id, elapsed),
                    elapsed >= RESOLVER_TIME_THRESHOLD
                        ? ERR_WARNING : ERR_INFO);
            }
        }
    }

    return error;
}


char *
s_sockaddr_in(
        struct sockaddr_in *paddr
)
{
        static char buf[64];
        (void) sprintf(buf,
                "sin_port %5d, sin_addr %s",
                paddr->sin_port,
                inet_ntoa(paddr->sin_addr));
        return buf;
}


/*
 * Puts the address of the current host into *paddr
 * Returns 0 on success, -1 failure
 */
int
gethostaddr_in(
        struct sockaddr_in *paddr
)
{
        char hostname[MAXHOSTNAMELEN];

        if(gethostname(hostname,MAXHOSTNAMELEN) == -1)
                return -1;

        return addrbyhost(hostname, paddr);
}


/*
 * Return the well known port for (servicename, proto)
 * or -1 on failure.
 */
int
getservport(
        const char *servicename,
        const char *proto
)
{
        struct servent *se;
        se = getservbyname(servicename, proto);
        if(se == NULL)
                return -1;
        /* else */
        return se->s_port;
}


/*
 * Attempt to connect to a Internet domain udp socket.
 * Create & connect.
 * Returns (socket) descriptor or -1 on error.
 */
int
udpopen(
        const char *hostname, 
        const char *servicename
)
{
        int sock;
        int port;
        struct sockaddr_in addr;        /* AF_INET address */

        sock = socket(AF_INET, SOCK_DGRAM, 0);
        if(sock == -1)
                return -1;
        /* else */

        if(addrbyhost(hostname, &addr) == -1)
        {
                (void) close(sock);
                return -1;
        }
        /* else */

        if((port = getservport(servicename, "udp")) == -1)
        {
                (void) close(sock);
                return -1;
        }
        /* else */
        addr.sin_port = (unsigned short) port;

        if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == -1)
        {
                (void) close(sock);
                return -1;
        }
        /* else */

        return sock;
}


/*
 * Macro for rounding-up the positive value x to the nearest multiple of n:
 */
#undef  ROUNDUP
#define ROUNDUP(x,n)    ((x % n) ? (x + (n - (x % n))) : x)


/*
 * Return a new (allocated) host entry.
 */
static
struct hostent*
hostent_new(
    const char          *name
)
{
    struct hostent      *new = NULL;
    struct hostent      *entry;

    /*
     * Retrieve the host's entry.
     */
    entry = gethostbyname(name);
    if (NULL == entry)
        log_error_q("Couldn't get information on host %s: %s", name, host_err_str());
    else
    {
        int             num_aliases;
        int             num_addr;
        char            **from_alias;
        char            *cp;
        size_t          nbytes;
        size_t          h_name_off;
        size_t          h_aliases_off;
        size_t          addrs_off;
        size_t          h_addr_list_off;
        struct in_addr  **from_addr;
        struct in_addr  *addrp;

        /*
         * Compute the size requirements and offsets for the new host entry.
         */

        /* Basic size of host entry structure: */
        nbytes = sizeof(struct hostent);

        /* Offset and size of official name string: */
        h_name_off = nbytes;
        nbytes += strlen(entry->h_name) + 1;

        /* Offset and length of aliases: */
        nbytes = (size_t)ROUNDUP(nbytes, sizeof(char*));
        h_aliases_off = nbytes;
        for (from_alias = entry->h_aliases; NULL != *from_alias; from_alias++)
             nbytes += strlen(*from_alias) + 1;
        num_aliases = (int)(from_alias - entry->h_aliases);
        nbytes += sizeof(char*) * (num_aliases + 1);

        /* Offset and length of addresses: */
        nbytes = (size_t)ROUNDUP(nbytes, sizeof(struct in_addr*));
        h_addr_list_off = nbytes;
        for (from_addr = (struct in_addr**)entry->h_addr_list;
                        NULL != *from_addr; )
            from_addr++;
        num_addr = (int)(from_addr - (struct in_addr**)entry->h_addr_list);
        nbytes += sizeof(struct in_addr*) * (num_addr + 1);
        nbytes = (size_t)ROUNDUP(nbytes, sizeof(struct in_addr));
        addrs_off = nbytes;
        nbytes += sizeof(struct in_addr) * num_addr;

        /*
         * Allocate the new host entry.
         */
        new = (struct hostent *) malloc(nbytes);
        if (NULL == new)
            log_syserr(
            "Couldn't allocate %lu bytes for information on host \"%s\"", 
                   (unsigned long) nbytes, name);
        else
        {
            /* Copy non-pointer members. */
            new->h_addrtype = entry->h_addrtype;
            new->h_length = entry->h_length;

            /* Copy official host name. */
            new->h_name = (char *) new + h_name_off;
            (void) strcpy(new->h_name, entry->h_name);

            /* Copy aliases. */
            new->h_aliases = (char**)((char*)new + h_aliases_off);
            cp = (char *) (new->h_aliases + num_aliases);
            char            **to_alias;
            for (from_alias = entry->h_aliases, to_alias = new->h_aliases;
                 NULL != *from_alias;
                 from_alias++, to_alias++)
            {
                *to_alias = cp;
                (void) strcpy(*to_alias, *from_alias);
                cp += strlen(*to_alias) + 1;
            }
            *to_alias = NULL;

            /* Copy addresses. */
            new->h_addr_list = (char**)((char*)new + h_addr_list_off);
            addrp = (struct in_addr*)((char*)new + addrs_off);
            struct in_addr  **to_addr;
            for (from_addr = (struct in_addr**)entry->h_addr_list,
                    to_addr = (struct in_addr**)new->h_addr_list;
                 NULL != *from_addr;
                 from_addr++, to_addr++)
            {
                *to_addr = addrp++;
                **to_addr = **from_addr;
            }
            *to_addr = NULL;
        }                                       /* new host entry allocated */
    }                                           /* host entry retrieved */

    return new;
}


/*
 * Compare two (possibly fully-qualified) hostnames.  Indicate if they
 * refer to the same host.  If one of them isn't fully-qualified, then
 * assume it's in the same domain as the other.
 *
 * Returns:
 *      0       Different hosts
 *      1       Same host
 */
static int
same_host(
    const char  *name1,
    const char  *name2
)
{
    return (name1 == name2) ||
           (strcmp(name1, name2) == 0) ||
           (strstr(name1, name2) == name1 && name1[strlen(name2)] == '.') ||
           (strstr(name2, name1) == name2 && name2[strlen(name1)] == '.');
}


/*
 * Attempt to determine if "remote" is the same as this host.
 * Could be lots smarter...
 */
int
isMe(
        const char *remote
)
{
        static char *names[] = {
                "localhost",
                "loopback",
                NULL /* necessary terminator */
        };
        char *me;
        char **npp;
        static struct hostent *hp;

        /* Check `local host' aliases. */
        for (npp = names; *npp != NULL; npp++)
                if (same_host(remote, *npp))
                        return 1;

        me = ghostname();
        if (me == NULL)
                return 0;

        /* Check my nominal hostname. */
        if (same_host(me, remote))
                return 1;

        /* Cache host information on myself. */
        if (NULL == hp)
                hp = hostent_new(me);

        /* Check my aliases. */
        if (hp != NULL)
        {
                for(npp = hp->h_aliases; *npp != NULL; npp++)
                        if (same_host(*npp, remote))
                                return 1;
        }

        return 0;
}


/**
 * Sets the socket Internet address of the local host.
 *
 * Retval 0      Success
 * Retval !0     Failure.  errno set. `log_add()` called.
 */
int
local_sockaddr_in(struct sockaddr_in* addr)
{
    int                       error;
    static int                cached = 0;
    static struct sockaddr_in cachedAddr;

    if (cached) {
        (void)memcpy(addr, &cachedAddr, sizeof(cachedAddr));
        error = 0;
    }
    else {
        char name[256];

        (void)memset(&cachedAddr, 0, sizeof(cachedAddr));

        if (gethostname(name, sizeof(name))) {
            log_add_syserr("gethostname()");
            error = errno;
        }
        else {
            error = 0;

            if (addrbyhost(name, &cachedAddr)) {
                log_add("Couldn't get IP address of %s", name);

                if (addrbyhost("localhost", &cachedAddr)) {
                    log_add("Couldn't get IP address of localhost");

                    if (addrbyhost("0.0.0.0", &cachedAddr)) {
                        log_add("Couldn't get IP address of 0.0.0.0");
                        error = errno;
                    }
                }
            }

            if (!error)
                (void)memcpy(addr, &cachedAddr, sizeof(cachedAddr));
        }

        if (!error)
            cached = true;
    } // Not cached

    return error;
}


#ifndef TIRPC
/*
 * Create a socket of type "udp" or "tcp" and bind it
 * to port.
 * Return the socket or -1 on error.
 */
int
sockbind(
        const char *type,
        unsigned short port
)
{
        int sock = -1;
        struct sockaddr_in addr;
        size_t len = sizeof(struct sockaddr_in);

        if(type == NULL)
                return -1;

        if(*type == 't')
                sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        else if(*type == 'u')
                sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

        if(sock == -1) 
                return -1;

        /*
         * Eliminate problem with EADDRINUSE for reserved socket.
         * We get this if an upstream data source hasn't tried to
         * write on the other and we are in FIN_WAIT_2
         */
        if(*type == 't')
        {
                int on = 1;
                (void) setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
                        (char *)&on, sizeof(on));
        }

        (void) memset((char *)&addr, 0, len);
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = (short)htons((short)port);

        if(bind(sock, (struct sockaddr *)&addr, len) < 0)
        {
                (void) close(sock);             
                return -1;
        }

        return sock;
}
#else
/*
 * TLI version of above function.
 * Create a TLI transport endpoint of type "udp" or "tcp"
 * and bind it to port.
 * Return the descriptor or -1 on error.
 * Only tested on SunOS 5.x
 */
#include <tiuser.h>
#include <fcntl.h>

int
sockbind(
        const char *type,
        unsigned short port
)
{
        int sock = -1;
        struct sockaddr_in sin_req;
        struct t_bind *req, *ret;
        extern void terror();

        if(type == NULL)
                return -1;

        if(*type == 't')
                sock = t_open("/dev/tcp", O_RDWR, NULL);
        else if(*type == 'u')
                sock = t_open("/dev/udp", O_RDWR, NULL);

        if((sock == -1) || ( t_getstate(sock) != T_UNBND) )
        {
                terror("sockbind: t_open");
                goto err0;
        }

        req = (struct t_bind *)t_alloc(sock, T_BIND, T_ADDR);
        if(req == NULL)
        {
                terror("sockbind: t_alloc req");
                goto err1;
        }
        ret = (struct t_bind *)t_alloc(sock, T_BIND, T_ADDR);
        if(ret == NULL)
        {
                terror("sockbind: t_alloc ret");
                goto err2;
        }

        (void) memset((char *)&sin_req, 0, sizeof(sin_req));
        sin_req.sin_family = AF_INET;
        sin_req.sin_addr.s_addr = INADDR_ANY;
        sin_req.sin_port = htons(port);

        (void) memcpy(req->addr.buf, (char *)&sin_req, sizeof(sin_req));
        req->addr.len = sizeof(sin_req);
        req->qlen = 32; /* rpc_soc.c uses 8 */

        if(t_bind(sock, req, ret) < 0)
        {
                terror("sockbind: t_bind");
                goto err3;
        }
        if(memcmp(req->addr.buf, ret->addr.buf, ret->addr.len) != 0)
        {
                log_error_q("memcmp: t_bind changed address");
        }

        (void) t_free((char *)req, T_BIND);
        (void) t_free((char *)ret, T_BIND);
        return sock;

err3 :
        (void) t_free((char *)ret, T_BIND);
err2 :
        (void) t_free((char *)req, T_BIND);
err1 :
        (void) close(sock);             
err0 :
        return -1;
}

#endif /* !TIRPC */

/**
 * Returns the linked list of address information structures corresponding to
 * given information.
 *
 * @param[in]  nodeName  The Internet identifier. May be a name or a formatted
 *                       IP address.
 * @param[in]  servName  The service name or NULL. May be a formatted port
 *                       number.
 * @param[in]  hints     Hints for retrieving address information.
 * @param[out] addrInfo  The first address information structure corresponding
 *                       to the input.
 * @retval     0         Success. `*addrInfo` is set. Caller should call
 *                       `freeaddrinfo(*addrInfo)` when it's no longer needed.
 * @retval     EAGAIN    A necessary resource is temporarily unavailable.
 *                       `log_add()` called.
 * @retval     EINVAL    Invalid Internet identifier or address family.
 *                       `log_add()` called.
 * @retval     ENOENT    The Internet identifier doesn't resolve to an IP
 *                       address. `log_add()` called.
 * @retval     ENOMEM    Out-of-memory. `log_add()` called.
 * @retval     ENOSYS    A non-recoverable error occurred when attempting to
 *                       resolve the name. `log_add()` called.
 */
static int
getAddrInfo(
    const char* const restrict            nodeName,
    const char* const restrict            servName,
    const struct addrinfo* const restrict hints,
    struct addrinfo** const restrict      addrInfo)
{
    int status = getaddrinfo(nodeName, servName, hints, addrInfo);

    if (status == 0)
        return 0;

    log_add("Couldn't get %s address of \"%s\": %s",
            hints->ai_family == AF_INET ? "IPv4" :
                    hints->ai_family == AF_INET6 ? "IPv6" : "IP",
            nodeName, gai_strerror(status));

    /*
     * Possible values: EAI_FAMILY, EAI_AGAIN, EAI_FAIL, EAI_MEMORY,
     * EAI_NONAME, EAI_SYSTEM, EAI_OVERFLOW
     */
    if (status == EAI_NONAME)
        return ENOENT;
    if (status == EAI_AGAIN)
        return EAGAIN;
    if (status == EAI_FAMILY)
        return EINVAL;
    if (status == EAI_MEMORY)
        return ENOMEM;

    return ENOSYS;
}

/**
 * Gets the IP address corresponding to a host identifier.
 *
 * @param[in]  id      The host identifier as either a name or an IP address in
 *                     dotted-quad form (NNN.NNN.NNN.NNN).
 * @param[out] paddr   Pointer to structure to be set to the IP address of the
 *                     given host.  Modified on and only on success.  The port
 *                     is set to zero, the address family to AF_INET, and the
 *                     rest is cleared.
 * @retval     0       Success.
 * @retval     EAGAIN  A necessary resource is temporarily unavailable.
 *                     `log_add()` called.
 * @retval     ENOENT  The Internet identifier doesn't resolve to an IP
 *                     address. `log_add()` called.
 * @retval     ENOMEM  Out-of-memory. `log_add()` called.
 * @retval     ENOSYS  A non-recoverable error occurred when attempting to
 *                     resolve the name. `log_add()` called.
 */
int
addrbyhost(
    const char* const           id,
    struct sockaddr_in* const   paddr)
{
    int       status = 0;    /* success */
    in_addr_t ipAddr;

    ipAddr = inet_addr(id);

    if (ipAddr != (in_addr_t)-1) {
        /*
         * The identifier is a dotted-quad IP address.
         */
        memset(paddr, 0, sizeof(*paddr));
        paddr->sin_family = AF_INET;
        paddr->sin_addr.s_addr = ipAddr;
    }
    else {
        struct addrinfo  hints = {
                .ai_family=AF_INET,
                .ai_protocol=IPPROTO_TCP,
                .ai_socktype=SOCK_STREAM,
                /*
                 * AI_NUMERICSERV  `servName` is a port number.
                 * AI_PASSIVE      The returned socket address is suitable for a
                 *                 `bind()` operation.
                 * AI_ADDRCONFIG   The local system must be configured with an
                 *                 IP address of the specified family.
                 */
                .ai_flags=AI_ADDRCONFIG};

        timestampt       start;
        (void)set_timestamp(&start);

        struct addrinfo* addrInfo;
        status = getAddrInfo(id, NULL, &hints, &addrInfo);

        timestampt       stop;
        (void)set_timestamp(&stop);

        double    elapsed = d_diff_timestamp(&stop, &start);
        ErrorObj* error;

        if (status) {
            log_assert(status != EINVAL); // Shouldn't happen
            error = ERR_NEW2(0, NULL,
                "Couldn't resolve \"%s\" to an Internet address in %g seconds",
                id, elapsed);
            status = -1;               /* failure */
        }
        else {
            if (addrInfo->ai_family != AF_INET) {
                err_log_and_free(
                    ERR_NEW1(0, NULL, "\"%s\" isn't an Internet host identifier",
                        id),
                    ERR_WARNING);
                error = NULL;
                status = -1;               /* failure */
            } else {
                (void)memcpy((char*)paddr, addrInfo->ai_addr, sizeof(*paddr));

                if (elapsed < RESOLVER_TIME_THRESHOLD &&
                        !log_is_enabled_info) {
                    error = NULL;
                }
                else {
                    error = ERR_NEW3(0, NULL, "Resolving %s to %s took %g seconds",
                        id, inet_ntoa(paddr->sin_addr), elapsed);
                }
            }

            freeaddrinfo(addrInfo);
        } // `addrInfo` is allocated

        if (error)
            err_log_and_free(error,
                elapsed >= RESOLVER_TIME_THRESHOLD ? ERR_WARNING : ERR_INFO);
    }

    return status;
}

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
    char* const restrict out)
{
    struct addrinfo  hints;
    struct addrinfo* addrInfo;

    (void)memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; // IPv4

    int status = getAddrInfo(inetId, NULL, &hints, &addrInfo);

    if (status == 0) {
        (void)inet_ntop(AF_INET,
                &((struct sockaddr_in*)addrInfo->ai_addr)->sin_addr.s_addr,
                out, INET_ADDRSTRLEN); // can't fail
        freeaddrinfo(addrInfo);
    } // `addrInfo` allocated

    return status;
}

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
        const char* const restrict spec)
{
    int       status;

    if (NULL == spec) {
        *addr = INADDR_ANY;
        status = 0;
    }
    else {
        in_addr_t a = inet_addr(spec);

        if ((in_addr_t)-1 == a) {
            log_add("Invalid IPv4 address: \"%s\"", spec);
            status = 1;
        }
        else {
            *addr = a;
            status = 0;
        }
    }

    return status;
}

/**
 * Vets a multicast IPv4 address.
 *
 * @param[in] addr   The IPv4 address to be vetted in network byte order.
 * @retval    true   The IPv4 address is a valid multicast address.
 * @retval    false  The IPv4 address is not a valid multicast address.
 */
bool
mcastAddr_isValid(
        const in_addr_t addr)
{
    return (ntohl(addr) & 0xF0000000) == 0xE0000000;
}

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
        const char* const restrict     spec)
{
    in_addr_t addr;
    int       status = addr_init(&addr, spec);

    if (0 == status) {
        (void)memset(inetAddr, 0, sizeof(*inetAddr));
        inetAddr->s_addr = addr;
    }

    return status;
}

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
        const unsigned short               port)
{
    (void)memset(sockAddr, 0, sizeof(*sockAddr));
    sockAddr->sin_family = AF_INET;
    sockAddr->sin_addr.s_addr = addr;
    sockAddr->sin_port = htons(port);
}

/**
 * Initializes a UDP socket from an IPv4 socket address.
 *
 * @param[out] sock      The socket.
 * @param[in]  sockAddr  The IPv4 socket address to which the socket will be
 *                       bound.
 * @retval     0         Success.
 * @retval     2         System failure. `log_add()` called.
 */
int
udpSock_init(
        int* const restrict                      sock,
        const struct sockaddr_in* const restrict sockAddr)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    int status;

    if (-1 == fd) {
        log_syserr("Couldn't create UDP socket");
        status = 2;
    }
    else {
        status = bind(fd, (struct sockaddr*)sockAddr, sizeof(*sockAddr));
        if (status) {
            log_syserr("Couldn't bind UDP socket");
            (void)close(fd);
            status = 2;
        }
        else {
            *sock = fd;
        }
    } // `fd` is open

    return status;
}

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
        const struct in_addr* const restrict ifaceAddr)
{
    struct ip_mreq  mreq;

    mreq.imr_multiaddr = *mcastAddr;
    mreq.imr_interface = *ifaceAddr;

    int status = setsockopt(socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, (void*)&mreq,
            sizeof(mreq));
    if (status) {
        log_add_syserr("setsockopt() failure");
        status = 2;
    }

    return status;
}

/**
 * Initializes a socket for receiving IPv4 multicast.
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
        const struct in_addr* const restrict     ifaceAddr)
{
    int sock;
    int status = udpSock_init(&sock, mcastSockAddr);

    if (status) {
        log_add("Couldn't initialize UDP socket %s:%u",
                inet_ntoa(mcastSockAddr->sin_addr),
                ntohs(mcastSockAddr->sin_port));
    }
    else {
        status = mcastRecvSock_joinGroup(sock, &mcastSockAddr->sin_addr,
        		ifaceAddr);
        if (status) {
            log_add("Couldn't join multicast group %s:%u on interface %s",
                    inet_ntoa(mcastSockAddr->sin_addr),
                    ntohs(mcastSockAddr->sin_port), inet_ntoa(*ifaceAddr));
            (void)close(sock);
        }
        else {
            *socket = sock;
        }
    } // `sock` is open

    return status;
}

/**
 * Returns a new service address.
 *
 * @param[out] svcAddr  Service address. Caller should call
 *                      `sa_free(*serviceAddr)` when it's no longer needed.
 * @param[in]  addr     Identifier of the service. May be a name or formatted IP
 *                      address. Client may free upon return.
 * @param[in]  port     Port number of the service in host byte-order. `0` means
 *                      O/S will select.
 * @retval     0        Success. `*svcAddr` is set.
 * @retval     EINVAL   Invalid Internet address or port number. `log_add()`
 *                      called.
 * @retval     ENOMEM   Out-of-memory. `log_add()` called.
 */
int
sa_new(
    ServiceAddr** const  svcAddr,
    const char* const    addr,
    const int            port)
{
    int status;

    if (NULL == addr) {
        log_add("NULL internet ID");
        status = EINVAL;
    }
    else {
        ServiceAddr* sa = log_malloc(sizeof(ServiceAddr), "service address");

        if (sa == NULL) {
            status = ENOMEM;
        }
        else {
            char* id = strdup(addr);

            if (id == NULL) {
                log_add_syserr("Couldn't duplicate service address \"%s\"",
                        addr);
                free(sa);
                status = ENOMEM;
            }
            else {
                sa->inetId = id;
                sa->port = port;
                *svcAddr = sa;
                status = 0;
            }   // `id` allocated
        }       // "sa" allocated
    }           // valid input arguments

    return status;
}

/**
 * Destroys a service address.
 *
 * @param[in] sa  Service address to be destroyed.
 */
void
sa_destroy(
        ServiceAddr* const sa)
{
    free(sa->inetId);
}

/**
 * Frees a service address.
 *
 * @param[in] sa  Pointer to the service address to be freed or NULL.
 */
void
sa_free(
    ServiceAddr* const sa)
{
    if (sa != NULL) {
        sa_destroy(sa);
        free(sa);
    }
}

/**
 * Copies a service address.
 *
 * @param[out] dest   The destination.
 * @param[in]  src    The source. The caller may free.
 * @retval     true   Success. `*dest` is set.
 * @retval     false  Failure. `log_add()` called.
 */
bool
sa_copy(
    ServiceAddr* const restrict       dest,
    const ServiceAddr* const restrict src)
{
    char* const inetId = strdup(src->inetId);

    if (inetId == NULL) {
        log_syserr("Couldn't copy Internet identifier");
        return false;
    }

    dest->inetId = inetId;
    dest->port = src->port;

    return true;
}

/**
 * Clones a service address.
 *
 * @param[in] sa    Pointer to the service address to be cloned.
 * @retval    NULL  Failure. \c log_add() called.
 * @return          Pointer to a clone of the service address. Caller should
 *                  pass it to `sa_free()` when it's no longer needed.
 */
ServiceAddr*
sa_clone(
    const ServiceAddr* const sa)
{
    ServiceAddr* svcAddr;

    return sa_new(&svcAddr, sa->inetId, sa->port) ? NULL : svcAddr;
}

/**
 * Returns the Internet identifier of a service.
 *
 * @param[in] sa  Pointer to the service address.
 * @return        Pointer to the associated identifier. May be a hostname or
 *                formatted IP address.
 */
const char*
sa_getInetId(
    const ServiceAddr* const sa)
{
    return sa->inetId;
}

/**
 * Returns the port number of a service address.
 *
 * @param[in] sa  Pointer to the service address.
 * @return        The associated port number.
 */
unsigned short
sa_getPort(
    const ServiceAddr* const sa)
{
    return sa->port;
}

/**
 * Returns the formatting string appropriate to a service address.
 *
 * @param[in] sa  The service address.
 * @return        The formatting string appropriate for the service address.
 */
static const char*
sa_getFormat(
    const ServiceAddr* const sa)
{
    return strchr(sa->inetId, ':') ? "[%s]:%u" : "%s:%u";
}

/**
 * Returns the formatted representation of a service address.
 *
 * @param[in]  sa   Pointer to the service address.
 * @param[out] buf  Pointer to the buffer into which to write the formatted
 *                  representation. Will always be NUL-terminated.
 * @param[in]  len  The size of the buffer in bytes.
 * @return          The number of bytes that would be written to the buffer --
 *                  excluding the terminating NUL character. If the returned
 *                  number of bytes is equal to or greater than the size of the
 *                  buffer, then some characters weren't written.
 */
int
sa_snprint(
    const ServiceAddr* const sa,
    char* const           buf,
    const size_t          len)
{
    return snprintf(buf, len, sa_getFormat(sa), sa->inetId, sa->port);
}

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
char*
sa_format(
    const ServiceAddr* const sa)
{
    return ldm_format(128, sa_getFormat(sa), sa->inetId, sa->port);
}

#if defined(HOST_NAME_MAX) && HOST_NAME_MAX > _POSIX_HOST_NAME_MAX
#   define HOSTNAME_MAX  HOST_NAME_MAX
#else
#   define HOSTNAME_MAX  _POSIX_HOST_NAME_MAX
#endif
#define STRING1(x)      #x
#define STRING2(x)      STRING1(x)
#define HOSTNAME_WIDTH  STRING2(HOSTNAME_MAX)
#define IPV6_WIDTH      STRING2(INET6_ADDRSTRLEN)
#define IPV4_WIDTH      STRING2(INET_ADDRSTRLEN)
#define IPV6_FORMAT     "%" IPV6_WIDTH "[0-9A-Fa-f:]"
#define IPV4_FORMAT     "%" IPV4_WIDTH "[0-9.]"
#define HOSTNAME_FORMAT "%" HOSTNAME_WIDTH "[A-Za-z0-9._-]"

/**
 * Parses a formatted Internet service address. An Internet service address has
 * the general form `id:port`, where `id` is the Internet identifier (either a
 * name, a formatted IPv4 address, or a formatted IPv6 address enclosed in
 * square brackets) and `port` is the port number.
 *
 * @param[out] svcAddr      Internet service address. Caller should call
 *                          `sa_free(*sa)` when it's no longer needed.
 * @param[in]  spec         String containing the specification. Caller may
 *                          free.
 * @retval     0            Success. `*sa` is set.
 * @retval     EINVAL       Invalid specification. `log_add()` called.
 * @retval     ENOMEM       Out of memory. `log_add()` called.
 */
int
sa_parse(
    ServiceAddr** const restrict svcAddr,
    const char* restrict         spec)
{
    int status = EINVAL;

    if (NULL == spec) {
        log_add("NULL argument");
    }
    else {
        char                 inetId[HOSTNAME_MAX+1];
        unsigned short       port;
        const char*          formats[] = {
            "[" IPV6_FORMAT "]" ":%hu %n",
                IPV4_FORMAT     ":%hu %n",
                HOSTNAME_FORMAT ":%hu %n"
        };

        for (int i = 0; i < sizeof(formats)/sizeof(formats[0]); i++) {
            int nbytes;
            int n = sscanf(spec, formats[i], inetId, &port, &nbytes);

            if (2 == n && 0 == spec[nbytes]) {
                status = sa_new(svcAddr, inetId, port);
                break;
            }
        }
    }

    return status;
}

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
        const int                    defPort)
{
    int status;

    if (strchr(spec, ':')) {
        status = sa_parse(svcAddr, spec);
    }
    else {
        unsigned short port;
        int            nbytes;
        char           buf[HOSTNAME_MAX+1];

        if (sscanf(spec, "%5hu %n", &port, &nbytes) == 1 && spec[nbytes] == 0) {
            status = sa_new(svcAddr, defId, port);
        }
        else if ((sscanf(spec, HOSTNAME_FORMAT " %n", buf, &nbytes) == 1 ||
                  sscanf(spec, IPV6_FORMAT     " %n", buf, &nbytes) == 1 ||
                  sscanf(spec, IPV4_FORMAT     " %n", buf, &nbytes) == 1) &&
                 0 == spec[nbytes]) {
            status = sa_new(svcAddr, buf, defPort);
        }
        else {
            log_add("Invalid service address specification: \"%s\"", spec);
            status = EINVAL;
        }
    }

    return status;
}

/**
 * Returns the Internet socket address corresponding to a TCP service address.
 * Supports both IPv4 and IPv6.
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
 * @retval     0             Success. `inetSockAddr` and `sockLen` are set.
 * @retval     EAGAIN        A necessary resource is temporarily unavailable.
 *                           `log_add()` called.
 * @retval     EINVAL        Invalid argument. `log_add()` called.
 * @retval     ENOENT        The service address doesn't resolve into an IP
 *                           address.
 * @retval     ENOMEM        Out-of-memory. `log_add()` called.
 * @retval     ENOSYS        A non-recoverable error occurred when attempting to
 *                           resolve the name. `log_add()` called.
 */
int
sa_getInetSockAddr(
    const ServiceAddr* const servAddr,
    const int                family,
    const bool               serverSide,
    struct sockaddr* const   inetSockAddr,
    socklen_t* const         sockLen)
{
    int status;

    if (servAddr == NULL || (family != AF_UNSPEC && family != AF_INET &&
            family != AF_INET6) || inetSockAddr == NULL || sockLen == 0) {
        log_add("Invalid argument");
        status = EINVAL;
    }
    else {
        char           servName[6];
        unsigned short port = sa_getPort(servAddr);

        if (port == 0 || snprintf(servName, sizeof(servName), "%u", port) >=
                sizeof(servName)) {
            log_add("Invalid port number: %u", port);
            status = EINVAL;
        }
        else {
            struct addrinfo   hints;
            struct addrinfo*  addrInfo;
            const char* const inetId = sa_getInetId(servAddr);

            (void)memset(&hints, 0, sizeof(hints));
            hints.ai_family = family;
            hints.ai_protocol = IPPROTO_TCP;
            hints.ai_socktype = SOCK_STREAM;
            /*
             * AI_NUMERICSERV  `servName` is a port number.
             * AI_PASSIVE      The returned socket address is suitable for a
             *                 `bind()` operation.
             * AI_ADDRCONFIG   The local system must be configured with an
             *                 IP address of the specified family.
             */
            hints.ai_flags = serverSide
                ? AI_NUMERICSERV | AI_PASSIVE
                : AI_NUMERICSERV | AI_ADDRCONFIG;

            status = getAddrInfo(inetId, servName, &hints, &addrInfo);

            if (status == 0) {
                *sockLen = addrInfo->ai_addrlen;
                (void)memcpy(inetSockAddr, addrInfo->ai_addr, *sockLen);
                freeaddrinfo(addrInfo);
            } /* "addrInfo" allocated */
        } /* valid port number */
    } // Valid arguments

    return status;
}

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
    const ServiceAddr* const sa2)
{
    int cmp = strcmp(sa1->inetId, sa2->inetId);

    if (0 == cmp)
        cmp = (sa1->port < sa2->port)
                ? -1
                : (sa1->port == sa2->port)
                      ? 0
                      : 1;

    return cmp;
}

static int
sockaddr_in_format(
        char* const                     buf,
        const size_t                    bufSize,
        const struct sockaddr_in* const sockAddr)
{
    char addrStr[INET_ADDRSTRLEN];

    // Can't fail
    (void)inet_ntop(AF_INET, &sockAddr->sin_addr, addrStr, sizeof(addrStr));

    return snprintf(buf, bufSize, "%s:%u", addrStr, ntohs(sockAddr->sin_port));
}

static int
sockaddr_in6_format(
        char* const                      buf,
        const size_t                     bufSize,
        const struct sockaddr_in6* const sockAddr)
{
    char addrStr[INET6_ADDRSTRLEN];

    // Can't fail
    (void)inet_ntop(AF_INET6, &sockAddr->sin6_addr, addrStr, sizeof(addrStr));

    return snprintf(buf, bufSize, "[%s]:%u", addrStr,
            ntohs(sockAddr->sin6_port));
}

int
sockaddr_format(
        const struct sockaddr* const sockAddr,
        char* const                  buf,
        const size_t                 bufSize)
{
    int nbytes;

    if (sockAddr == NULL || (buf == NULL && bufSize > 0)) {
        log_add("Invalid argument");
        nbytes = -1;
    }
    else {
        unsigned family = sockAddr->sa_family;

        if (family != AF_INET && family != AF_INET6) {
            log_add("Unknown address family: %u", family);
            nbytes = -1;
        }
        else {
            nbytes = (family == AF_INET)
                    ? sockaddr_in_format(buf, bufSize,
                            (struct sockaddr_in*)sockAddr)
                    : sockaddr_in6_format(buf, bufSize,
                            (struct sockaddr_in6*)sockAddr);

            if (bufSize && buf)
                buf[bufSize-1] = 0;
        }
    } // Valid arguments

    return nbytes;
}
