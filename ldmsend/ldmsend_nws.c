/* 
 * Sends files to an LDM as data-products.
 *
 * See file ../COPYRIGHT for copying and redistribution conditions.
 */

/* 09/18/2015: Modified by NWS to check for queue insert and resend if not successful */
/* Last modified: 09/25/2015 */

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
#include <regex.h>

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
#include "ulog.h"
#include "log.h"
#include "RegularExpressions.h"
#include "ldm5.h"
#include "ldm5_clnt.h"

#ifdef NO_ATEXIT
#include "atexit.h"
#endif

#ifndef DEFAULT_FEEDTYPE
/* default to using the "experimental" feedtype */
#define DEFAULT_FEEDTYPE EXP
#endif

/* Added for Notification */
#ifndef DEFAULT_REMOTE
#define DEFAULT_REMOTE "localhost"
#endif
#ifndef DEFAULT_TIMEO
#define DEFAULT_TIMEO  25
#endif
#ifndef DEFAULT_TOTALTIMEO
#define DEFAULT_TOTALTIMEO  (12*DEFAULT_TIMEO)
#endif
#ifndef DEFAULT_PATTERN
#define DEFAULT_PATTERN ".*"
#endif
#ifndef DEFAULT_RETRIES
#define DEFAULT_RETRIES 3
#endif
#ifndef DEFAULT_RETRIES_WAIT_SECS
#define DEFAULT_RETRIES_WAIT_SECS 300
#endif

static char **input_filenames;
static unsigned num_input_filenames;
static prod_class notifyme_clss;
static ldm_replyt reply = { OK };
static const char *remote = NULL; /* hostname of data remote */
static LdmProxy *ldmProxy = NULL;
static int hits;
static int misses;
static int debug;
static int verbose;
static int error_level;
static int user_seq_start;

static void usage(char *av0 /*  id string */) {
  (void)fprintf(stderr,
		"Usage: %s [options] filename ...\n\tOptions:\n", av0);
  (void)fprintf(stderr,
		"\t-v             Verbose, tell me about each product\n");
  (void)fprintf(stderr,
		"\t-l logfile     log to a file rather than stderr\n");
  (void)fprintf(stderr,
		"\t-h remote      remote service host, defaults to \"localhost\"\n");
  (void)fprintf(stderr,
		"\t-s seqno       set initial product sequence number to \"seqno\", defaults to 0\n");
  (void)fprintf(stderr,
		"\t-f feedtype    assert your feed type as \"feedtype\", defaults to \"%s\"\n", s_feedtypet(DEFAULT_FEEDTYPE));

  (void)fprintf(stderr,
                "\t-n             Enable notification to verify upload\n");

  (void)fprintf(stderr,
                "\t-x             Debug mode for notification\n");
  (void)fprintf(stderr,
                "\t-p pattern     Notification products matching \"pattern\" (default \"%s\")\n", DEFAULT_PATTERN);
  (void)fprintf(stderr,
                "\t-o offset      Set notification the \"from\" time offset secs before now\n");
  (void)fprintf(stderr,
                "\t-t timeout     Set RPC timeout to \"timeout\" seconds (default %d)\n",
		DEFAULT_TIMEO);

  (void)fprintf(stderr,
                "\t-r retries     Number of send retries (default %d)\n",
		DEFAULT_RETRIES);
  
  (void)fprintf(stderr,
                "\t-R waits       Num seconds to wait between retries fails (default %d)\n",
		DEFAULT_RETRIES_WAIT_SECS);

  (void)fprintf(stderr,
                "\t-T TotalTimeo  Give up notification after this many secs (default %d)\n",
		DEFAULT_TOTALTIMEO);
  (void)fprintf(stderr, "\n");
  exit(1);
}

void cleanup(void)
{
  if (ldmProxy != NULL) {
    if(debug) unotice("Freeing ldmProxy resources");
    lp_free(ldmProxy);
    ldmProxy = NULL;
  }
  unotice("Exiting LDM send with error level %d", error_level);
  if(verbose) {
    /* Code needed by NWS to send alert messages if LDM send fails */
    if(error_level == 0) {
      printf("PASS");
    }
    else {
      printf("FAIL");
    }
  }

  (void) closeulog();
}

