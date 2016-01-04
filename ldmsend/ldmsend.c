/* 
 * Sends files to an LDM as data-products.
 *
 * See file ../COPYRIGHT for copying and redistribution conditions.
 */

#define TIRPC
#include <config.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <rpc/rpc.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "exitStatus.h"
#include "ldm.h"        /* needed by following */
#include "LdmProxy.h"
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
#include "mylog.h"

#ifdef NO_ATEXIT
#include "atexit.h"
#endif

#ifndef DEFAULT_FEEDTYPE
        /* default to using the "experimental" feedtype */
#define DEFAULT_FEEDTYPE EXP
#endif

static const char*      remote = NULL; /* hostname of data remote */
static LdmProxy*        ldmProxy = NULL;

static void
usage(
    char *av0 /*  id string */)
{
    (void)fprintf(stderr,
            "Usage: %s [options] filename ...\n\tOptions:\n", av0);
    (void)fprintf(stderr,
            "\t-v           Verbose, tell me about each product\n");
    (void)fprintf(stderr,
            "\t-l logfile   log to a file rather than stderr\n");
    (void)fprintf(stderr,
            "\t-h remote    remote service host, defaults to \"localhost\"\n");
    (void)fprintf(stderr,
            "\t-s seqno     set initial product sequence number to \"seqno\", defaults to 0\n");
    (void)fprintf(stderr,
            "\t-f feedtype  assert your feed type as \"feedtype\", defaults to \"%s\"\n", s_feedtypet(DEFAULT_FEEDTYPE));
    exit(1);
}

void
cleanup(void)
{
    if (ldmProxy != NULL) {
        lp_free(ldmProxy);
        ldmProxy = NULL;
    }
    (void)mylog_fini();
}

static void
signal_handler(int sig)
{
#ifdef SVR3SIGNALS
    /* 
     * Some systems reset handler to SIG_DFL upon entry to handler.
     * In that case, we reregister our handler.
     */
    (void) signal(sig, signal_handler);
#endif
    switch(sig) {
      case SIGINT :
         exit(1);
         return;
      case SIGTERM :
         done = 1;
         return;
      case SIGPIPE :
         exit(1);
    }
}

static void
set_sigactions(void)
{
#ifdef HAVE_SIGACTION
    struct sigaction sigact;

    sigact.sa_handler = signal_handler;
    sigemptyset(&sigact.sa_mask);

    sigact.sa_flags = 0;
    (void) sigaction(SIGINT, &sigact, NULL);

    sigact.sa_flags |= SA_RESTART;
    (void) sigaction(SIGTERM, &sigact, NULL);
    (void) sigaction(SIGPIPE, &sigact, NULL);

    sigact.sa_handler = SIG_IGN;
    (void) sigaction(SIGALRM, &sigact, NULL);
#else
    (void) signal(SIGINT, signal_handler);
    (void) signal(SIGTERM, signal_handler);
    (void) signal(SIGPIPE, signal_handler);
    (void) signal(SIGALRM, SIG_IGN);
#endif
}

static int
fd_md5(MD5_CTX *md5ctxp, int fd, off_t st_size, signaturet signature)
{
    ssize_t     nread;
    char        buf[8192];

    MD5Init(md5ctxp);

    for (; exitIfDone(1) && st_size > 0; st_size -= (off_t)nread) {
        nread = read(fd, buf, sizeof(buf));
        if(nread <= 0) {
            mylog_syserr("fd_md5: read");
            return -1;
        } /* else */
        MD5Update(md5ctxp, (unsigned char *)buf, (unsigned int)nread);
    }

    MD5Final((unsigned char*)signature, md5ctxp);
    return 0;
}

/*
 * Sends a single, open file to an LDM as a data-product. The number of bytes
 * to be sent is specified by the data-product's metadata. The bytes start at
 * the beginning of the file.
 *
 * Arguments:
 *      proxy           The LDM proxy data-structure.
 *      fd              The file-descriptor open on the file to be sent.
 *      info            The data-product's metadata. Must be completely set.
 *
 * Returns:
 *      0                       Success.
 *      SYSTEM_ERROR            O/S failure. "mylog_add()" called.
 *      CONNECTION_ABORTED      The connection was aborted. "mylog_add()"
 *                              called.
 */
