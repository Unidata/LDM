/*
 * Copyright 2013 University Corporation for Atmospheric Research. All rights
 * reserved. See the file COPYRIGHT in the top-level source directory for the
 * license.
 */

/* 
 * Implementation
 */

#include <config.h>
#include <sys/types.h> /* struct timeval */
#include <sys/time.h> /* struct timeval */
#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <rpc/rpc.h>
#include <rpc/pmap_prot.h>
#include <log.h>
#include <ctype.h>
#include "error.h"
#include "ldm_clnt_misc.h"
#include "ldm5.h"       /* feedtypet */
#include "ldmalloc.h"
#include "alrm.h"
#include "globals.h"
#include "remote.h"
#include "h_clnt.h"
#ifdef LDM_NBTCP
#include "clnt_nbtcp.h"
#endif

#ifndef INSTRUMENT
#define INSTRUMENT 1            /* default for now */
#endif /* !INSTRUMENT */

#if INSTRUMENT
#ifndef INSTR_WARN_TIME
#define INSTR_WARN_TIME 20      /* # of seconds which is too long */
#endif /* !INSTR_WARN_TIME */
#include "ldmprint.h"
#include "log.h"
#endif /* INSTRUMENT */


#ifndef TIMEO_TV_INF
/* arbitrary, 28 days (gets added to the current time, beware the epoch!) */
#define TIMEO_TV_INF 2419200
#endif

#ifndef RND_DEBUG
#define TV_ROUNDOFF(tv) \
        ((tv).tv_usec < 500000 ? (tv).tv_sec : ((tv).tv_sec + 1 ))
#else
/* debug */
#define TV_ROUNDOFF(tv) ((tv).tv_sec)
#endif

#define SET_TV_ALARM(timeo, jmplabel) \
        if((timeo).tv_sec  < TIMEO_TV_INF) \
                SET_ALARM(((timeo).tv_sec > 0 ? TV_ROUNDOFF((timeo)) : 1), jmplabel)

#define PROG_NONE 0
#define VERS_NONE 0

/*
 * translate the enum state to string
 */
char *
s_remote_state(remote_state state)
{
                switch (state) {
                case H_NONE :
                        return "NONE"; 
                case NAMED :
                        return "NAMED"; 
                case ADDRESSED :
                        return "ADDRESSED";
                case PMAP_CLNTED :
                        return "SVC_UNAVAIL";
                case MAPPED :
                        return "MAPPED";
                case H_CLNTED :
                        return "H_CLNTED";
                case RESPONDING :
                        return "RESPONDING";
                }
                return "";
}


/*
 * begin duplication of code from
 * sun sample implementation clnt_perror.c
 * Copyr 1984 Sun Micro
 */
struct auth_errtab {
        enum auth_stat status;  
        const char *message;
};

static struct auth_errtab auth_errlist[] = {
        { AUTH_OK,
                "Authentication OK" },
        /*
         * failed at remote end
         */
        { AUTH_BADCRED,
                "Invalid client credential" },
        { AUTH_REJECTEDCRED,
                "Server rejected credential" },
        { AUTH_BADVERF,
                "Invalid client verifier" },
        { AUTH_REJECTEDVERF,
                "Server rejected verifier" },
        { AUTH_TOOWEAK,
                "Client credential too weak" },
        /*
         * failed locally
         */
        { AUTH_INVALIDRESP,
                "Invalid server verifier" },
        { AUTH_FAILED,
                "Failed (unspecified error)" }
};

static const char *
auth_errmsg(enum auth_stat stat)
{
        size_t i;

        for (i = 0; i < sizeof(auth_errlist)/sizeof(struct auth_errtab); i++) {
                if (auth_errlist[i].status == stat) {
                        return(auth_errlist[i].message);
                }
        }
        return(NULL);
}

/* End  clnt_perror.c Excerpt */

/**
 * Constructs a client-side error message. Sort of like clnt_sperror() but
 * hopefully safer.
 *
 * @param rpch      Pointer to the client-side structure.
 * @param s         Pointer to the string that will be the initial label.
 * @param str       Pointer to the output buffer.
 * @param len       Size of the output buffer, including the terminating NUL.
 */