static void signal_handler(int sig)
{
#ifdef SVR3SIGNALS
  /* 
   * Some systems reset handler to SIG_DFL upon entry to handler.
   * In that case, we reregister our handler.
   */
  (void) signal(sig, signal_handler);
#endif
  switch(sig) {
    case SIGHUP :
      return;
    case SIGINT :
      /*FALLTHROUGH*/
    case SIGTERM :
      done = !0;
      return;
    case SIGUSR1 :
      return;
    case SIGUSR2 :
      toggleulogpri(LOG_INFO);
      return;
    case SIGPIPE :
      return;
  }
}

static void set_sigactions(void)
{
#ifdef HAVE_SIGACTION
  struct sigaction sigact;
  
  sigact.sa_handler = signal_handler;
  sigemptyset(&sigact.sa_mask);
  sigact.sa_flags = 0;
  
  (void) sigaction(SIGHUP, &sigact, NULL);
  (void) sigaction(SIGINT, &sigact, NULL);
  (void) sigaction(SIGTERM, &sigact, NULL);
  (void) sigaction(SIGUSR1, &sigact, NULL);
  (void) sigaction(SIGUSR2, &sigact, NULL);
  (void) sigaction(SIGPIPE, &sigact, NULL);
#else
  (void) signal(SIGHUP, signal_handler);
  (void) signal(SIGINT, signal_handler);
  (void) signal(SIGTERM, signal_handler);
  (void) signal(SIGUSR1, signal_handler);
  (void) signal(SIGUSR2, signal_handler);
  (void) signal(SIGPIPE, signal_handler);
#endif
}