static int
send_product(
    LdmProxy*           proxy,
    int                 fd,
    prod_info* const    info)
{
    int                 status;
    product             product;

    product.info = *info;
    product.data = mmap(NULL, info->sz, PROT_READ, MAP_PRIVATE, fd, 0);

    if (MAP_FAILED == product.data) {
        mylog_syserr("Couldn't memory-map file");
        status = SYSTEM_ERROR;
    }
    else {
        status = lp_send(proxy, &product);
        if (LP_UNWANTED == status) {
            mylog_notice("Unwanted product: %s", s_prod_info(NULL, 0, info,
                        mylog_is_enabled_debug));
            status = 0;
        }
        (void)munmap(product.data, info->sz);
    }                                           /* file is memory-mapped */

    return status;
}

/*
 * Sends a list of files to the LDM as data-products.
 *
 * Arguments:
 *      ldmProxy        The LDM proxy data-structure.
 *      offer           The description of the class of data-products that this
 *                      process is willing to send.
 *      origin          The identifier of the host that created the
 *                      data-products (typically the host running this program).
 *      seq_start       The starting value of the data-product sequence number.
 *      nfiles          The number of files to send.
 *      filenames       The pathnames of the files to send.
 *
 * Returns:
 *      0                       Success.
 *      SYSTEM_ERROR            O/S failure. "mylog_add()" called.
 *      CONNECTION_ABORTED      The connection was aborted. "mylog_add()"        *                              called.
 */
static int
ldmsend(
    LdmProxy*           ldmProxy,
    prod_class_t*       offer,
    char*               origin,
    int                 seq_start,
    int                 nfiles,
    char*               filenames[])
{
    int                 status = 0;
    char*               filename;
    int                 fd;
    struct stat         statb;
    prod_info           info;
    MD5_CTX*            md5ctxp = NULL;
    prod_class_t*       want;

    /*
     * Allocate an MD5 context
     */
    md5ctxp = new_MD5_CTX();
    if (md5ctxp == NULL)
    {
        mylog_syserr("new_md5_CTX failed");
        return SYSTEM_ERROR;
    }

    status = lp_hiya(ldmProxy, offer, &want);

    if (status != 0) {
        status = CONNECTION_ABORTED;
    }
    else {
        /* These members are constant over the loop. */
        info.origin = origin;
        info.feedtype = offer->psa.psa_val->feedtype;

        for (info.seqno = seq_start; exitIfDone(1) && nfiles > 0;
                filenames++, nfiles--, info.seqno++) {
            filename = *filenames;
            info.ident = filename;
            /*
             * ?? This could be the creation time of the file.
             */
            (void) set_timestamp(&info.arrival);

            /*
             * Checks 'arrival', 'feedtype', and 'ident'
             * against what the other guy has said he wants.
             */
            if (!prodInClass(offer, &info)) {
                mylog_info("Not going to send %s", filename);
                continue;       
            }
            if (!prodInClass(want, &info)) {
                mylog_info("%s doesn't want %s", lp_host(ldmProxy), filename);
                continue;       
            }

            fd = open(filename, O_RDONLY, 0);
            if (fd == -1) {
                mylog_syserr("open: %s", filename);
                continue;
            }

            if (fstat(fd, &statb) == -1) {
                mylog_syserr("fstat: %s", filename);
                (void) close(fd);
                continue;
            }

            mylog_info("Sending %s, %d bytes", filename, statb.st_size);
            
            /* These members, and seqno, vary over the loop. */
            if (fd_md5(md5ctxp, fd, statb.st_size, info.signature) != 0) {
                (void) close(fd);
                continue;
            }
            if (lseek(fd, 0, SEEK_SET) == (off_t)-1) {
                mylog_syserr("rewind: %s", filename);
                (void) close(fd);
                continue;
            }

            info.sz = (u_int)statb.st_size;

            (void)exitIfDone(1);

            status = send_product(ldmProxy, fd, &info);

            (void) close(fd);

            if (0 != status) {
                mylog_add("Couldn't send file \"%s\" to LDM", filename);
                break;
            }
        }                                       /* file loop */

        if (lp_flush(ldmProxy))
            mylog_add("Couldn't flush connection");

        free_prod_class(want);
    }                                           /* HIYA succeeded */

    free_MD5_CTX(md5ctxp);  
    return status;
}