static void
c_sperror(CLIENT *rpch, const char *s, char *str, size_t len)
{
        struct rpc_err e;
        const char *err;
        size_t sl = 0;

        if (str == 0)   /* N.B.: silent */
                return;
        CLNT_GETERR(rpch, &e);

        if(s && *s)
        {
                (void) snprintf(str, len, "%s: ", s);
                sl = strlen(str);
                str += sl;
                len -= sl;
        }
        if(len <=1 )
                return;

        (void) strncpy(str, clnt_sperrno(e.re_status), len);  
        sl = strlen(str);
        str += sl;
        len -= sl;

        switch (e.re_status) {
        case RPC_SUCCESS:
        case RPC_CANTENCODEARGS:
        case RPC_CANTDECODERES:
        case RPC_TIMEDOUT:     
        case RPC_PROGUNAVAIL:
        case RPC_PROCUNAVAIL:
        case RPC_CANTDECODEARGS:
        case RPC_SYSTEMERROR:
        case RPC_UNKNOWNHOST:
        case RPC_UNKNOWNPROTO:
        case RPC_PMAPFAILURE:
        case RPC_PROGNOTREGISTERED:
        case RPC_FAILED:
                break;

        case RPC_CANTSEND:
        case RPC_CANTRECV:
                if(e.re_errno > 0) {
                        err = strerror(e.re_errno);
                        if(err && *err) {
                                sl = strlen(err);
                                if(len <= sl +10)
                                        return;
                                (void) snprintf(str, len, "; errno = %s", err);
                        }
                }
                break;

        case RPC_VERSMISMATCH:
                if(len <= 48)
                        return;
                (void) snprintf(str, len,
                        "; low version = %lu, high version = %lu", 
                        e.re_vers.low, e.re_vers.high);
                str[len-1] = 0;
                break;

        case RPC_AUTHERROR:
                err = auth_errmsg(e.re_why);
                (void) snprintf(str, len, "; why = ");
                sl = strlen(str);
                str += sl;
                len -= sl;
                if(err && *err) {
                        sl = strlen(err);
                        if(len <= sl +1)
                                return;
                } else if (len <= 36)
                        return;
                if (err != NULL) {
                        (void) snprintf(str, len, "%s",err);
                } else {
                        (void) snprintf(str, len,
                                "(unknown authentication error - %d)",
                                (int) e.re_why);
                }
                break;

        case RPC_PROGVERSMISMATCH:
                if(len <= 48)
                        return;
                (void) snprintf(str, len,
                        "; low version = %lu, high version = %lu", 
                        e.re_vers.low, e.re_vers.high);
                str[len-1] = 0;
                break;

        default:        /* unknown */
                if(len <= 36)
                        return;
                (void) snprintf(str, len,
                        "; s1 = %lu, s2 = %lu", 
                        (unsigned long)e.re_lb.s1, (unsigned long)e.re_lb.s2);
                str[len-1] = 0;
                break;
        }
}


/*
 * clnt_sperrno with greater signal to noise
 */
const char *
s_hclnt_sperrno(h_clnt *hcp)
{
        if(hcp->errmsg[0] != 0)
        {
                return hcp->errmsg;
        }
        else if(hcp->state > ADDRESSED)
        {
                switch (hcp->rpcerr.re_status) {
                case RPC_SUCCESS :
                        break;
                default :
                        return clnt_sperrno(hcp->rpcerr.re_status);
                }
        }
        return "";
}


#ifndef UNKNOWN_CT_DATA
/* if you know the shape of "struct ct_data", this can be used */

#define MCALL_MSG_SIZE 24
struct ct_data {
        int     ct_sock;
        bool_t      ct_closeit;
        struct timeval  ct_wait;
        bool_t          ct_waitset;       /* wait set by clnt_control? */
#ifndef TIRPC
#ifdef __sgi
        int pad;        /* IRIX 4.0.5 */
#endif
        struct sockaddr_in ct_addr;
#else
        struct netbuf   ct_addr;    /* remote service address */
#endif /* !TIRPC */
        struct rpc_err  ct_error;
        char        ct_mcall[MCALL_MSG_SIZE];   /* marshalled callmsg */
        unsigned int       ct_mpos;            /* pos after marshal */
        XDR     ct_xdrs;
};

static void
clnt_seterr(CLIENT *clnt, enum clnt_stat re_status)
{
        struct ct_data *ct = (struct ct_data *) clnt->cl_private;
        ct->ct_error.re_status = re_status;
}

#else

static void
clnt_seterr(CLIENT *clnt, enum clnt_stat re_status)
{
        /* NOOP, error messages may get hosed */
}

#endif /* !UNKNOWN_CT_DATA */


int
h_clntfileno(h_clnt *hcp)
{
        if(hcp == NULL)
                return -1;
        if(hcp->state < H_CLNTED)
                return -1;
#if defined(CLGET_FD)
        {
            int fd = 1;

            if(hcp->clnt != NULL)
                    (void) clnt_control(hcp->clnt, CLGET_FD, (void *)&fd);
            return fd;
        }
#elif !defined(UNKNOWN_CT_DATA)
        {
                struct ct_data *ct = (struct ct_data *) hcp->clnt->cl_private;
                return ct->ct_sock;
        }
#else
        /* CLGET_FD not supported */
        return 512; /* a positive number which is not another descriptor */
#endif
}


char *
h_clnt_name(h_clnt *hcp)
{
        if(hcp == NULL)
                return NULL;
        return hcp->remote;
}


/*
 * take the difference between two timevals
 *
 * N.B. Meaningful only if "afta" is later than "b4",
 * negative differences map to 0.0 
 */
struct timeval 
diff_timeval(
        struct timeval *afta,
        struct timeval *b4)
{
        struct timeval diff;

        (void)memset(&diff, 0, sizeof(diff));

        diff.tv_sec = afta->tv_sec -  b4->tv_sec;
        diff.tv_usec = afta->tv_usec -  b4->tv_usec;

        if(diff.tv_usec < 0)
        {
                if(diff.tv_sec > 0)
                {
                        /* borrow */
                        diff.tv_sec--;
                        diff.tv_usec += 1000000;
                }
                else
                {
                        /* truncate to zero */
                        diff.tv_sec = diff.tv_usec = 0;
                }
        }

        return diff;
}


/*
 * set the elapsed time timer
 */
