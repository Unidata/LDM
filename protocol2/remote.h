/*
 *   See file ../COPYRIGHT for copying and redistribution conditions.
 */
#ifndef REMOTE_H
#define REMOTE_H

#include <netinet/in.h>
#include <rpc/svc.h>
#include <ldm.h>
#include "peer_info.h"

#ifdef __cplusplus
exern "C" {
#endif

extern void free_remote_clss(void);
extern void ensureRemoteName(
    const struct sockaddr_in* const     paddr);
extern void setremote(
    const struct sockaddr_in* const     paddr,
    const int                           sock);
extern void svc_setremote(struct svc_req *rqstp);
extern void str_setremote(const char *id);
extern const char* remote_name(void);
extern int update_remote_clss(prod_class_t *want);

/*
 * Sets the product-class of the remote LDM.
 *
 * Arguments:
 *      prodClass       Pointer to the product-class of the remote site.  May
 *                      be NULL.  May be freed upon return.
 * Returns:
 *      0               Success.
 *      ENOMEM          Out of memory.  "log_add()" called.
 */
int
set_remote_class(
    const prod_class_t* const   prodClass);

/*
 * Returns the informational structure for the remote LDM.
 *
 * Returns:
 *      The informational structure for the remote LDM.
 */
peer_info* get_remote(void);

#ifdef __cplusplus
}
#endif

#endif
