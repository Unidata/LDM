/* @(#)clnt_tcp.c	2.2 88/08/01 4.0 RPCSRC */
/*
 * Sun RPC is a product of Sun Microsystems, Inc. and is provided for
 * unrestricted use provided that this legend is included on all tape
 * media and as a part of the software program in whole or part.  Users
 * may copy or modify Sun RPC without charge, but are not authorized
 * to license or distribute it to anyone else except as part of a product or
 * program developed by the user.
 * 
 * SUN RPC IS PROVIDED AS IS WITH NO WARRANTIES OF ANY KIND INCLUDING THE
 * WARRANTIES OF DESIGN, MERCHANTIBILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE, OR ARISING FROM A COURSE OF DEALING, USAGE OR TRADE PRACTICE.
 * 
 * Sun RPC is provided with no support and without any obligation on the
 * part of Sun Microsystems, Inc. to assist in its use, correction,
 * modification or enhancement.
 * 
 * SUN MICROSYSTEMS, INC. SHALL HAVE NO LIABILITY WITH RESPECT TO THE
 * INFRINGEMENT OF COPYRIGHTS, TRADE SECRETS OR ANY PATENTS BY SUN RPC
 * OR ANY PART THEREOF.
 * 
 * In no event will Sun Microsystems, Inc. be liable for any lost revenue
 * or profits or other special, indirect and consequential damages, even if
 * Sun has been advised of the possibility of such damages.
 * 
 * Sun Microsystems, Inc.
 * 2550 Garcia Avenue
 * Mountain View, California  94043
 */
#if !defined(lint) && defined(SCCSIDS)
static char sccsid[] = "@(#)clnt_tcp.c 1.37 87/10/05 Copyr 1984 Sun Micro";
#endif
 
/*
 * clnt_tcp.c, Implements a TCP/IP based, client side RPC.
 *
 * Copyright (C) 1984, Sun Microsystems, Inc.
 *
 * TCP based RPC supports 'batched calls'.
 * A sequence of calls may be batched-up in a send buffer.  The rpc call
 * return immediately to the client even though the call was not necessarily
 * sent.  The batching occurs if the results' xdr routine is NULL (0) AND
 * the rpc timeout value is zero (see clnt.h, rpc).
 *
 * Clients should NOT casually batch calls that in fact return results; that is,
 * the server side should be aware that a call is batched and not produce any
 * return message.  Batched calls that produce many result messages can
 * deadlock (netlock) the client and the server....
 *
 * Now go hang yourself.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <netdb.h>
#include <errno.h>
#include <unistd.h>

#include "rpc.h"
#include "pmap_clnt.h"

#define MCALL_MSG_SIZE 24

struct ct_data {
	int		ct_sock;
	bool_t		ct_closeit;
	struct timeval	ct_wait;
	bool_t          ct_waitset;       /* wait set by clnt_control? */
	struct sockaddr_in ct_addr; 
	struct rpc_err	ct_error;
	char		ct_mcall[MCALL_MSG_SIZE];	/* marshalled callmsg */
	unsigned	ct_mpos;			/* pos after marshal */
	XDR		ct_xdrs;
};


static enum clnt_stat
clnttcp_call(
	register CLIENT *h,
	unsigned long proc,
	xdrproc_t xdr_args,
	char* args_ptr,
	xdrproc_t xdr_results,
	char* results_ptr,
	struct timeval timeout)
{
	register struct ct_data *ct = (struct ct_data *) h->cl_private;
	register XDR *xdrs = &(ct->ct_xdrs);
	struct rpc_msg reply_msg;
	unsigned long x_id;
	uint32_t *msg_x_id = (uint32_t *)(ct->ct_mcall);	/* yuk */
	register bool_t shipnow;
	int refreshes = 2;

	if (!ct->ct_waitset) {
		ct->ct_wait = timeout;
	}