/*
 * Returns:
 *      0               Success
 *      SYSTEM_ERROR    O/S failure. "mylog_add()" called.
 *      LP_TIMEDOUT     The RPC call timed-out. "mylog_add()" called.
 *      LP_RPC_ERROR    RPC error. "mylog_add()" called.
 *      LP_LDM_ERROR    LDM error. "mylog_add()" called.
 */
int
main(
    int         ac,
    char*       av[])
{
    char            myname[_POSIX_HOST_NAME_MAX];
    char*           progname = av[0];
    prod_class_t    clss;
    prod_spec       spec;
    int             seq_start = 0;
    int             status;
    ErrorObj*       error;
    unsigned        remotePort = LDM_PORT;

    /*
     * Set up error logging
     */
    (void)mylog_init(progname);

    remote = "localhost";

    (void)set_timestamp(&clss.from);
    clss.to = TS_ENDT;
    clss.psa.psa_len = 1;
    clss.psa.psa_val = &spec;
    spec.feedtype = DEFAULT_FEEDTYPE;
    spec.pattern = ".*";

    {
        extern int optind;
        extern char *optarg;
        int ch;

        while ((ch = getopt(ac, av, "vxl:h:f:P:s:")) != EOF)
            switch (ch) {
            case 'v':
                (void)mylog_set_level(MYLOG_LEVEL_INFO);
                break;
            case 'x':
                (void)mylog_set_level(MYLOG_LEVEL_DEBUG);
                break;
            case 'l':
                (void)mylog_set_output(optarg);
                break;
            case 'h':
                remote = optarg;
                break;
            case 'f':
                spec.feedtype = atofeedtypet(optarg);
                if(spec.feedtype == NONE)
                {
                    fprintf(stderr, "Unknown feedtype \"%s\"\n",
                            optarg);
                        usage(progname);        
                }
                break;
            case 'P': {
                char*       suffix = "";
                long        port;

                errno = 0;
                port = strtol(optarg, &suffix, 0);

                if (0 != errno || 0 != *suffix ||
                    0 >= port || 0xffff < port) {

                    (void)fprintf(stderr, "%s: invalid port %s\n",
                         av[0], optarg);
                    usage(av[0]);   
                }

                remotePort = (unsigned)port;

                break;
            }
            case 's':
                seq_start = atoi(optarg);
                break;
            case '?':
                usage(progname);
                break;
            }

        ac -= optind; av += optind;

        if(ac < 1) usage(progname);
    }

    /*
     * Register the exit handler
     */
    if(atexit(cleanup) != 0)
    {
        mylog_syserr("atexit");
        exit(SYSTEM_ERROR);
    }

    /*
     * Set up signal handlers
     */
    set_sigactions();

    (void) strncpy(myname, ghostname(), sizeof(myname));
    myname[sizeof(myname)-1] = 0;

    (void)exitIfDone(INTERRUPTED);

    /*
     * Connect to the LDM.
     */
    status = lp_new(remote, &ldmProxy);

    if (0 != status) {
        mylog_flush_error();
        status = (LP_SYSTEM == status)
            ? SYSTEM_ERROR
            : CONNECTION_ABORTED;
    }
    else {
        mylog_debug("version %u", lp_version(ldmProxy));

        status = ldmsend(ldmProxy, &clss, myname, seq_start, ac, av);

        if (0 != status)
            mylog_flush_error();

        lp_free(ldmProxy);
        ldmProxy = NULL;
    }                                       /* "ldmProxy" allocated */

    return status; 
}