static int
set_begin(h_clnt *hcp)
{
        /* 
         * Next variable is unused.
         * Some systems  won't take null as arg 2 of gettimeofday().
         * On systems (SunOS 5.x) where the definition has only one arg,
         * the second arg is ignored for backward compatibility...
         */
        return gettimeofday(&hcp->begin, NULL);
}


/*
 * compute the elapsed time
 */
static int
set_elapsed(h_clnt *hcp)
{
        struct timeval afta;
        int status = gettimeofday(&afta, 0);
        if(status == 0)
        {
                hcp->elapsed = diff_timeval(&afta, &hcp->begin);
        }
        return status;
}


/*
 * how much time is remaining?
 */
/*ARGSUSED*/
static struct timeval
time_remaining(
        h_clnt *hcp,
        struct timeval end
)
{
        struct timeval now, remaining;

        remaining.tv_sec = -1;
        remaining.tv_usec = 0;

        if(gettimeofday(&now, 0) == 0)
        {
                remaining = diff_timeval(&end, &now);
        }

        if(remaining.tv_sec < 0) /* gettimeofday() or roundoff err */
                remaining.tv_sec = end.tv_usec = 0;
#ifdef DEBUG
        (void)fprintf(stderr, "%3ld.%06ld\n",
                        remaining.tv_sec, remaining.tv_usec
                        );
#endif

        return remaining;
}


/*
 * Free up resources associated with the pmap_clnt.
 * This may called "independently" to save some file descriptors,
 * or as part of closing/freeing the hcp.
 * N.B. No state change. Metastate contained in NULL of pmap_clnt.
 */
void
free_h_pmap(h_clnt *hcp)
{
        if(hcp == NULL || hcp->state < PMAP_CLNTED || hcp->pmap_clnt == NULL)
                return;
        clnt_destroy(hcp->pmap_clnt);
        hcp->pmap_clnt = NULL;
}


/*
 * Close the connection, return to the NAMED state.
 */
void
close_h_clnt(h_clnt *hcp)
{
        if(hcp == NULL)
                return;
        if(hcp->state >= H_CLNTED)
        {

                log_assert(hcp->clnt != NULL); /* if state >= H_CLNTED, this must be */
                clnt_destroy(hcp->clnt);
                hcp->clnt = NULL;
        }

        log_assert(hcp->clnt == NULL);

        free_h_pmap(hcp);

        if(hcp->state > NAMED)
                hcp->state = NAMED;
}


void
free_h_clnt(h_clnt *hcp)
{
        if(hcp == NULL)
                return;

        close_h_clnt(hcp);

        free(hcp);
}


/*
 * Initialize an h_clnt struct.
 * State moves from undefined to NAMED.
 */
remote_state
init_h_clnt(
        h_clnt *hcp,
        const char *remote,
        unsigned long program,
        unsigned long version,
        unsigned int protocol)
{
        (void)strncpy(hcp->remote, remote, sizeof(hcp->remote));
        hcp->remote[sizeof(hcp->remote)-1] = 0;
        hcp->prog = program;
        hcp->vers = version;
        hcp->prot = protocol;
        hcp->state = NAMED;
        (void)memset(&hcp->addr, 0, sizeof(struct sockaddr_in));
        hcp->pmap_clnt = NULL;
        hcp->rpcerr.re_status = RPC_CANTSEND; /* in the NAMED state */
        hcp->port = 0;
        hcp->clnt = NULL;
        hcp->h_timeo = TIMEO_INF;
        hcp->elapsed.tv_sec = 0;
        hcp->elapsed.tv_usec = 0;
        hcp->errmsg[0] = 0;
        return hcp->state;
}


h_clnt *
new_h_clnt(
        const char *remote,
        unsigned long program,
        unsigned long version,
        unsigned int protocol)
{
        h_clnt *hcp;
        hcp = Alloc(1, h_clnt);
        if(hcp == NULL)
                return NULL;
        (void) init_h_clnt(hcp, remote, program, version, protocol);
        return hcp;
}



/*
 * Fill in hcp->addr (a struct sockaddr_in).
 * State moves to ADDRESSED.
 * Failure drops back to state NAMED.
 */
static remote_state
get_addr(h_clnt *hcp, struct timeval timeo)
{
        ErrorObj*        error;

        log_assert(hcp->prog != PROG_NONE);

        hcp->errmsg[0] = 0;

        (void)memset(&hcp->addr, 0, sizeof(hcp->addr));

        SET_TV_ALARM(timeo, get_addr_timeo);

        error = ldm_clnt_addr(hcp->remote, &hcp->addr);

        CLR_ALRM();

        if (error != NULL) {
                (void)snprintf(hcp->errmsg, sizeof(hcp->errmsg), "ldm_clnt_addr(%s): %s",
                        hcp->remote, err_message(error));
                hcp->errmsg[sizeof(hcp->errmsg)-1] = 0;
                err_free(error);

                hcp->rpcerr.re_status = rpc_createerr.cf_stat = RPC_UNKNOWNHOST;
                hcp->state = NAMED;
                return NAMED;
        }

        hcp->addr.sin_family = AF_INET;
        hcp->rpcerr.re_status = RPC_CANTSEND;
        hcp->addr.sin_port =  0;
        if(hcp->prog == LDMPROG)
        {
                /*
                 * If we are contacting the LDM,
                 * first attempt the default port,
                 * skipping the portmap lookup.
                 */
                hcp->port = LDM_PORT;
                hcp->state = MAPPED;
        }
        else
        {
                hcp->state = ADDRESSED;
        }
        return hcp->state;
get_addr_timeo :
        /* ALRM, longjmp, goto => timed out */
        (void)snprintf(hcp->errmsg, sizeof(hcp->errmsg),
                "ldm_clnt_addr(%s): lookup Timed out", hcp->remote);
        hcp->rpcerr.re_status = rpc_createerr.cf_stat = RPC_UNKNOWNHOST;
        hcp->state = NAMED;
        return NAMED;
}


