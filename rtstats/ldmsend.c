/**
 *   Copyright 2023, University Corporation for Atmospheric Research
 *   See file ../COPYRIGHT for copying and redistribution conditions.
 */

/* 
 * ldm client to ship files
 */

#define TIRPC
#include <config.h>

#include "ldm.h"
#include <log.h>
#include <errno.h>
#include <fcntl.h>
#include <rpc/rpc.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "atofeedt.h"
#include "error.h"
#include "globals.h"
#include "remote.h"
#include "inetutil.h"
#include "ldm_clnt_misc.h"
#include "ldmprint.h"
#include "md5.h"
#include "prod_class.h"
#include "rpcutil.h"
#include "log.h"

#ifdef NO_ATEXIT
#include "atexit.h"
#endif

#ifndef DEFAULT_FEEDTYPE
        /* default to using the "experimental" feedtype for reported statistics*/
#define DEFAULT_FEEDTYPE EXP
#endif

const char*         remote = NULL; /* hostname of data remote */
extern unsigned     remotePort;
static int          signed_on_hiya=0;
static CLIENT*      clnt = NULL;
static max_hereis_t max_hereis;
static unsigned     version;
static int        (*hiya)(CLIENT* clnt, prod_class_t** clsspp);
static void       (*send_product)(
    CLIENT*          clnt,
    const char*      statsdata,
    const prod_info* infop);
static void*      (*nullproc)(void* arg, CLIENT *clnt);


/* Begin Convenience functions */
static struct timeval timeo = {25, 0}; /* usual RPC default */
static ldm_replyt     reply;

static enum clnt_stat
my_comingsoon_5(
        CLIENT*     clnt,
        const prod_info* const  infop,
        u_int       pktsz,
        ldm_replyt* replyp)
{
        comingsoon_args arg;
        arg.infop = (prod_info*)infop;
        arg.pktsz = pktsz;

        memset(replyp, 0, sizeof(ldm_replyt));

        return clnt_call(clnt, COMINGSOON,
                xdr_comingsoon_args, (caddr_t)&arg,
                xdr_ldm_replyt, (caddr_t)replyp,
                timeo);
}

static enum clnt_stat
my_blkdata_5(CLIENT *clnt, datapkt *dpkp, ldm_replyt *replyp)
{
        memset(replyp, 0, sizeof(ldm_replyt));

        return clnt_call(clnt, BLKDATA,
                xdr_datapkt, (caddr_t)dpkp,
                xdr_ldm_replyt, (caddr_t)replyp,
                timeo);
}

static int
my_hiya_5(CLIENT *clnt, prod_class_t **clsspp)
{
        static ldm_replyt reply;
        enum clnt_stat rpc_stat;

        memset(&reply, 0, sizeof(ldm_replyt));

        rpc_stat = clnt_call(clnt, HIYA,
                xdr_prod_class, (caddr_t)*clsspp,
                xdr_ldm_replyt, (caddr_t)&reply,
                timeo);

        if(rpc_stat != RPC_SUCCESS)
        {
                log_error_q("hiya %s:  %s", remote, clnt_sperrno(rpc_stat));
                return ECONNABORTED; /* Perhaps could be more descriptive */
        }
        switch (reply.code) {
                case OK:
                        break;
                case SHUTTING_DOWN:
                        log_error_q("%s is shutting down", remote);
                        return ECONNABORTED;
                case DONT_SEND:
                case RESTART:
                case REDIRECT: /* TODO */
                default:
                        log_error_q("%s: unexpected reply type %s",
                                remote, s_ldm_errt(reply.code));
                        return ECONNABORTED;
                case RECLASS:
                        *clsspp = reply.ldm_replyt_u.newclssp;
                        clss_regcomp(*clsspp);
                        /* N.B. we use the downstream patterns */
                        if (log_is_enabled_info)
                                log_info_q("%s: reclass: %s",
                                        remote, s_prod_class(NULL, 0, *clsspp));
                        break;
        }
        return 0;
}

/**
 * Sends a HIYA message to an LDM-6 server.
 *
 * @param clnt          [in] The client-side handle.
 * @param clsspp        [in] The class of data-products to be sent.
 * @retval 0            Success.
 * @retval ECONNABORTED Failure (for many possible reasons).
 */
