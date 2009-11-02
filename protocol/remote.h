/*
 *   See file ../COPYRIGHT for copying and redistribution conditions.
 */
#ifndef REMOTE_H
#define REMOTE_H

#include <netinet/in.h>
#include <rpc/svc.h>
#include <ldm.h>

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

#ifdef __cplusplus
}
#endif

#endif