/*
 * Contact the remote portmapper.
 * State moves to PMAP_CLNTED.
 * Failure drops back to state NAMED. (Force address lookup again).
 */
static remote_state
get_pmap_clnt(h_clnt *hcp, struct timeval timeo)
{
        struct sockaddr_in pmap_addr;
        struct timeval wait;
        int sock = RPC_ANYSOCK;
        CLIENT * pmap_clnt;

        if(hcp->prog == PROG_NONE)
        {
                /* trap h_xprt_turn() clients */
                hcp->state = H_NONE;
                return H_NONE;
        }

        if(hcp->state < ADDRESSED)
        {
                /* can't get pmap_clnt without valid remote address */
                return hcp->state;
        }

        hcp->errmsg[0] = 0;
        (void)memcpy(&pmap_addr, (char *)&hcp->addr,
               sizeof(struct sockaddr_in));
        pmap_addr.sin_port = (short)htons(PMAPPORT);
        /* UDP pmap query */
        
        /* log_assert alrm not needed here since it is udp ? */

        /* Determine retry interval. It is 5 secs in pmap_getport() */
        wait.tv_usec = 0;
        wait.tv_sec = timeo.tv_sec/2;
        if(wait.tv_sec < 1)
                wait.tv_sec = 1;
        if(wait.tv_sec > 5)
                wait.tv_sec = 5;

        pmap_clnt = clntudp_bufcreate(&pmap_addr, PMAPPROG, PMAPVERS,
                wait, &sock, 
                RPCSMALLMSGSIZE, RPCSMALLMSGSIZE);
        if(pmap_clnt == NULL)
        {
                (void)snprintf(hcp->errmsg, sizeof(hcp->errmsg),
                        "can't connect to portmapper : %s",
                        strerror(rpc_createerr.cf_error.re_errno));
                hcp->rpcerr.re_status = rpc_createerr.cf_stat;
                /* force another address lookup */
                hcp->state = NAMED;
                return hcp->state;
        }
        /*
         * clntudp_bufcreate(..., &sock == RPC_ANYSOCK, ...)
         *      => cu_closeit = TRUE
         */

#if 1
        /* ping portmapper */
        if(timeo.tv_sec > 25)
        {
                /* Don't waste time here.
                 * timeo in clnt_call is 25.
                 */
                timeo.tv_sec = 25;
                timeo.tv_usec = 0;
        }
        (void) clnt_control(pmap_clnt, CLSET_TIMEOUT, (char *) &timeo);
        hcp->rpcerr.re_status = clnt_call(pmap_clnt, 0, (xdrproc_t)xdr_void, NULL,
                (xdrproc_t)xdr_void, NULL,
                timeo);
        if(hcp->rpcerr.re_status != RPC_SUCCESS)
        {
                clnt_geterr(pmap_clnt, &hcp->rpcerr);
                c_sperror(pmap_clnt, "can't contact portmapper",
                        hcp->errmsg, H_CLNT_ERRMSG_SIZE);
                clnt_destroy(pmap_clnt);
                hcp->rpcerr.re_status = rpc_createerr.cf_stat = RPC_PMAPFAILURE; /* ?? */
                /* force another address lookup */
                hcp->state = NAMED;
                return hcp->state;
        }
#endif
        
        hcp->pmap_clnt = pmap_clnt;
        hcp->state = PMAP_CLNTED;
        return PMAP_CLNTED;
}


/*
 * Get the remote port for the service of interest.
 * State moves MAPPED.
 * Failure drops back to state NAMED. (Force address lookup and new
 * portmapper connection)
 */