static int
my_hiya_6(CLIENT *clnt, prod_class_t **clsspp)
{
    static hiya_reply_t* reply;
    int                  error;
    
    reply = hiya_6(*clsspp, clnt);

    if (NULL == reply) {
        log_error_q("%s: HIYA_6 failure: %s", remote, clnt_errmsg(clnt));

        error = ECONNABORTED;
    }
    else {
        switch (reply->code) {
            case OK:
                max_hereis = reply->hiya_reply_t_u.max_hereis;
                error = 0;
                break;

            case SHUTTING_DOWN:
                log_error_q("%s: LDM shutting down", remote);
                error = ECONNABORTED;
                break;

            case BADPATTERN:
                log_error_q("%s: Bad product-class pattern", remote);
                error = ECONNABORTED;
                break;

            case DONT_SEND:
                log_error_q("%s: LDM says don't send", remote);
                error = ECONNABORTED;
                break;

            case RESEND:
                log_error_q("%s: LDM says resend (ain't gonna happen)", remote);
                error = ECONNABORTED;
                break;

            case RESTART:
                log_error_q("%s: LDM says restart (ain't gonna happen)", remote);
                error = ECONNABORTED;
                break;

            case REDIRECT:
                log_error_q("%s: LDM says redirect (ain't gonna happen)", remote);
                error = ECONNABORTED;
                break;

            case RECLASS:
                *clsspp = reply->hiya_reply_t_u.feedPar.prod_class;
                max_hereis = reply->hiya_reply_t_u.feedPar.max_hereis;
                clss_regcomp(*clsspp);
                /* N.B. we use the downstream patterns */
                if (log_is_enabled_info)
                    log_info_q("%s: reclass: %s",
                        remote, s_prod_class(NULL, 0, *clsspp));
                error = 0;
                break;
        }

        if (!error)
            log_debug("max_hereis = %u", max_hereis);
    }

    return error;
}


/* End Convenience functions */



/*
 * Send a product
 * from descriptor fd to clnt using LDM-5 protocols.
 */
static void
send_product_5(
    CLIENT* const          clnt,
    const char* const      statsdata,
    const prod_info* const infop)
{
        enum clnt_stat rpc_stat;
        datapkt        pkt;
        ssize_t unsent;
        size_t nread;
        const char* buf;

        rpc_stat = my_comingsoon_5(clnt, infop, DBUFMAX, &reply);
        if(rpc_stat != RPC_SUCCESS)
        {
                log_error_q("send_product_5: %s %s",
                        infop->ident,
                        clnt_sperrno(rpc_stat));
                return;
        }
        /* else */

        if(reply.code != OK)
        {
                if(reply.code == DONT_SEND)
                   log_info_q("send_product_5: %s: %s",
                        infop->ident,
                        s_ldm_errt(reply.code));
                else
                   log_error_q("send_product_5: %s: %s",
                        infop->ident,
                        s_ldm_errt(reply.code));
                return;
        }

        pkt.signaturep = (signaturet*)infop->signature;
        pkt.pktnum = 0;

        buf = statsdata;
        for(unsent = (ssize_t)infop->sz; unsent > 0;
                        unsent -= nread )
        {

                if(strlen(statsdata) > DBUFMAX)
                   nread = DBUFMAX;
                else
                   nread = strlen(statsdata);

                pkt.data.dbuf_len = (u_int)nread;
                pkt.data.dbuf_val = (char*)buf;         /* remove "const" */
                rpc_stat = my_blkdata_5(clnt, &pkt, &reply);
                if(rpc_stat != RPC_SUCCESS)
                        break;
                if(reply.code != OK)
                        break;
                pkt.pktnum++;
                buf += nread;
        }
}


/**
 * Sends a data-product to an LDM server using LDM-6 protocols. Calls log_error_q() on error.
 *
 * @param clnt      [in/out] The client-side handle.
 * @param statsdata [in] The data portion of the data-product.
 * @param infop     [in] The metadata portion of the data-product.
 */
