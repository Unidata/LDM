/*
 *   See file ../COPYRIGHT for copying and redistribution conditions.
 */

#ifndef LDM_CLNT_H
#define LDM_CLNT_H

#include <signal.h>
#include <rpc/rpc.h>

#include "error.h"

typedef enum {
    LDM_CLNT_UNKNOWN_HOST = 1,
    LDM_CLNT_TIMED_OUT,
    LDM_CLNT_BAD_VERSION,
    LDM_CLNT_NO_CONNECT,
    LDM_CLNT_SYSTEM_ERROR,
    LDM_CLNT_DONE
} ldm_clnt_error_t;

#ifdef __cplusplus
extern "C" {
#endif

ErrorObj*
ldm_clnt_addr(
    const char* const		name,
    struct sockaddr_in*		addr);

ErrorObj*
ldm_clnttcp_create_vers(
    const char* const            upName,
    const unsigned               port,
    unsigned const               version,
    CLIENT** const               client,
    int* const                   socket,
    struct sockaddr_in*          upAddr);

#ifdef __cplusplus
}
#endif

#endif
