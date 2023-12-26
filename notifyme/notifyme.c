/**
 * Requests notification of available data-products from an LDM.
 *
 * Copyright 2023, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 */

/* 
 * get notification
 */

#include <config.h>

#include "ldm.h"
#include "ldm5.h"
#include "globals.h"
#include "remote.h"
#include "ldm_clnt_misc.h"
#include "ldmprint.h"
#include "atofeedt.h"
#include "log.h"
#include "inetutil.h"
#include "ldm5_clnt.h"
#include "prod_class.h"
#include "RegularExpressions.h"
#include "rpcutil.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <rpc/rpc.h>
#include <signal.h>
#include <sys/socket.h>
#include <errno.h>
#include <regex.h>

#ifdef NO_ATEXIT
#include "atexit.h"
#endif

#ifndef DEFAULT_REMOTE
#define DEFAULT_REMOTE "localhost"
#endif
#ifndef DEFAULT_TIMEO
#define DEFAULT_TIMEO  25
#endif
#ifndef DEFAULT_TOTALTIMEO
#define DEFAULT_TOTALTIMEO  (12*DEFAULT_TIMEO)
#endif
#ifndef DEFAULT_FEEDTYPE
#define DEFAULT_FEEDTYPE ANY
#endif
#ifndef DEFAULT_PATTERN
#define DEFAULT_PATTERN ".*"
#endif


static const char *remote = DEFAULT_REMOTE; /* hostname of data remote */
static ldm_replyt reply = { OK };
static int        showProdOrigin = false;


/*
 * Called at exit.
 * This callback routine registered by atexit().
 */
static void
cleanup(void)
{
        log_notice_q("exiting");

        /* TODO: sign off */

        log_fini();
}


/*
 * Called upon receipt of signals.
 * This callback routine registered in set_sigactions() .
 */
static void
signal_handler(int sig)
{
    switch(sig) {
    case SIGINT :
            /*FALLTHROUGH*/
    case SIGTERM :
            done = !0;
            return;
    case SIGUSR1 :
            log_refresh();
            return;
    case SIGUSR2 :
            log_roll_level();
            return;
    case SIGPIPE :
            return;
    }
}


static void
set_sigactions(void)
{
    struct sigaction sigact;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;

    // Handle the following
    sigact.sa_handler = signal_handler;

    // Don't restart the following
    (void) sigaction(SIGINT,  &sigact, NULL);
    (void) sigaction(SIGTERM, &sigact, NULL);
    (void) sigaction(SIGPIPE, &sigact, NULL);

    // Restart the following
    sigact.sa_flags = SA_RESTART;
    (void) sigaction(SIGUSR1, &sigact, NULL);
    (void) sigaction(SIGUSR2, &sigact, NULL);

    sigset_t sigset;
    (void)sigemptyset(&sigset);
    (void)sigaddset(&sigset, SIGUSR1);
    (void)sigaddset(&sigset, SIGUSR2);
    (void)sigaddset(&sigset, SIGINT);
    (void)sigaddset(&sigset, SIGTERM);
    (void)sigaddset(&sigset, SIGPIPE);
    (void)sigprocmask(SIG_UNBLOCK, &sigset, NULL);
}


/**
 * @param[in] av0  Program name
 */
static void
usage(const char *av0)
{
    log_add(
"Usage: %s [options]\n"
"where:\n"
"  -h host        Have \"host\" send us the metadata (default \"%s\")\n"
"  -f feed        Request metadata for products of feedtype \"feed\" (default: ANY)\n"
"  -l dest        Log to `dest`. One of: \"\" (system logging daemon), \"-\"\n"
"                 (standard error), or file `dest`. Default is \"%s\"\n"
"  -O             Include product origin in output\n"
"  -o offset      Set the \"from\" time \"offset\" seconds before now\n"
"  -p pattern     Only show products whose ID matches \"pattern\" (default \"%s\")\n"
"  -T TotalTimeo  Give up after this many seconds (default %d)\n"
"  -t timeout     Set RPC timeout to \"timeout\" seconds (default %d)\n"
"  -v             Log INFO (and higher priority) messages\n"
"  -x             Log DEBUG (and higher priority) messages\n",
            av0,
            DEFAULT_REMOTE,
            log_get_default_destination(),
            DEFAULT_PATTERN,
            DEFAULT_TOTALTIMEO,
            DEFAULT_TIMEO);
    log_flush_error();
    exit(1);
}


static prod_class clss;

