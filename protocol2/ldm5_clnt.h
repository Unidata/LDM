/*
 *   See file ../COPYRIGHT for copying and redistribution conditions.
 */

#ifndef _LDMCLNT_H_
#define _LDMCLNT_H_

#include <rpc/rpc.h> 
#include "h_clnt.h"

#ifdef __cplusplus
extern "C" {
#endif

extern enum clnt_stat
nullproc5(h_clnt *hcp, unsigned int timeo) ;

extern enum clnt_stat
hereis5( h_clnt *hcp, product *prod, unsigned int timeo, 
	ldm_replyt *replyp);

extern enum clnt_stat
xhereis5(h_clnt *hcp, void *xprod, size_t size, unsigned int timeo,
	ldm_replyt *replyp);

extern enum clnt_stat
feedme5(h_clnt *hcp, const prod_class *clssp, unsigned int timeo,
	ldm_replyt *replyp);

extern enum clnt_stat
hiya5(h_clnt *hcp, const prod_class *clssp, unsigned int timeo,
	ldm_replyt *replyp);

enum clnt_stat
notification5(h_clnt *hcp, const prod_info *infop, unsigned int timeo,
	ldm_replyt *replyp);

extern enum clnt_stat
notifyme5(h_clnt *hcp, const prod_class *clssp, unsigned int timeo,
	ldm_replyt *replyp);

enum clnt_stat
comingsoon5(h_clnt *hcp, const prod_info *infop, unsigned int pktsz, unsigned int timeo,
	ldm_replyt *replyp);

extern enum clnt_stat
blkdata5(h_clnt *hcp, const datapkt *pktp,
	unsigned int timeo, ldm_replyt *replyp);

/***/

/*
 * Sortof like svc_run(3RPC). Except...
 * Runs on one socket, xp_sock,
 * until the other end gets closed, timeo expires without
 * action or *donep is set.
 * Returns zero on success, errno otherwise:
 * timeo expiration mapped to ETIMEDOUT,
 * other guy going away mapped to ECONNRESET.
 */ 
int
one_svc_run(const int xp_sock, const int inactive_timeo);

/*
 * Send a FEEDME or NOTIFYME request (proc)
 * to remote for clssp using rpctimeo.
 * If it fails, sleep interval and try again.
 * Keep trying until inactive_timeo.
 * When you get connected, turn around the connection,
 * and run dispatch until disconnect,
 * no activity for inactive_timeo, 
 * or *donep is set.
 * Whew!
 */
int
forn5(
    const unsigned long         proc,
    const char* const           remote,
    prod_class_t** const        request,
    const unsigned              rpctimeo, 
    const int                   inactive_timeo,
    void (* const               dispatch)(struct svc_req*, SVCXPRT*));

#ifdef __cplusplus
}
#endif

#endif /* !_LDMCLNT_H_ */