	/*
	 * The RPC message is flushed to the TCP connection if a reply is
	 * expected (including `xdr_void()`) or a non-zero timeout is given.
	 */
	shipnow =
	    (xdr_results == (xdrproc_t)0 && timeout.tv_sec == 0
	    && timeout.tv_usec == 0) ? FALSE : TRUE;

call_again:
	xdrs->x_op = XDR_ENCODE;
	ct->ct_error.re_status = RPC_SUCCESS;
	x_id = ntohl(--(*msg_x_id));
	if ((! XDR_PUTBYTES(xdrs, ct->ct_mcall, ct->ct_mpos)) ||
	    (! xdr_u_long(xdrs, &proc)) ||
	    (! AUTH_MARSHALL(h->cl_auth, xdrs)) ||
	    (! (*xdr_args)(xdrs, args_ptr))) {
		if (ct->ct_error.re_status == RPC_SUCCESS)
			ct->ct_error.re_status = RPC_CANTENCODEARGS;
		(void)xdrrec_endofrecord(xdrs, TRUE);
		return (ct->ct_error.re_status);
	}
	if (! xdrrec_endofrecord(xdrs, shipnow))
		return (ct->ct_error.re_status = RPC_CANTSEND);
	if (! shipnow)
		return (RPC_SUCCESS);
	/*
	 * Hack to provide rpc-based message passing
	 */
	if (timeout.tv_sec == 0 && timeout.tv_usec == 0) {
		return(ct->ct_error.re_status = RPC_TIMEDOUT);
	}


	/*
	 * Keep receiving until we get a valid transaction id
	 */
	xdrs->x_op = XDR_DECODE;
	for (;;) {
		reply_msg.acpted_rply.ar_verf = _null_auth;
		reply_msg.acpted_rply.ar_results.where = NULL;
		reply_msg.acpted_rply.ar_results.proc = (xdrproc_t)xdr_void;
		if (! xdrrec_skiprecord(xdrs))
			return (ct->ct_error.re_status);
		/* now decode and validate the response header */
		if (! xdr_replymsg(xdrs, &reply_msg)) {
			if (ct->ct_error.re_status == RPC_SUCCESS)
				continue;
			return (ct->ct_error.re_status);
		}
		if (reply_msg.rm_xid == x_id)
			break;
	}

	/*
	 * process header
	 */
	_seterr_reply(&reply_msg, &(ct->ct_error));
	if (ct->ct_error.re_status == RPC_SUCCESS) {
		if (! AUTH_VALIDATE(h->cl_auth, reply_msg.acpted_rply.ar_verf)) {
			ct->ct_error.re_status = RPC_AUTHERROR;
			ct->ct_error.re_why = AUTH_INVALIDRESP;
		} else if (! (*xdr_results)(xdrs, results_ptr)) {
			if (ct->ct_error.re_status == RPC_SUCCESS)
				ct->ct_error.re_status = RPC_CANTDECODERES;
		}
		/* free verifier ... */
		if (reply_msg.acpted_rply.ar_verf.oa_base != NULL) {
			xdrs->x_op = XDR_FREE;
			(void)xdr_opaque_auth(xdrs, &(reply_msg.acpted_rply.ar_verf));
		}
	}  /* end successful completion */
	else {
		/* maybe our credentials need to be refreshed ... */
		if (refreshes-- && AUTH_REFRESH(h->cl_auth))
			goto call_again;
	}  /* end of unsuccessful completion */
	return (ct->ct_error.re_status);
}

static void
clnttcp_geterr(
	CLIENT *h,
	struct rpc_err *errp)
{
	register struct ct_data *ct =
	    (struct ct_data *) h->cl_private;

	*errp = ct->ct_error;
}

static bool_t
clnttcp_freeres(
	CLIENT *cl,
	xdrproc_t xdr_res,
	char* res_ptr)
{
	register struct ct_data *ct = (struct ct_data *)cl->cl_private;
	register XDR *xdrs = &(ct->ct_xdrs);

	xdrs->x_op = XDR_FREE;
	return ((*xdr_res)(xdrs, res_ptr));
}

/*ARGSUSED*/
static void
clnttcp_abort(CLIENT *h)
{
}