/*
 * The LDM-5 RPC dispatch routine for this program.
 * Registered as a callback by svc_register() below.
 * Note that only NULLPROC and NOTIFICATION rpc procs are
 * handled by this program.
 */
static void
notifymeprog_5(struct svc_req *rqstp, SVCXPRT *transp)
{
        prod_info notice;

        switch (rqstp->rq_proc) {

        case NULLPROC:
                (void)svc_sendreply(transp, (xdrproc_t)xdr_void, (caddr_t)NULL);
                return;

        case NOTIFICATION:
                (void) memset((char*)&notice, 0, sizeof(notice));
                if (!svc_getargs(transp, (xdrproc_t)xdr_prod_info,
                    (caddr_t)&notice))
                {
                        svcerr_decode(transp);
                        return;
                }

                (void)exitIfDone(0);

                /*
                 * Update the request filter with the timestamp
                 * we just recieved.
                 * N.B.: There can still be duplicates after
                 * a reconnect.
                 */
                clss.from = notice.arrival;
                timestamp_incr(&clss.from);


                /* 
                 * your code here, example just logs it 
                 */
                if (showProdOrigin) {
                    log_info("%s %s", s_prod_info(NULL, 0, &notice, log_is_enabled_debug),
                            notice.origin);
                } else {
                    log_info("%s", s_prod_info(NULL, 0, &notice, log_is_enabled_debug));
                }


                if(!svc_sendreply(transp, (xdrproc_t)xdr_ldm_replyt,
                    (caddr_t) &reply))
                {
                        svcerr_systemerr(transp);
                }

                (void)exitIfDone(0);

                if(!svc_freeargs(transp, xdr_prod_info, (caddr_t) &notice)) {
                        log_error_q("unable to free arguments");
                        exit(1);
                }
                /* no break */

        default:
                svcerr_noproc(transp);
                return;
        }
}

/**
 * The LDM-6 RPC service routine for this program. Registered as a callback by svc_register()
 * below. Note that only NULLPROC and NOTIFICATION functions are handled by this program (as
 * opposed to ldmprog_6() in ldm_svc.c, for example).
 * @param[in] rqstp   RPC request
 * @param[in] transp  RPC transport
 */
static void
notifymeprog_6(
        struct svc_req* rqstp,
        SVCXPRT*        transp)
{
    prod_info notice = {};

    switch (rqstp->rq_proc) {

 	case NULLPROC:
		(void)svc_sendreply(transp, (xdrproc_t)xdr_void, (char*)NULL);
		return;

    case NOTIFICATION:
        if (!svc_getargs(transp, (xdrproc_t)xdr_prod_info, (char*)&notice)) {
            svcerr_decode(transp);
            return;
        }

        /*
         * Update the request filter with the timestamp we just recieved.
         * N.B.: There can still be duplicates after a reconnect.
         */
        clss.from = notice.arrival;
        timestamp_incr(&clss.from);

        // Your code here. Example just logs it.
        if (showProdOrigin) {
            log_info("%s %s", s_prod_info(NULL, 0, &notice, log_is_enabled_debug), notice.origin);
        } else {
            log_info("%s", s_prod_info(NULL, 0, &notice, log_is_enabled_debug));
        }

        (void)exitIfDone(0);

        if(!svc_freeargs(transp, xdr_prod_info, (char*)&notice)) {
            log_error("Unable to free arguments");
            exit(1);
        }
        // No break

    default:
        svcerr_noproc(transp);
        return;
    }
}

/**
 * Makes a NOTIFYME call to the remote LDM.
 * @param[in] client  Client-side handle
 * @retval 0          Success
 * @retval 1          Non-fatal error. `log_add()` called.
 * @retval 2          Fatal error. `log_add()` called.
 */