static void
send_product_6(
    CLIENT* const          clnt,
    const char* const      statsdata,
    const prod_info* const infop)
{
    unsigned size = infop->sz;

    if (size <= max_hereis) {
        /*
         * The file is small enough to be sent in a single HEREIS message.
         */
        product product;

        log_debug("Sending file via HEREIS");

        product.info = *infop;
        product.data = (void*)statsdata;

        (void)hereis_6(&product, clnt);
        /*
         * The status will be RPC_TIMEDOUT unless an error occurs because the
         * RPC call uses asynchronous message-passing.
         */
        if (clnt_stat(clnt) != RPC_TIMEDOUT)
            log_error_q("%s: HEREIS_6 failure: %s", remote, clnt_errmsg(clnt));
    }
    else {
        /*
         * The product is so large that it must be sent via COMINGSOON/BLKDATA 
         * messages.
         */
        comingsoon_reply_t* reply;
        comingsoon_args     soonArg;

        log_debug("Sending file via COMINGSOON/BLKDATA");

        soonArg.infop = (prod_info*)infop;              /* remove "const" */
        soonArg.pktsz = size;
        
        reply = comingsoon_6(&soonArg, clnt);

        if (NULL == reply) {
            log_error_q("%s: COMINGSOON_6 failure: %s", remote, clnt_errmsg(clnt));
        }
        else {
            if (DONT_SEND == *reply) {
                if (log_is_enabled_info ||
                        log_is_enabled_debug)
                    log_info_q("Downstream LDM says don't send: %s",
                        s_prod_info(NULL, 0, infop,
                                log_is_enabled_debug));
            }
            else if (0 != *reply) {
                log_warning_q("Unexpected reply (%s) from downstream LDM: %s",
                    s_prod_info(NULL, 0, infop,
                            log_is_enabled_debug));
            }
            else {
                datapkt packet;

                /* Remove "const" from following. */
                packet.signaturep = (signaturet*)&infop->signature;
                packet.pktnum = 0;
                packet.data.dbuf_len = size;
                packet.data.dbuf_val = (void*)statsdata;

                (void)blkdata_6(&packet, clnt);
                /*
                 * The status will be RPC_TIMEDOUT unless an error occurs
                 * because the RPC call uses asynchronous message-passing.
                 */
                if (clnt_stat(clnt) != RPC_TIMEDOUT) {
                    log_error_q("%s: BLKDATA_6 failure: %s",
                        remote, clnt_errmsg(clnt));
                }
            }
        }
    }
}


/**
 * Sends a textual LDM data-product on an ONC RPC client handle.
 *
 * @param clnt          [in/out] The client handle.
 * @param clssp         [in] The class of the data-product.
 * @param origin        [in] The name of the host that created the data-product.
 * @param seq_start     [in] The sequence number of the data-product.
 * @param statsdata     [in] The data of the data-product.
 * @retval 0            Success.
 * @retval ENOMEM       Out-of-memory.
 * @retval ECONNABORTED The transmission attempt failed for some reason.
 */
static int
sendProd(
    CLIENT*       clnt,
    prod_class_t* clssp,
    const char*   origin,
    int*          seq_start,
    char*         statsdata)
{
    log_assert(clnt != NULL);

    int         status = 0;
    int         idlen;
    int         icnt;
    char        filename[255];
    char        feedid[80]="\0";
    char        prodo[HOSTNAMESIZE]="\0";
    char*       cpos;
    prod_info   info;
    MD5_CTX*    md5ctxp = NULL;
    prod_class_t* test_clssp = clssp;

    /* ldmproduct "filename" length = 255
     * log_assert that
     * sprintf(filename,"rtstats-%s/%s/%s/%s\0",PACKAGE_VERSION,
     * origin,feedid,prodo); will fit into allocated space.
     */
    log_assert ( ( strlen(PACKAGE_VERSION) + 2 * HOSTNAMESIZE + 80 + 9 ) < 255 );

    /*
     * time_insert time_arrive myname feedid product_origin
     */
    icnt = 0;   
    cpos = statsdata;

    while ( ( (cpos = (char *)strchr(cpos,' ')) != NULL ) && ( icnt < 4 ) ) {
       icnt++;
       while ( cpos[0] == ' ' ) cpos++;
       if ( icnt == 3 ) {
          idlen = strcspn(cpos," ");
          if(idlen > 79) idlen = 79;
          strncat(feedid,cpos,idlen);
       }
       if ( icnt == 4 ) {
          idlen = strcspn(cpos," ");
          if(idlen > HOSTNAMESIZE) idlen = HOSTNAMESIZE;
          strncat(prodo,cpos,idlen);
       }
    }

    /*
     * Allocate an MD5 context
     */
    md5ctxp = new_MD5_CTX();
    if(md5ctxp == NULL)
    {
        status = errno;
        log_syserr("new_md5_CTX failed");
    }
    else {
        if(signed_on_hiya) {
           log_debug("already signed on");
        }
        else {
            status = (*hiya)(clnt, &clssp); // my_hiya_6() calls log_error_q() on error

            if(status == 0)
                signed_on_hiya = 1;
        }

        if(status == 0) {
            /* These members are constant over the loop. */
            info.origin = (char*)origin; /* safe because "info.origin" isn't modified */
            info.feedtype = clssp->psa.psa_val->feedtype;

            info.seqno = *seq_start;

            sprintf(filename,"rtstats-%s/%s/%s/%s",PACKAGE_VERSION,origin,
                feedid,prodo);
            info.ident = filename;
            /*
             * ?? This could be the creation time of the file.
             */
            (void) set_timestamp(&info.arrival);

            /*
             * Checks 'arrival', 'feedtype', and 'ident'
             * against what the other guy has said he wants.
             */
            if(!prodInClass(clssp, &info))
            {
                log_info_q("%s doesn't want %s", remote, filename);
                if(test_clssp != clssp) free_prod_class(clssp);
            }
            else {
                log_info_q("Sending %s, %d bytes", filename, strlen(statsdata));
                
                MD5Init(md5ctxp);
                MD5Update(md5ctxp, (unsigned char *)statsdata,
                    (unsigned int)strlen(statsdata));
                MD5Final((unsigned char*)info.signature, md5ctxp);

                info.sz = (u_int)strlen(statsdata);

                (*send_product)(clnt, statsdata, &info);
            }
        }

        free_MD5_CTX(md5ctxp);  
    }                                   /* MD5 object allocated */
            
    if (test_clssp != clssp)
        free_prod_class(clssp);

    (*seq_start)++;

    return status;
}

