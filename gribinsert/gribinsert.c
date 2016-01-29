/*
 *   Copyright 2011, University Corporation for Atmospheric Research
 *
 *   See file COPYRIGHT in the top-level source-directory for copying and
 *   redistribution conditions.
 */
#include <config.h>

/*
 * Convert files to ldm "products" and insert in local product-queue
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <rpc/rpc.h>
#include <signal.h>
#ifndef NO_MMAP
#include <sys/mman.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#include "ldm.h"
#include "pq.h"
#include "globals.h"
#include "atofeedt.h"
#include "ldmprint.h"
#include "inetutil.h"
#include "log.h"
#include "md5.h"
#include "gribinsert.h"

#ifdef NO_ATEXIT
#include "atexit.h"
#endif

/* N.B.: assumes hostname doesn't change during program execution :-) */
static char myname[HOSTNAMESIZE];
static feedtypet feedtype = EXP;

typedef struct stat_info
{
  int seqno;
  char *prodname;
  int prodsz;
  int insertstatus;
  struct stat_info *next;
} stat_info;



static void
usage (char *av0		/*  id string */
  )
{
  (void) fprintf (stderr,
		  "Usage: %s [options] filename ...\n\tOptions:\n", av0);
  (void) fprintf (stderr,
		  "\t-v           Verbose, tell me about each product\n");
  (void) fprintf (stderr,
		  "\t-l logfile   log to a file rather than stderr\n");
  (void) fprintf (stderr, "\t-q queue     default \"%s\"\n", getDefaultQueuePath());
  (void) fprintf (stderr,
		  "\t-s seqno     set initial product sequence number to \"seqno\", defaults to 0\n");
  (void) fprintf (stderr,
		  "\t-f feedtype  assert your feed type as \"feedtype\", defaults to \"EXP\"\n");
  (void) fprintf (stderr, "\t-S           Do not create .status product\n");
  exit (1);
}


void
cleanup (void)
{
  if (pq)
    {
      (void) pq_close (pq);
      pq = NULL;
    }
  (void)log_fini();
}

static void
signal_handler (int sig)
{
#ifdef SVR3SIGNALS
  /*
   * Some systems reset handler to SIG_DFL upon entry to handler.
   * In that case, we reregister our handler.
   */
  (void) signal (sig, signal_handler);
#endif
  switch (sig)
    {
    case SIGINT:
      exit (1);
    case SIGTERM:
      exit (1);
    case SIGPIPE:
      log_debug("SIGPIPE");
      exit (1);
    }
  log_debug("signal_handler: unhandled signal: %d", sig);
}


static void
set_sigactions (void)
{
#ifndef NO_POSIXSIGNALS
  struct sigaction sigact;

  sigemptyset (&sigact.sa_mask);
  sigact.sa_flags = 0;

  /* Ignore these */
  sigact.sa_handler = SIG_IGN;
  (void) sigaction (SIGHUP, &sigact, NULL);
  (void) sigaction (SIGPIPE, &sigact, NULL);
  (void) sigaction (SIGALRM, &sigact, NULL);
  (void) sigaction (SIGCHLD, &sigact, NULL);

  /* Handle these */
#ifdef SA_RESTART		/* SVR4, 4.3+ BSD */
  /* usually, restart system calls */
  sigact.sa_flags |= SA_RESTART;
#endif
  sigact.sa_handler = signal_handler;
  (void) sigaction (SIGTERM, &sigact, NULL);
  (void) sigaction (SIGPIPE, &sigact, NULL);
  /* Don't restart after interrupt */
  sigact.sa_flags = 0;
#ifdef SA_INTERRUPT		/* SunOS 4.x */
  sigact.sa_flags |= SA_INTERRUPT;
#endif
  (void) sigaction (SIGINT, &sigact, NULL);
#else

  (void) signal (SIGHUP, SIG_IGN);
  (void) signal (SIGPIPE, SIG_IGN);
  (void) signal (SIGALRM, SIG_IGN);
  (void) signal (SIGCHLD, SIG_IGN);

  (void) signal (SIGTERM, signal_handler);
  (void) signal (SIGPIPE, signal_handler);
  (void) signal (SIGINT, signal_handler);
#endif
}

static int
mm_md5 (MD5_CTX * md5ctxp, void *vp, size_t sz, signaturet signature)
{
  MD5Init (md5ctxp);

  MD5Update (md5ctxp, vp, sz);

  MD5Final (signature, md5ctxp);
  return 0;
}