static bool
sendNotifyMe(CLIENT* client)
{
    int             status = 2;
    prod_class_t*   prodClass = dup_prod_class(&clss);

    if (prodClass == NULL) {
        log_add("Couldn't duplicate product-class");
    }
    else {
        fornme_reply_t* reply;

        for (;;) {
            reply = notifyme_6(prodClass, client);
            exitIfDone(0);
            if (reply == NULL) {
                struct rpc_err rpcErr;
                log_add("notifyme_6() failure. %s", clnt_errmsg(client));
                status = 2;
                break;
            }
            if (reply->code == 0) {
                status = 0;
                break;
            }
            if (reply->code == BADPATTERN) {
                log_add("Invalid pattern: \"%s\"", prodClass->psa.psa_val->pattern);
                break;
            }
            if (reply->code == RECLASS) {
                if (reply->fornme_reply_t_u.prod_class->psa.psa_len == 0 ||
                        reply->fornme_reply_t_u.prod_class->psa.psa_val == NULL) {
                    int  nbytes = pc_format(prodClass, NULL, 0);
                    char buf[nbytes+1];
                    pc_format(prodClass, buf, sizeof(buf));
                    log_add("NOTIFYME request for \"%s\" denied by upstream LDM", buf);
                    status = 1;
                    break;
                }
                else {
                    int  nbytes = pc_format(prodClass, NULL, 0);
                    char original[nbytes+1];
                    pc_format(prodClass, original, sizeof(original));

                    nbytes = pc_format(reply->fornme_reply_t_u.prod_class, NULL, 0);
                    char reclass[nbytes+1];
                    pc_format(reply->fornme_reply_t_u.prod_class, reclass, sizeof(reclass));

                    log_add("NOTIFYME reclassified: \"%s\" -> \"%s\"", original, reclass);
                    free_prod_class(prodClass);
                    prodClass = dup_prod_class(reply->fornme_reply_t_u.prod_class);
                    if (prodClass == NULL) {
                        log_add("Couldn't duplicate reclassified product-class");
                        status = 2;
                        break;
                    }
                }
                continue;
            }
            log_add("Unsupported notifyme_t() status code: %d", reply->code);
            status = 2;
            break;
        }

        free_prod_class(prodClass);
    } // `prodClass` allocated

    return status;
}

/**
 * Executes the LDM-6 service. Doesn't return until an error occurs. On return, the transport is
 * destroyed.
 * @param[in] xprt  Server-side transport. Destroyed upon return.
 * @retval false    Non-fatal error. `log_add()` called.
 * @retval true     Fatal error. `log_add()` called.
 */
static bool
executeService(SVCXPRT* xprt)
{
    bool fatalError = false;

    /*
     * The only possible return values are:
     *     0          if as_shouldSwitch()
     *     ETIMEDOUT  if timeout exceeded.
     *     ECONNRESET if connection closed.
     *     EBADF      if socket not open.
     *     EINVAL     if invalid timeout.
     * 3rd argument is the timeout interval. It should be large enough to allow reception of at
     * least one NULLPROC call.
     */
    int status = one_svc_run(xprt->xp_sock, 3*interval); // Calls exit(0) on SIGTERM

    if (status == ECONNRESET) {
        // one_svc_run() called svc_getreqset(), which called svc_destroy(), which frees `xprt`
        log_add("Connection reset by upstream");
    }
    else {
        if (status == ETIMEDOUT) {
            log_add("Connection to upstream LDM timed-out");
        }
        else if (status) {
            log_add("Couldn't execute downstream LDM-6 service");
            fatalError = true;
        }
        svc_destroy(xprt); // Destroys the transport & frees `xprt`
    } // Connection wasn't reset and `xprt` wasn't destroyed

    return fatalError;
}

/**
 * Executes a NOTIFYME call using LDM-6 protocols. Doesn't return until an error occurs.
 * @retval false  Non-fatal error. `log_add()` called.
 * @retval true   Fatal error. `log_add()` called.
 */
static bool
notifyme6(void)
{
    bool fatalError = true; // Default fatal error

    // Create a client-side handle
    int                sd = RPC_ANYSOCK;
    struct sockaddr_in upAddr;
    CLIENT*            client;
    ErrorObj*          err = ldm_clnttcp_create_vers(remote, LDM_PORT, SIX, &client, &sd, &upAddr);

    if (err) {
        err_log_and_free(err, ERR_ERROR);
    }
    else {
        log_notice("OK");

        // Send the NOTIFYME request
        int status = sendNotifyMe(client);
        if (status) {
            log_add("NOTIFYME failure");
            if (status == 1)
                fatalError = false;
        }
        else {
            // Create a server-side transport
            SVCXPRT* xprt = svcfd_create(sd, 0, MAX_RPC_BUF_NEEDED);

            if (xprt == NULL) {
                log_add_syserr("Couldn't create server-side transport");
            }
            else {
                // Register the service routine
                if (!svc_register(xprt, LDMPROG, SIX, notifymeprog_6, 0)) {
                    log_add("Couldn't register service routine");
                }
                else {
                    // Execute the service
                    fatalError = executeService(xprt); // `xprt` is destroyed on return
                }
            } // `xprt` allocated
        } // NOTIFYME call was successful

        auth_destroy(client->cl_auth);
        clnt_destroy(client);
    } // `client` created

    return fatalError;
}