/*
 * Public Interface:
 */

/**
 * Connects to the downstream LDM. Doesn't send any LDM message.
 *
 * @pre                           `clnt == NULL`
 * @retval 0                      Success.
 * @retval LDM_CLNT_UNKNOWN_HOST  Unknown downstream host. Error message logged.
 * @retval LDM_CLNT_TIMED_OUT     Call to downstream host timed-out. Error message logged.
 * @retval LDM_CLNT_BAD_VERSION   Downstream LDM isn't given version. Error message logged.
 * @retval LDM_CLNT_NO_CONNECT    Other connection-related error. Error message logged.
 * @retval LDM_CLNT_SYSTEM_ERROR  A fatal system-error occurred. Error message logged.
 * @post                          `clnt != NULL` on success
 */
int ldmsend_connect(void)
{
    int status = 0; // success

    log_assert(NULL == clnt);

    version = SIX;
    ErrorObj* error = ldm_clnttcp_create_vers(remote, remotePort, SIX, &clnt, NULL, NULL);

    if (error && LDM_CLNT_BAD_VERSION == err_code(error)) {
        err_free(error);
        version = FIVE;
        error = ldm_clnttcp_create_vers(remote, LDM_PORT, FIVE, &clnt, NULL, NULL);
    }

    if (error) {
        err_log_and_free(error, ERR_ERROR);
        status = err_code(error);
    }
    else {
        log_assert(clnt);
        log_debug("version = %u", version);
        signed_on_hiya = 0;
    }

    return status;
}


/**
 * Disconnects from the downstream LDM.
 */
void ldmsend_disconnect(void)
{
    if (clnt != NULL) {
        auth_destroy(clnt->cl_auth);
        clnt_destroy(clnt);
        clnt = NULL;
    }
}

/**
 * Sends textual data to an LDM server.
 *
 * @param[in] statsdata     The data to be sent.
 * @param[in] myname        The name of the local host.
 * @retval    0             Success.
 * @retval    ENOMEM        Out-of-memory.
 * @retval    ECONNABORTED  The transmission attempt failed for some reason.
 */
int ldmsend_send(
        char*             statsdata,
        const char* const myname)
{
    prod_class_t    clss;
    prod_spec       spec;
    static int      seq_start = 0;
    int             status = 0; /* success */

    clss.from = TS_ZERO;
    clss.to = TS_ENDT;
    clss.psa.psa_len = 1;
    clss.psa.psa_val = &spec;
    spec.feedtype = DEFAULT_FEEDTYPE;
    spec.pattern = ".*";

    log_assert(clnt);

    if (FIVE == version) {
        hiya = my_hiya_5;
        send_product = send_product_5;
        nullproc = NULL;
    }
    else if (SIX == version) {
        hiya = my_hiya_6;
        send_product = send_product_6; // Calls log_error_q() on error
        nullproc = nullproc_6;
    }
    else {
        log_error("Unsupported LDM version: %u", version);
        status = ECONNABORTED;
    }

    if (status == 0) {
        status = sendProd(clnt, &clss, myname, &seq_start, statsdata);

        if (seq_start > 999)
            seq_start = 0;
    }

    return status;
}