int
main (int ac, char *av[])
{
  const char* pqfname;
  char *progname = av[0];
  int status;
  int seq_start = 0;
  stat_info *sinfo, *shead = NULL, *slast = NULL;
  int statusoff=0;

  /*
   * Set up error logging
   */
  (void)log_init(progname);


  /*
   * Check the environment for some options.
   * May be overridden by command line switches below.
   */
  {
    const char *ldmpqfname = getenv ("LDMPQFNAME");
    if (ldmpqfname != NULL)
      pqfname = ldmpqfname;
  }

  {
    extern int optind;
    extern int opterr;
    extern char *optarg;
    int ch;

    opterr = 1;

    while ((ch = getopt (ac, av, "vxl:q:f:s:S")) != EOF)
      switch (ch)
	{
	case 'v':
          if (!log_is_enabled_info)
            (void)log_set_level(LOG_LEVEL_INFO);
	  break;
	case 'x':
          (void)log_set_level(LOG_LEVEL_DEBUG);
	  break;
	case 'l':
	  log_set_destination(optarg);
	  break;
	case 'q':
	  setQueuePath(optarg);
	  break;
	case 's':
	  seq_start = atoi (optarg);
	  break;
	case 'f':
	  feedtype = atofeedtypet (optarg);
	  if (feedtype == NONE)
	    {
	      fprintf (stderr, "Unknown feedtype \"%s\"\n", optarg);
	      usage (progname);
	    }
	  break;
	case 'S':
	     statusoff=1;
	     break;
	case '?':
	  usage (progname);
	  break;
	}

    pqfname = getQueuePath();

    ac -= optind;
    av += optind;

    if (ac < 1)
      usage (progname);
  }

  /*
   * register exit handler
   */
  if (atexit (cleanup) != 0)
    {
      log_syserr ("atexit");
      exit (1);
    }

  /*
   * set up signal handlers
   */
  set_sigactions ();

  /*
   * who am i, anyway
   */
  (void) strcpy (myname, ghostname ());

  /*
   * open the product queue
   */
  if (status = pq_open (pqfname, PQ_DEFAULT, &pq))
    {
      if (status > 0) {
          log_syserr("\"%s\" failed", pqfname);
      }
      else {
          log_error("\"%s\" failed: %s", pqfname, "Internal error");
      }
      exit (2);
    }


  {
    char *filename;
    int fd;
    struct stat statb;
    product prod;
    unsigned char *prodmmap;
    MD5_CTX *md5ctxp = NULL;
    int gversion;

    /*
     * Allocate an MD5 context
     */
    md5ctxp = new_MD5_CTX ();
    if (md5ctxp == NULL)
      {
	log_syserr ("new_md5_CTX failed");
	exit (6);
      }

    /* These members are constant over the loop. */
    prod.info.origin = myname;
    prod.info.feedtype = feedtype;

    prod.info.seqno = seq_start;

    /*
     * Open the file to be inserted and process
     */
    while (ac > 0)
      {
        long insert_sum = 0;
	long sinfo_cnt = 0;
        long stat_size = 0;

	filename = *av;
	av++;
	ac--;

	log_notice ("open and memorymap %s\0", filename);

	fd = open (filename, O_RDONLY, 0);
	if (fd == -1)
	  {
	    log_syserr ("open: %s", filename);
	    continue;
	  }

	if (fstat (fd, &statb) == -1)
	  {
	    log_syserr ("fstat: %s", filename);
	    (void) close (fd);
	    continue;
	  }

	if ((prodmmap = (unsigned char *) mmap (0, statb.st_size,
				       PROT_READ, MAP_PRIVATE, fd,
				       0)) == MAP_FAILED)
	  {
	    log_syserr ("allocation failed");
	  }
	else
	  {
	    int GRIBDONE = 0;
	    off_t griboff = 0;
	    size_t griblen = 0;
	    log_notice ("%ld bytes memory mapped\0", (long) statb.st_size);

	    while (!GRIBDONE)
	      {
		log_debug("griboff %d\0", (int) griboff);
		/* get offset of next grib product */
		status =
		  get_grib_info (prodmmap, statb.st_size, &griboff, &griblen,
				 &gversion);

		switch (status)
		  {
		  case 0:
		    prod.data = prodmmap + griboff;
		    prod.info.sz = griblen;

		    /*
		     * revised MD5 calculation...using filename
		     * to allow duplicate products in different files.
		     */
		    MD5Init (md5ctxp);
  		    MD5Update (md5ctxp, (void *)filename, strlen(filename));
  		    /*MD5Update (md5ctxp, (void *)prod.data, prod.info.sz);*/
		    if ( prod.info.sz > 10000 )
  		       MD5Update (md5ctxp, (void *)prod.data, 10000);
		    else
  		       MD5Update (md5ctxp, (void *)prod.data, prod.info.sz);
  		    MD5Final (prod.info.signature, md5ctxp);

		    /*if (mm_md5 (md5ctxp, prod.data, prod.info.sz,
				prod.info.signature) != 0)
		      {
			log_error ("could not compute MD5\0");
		      }
		    else
		      { */
			prod.info.ident = (char *) malloc (KEYSIZE + 1);
			get_gribname (gversion, prod.data, prod.info.sz,
				      filename, prod.info.seqno,
				      prod.info.ident);
			/*
			 * Do the deed
			 */
			status = set_timestamp (&prod.info.arrival);
			if (status != ENOERR)
			  {
			    log_syserr ("could not set timestamp");
			  }
			/*
			 * Insert the product
			 */
			status = pq_insert (pq, &prod);
			log_info ("%d %s\0", status, prod.info.ident);
                
			if ( status == ENOERR )
			   insert_sum += prod.info.sz;

			if (! statusoff )
			  {
			  /*
			   * Log this status
			   */
			    sinfo_cnt++;
			    sinfo = (stat_info *)malloc(sizeof(stat_info));
                   	    sinfo->insertstatus = status;
			    sinfo->prodname = (char *)malloc(strlen(prod.info.ident)+1);
                   	    strcpy(sinfo->prodname, prod.info.ident);
                   	    sinfo->seqno = prod.info.seqno;
                   	    sinfo->prodsz = prod.info.sz;
                   	    sinfo->next = NULL;
                   	    stat_size += strlen(sinfo->prodname);
                   	    if(shead == NULL)
			      {
                      	        shead = sinfo;
                      	        slast = sinfo;
                   	      }
                   	    else 
			      {
                      	        slast->next = sinfo;
                      	        slast = sinfo;
                   	      }
			  }
		      /*}*/
		    griboff += griblen;
		    prod.info.seqno++;
		    break;
		  case -1:
		    GRIBDONE = 1;
		    break;
		  case -2:
		    log_error ("truncated grib file at: %d", prod.info.seqno);
		    GRIBDONE = 1;
		    break;
		  case -7:
		    log_error ("End sequence 7777 not found where expected: %d",
			    prod.info.seqno);
		    griboff += griblen;
		    log_error("resume looking at %d\0",griboff);
		    break;
		  default:
		    log_error ("unknown error %d\0", status);
		    griboff += griblen;
		    if (griboff >= statb.st_size)
		      GRIBDONE = 1;
		    break;
		  }

		if (griboff >= statb.st_size)
		  GRIBDONE = 1;
	      }


	    log_notice ("munmap\0");
	    (void) munmap ((void *)prodmmap, statb.st_size);

	    if ( stat_size != 0 )
	    /*
	     * Add a status message to product queue
	     */
	      {
		char *statusmess;
		log_notice("stats_size %ld %ld\0",stat_size,sinfo_cnt);

		statusmess = (char *)malloc((30 * sinfo_cnt) + stat_size +
                        strlen(filename) + 128);
                if(statusmess == NULL) 
		  {
              	    log_syserr("could not malloc status message %ld\0",
              	            stat_size);
           	  }
           	else
		  {
		    char tmpprod[512];
                    sinfo = shead; slast = NULL;

                    memset(statusmess, 0, sizeof(statusmess));

                    status = set_timestamp(&prod.info.arrival);
                    /* ctime ends with \n\0" */
                    sprintf(statusmess,"%s complete (%ld bytes) at %sInserted %ld of %ld\n\0",
                       filename,(long)statb.st_size, ctime(&prod.info.arrival.tv_sec),
                       insert_sum,(long)statb.st_size);

                    while(sinfo != NULL) 
		      {
                        sprintf(tmpprod,"%3d %5d %8d %s\n\0",sinfo->insertstatus,
                            sinfo->seqno,sinfo->prodsz,sinfo->prodname);
                        strcat(statusmess,tmpprod);

                        slast = sinfo;
                 	sinfo = sinfo->next;

                        free(slast->prodname);
                    	free(slast);

                      }

		    shead = NULL;

                    sprintf(tmpprod,".status.%s %06d\0",filename, prod.info.seqno);
                    prod.info.ident = tmpprod;
                    prod.data = statusmess;
                    prod.info.sz = strlen(statusmess);
                    status = mm_md5(md5ctxp, prod.data, prod.info.sz, prod.info.signature);
                    status = set_timestamp(&prod.info.arrival);
                    status = pq_insert(pq, &prod);
                    if(log_is_enabled_info)
                        log_info("%s", s_prod_info(NULL, 0, &prod.info,
                                log_is_enabled_debug)) ;
                    free(statusmess);
		    prod.info.seqno++;
		  }
	      }
	  }

	(void) close (fd);
      }
  }

exit(0);
}
