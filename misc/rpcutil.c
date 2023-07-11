/*
 * This module contains RPC helper functions.
 */

#include "config.h"

#include <rpc/rpc.h>
#include <arpa/inet.h>

#include <rpc/pmap_clnt.h>
#include <rpc/pmap_prot.h>
#include <string.h>
#include <unistd.h>

#include "rpcutil.h"
#include "inetutil.h"
#include "log.h"


/*
 * Print reply error info
 *
 * This is derived from RPC 4.0 source.  It's here because at least one
 * implementation of the RPC function clnt_sperror() results in a segmentation
 * violation (SunOS 5.8).
 */
char*
clnt_errmsg(CLIENT* clnt)
{
    struct       rpc_err e;
    static char  buf[512];
    char        *str = buf;

    clnt_geterr(clnt, &e);

    (void) strncpy(str, clnt_sperrno(e.re_status), sizeof(buf));
    str[sizeof(buf)-1] = 0;
    str += strlen(str);

    switch (e.re_status) {
        case RPC_SUCCESS:
            (void)sprintf(str, "; success");
            str += strlen(str);
            break;
        case RPC_CANTENCODEARGS:
            (void)sprintf(str, "; can't encode arguments");
            str += strlen(str);
            break;
        case RPC_CANTDECODERES:
            (void)sprintf(str, "; can't decode response");
            str += strlen(str);
            break;
        case RPC_TIMEDOUT:     
            (void)sprintf(str, "; timeout");
            str += strlen(str);
            break;
        case RPC_PROGUNAVAIL:
            (void)sprintf(str, "; program unavailable");
            str += strlen(str);
            break;
        case RPC_PROCUNAVAIL:
            (void)sprintf(str, "; procedure unavailable");
            str += strlen(str);
            break;
        case RPC_CANTDECODEARGS:
            (void)sprintf(str, "; can't decode arguments");
            str += strlen(str);
            break;
        case RPC_SYSTEMERROR:
            (void)sprintf(str, "; %s", strerror(e.re_errno));
            str += strlen(str);
            break;
        case RPC_UNKNOWNHOST:
            (void)sprintf(str, "; unknown host");
            str += strlen(str);
            break;
        case RPC_UNKNOWNPROTO:
            (void)sprintf(str, "; unknown protocol");
            str += strlen(str);
            break;
        case RPC_PMAPFAILURE:
        case RPC_PROGNOTREGISTERED:
            (void)sprintf(str, "; program not registered");
            str += strlen(str);
            break;
        case RPC_FAILED:
            (void)sprintf(str, "; RPC failed");
            str += strlen(str);
            break;
                break;

        case RPC_CANTSEND:
        case RPC_CANTRECV:
            (void) sprintf(str, "; errno = %s", strerror(e.re_errno)); 
            str += strlen(str);
            break;

        case RPC_VERSMISMATCH:
            (void) sprintf(str, "; low version = %lu, high version = %lu", 
                e.re_vers.low, e.re_vers.high);
            str += strlen(str);
            break;

        case RPC_AUTHERROR:
            (void) sprintf(str,"; why = ");
            str += strlen(str);
            (void) sprintf(str, "(authentication error %d)",
                (int) e.re_why);
            str += strlen(str);
            break;

        case RPC_PROGVERSMISMATCH:
            (void) sprintf(str, "; low version = %lu, high version = %lu", 
                e.re_vers.low, e.re_vers.high);
            str += strlen(str);
            break;

        default:        /* unknown */
            (void) sprintf(str, "; s1 = %lu, s2 = %lu", 
                    e.re_lb.s1, e.re_lb.s2);
            str += strlen(str);
            break;
    }

    //(void) sprintf(str, "\n");

    return buf;
}


/*
 * Indicate whether or not the portmapper daemon is running on the local host.
 *
 * Returns:
 *  -1          Error.  errno set.
 *   0          Portmapper daemon is NOT running on the local host.
 *   1          Portmapper daemon is running on the local host.
 */
int
local_portmapper_running()
{
    static int status;
    static int cached = 0;

    if (!cached) {
        struct sockaddr_in addr;

        if (local_sockaddr_in(&addr)) {
            log_warning("Couldn't get IP address of local host");
            status = -1;
        }
        else {
            CLIENT*            client;
            int                socket = RPC_ANYSOCK;

            addr.sin_port = (short)htons((short)PMAPPORT);

            if ((client = clnttcp_create(&addr, PMAPPROG, PMAPVERS, &socket, 50,
                    500)) == NULL) {
                status = 0;
                log_info("Portmapper daemon is not available on local host");
            }
            else {
                status = 1;

                auth_destroy(client->cl_auth);
                clnt_destroy(client);
                (void)close(socket);
            }
        }

        cached = 1;
    }

    return status;
}

/**
 * Returns an identifier of the remote client.
 *
 * @param[in] rqstp  Client-request object.
 */
const char*
rpc_getClientId(
    struct svc_req* const rqstp)
{
    return hostbyaddr(svc_getcaller(rqstp->rq_xprt));
}