static remote_state
get_port(h_clnt *hcp, struct timeval timeo)
{
        struct pmap parms;
        unsigned short port = 0;

        log_assert(hcp->prog != PROG_NONE);

        if(hcp->pmap_clnt == NULL)
        {
                /* we recycled the resources */
                hcp->state = ADDRESSED;
                return hcp->state;
        }

#if FD_DEBUG
        {
                int pmfd;
                clnt_control(hcp->pmap_clnt, CLGET_FD, (void *)&pmfd);
                udebug("* pmap fd %d", pmfd);
        }
#endif

        if(hcp->state < PMAP_CLNTED)
        {
                /* can't get remote port without remote portmapper */
                return hcp->state;
        }

        hcp->errmsg[0] = 0;
        parms.pm_prog = hcp->prog;
        parms.pm_vers = hcp->vers;
#ifdef LDM_NBTCP
        if(hcp->prot == LDM_NBTCP)
                parms.pm_prot = IPPROTO_TCP;
        else
#endif /* LDM_NBTCP */
                parms.pm_prot = hcp->prot;
        parms.pm_port = 0;

        if(timeo.tv_sec > 60)
        {
                /* Don't waste time here.
                 * timeo in pmap_getport() is 60.
                 */
                timeo.tv_sec = 60;
                timeo.tv_usec = 0;
        }
        (void) clnt_control(hcp->pmap_clnt, CLSET_TIMEOUT, (char *) &timeo);
        hcp->rpcerr.re_status = clnt_call(hcp->pmap_clnt, PMAPPROC_GETPORT,
                (xdrproc_t)xdr_pmap, (void*)&parms,
                (xdrproc_t)xdr_u_short, (void*)&port,
                timeo);
        if(hcp->rpcerr.re_status != RPC_SUCCESS)
        {
                clnt_geterr(hcp->pmap_clnt, &hcp->rpcerr);
                c_sperror(hcp->pmap_clnt, "pmap: can't get port",
                        hcp->errmsg, H_CLNT_ERRMSG_SIZE);
                clnt_destroy(hcp->pmap_clnt);
                hcp->pmap_clnt = NULL;
                hcp->rpcerr.re_status = rpc_createerr.cf_stat = RPC_PMAPFAILURE;
                hcp->pmap_clnt = NULL;
                /* force address lookup again */
                hcp->state = NAMED;
                return hcp->state;
        }

        if(port == 0)
        {
                /* not registered */
                hcp->rpcerr.re_status = rpc_createerr.cf_stat = RPC_PROGNOTREGISTERED;
                return hcp->state;
        }
        hcp->port = port;
        hcp->state = MAPPED;
        return hcp->state;
}


/*
 * Create a client side handle.
 * State moves up to H_CLNTED.
 * Failure drops back to state PMAP_CLNTED.
 */
static remote_state
get_clnt(h_clnt *hcp, struct timeval timeo)
{
        int sock = RPC_ANYSOCK;
        CLIENT *clnt = NULL;

        log_assert(hcp->prog != PROG_NONE);

        if(hcp->state < MAPPED)
        {
                /* can't get clnt without a port */
                return hcp->state;
        }

        hcp->errmsg[0] = 0;
        hcp->addr.sin_port = (short)htons(hcp->port);

        SET_TV_ALARM(timeo, get_clnt_timeo);
        
#ifdef LDM_NBTCP
        if(hcp->prot == LDM_NBTCP)
        {
                clnt = clntnbtcp_create(&hcp->addr, hcp->prog, hcp->vers,
                        &sock, 64240, 1460); /* the sizes are voodoo */
        } else
#endif /* LDM_NBTCP */
        if(hcp->prot == IPPROTO_TCP)
        {
                clnt = clnttcp_create(&hcp->addr, hcp->prog, hcp->vers,
                        &sock, 0, 0);
        }
        else if(hcp->prot == IPPROTO_UDP)
        {
                struct timeval wait;
                wait.tv_usec = 0;
                wait.tv_sec = (timeo.tv_sec +1)/5;
                if(wait.tv_sec < 3) wait.tv_sec = 3;
                clnt = clntudp_create(&hcp->addr, hcp->prog, hcp->vers,
                        wait, &sock);
        }

        CLR_ALRM();

        if(clnt == NULL)
        {
                hcp->rpcerr.re_status = rpc_createerr.cf_stat;
                (void)snprintf(hcp->errmsg, sizeof(hcp->errmsg), "h_clnt_create(%s): %s",
                        hcp->remote, strerror(rpc_createerr.cf_error.re_errno));
                hcp->errmsg[sizeof(hcp->errmsg)-1] = 0;
                /* force (at least) portmap lookup again */
                hcp->port = 0;
                if(hcp->pmap_clnt != NULL)
                        hcp->state = PMAP_CLNTED;
                else
                        hcp->state = ADDRESSED;
                return hcp->state;
        }
        /*
         * clntXXX_bufcreate(..., &sock == RPC_ANYSOCK, ...)
         *      => closeit = TRUE
         */

        hcp->clnt = clnt;
        hcp->state = H_CLNTED;
        return hcp->state;

get_clnt_timeo:
        /* ALRM, longjmp, goto => timed out */
        if(clnt != NULL)
        {
                clnt_destroy(clnt);
        }
        if(sock != RPC_ANYSOCK)
        {
                (void) close(sock);
        }
        hcp->rpcerr.re_status = rpc_createerr.cf_stat = RPC_TIMEDOUT;
        (void)snprintf(hcp->errmsg, sizeof(hcp->errmsg),
                        "h_clnt_create(%s): Timed out while creating connection",
                        hcp->remote);
        /* force (at least) portmap lookup again */
        hcp->port = 0;
        hcp->state = PMAP_CLNTED;
        return hcp->state;
}


/* 
 * (Finally) try a clnt_call to remote client
 * State moves up to RESPONDING.
 * Error moves down to PMAP_CLNTED, except RPC_TIMEDOUT moves down
 * H_CLNTED.
 */
static remote_state
hC_clnt_call(
        h_clnt *hcp,
        unsigned long proc,
        xdrproc_t xargs,
        void* argsp,
        xdrproc_t xres,
        void* resp,
        struct timeval timeo)
{

        if(hcp->state < H_CLNTED)
        {
                /* can't call without a clnt */
                return hcp->state;
        }