int main(int ac, char *av[])
{
        unsigned      timeo = DEFAULT_TIMEO;
        unsigned      interval = DEFAULT_TIMEO;
        unsigned      TotalTimeo = DEFAULT_TOTALTIMEO;
        prod_spec     spec;
        int           status;
        prod_class_t* clssp;

        /*
         * initialize logger
         */
        if (log_init(av[0])) {
            log_syserr("Couldn't initialize logging module");
            exit(1);
        }

        if(set_timestamp(&clss.from) != 0)
        {
                fprintf(stderr, "Couldn't set timestamp\n");
                exit(1);
        }
        clss.to = TS_ENDT;
        clss.psa.psa_len = 1;
        clss.psa.psa_val = &spec;
        spec.feedtype = DEFAULT_FEEDTYPE;
        spec.pattern = DEFAULT_PATTERN;

        { /* Begin getopt block */
        extern int optind;
        extern int opterr;
        extern char *optarg;
        int ch;
        int fterr;

        opterr = 1;

        while ((ch = getopt(ac, av, "vxl:f:Oo:t:h:P:p:T:")) != EOF)
                switch (ch) {
                case 'v':
                        if (!log_is_enabled_info)
                            log_set_level(LOG_LEVEL_INFO);
                        break;
                case 'x':
                        log_set_level(LOG_LEVEL_DEBUG);
                        break;
                case 'l':
                        if (log_set_destination(optarg)) {
                            log_syserr("Couldn't set logging destination to \"%s\"",
                                    optarg);
                            usage(av[0]);
                        }
                        break;
                case 'h':
                        remote = optarg;
                        break;
                case 'O': {
                    showProdOrigin = true;
                    break;
                }
                case 'P': {
                    log_warning("Port specification is ignored");
                    break;
                }
                case 'p':
                        spec.pattern = optarg;
                        /* compiled below */
                        break;
                case 'f':
                        fterr = strfeedtypet(optarg, &spec.feedtype);
                        if(fterr != FEEDTYPE_OK)
                        {
                                fprintf(stderr, "Bad feedtype \"%s\", %s\n",
                                        optarg, strfeederr(fterr));
                                usage(av[0]);   
                        }
                        break;
                case 'o':
                        clss.from.tv_sec -= atoi(optarg);
                        break;
                case 'T':
                        TotalTimeo = atoi(optarg);
                        if(TotalTimeo == 0)
                        {
                                fprintf(stderr, "%s: invalid TotalTimeo %s", av[0], optarg);
                                usage(av[0]);   
                        }
                        break;
                case 't':
                        timeo = (unsigned)atoi(optarg);
                        if(timeo == 0 || timeo > 32767)
                        {
                                fprintf(stderr, "%s: invalid timeout %s", av[0], optarg);
                                usage(av[0]);   
                        }
                        break;
                case '?':
                        usage(av[0]);
                        break;
                }

        if(ac - optind > 0)
                usage(av[0]);

        if (re_isPathological(spec.pattern))
        {
                fprintf(stderr, "Adjusting pathological regular-expression: "
                    "\"%s\"\n", spec.pattern);
                re_vetSpec(spec.pattern);
        }
        status = regcomp(&spec.rgx,
                spec.pattern,
                REG_EXTENDED|REG_NOSUB);
        if(status != 0)
        {
                fprintf(stderr, "Bad regular expression \"%s\"\n",
                        spec.pattern);
                usage(av[0]);
        }

        if(TotalTimeo < timeo)
        {
                fprintf(stderr, "TotalTimeo %u < timeo %u\n",
                         TotalTimeo, timeo);
                usage(av[0]);
        }

        } /* End getopt block */

        log_notice_q("Starting Up: %s: %s",
                        remote,
                        s_prod_class(NULL, 0, &clss));

        /*
         * register exit handler
         */
        if(atexit(cleanup) != 0)
        {
                log_syserr("atexit");
                exit(1);
        }

        /*
         * set up signal handlers
         */
        set_sigactions();


        /*
         * Try forever.
         */
        for (;;) {
            if (notifyme6()) {
                log_flush_fatal();
                exit(1);
            }
            log_flush_error();
            sleep(30);
        }

        exit(0);
        /*NOTREACHED*/
}