static bool_t
clnttcp_control(
	CLIENT *cl,
	unsigned request,
	void *info)
{
	register struct ct_data *ct = (struct ct_data *)cl->cl_private;

	switch (request) {
	case CLSET_TIMEOUT:
		ct->ct_wait = *(struct timeval *)info;
		ct->ct_waitset = TRUE;
		break;
	case CLGET_TIMEOUT:
		*(struct timeval *)info = ct->ct_wait;
		break;
	case CLGET_SERVER_ADDR:
		*(struct sockaddr_in *)info = ct->ct_addr;
		break;
	default:
		return (FALSE);
	}
	return (TRUE);
}


static void
clnttcp_destroy(
	CLIENT *h)
{
	register struct ct_data *ct =
	    (struct ct_data *) h->cl_private;

	if (ct->ct_closeit) {
		(void)close(ct->ct_sock);
	}
	XDR_DESTROY(&(ct->ct_xdrs));
	mem_free((char*)ct, sizeof(struct ct_data));
	mem_free((char*)h, sizeof(CLIENT));
}

/*
 * Interface between xdr serializer and tcp connection.
 * Behaves like the system calls, read & write, but keeps some error state
 * around for the rpc level.
 */
static int
readtcp(
	register struct ct_data *ct,
	char* buf,
	register int len)
{
#ifdef FD_SETSIZE
	fd_set mask;
	fd_set readfds;

	if (len == 0)
		return (0);
	FD_ZERO(&mask);
	FD_SET(ct->ct_sock, &mask);
#else
	register int mask = 1 << (ct->ct_sock);
	int readfds;

	if (len == 0)
		return (0);

#endif /* def FD_SETSIZE */
	for (;;) {
		struct timeval	timeout = ct->ct_wait;

		readfds = mask;
		switch (select(ct->ct_sock+1, &readfds, NULL, NULL,
			       &timeout)) {
		case 0:
			ct->ct_error.re_status = RPC_TIMEDOUT;
			return (-1);

		case -1:
                    /*
                     * The following is commented-out because 1) it prevents
                     * a program from using signals to terminate a thread;
                     * and 2) alternatives exist (e.g., using `sigaction(2)`
                     * with `SA_RESTART`).
                     * Steven Emmerson 2018-03-26
			if (errno == EINTR)
				continue;
                     */
			ct->ct_error.re_status = RPC_CANTRECV;
			ct->ct_error.re_errno = errno;
			return (-1);
		}
		break;
	}
	switch (len = (int)read(ct->ct_sock, buf, len)) {

	case 0:
		/* premature eof */
		ct->ct_error.re_errno = ECONNRESET;
		ct->ct_error.re_status = RPC_CANTRECV;
		len = -1;  /* it's really an error */
		break;

	case -1:
		ct->ct_error.re_errno = errno;
		ct->ct_error.re_status = RPC_CANTRECV;
		break;
	}
	return (len);
}

static int
writetcp(
	register struct ct_data *ct,
	char* buf,
	int len)
{
	register int i, cnt;

	for (cnt = len; cnt > 0; cnt -= i, buf += i) {
		if ((i = (int)write(ct->ct_sock, buf, cnt)) == -1) {
			ct->ct_error.re_errno = errno;
			ct->ct_error.re_status = RPC_CANTSEND;
			return (-1);
		}
	}
	return (len);
}

static struct clnt_ops tcp_ops = {
	clnttcp_call,
	clnttcp_abort,
	clnttcp_geterr,
	clnttcp_freeres,
	clnttcp_destroy,
	clnttcp_control
};

/*
 * Create a client handle for a tcp/ip connection.
 * If *sockp<0, *sockp is set to a newly created TCP socket and it is
 * connected to raddr.  If *sockp non-negative then
 * raddr is ignored.  The rpc/tcp package does buffering
 * similar to stdio, so the client must pick send and receive buffer sizes,];
 * 0 => use the default.
 * If raddr->sin_port is 0, then a binder on the remote machine is
 * consulted for the right port number.
 * NB: *sockp is copied into a private area.
 * NB: It is the clients responsibility to close *sockp.
 * NB: The rpch->cl_auth is set null authentication.  Caller may wish to set this
 * something more useful.
 */