        log_assert(hcp->clnt != NULL); /* if state >= H_CLNTED, this must be */

#if FD_DEBUG
        {
                int pmfd, clntfd;
                clnt_control(hcp->pmap_clnt, CLGET_FD, (void *)&pmfd);
                clnt_control(hcp->clnt, CLGET_FD, (void *)&clntfd);
                udebug("**pmap fd %d, clnt fd %d", pmfd, clntfd);
        }
#endif

        hcp->errmsg[0] = 0;

        SET_TV_ALARM(timeo, call_timeo);

        if(xres == (xdrproc_t)0)
        {
                /*
                 * Proc not expecting a reply.
                 * Set timeout to zero so tcp lower layers
                 * know it is batched.
                 */
                timeo.tv_sec =  0;
                timeo.tv_usec =  0;
        }

        /* make the call */
        (void) clnt_control(hcp->clnt, CLSET_TIMEOUT, (char *) &timeo);
        hcp->rpcerr.re_status = clnt_call(hcp->clnt, proc,
                xargs, argsp,
                xres, resp,
                timeo);

        CLR_ALRM();

        if (timeo.tv_sec == 0 && timeo.tv_usec == 0
                        && hcp->rpcerr.re_status == RPC_TIMEDOUT)
        {
                /*
                 * Normal return given timeout == {0,0} and xres != 0
                 */
                hcp->rpcerr.re_status = RPC_SUCCESS;
                clnt_seterr(hcp->clnt, hcp->rpcerr.re_status);
        }

        switch (hcp->rpcerr.re_status) {
        case RPC_SUCCESS :
                /* all is well */
                hcp->state = RESPONDING;
                break;
        case RPC_TIMEDOUT :
                /*
                 * This timeout on the reply, so our state is consistant
                 * and we can try again...
                 * ?? but should we
                 */
                c_sperror(hcp->clnt, "select",
                        hcp->errmsg, H_CLNT_ERRMSG_SIZE);
                /*FALLTHROUGH*/
                /* no break */
        case RPC_PROCUNAVAIL :
                hcp->state = H_CLNTED;
                break;
        default :
                /* some error, disconnect */
                clnt_geterr(hcp->clnt, &hcp->rpcerr);
                c_sperror(hcp->clnt, NULL,
                        hcp->errmsg, H_CLNT_ERRMSG_SIZE);
                goto disco;
                /*NOTREACHED*/
                break;
        }
        
        return hcp->state;

call_timeo:
        /* ALRM, longjmp, goto => timed out */
        hcp->rpcerr.re_status = RPC_TIMEDOUT;
        clnt_seterr(hcp->clnt, hcp->rpcerr.re_status);
        (void)strncpy(hcp->errmsg, clnt_sperrno(hcp->rpcerr.re_status),
                H_CLNT_ERRMSG_SIZE -1);
        /* we may have done a partial write, must disconnect */
        /*FALLTHROUGH*/
disco:
        /*
         * disconnect, force a portmap lookup (which may fail and
         * send us down to even lower states)
         */
        clnt_destroy(hcp->clnt);
        hcp->clnt = NULL;
        hcp->port = 0;
        if(hcp->pmap_clnt != NULL)
                hcp->state = PMAP_CLNTED;
        else
                hcp->state = ADDRESSED;
        return hcp->state;
}



/*
 * The nut of the matter.
 * Climb to the highest state you can,
 * Make the call if you can
 *
 * Returns:
 *   RPC_FAILED            if "hcp" or "hcp->state" is NULL.
 *   hcp->rpcerr.re_status for everything else.  Enumeration constants
 *                         are defined in /usr/include/rpc/clnt_stat.h.
 */
enum clnt_stat
h_clnt_call(
        h_clnt *hcp,
        unsigned long proc,
        xdrproc_t xargs,
        void* argsp,
        xdrproc_t xres,
        void* resp,
        unsigned int timeout)
{
        remote_state state, sav;
        struct timeval end, remaining;

        if(hcp == NULL)
                return RPC_FAILED;

        (void)set_begin(hcp);

        if(timeout == USE_H_TIMEO)
        {
                timeout = hcp->h_timeo;
        }

        remaining.tv_usec = 0;
        remaining.tv_sec = timeout > 0 ? timeout : (2 * TIMEO_TV_INF);
        end = hcp->begin;
        end.tv_sec += remaining.tv_sec;

        state = hcp->state;
        if(state == H_NONE)
                return RPC_FAILED;

        do {
                sav = state;

#if 0
                if (ulogIsDebug())
                    udebug("%s:%3ld.%06ld",
                            s_remote_state(state), remaining.tv_sec,  
                            remaining.tv_usec);
#endif
                switch (state) {
                case NAMED :
                        state = get_addr(hcp, remaining);
                        break;
                case ADDRESSED :
                        state = get_pmap_clnt(hcp, remaining);
                        break;
                case PMAP_CLNTED :
                        state = get_port(hcp, remaining);
                        break;
                case MAPPED :
                        state = get_clnt(hcp, remaining);
                        break;
                case H_CLNTED :
                case RESPONDING :
                        state = hC_clnt_call(hcp, proc,
                                                xargs, argsp, xres, resp, remaining);
                        break;
                }

                if(state == RESPONDING)
                        break;

                remaining = time_remaining(hcp, end);

        } while(!done && (sav < state || (sav == MAPPED && state ==
            ADDRESSED)));

        (void)set_elapsed(hcp);

#if INSTRUMENT
        if(hcp->rpcerr.re_status == RPC_SUCCESS
                        && hcp->elapsed.tv_sec > INSTR_WARN_TIME)
                log_error("h_clnt_call: %s: %s: time elapsed %3ld.%06ld",
                        hcp->remote,
                        s_ldmproc(proc),
                        hcp->elapsed.tv_sec, hcp->elapsed.tv_usec);
#endif /* INSTRUMENT */
        return hcp->rpcerr.re_status;
}