static int fd_md5(MD5_CTX *md5ctxp, int fd, off_t st_size, signaturet signature)
{
  ssize_t     nread;
  char        buf[8192];
  
  MD5Init(md5ctxp);
  
  for (; exitIfDone(1) && st_size > 0; st_size -= (off_t)nread) {
    nread = read(fd, buf, sizeof(buf));
    if(nread <= 0) {
      serror("fd_md5: read");
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
 *      SYSTEM_ERROR            O/S failure. "log_start()" called.
 *      CONNECTION_ABORTED      The connection was aborted. "log_start()"
 *                              called.
 */
static int send_product(LdmProxy *proxy, int fd, prod_info* const info)
{
  int                 status;
  product             product;
  
  product.info = *info;
  product.data = mmap(NULL, info->sz, PROT_READ, MAP_PRIVATE, fd, 0);
  
  if (MAP_FAILED == product.data) {
    LOG_SERROR0("Couldn't memory-map file");
    status = SYSTEM_ERROR;
  }
  else {
    status = lp_send(proxy, &product);
    if (LP_UNWANTED == status) {
      unotice("Unwanted product: %s", s_prod_info(NULL, 0, info, ulogIsDebug()));
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
 *      SYSTEM_ERROR            O/S failure. "log_start()" called.
 *      CONNECTION_ABORTED      The connection was aborted. "log_start()"        *                              called.
 */
static int ldmsend(LdmProxy*           ldmProxy,
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
  if (md5ctxp == NULL) {
    LOG_SERROR0("new_md5_CTX failed");
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
	uinfo("Not going to send %s", filename);
	continue;       
      }
      if (!prodInClass(want, &info)) {
	uinfo("%s doesn't want %s", lp_host(ldmProxy), filename);
	continue;       
      }
      
      fd = open(filename, O_RDONLY, 0);
      if (fd == -1) {
	serror("open: %s", filename);
	continue;
      }
      
      if (fstat(fd, &statb) == -1) {
	serror("fstat: %s", filename);
	(void) close(fd);
	continue;
      }
      
      uinfo("Sending %s, %d bytes", filename, statb.st_size);
      
      /* These members, and seqno, vary over the loop. */
      if (fd_md5(md5ctxp, fd, statb.st_size, info.signature) != 0) {
	(void) close(fd);
	continue;
      }
      if (lseek(fd, 0, SEEK_SET) == (off_t)-1) {
	serror("rewind: %s", filename);
	(void) close(fd);
	continue;
      }
      
      info.sz = (u_int)statb.st_size;
      
      (void)exitIfDone(1);
      
      status = send_product(ldmProxy, fd, &info);
      
      (void) close(fd);
      
      if (0 != status) {
	LOG_ADD1("Couldn't send file \"%s\" to LDM", filename);
	break;
      }
    }                                       /* file loop */
    
    if (lp_flush(ldmProxy))
      log_add("Couldn't flush connection");
    
    free_prod_class(want);
  }                                           /* HIYA succeeded */
  
  free_MD5_CTX(md5ctxp);  
  return status;
}

/*
 * The RPC dispatch routine for this program.
 * Registered as a callback by svc_register() below.
 * Note that only NULLPROC and NOTIFICATION rpc procs are
 * handled by this program.
 */
static void notifymeprog_5(struct svc_req *rqstp, SVCXPRT *transp) 
{
  static prod_info notice;
  int i, rv;
  char *filename;
  char **filenames;
  struct stat buf;
  
  switch (rqstp->rq_proc) {
    
    case NULLPROC:
      (void)svc_sendreply(transp, (xdrproc_t)xdr_void, (caddr_t)NULL);
      return;
      
    case NOTIFICATION:
      (void) memset((char*)&notice, 0, sizeof(notice));
      if (!svc_getargs(transp, (xdrproc_t)xdr_prod_info, (caddr_t)&notice)) {
	svcerr_decode(transp);
	return;
      }

      /*
       * Update the request filter with the timestamp
       * we just recieved.
       * N.B.: There can still be duplicates after
       * a reconnect.
       */
      notifyme_clss.from = notice.arrival;
      timestamp_incr(&notifyme_clss.from);
      
      /* 
       * your code here, example just logs it 
       */
      uinfo("%s", s_prod_info(NULL, 0, &notice, ulogIsDebug()));

      if(!svc_sendreply(transp, (xdrproc_t)xdr_ldm_replyt, (caddr_t) &reply)) {
	svcerr_systemerr(transp);
      }

       filenames = input_filenames;
      for(i = 0; i < num_input_filenames; i++, filenames++) {
	filename = *filenames;
	if(debug) unotice("Filename[%d]: %s",  i, filename);
	rv = strcmp(filename, notice.ident);
	if(rv == 0) {
	  unotice("Found %s in LDM queue",  filename);
	  stat(filename, &buf);
	   buf.st_size;
	   if(buf.st_size == notice.sz) {
	     if(debug) unotice("File sizes match, %d", notice.sz);
	     if(notice.seqno >= user_seq_start) {
	       if(debug) unotice("Initial product sequence, %d", notice.seqno);
	       hits++;
	     }
	     else {
	       unotice("Initial product sequence number do not match, %d %d", notice.seqno, user_seq_start);
	     }
	   }
	   else {
	     unotice("File sizes do not match, %d", notice.sz);
	     misses++;
	   }
	}
      }

      if(debug) {
	unotice("size: %d", notice.sz);
	unotice("origin: %s",  notice.origin);
	unotice("arrival: %s", ctime(&notice.arrival.tv_sec));
	unotice("feedtype: %s", s_feedtypet(notice.feedtype));
	unotice("seqno: %d",  notice.seqno);
	unotice("ident: %s",  notice.ident);
	unotice("signature: %d\n", notice.signature);
	unotice("size: %d", notice.sz);
      }

      if(!svc_freeargs(transp, xdr_prod_info, (caddr_t) &notice)) {
	uerror("unable to free arguments");
	error_level = 255;
	exit(error_level);
      }
      
    default:
      svcerr_noproc(transp);
      return;
  }
}

/*
 * Returns:
 *      0               Success
 *      SYSTEM_ERROR    O/S failure. "log_start()" called.
 *      LP_TIMEDOUT     The RPC call timed-out. "log_start()" called.
 *      LP_RPC_ERROR    RPC error. "log_start()" called.
 *      LP_LDM_ERROR    LDM error. "log_start()" called.
 */
int main(int ac, char *av[])
{
  char            myname[_POSIX_HOST_NAME_MAX];
  char*           progname = av[0];
  char*           logfname;
  prod_class_t    clss;
  prod_class_t *clssp;
  prod_spec       spec;
  prod_spec       notifyme_spec;
  int             status;
  ErrorObj*       error;
  unsigned        remotePort = LDM_PORT;
  unsigned timeo = DEFAULT_TIMEO; 
  unsigned interval = DEFAULT_TIMEO; 
  unsigned TotalTimeo = DEFAULT_TOTALTIMEO;
  unsigned retries = DEFAULT_RETRIES;
  unsigned retries_wait_secs = DEFAULT_RETRIES_WAIT_SECS;
  unsigned retries_buf;
  int logmask = (LOG_MASK(LOG_ERR)|LOG_MASK(LOG_WARNING)|LOG_MASK(LOG_NOTICE));
  int notifyme = 0;
  user_seq_start = 0;
  debug = 0;
  verbose = 0;
  error_level = 0;
  hits = 0;
  misses = 0;
  num_input_filenames = 0;
  logfname = "-";
  remote = "localhost";
  
  if(set_timestamp(&clss.from) != 0) {
    fprintf(stderr, "Couldn't set ldmsend timestamp\n");
    exit(1);
  }
    
  if(set_timestamp(&notifyme_clss.from) != 0) {
    fprintf(stderr, "Couldn't set notification timestamp\n");
    exit(1);
  }

  clss.to = TS_ENDT;
  clss.psa.psa_len = 1;
  clss.psa.psa_val = &spec;
  spec.feedtype = DEFAULT_FEEDTYPE;
  spec.pattern = ".*";

  notifyme_spec.feedtype = DEFAULT_FEEDTYPE;
  notifyme_spec.pattern = DEFAULT_PATTERN;
  
  { /* Start of get options */
    extern int optind;
    extern char *optarg;
    int ch;
    int fterr;
    
    while ((ch = getopt(ac, av, "vxnl:h:f:P:s:o:p:t:T:r:R:")) != EOF)
      switch (ch) {
	case 'v':
	  logmask |= LOG_MASK(LOG_INFO);
	  verbose = 1;
	  break;
	case 'x':
	  debug = 1;
	  logmask |= LOG_MASK(LOG_DEBUG);
	  break;
	case 'l':
	  logfname = optarg;
	  break;
	case 'h':
	  remote = optarg;
	  break;
	case 'f':
	  spec.feedtype = atofeedtypet(optarg);
	  if(spec.feedtype == NONE) {
	    fprintf(stderr, "Unknown ldmsend feedtype \"%s\"\n", optarg);
	    usage(progname);        
	  }
	  fterr = strfeedtypet(optarg, &notifyme_spec.feedtype);
	  if(fterr != FEEDTYPE_OK) {
	    fprintf(stderr, "Bad notification feedtype \"%s\", %s\n",
		    optarg, strfeederr(fterr));
	    usage(av[0]);   
	  }
	  break;
	case 'P': {
	  char *suffix = "";
	  long port;
	  errno = 0;
	  port = strtol(optarg, &suffix, 0);
	  
	  if (0 != errno || 0 != *suffix || 0 >= port || 0xffff < port) {
	    (void)fprintf(stderr, "%s: invalid port %s\n", av[0], optarg);
	    usage(av[0]);   
	  }
	  remotePort = (unsigned)port;
	  break;
	}
	case 's':
	  user_seq_start = atoi(optarg);
	  break;
	case 'n':
	  notifyme = 1;
	  break;
	case 'p':
	  notifyme_spec.pattern = optarg;
	  /* compiled below */
	  break;
	case 'o':
	  notifyme_clss.from.tv_sec -= atoi(optarg);
	  break;
	case 'T':
	  TotalTimeo = atoi(optarg);
	  if(TotalTimeo == 0) {
	    fprintf(stderr, "%s: invalid TotalTimeo %s", av[0], optarg);
	    usage(av[0]);   
	  }
	  break;
	case 't':
	  timeo = (unsigned)atoi(optarg);
	  if(timeo == 0 || timeo > 32767) {
	    fprintf(stderr, "%s: invalid timeout %s", av[0], optarg);
	    usage(av[0]);   
	  }
	  break;
	case 'r':
	  retries = atoi(optarg);
	  if(retries <= 0) {
	    fprintf(stderr, "%s: invalid retry -r value %s", av[0], optarg);
	    usage(av[0]);   
	  }
	  break;
	case 'R':
	  retries_wait_secs = atoi(optarg);
	  if(retries_wait_secs <= 0) {
	    fprintf(stderr, "%s: invalid retry wait -R value %s", av[0], optarg);
	    usage(av[0]);   
	  }
	  break;
	case '?':
	  usage(progname);
	  break;
      }
    
    ac -= optind; av += optind;
    
    if(ac < 1) usage(progname);
    (void) setulogmask(logmask);

    if (re_isPathological(notifyme_spec.pattern)) {
      fprintf(stderr, "Adjusting pathological regular-expression: "
	      "\"%s\"\n", notifyme_spec.pattern);
      re_vetSpec(notifyme_spec.pattern);
    }
    status = regcomp(&notifyme_spec.rgx, notifyme_spec.pattern, REG_EXTENDED|REG_NOSUB);
    if(status != 0) {
      fprintf(stderr, "Bad regular expression \"%s\"\n", notifyme_spec.pattern);
      usage(av[0]);
    }
    
    if((TotalTimeo < timeo) && (notifyme)) {
      fprintf(stderr, "TotalTimeo %u < timeo %u\n",
	      TotalTimeo, timeo);
      usage(av[0]);
    }
    
  } /* End of get options */
  
  /*
   * Set up error logging
   */
  (void) openulog(ubasename(progname), LOG_NOTIME, LOG_LDM, logfname);

  /*
   * Register the exit handler
   */
  if(atexit(cleanup) != 0) {
    serror("atexit");
    exit(SYSTEM_ERROR);
  }

  /*
   * Set up signal handlers
   */
  set_sigactions();
  
  (void) strncpy(myname, ghostname(), sizeof(myname));
  myname[sizeof(myname)-1] = 0;

  num_input_filenames = ac;
  input_filenames = av;
  
  /*
   * Connect to the LDM.
   */
  retries_buf = retries;
  while(retries_buf--) {
    status = lp_new(remote, &ldmProxy);
    if(status != 0) {
      log_log(LOG_ERR);
      if(retries_buf == 0) break;
      unotice("Retry in %d second(s)", retries_wait_secs);
      sleep(retries_wait_secs);
    }
    else {
      break;
    }
  }
  
  if(status != 0) {
    if (ldmProxy != NULL) {
      lp_free(ldmProxy);
      ldmProxy = NULL;
    }
    error_level = (LP_SYSTEM == status) ? SYSTEM_ERROR : CONNECTION_ABORTED;
    exit(error_level);
  }

  udebug("version %u", lp_version(ldmProxy));

  retries_buf = retries;
  while(retries_buf--) {
    status = ldmsend(ldmProxy, &clss, myname, user_seq_start, ac, av);
    if(status != 0) {
      log_log(LOG_ERR);
      if(retries_buf == 0) break;
      unotice("Retry in %d second(s)", retries_wait_secs);
      sleep(retries_wait_secs);
    }
    else {
      break;
    }
  }

  if(status != 0) {
    lp_free(ldmProxy);
    ldmProxy = NULL;
    error_level = status;
    exit(error_level);
  }

  if(notifyme) {
    notifyme_clss.to = TS_ENDT;
    notifyme_clss.psa.psa_len = 1;
    notifyme_clss.psa.psa_val = &notifyme_spec;
    unotice("Starting Up: %s: %s", remote, s_prod_class(NULL, 0, &notifyme_clss));
    clssp = &notifyme_clss;
    
    retries_buf = retries;
    while(retries_buf--) {

      unotice("Start notify");
      status = forn5(NOTIFYME, remote, &clssp, timeo, TotalTimeo, notifymeprog_5);
      error_level = status;
      
      if(done) {
	unotice("No files are in LDM queue");
	error_level = 256;
	(void)exitIfDone(error_level);      
      }
     
      switch(status) {
	/* problems with remote, retry */       
	case ECONNABORTED:
	case ECONNRESET:
	case ETIMEDOUT:
	case ECONNREFUSED:
	  break;
	case 0:
	  /* assert(done); */
	  break;
	default:
	  /* some wierd error */
	  done = 1;
      }

      /* Account for multiple files in queue with same name and seq number */
      if(hits  > num_input_filenames) hits = num_input_filenames;


      if(hits == num_input_filenames) {
	error_level = 0;
	unotice("%d file(s) uploaded succefully", num_input_filenames);
	done = 1;
	break;
      }

      if((misses > 0) || (hits != num_input_filenames)) {
	if(debug) unotice("%d file(s) did not upload", misses);
	unotice("%d file(s) uploaded", hits);
	if(retries_buf > 0) {
	  unotice("Retry in %d second(s)", retries_wait_secs);
	  sleep(retries_wait_secs);
	  status = ldmsend(ldmProxy, &clss, myname, user_seq_start, ac, av);
	  if(status != 0) {
	    log_log(LOG_ERR);
	  }
	  continue;
	}
	else {
	  serror("No file(s) were uploaded");
	  error_level = 256;
	  done = 1;
	  break;
	}
      }
     
    }
  }

  return error_level;
}