CLIENT *
clnttcp_create(
	struct sockaddr_in *raddr,
	unsigned long prog,
	unsigned long vers,
	register int *sockp,
	unsigned sendsz,
	unsigned recvsz)
{
	CLIENT *h;
	register struct ct_data *ct;
	struct timeval now;
	struct rpc_msg call_msg;

	h  = (CLIENT *)mem_alloc(sizeof(*h));
	if (h == NULL) {
		(void)fprintf(stderr, "clnttcp_create: out of memory\n");
		rpc_createerr.cf_stat = RPC_SYSTEMERROR;
		rpc_createerr.cf_error.re_errno = errno;
		goto return_null;
	}
	ct = (struct ct_data *)mem_alloc(sizeof(*ct));
	if (ct == NULL) {
		(void)fprintf(stderr, "clnttcp_create: out of memory\n");
		rpc_createerr.cf_stat = RPC_SYSTEMERROR;
		rpc_createerr.cf_error.re_errno = errno;
		goto free_h;
	}

	/*
	 * If no port number given ask the pmap for one
	 */
	if (raddr->sin_port == 0) {
		unsigned short port;
		if ((port = pmap_getport(raddr, prog, vers, IPPROTO_TCP)) == 0) {
			mem_free((char*)ct, sizeof(struct ct_data));
			mem_free((char*)h, sizeof(CLIENT));
			return ((CLIENT *)NULL);
		}
		raddr->sin_port = htons(port);
	}

	/*
	 * If no socket given, open one
	 */
	if (*sockp < 0) {
		*sockp = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (*sockp < 0) {
			rpc_createerr.cf_stat = RPC_SYSTEMERROR;
			rpc_createerr.cf_error.re_errno = errno;
			goto fooy;
		}
		(void)bindresvport(*sockp, (struct sockaddr_in *)0);
		if (connect(*sockp, (struct sockaddr *)raddr, sizeof(*raddr))
                                < 0) {
			rpc_createerr.cf_stat = RPC_SYSTEMERROR;
			rpc_createerr.cf_error.re_errno = errno;
			(void)close(*sockp);
			goto fooy;
		}
		ct->ct_closeit = TRUE;
	} else {
		ct->ct_closeit = FALSE;
	}

	/*
	 * Set up private data struct
	 */
	ct->ct_sock = *sockp;
	ct->ct_wait.tv_usec = 0;
	ct->ct_waitset = FALSE;
	ct->ct_addr = *raddr;

	/*
	 * Initialize call message
	 */
	(void)gettimeofday(&now, NULL);
	call_msg.rm_xid = (getpid() ^ now.tv_sec ^ now.tv_usec);
	call_msg.rm_direction = CALL;
	call_msg.rm_call.cb_rpcvers = RPC_MSG_VERSION;
	call_msg.rm_call.cb_prog = prog;
	call_msg.rm_call.cb_vers = vers;

	/*
	 * pre-serialize the static part of the call msg and stash it away
	 */
	xdrmem_create(&(ct->ct_xdrs), ct->ct_mcall, MCALL_MSG_SIZE,
	    XDR_ENCODE);
	if (! xdr_callhdr(&(ct->ct_xdrs), &call_msg)) {
		if (ct->ct_closeit) {
			(void)close(*sockp);
		}
		goto fooy;
	}
	ct->ct_mpos = XDR_GETPOS(&(ct->ct_xdrs));
	XDR_DESTROY(&(ct->ct_xdrs));

	/*
	 * Create a client handle which uses xdrrec for serialization
	 * and authnone for authentication.
	 */
	xdrrec_create(&(ct->ct_xdrs), sendsz, recvsz,
	    (char*)ct,
	    (int (*)(void*, char*, int))readtcp,
	    (int (*)(void*, char*, int))writetcp);
	h->cl_ops = &tcp_ops;
	h->cl_private = (char*) ct;
	h->cl_auth = authnone_create();
	return (h);

fooy:
	/*
	 * Something goofed, free stuff and barf
	 */
	mem_free((char*)ct, sizeof(struct ct_data));
free_h:
	mem_free((char*)h, sizeof(CLIENT));
return_null:
	return ((CLIENT *)NULL);
}