static remote_state
h_clnt_open(h_clnt *hcp, unsigned int timeout)
{
        remote_state state, sav;
        struct timeval end, remaining;

        if(hcp == NULL)
                return H_NONE;

        (void)set_begin(hcp);

        remaining.tv_usec = 0;
        remaining.tv_sec = timeout > 0 ? timeout : (2 * TIMEO_TV_INF);
        end = hcp->begin;
        end.tv_sec += remaining.tv_sec;

        state = hcp->state;
        if(state == H_NONE)
                return state;

        do {
                sav = state;

                switch (state) {
                case NAMED :
                        state = get_addr(hcp, remaining);
                        break;
                case ADDRESSED :
                        state = get_pmap_clnt(hcp, remaining);
                        break;
                case PMAP_CLNTED :
                        state = get_port(hcp, remaining);
                        break;
                case MAPPED :
                        state = get_clnt(hcp, remaining);
                        break;
                case H_CLNTED :
                case RESPONDING :
                        break;
                }

                remaining = time_remaining(hcp, end);

        } while(sav < state || (sav == MAPPED && state == ADDRESSED));

        (void)set_elapsed(hcp);

        return state;
}


/*
 * Takes a SVCXPRT and turns it into an h_clnt.  The socket of the h_clnt
 * is dup(2)ed from the SVCXPRT.  The SVCXPRT is destroyed by svc_destroy(3).
 */
remote_state
h_xprt_turn(h_clnt *hcp,
        const char *remote,
        SVCXPRT *xprt,
        unsigned int sendsz,
        unsigned int recvsz)
{
        int sock;
        CLIENT *clnt = NULL;

        (void)init_h_clnt(hcp, remote, PROG_NONE, VERS_NONE, IPPROTO_TCP);

        (void)memcpy(&hcp->addr, svc_getcaller(xprt),
            sizeof(struct sockaddr_in));
        /* hcp->state = ADDRESSED; */

        hcp->port = (short)ntohs(hcp->addr.sin_port);
        hcp->state = MAPPED;

        sock = dup(xprt->xp_sock);
        if(sock == -1)
        {
                int errnum = errno;
                hcp->rpcerr.re_status = RPC_SYSTEMERROR;
                (void)snprintf(hcp->errmsg, sizeof(hcp->errmsg), "h_xprt_turn(%s): %s",
                        hcp->remote, strerror(errnum));
                hcp->errmsg[sizeof(hcp->errmsg)-1] = 0;
                return hcp->state;
        }

        svc_destroy(xprt);

        clnt = clnttcp_create(&hcp->addr, LDMPROG, FIVE, &sock,
                         sendsz, recvsz);
        if(clnt == NULL)
        {
                hcp->rpcerr.re_status = rpc_createerr.cf_stat;
                (void)snprintf(hcp->errmsg, sizeof(hcp->errmsg), "h_xprt_turn(%s): %s",
                        hcp->remote, strerror(rpc_createerr.cf_error.re_errno));
                hcp->errmsg[sizeof(hcp->errmsg)-1] = 0;
                return hcp->state;
        }
#ifdef CLSET_FD_CLOSE
        clnt_control(clnt, CLSET_FD_CLOSE, NULL);
#elif !defined(UNKNOWN_CT_DATA)
        {
                struct ct_data *ct = (struct ct_data *) clnt->cl_private;
                ct->ct_closeit = TRUE;
        }
#endif /* CLSET_FD_CLOSE */

        hcp->clnt = clnt;
        hcp->state = H_CLNTED;
        return hcp->state;
}


h_clnt *
open_h_clnt(
        const char *remote,
        unsigned long program,
        unsigned long version,
        unsigned int protocol,
        unsigned int timeout)
{
        h_clnt *hcp;
        hcp = new_h_clnt(remote, program, version, protocol);
        if(hcp == NULL)
        {
                /* serror("new_h_clnt %s", remote); */
                return NULL;
        }

        if( h_clnt_open(hcp, timeout) < H_CLNTED)
        {
#if 0 /* debug */
                log_error("open_h_clnt(%s, %d, %d, %s)%s"
                        host, prognum, versnum, protocol, clnt_spcreateerror(""));
#endif
                free_h_clnt(hcp);
                return NULL;
        }
        
        set_h_timeout(hcp, timeout);

        return hcp;
}


/*
 * Attempt the flush, external routine.
 * Return 0 if buffer empty, -1 on error, otherwise > 0.
 * Set *rpc_statp with the rpc error code.
 */
/*
 *      tcp based rpc can 'batch' calls, and we use this batching.
 *  However, under certain conditions we would like to flush the
 *  batch que.
 *  The following function is added to accomplish this.
 *  It should really be in the lower rpc layer, as it uses
 *  struct ct_data, usually private to rpc/clnt_tcp.c
 */
/*ARGSUSED*/
int
h_clnt_flush(
        h_clnt *hcp,
        bool_t block,
        unsigned int timeo,
        enum clnt_stat *rpc_statp /* return */
)
{
        int status = 0;
        if(hcp == NULL)
                return -1;

        if(hcp->state < H_CLNTED)
        {
#if 0
                return 0;
#else
        /* debug */
                *rpc_statp = hcp->rpcerr.re_status;
                return -1;
#endif
        }

        log_assert(hcp->clnt != NULL); /* if state >= H_CLNTED, this must be */

        (void)set_begin(hcp);

        hcp->errmsg[0] = 0;

        if(timeo == USE_H_TIMEO)
        {
                timeo = hcp->h_timeo;
        }

        if(timeo > 0)
        {
                SET_ALARM(timeo, flush_timeo);
        }

#ifdef LDM_NBTCP
        if(hcp->prot == LDM_NBTCP)
        {
                status = clntnbtcp_flush(hcp->clnt, block,  rpc_statp);
                hcp->rpcerr.re_status = *rpc_statp;
        } else
#endif /* LDM_NBTCP */
#ifndef UNKNOWN_CT_DATA
        if(hcp->prot == IPPROTO_TCP)
        {
                struct ct_data *ct = (struct ct_data *) hcp->clnt->cl_private;
                XDR *xdrs = &(ct->ct_xdrs);
                if(!xdrrec_endofrecord(xdrs, TRUE))
                {
                        status = -1;
                        hcp->rpcerr.re_status = ct->ct_error.re_status;
                        *rpc_statp = hcp->rpcerr.re_status;
                }
        }
        else
#endif  /* !UNKNOWN_CT_DATA */
        {
                /* UDP */
                struct timeval to;
                to.tv_usec = 0;
                to.tv_sec = timeo;
                /* NULLPROC */
                (void) clnt_control(hcp->clnt, CLSET_TIMEOUT, (char *) &to);
                hcp->rpcerr.re_status = clnt_call(hcp->clnt, NULLPROC,
                        (xdrproc_t)xdr_void, NULL,
                        (xdrproc_t)xdr_void, NULL,
                        to);
                *rpc_statp = hcp->rpcerr.re_status;
                status = (hcp->rpcerr.re_status == RPC_SUCCESS) ? 1 : -1;
        }

        CLR_ALRM();

        switch (hcp->rpcerr.re_status) {
        case RPC_SUCCESS :
                /* all is well */
                break;
        case RPC_TIMEDOUT :
                /* connected but not responding */
                c_sperror(hcp->clnt, "select",
                        hcp->errmsg, H_CLNT_ERRMSG_SIZE);
                break;
        default :
                clnt_geterr(hcp->clnt, &hcp->rpcerr);
                (void)strncpy(hcp->errmsg, clnt_sperrno(hcp->rpcerr.re_status),
                        H_CLNT_ERRMSG_SIZE -1);
                break;
        }

        /*FALLTHROUGH*/
done:
        (void)set_elapsed(hcp);

#if INSTRUMENT
        if(hcp->rpcerr.re_status == RPC_SUCCESS
                        && hcp->elapsed.tv_sec > INSTR_WARN_TIME)
                log_error("h_clnt_flush: %s: time elapsed %3ld.%06ld",
                        hcp->remote,
                        hcp->elapsed.tv_sec, hcp->elapsed.tv_usec);
#endif /* INSTRUMENT */
        if(status == -1) /* && !UDP TODO ?? */
        {
                /*
                 * disconnect, force a portmap lookup (which may fail and
                 * send us down to even lower states)
                 */
                clnt_destroy(hcp->clnt);
                hcp->clnt = NULL;
                hcp->port = 0;
                if(hcp->pmap_clnt == NULL)
                {
                        /* we recycled the resources */
                        hcp->state = ADDRESSED;
                        return status;
                }
                else
                        hcp->state = PMAP_CLNTED;
        }

        return status;
flush_timeo :
        /* ALRM, longjmp, goto => timed out */
        hcp->rpcerr.re_status = RPC_TIMEDOUT;
        *rpc_statp = hcp->rpcerr.re_status;
        clnt_seterr(hcp->clnt, hcp->rpcerr.re_status);
        (void)strncpy(hcp->errmsg, clnt_sperrno(hcp->rpcerr.re_status),
                H_CLNT_ERRMSG_SIZE -1);
        status = -1;
        goto done;
        /*NOTREACHED*/
        return 0;
}


enum clnt_stat
h_callrpc(
        char *remote,
        unsigned long program,
        unsigned long version,
        unsigned long proc,
        xdrproc_t xargs,
        void* argsp,
        xdrproc_t xres,
        void* resp,
        unsigned int timeout /* N.B.: this call could take 2x timeout */
)
{
        static h_clnt *cache_hcp = NULL;

        if(remote == NULL || !*remote)
                return RPC_FAILED;

        if(cache_hcp == NULL
                        || cache_hcp->prog != program
                        || cache_hcp->vers !=  version
                        || strcmp(remote, cache_hcp->remote))
        {
                if (cache_hcp)
                    free_h_clnt(cache_hcp);

                cache_hcp = open_h_clnt(remote, program, version,
                                IPPROTO_UDP, timeout);
                if(cache_hcp == NULL)
                {
                        return rpc_createerr.cf_stat;
                }
        }

        return h_clnt_call(cache_hcp, proc, xargs, argsp, xres, resp, timeout);
}
