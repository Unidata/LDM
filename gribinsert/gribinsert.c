/**
 * This file implements a program that creates LDM data-products from numerical
 * model output and inserts them into the LDM product-queue.
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
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
"\t-l dest      Log to `dest`. One of: \"\" (system logging daemon), \"-\"\n"
"\t             (standard error), or file `dest`. Default is \"%s\"\n",
                log_get_default_destination());
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
  log_fini();
}

static void
signal_handler (int sig)
{
    switch (sig) {
    case SIGINT:
        exit (1);
    case SIGTERM:
        exit (1);
    case SIGPIPE:
        exit (1);
    case SIGUSR1:
        log_refresh();
        return;
    }
}


static void
set_sigactions (void)
{
  struct sigaction sigact;

  sigemptyset (&sigact.sa_mask);
  sigact.sa_flags = 0;

  /* Ignore the following */
  sigact.sa_handler = SIG_IGN;
  (void)sigaction(SIGALRM, &sigact, NULL);
  (void)sigaction(SIGCHLD, &sigact, NULL);

  /* Handle the following */
  sigact.sa_handler = signal_handler;

  /* Don't restart the following*/
  (void)sigaction(SIGINT, &sigact, NULL);
  (void)sigaction(SIGTERM, &sigact, NULL);
  (void)sigaction(SIGPIPE, &sigact, NULL);

  /* Restart the following*/
  sigact.sa_flags |= SA_RESTART;
  (void)sigaction(SIGUSR1, &sigact, NULL);

  sigset_t sigset;
  (void)sigemptyset(&sigset);
  (void)sigaddset(&sigset, SIGINT);
  (void)sigaddset(&sigset, SIGPIPE);
  (void)sigaddset(&sigset, SIGTERM);
  (void)sigaddset(&sigset, SIGALRM);
  (void)sigaddset(&sigset, SIGCHLD);
  (void)sigaddset(&sigset, SIGUSR1);
  (void)sigprocmask(SIG_UNBLOCK, &sigset, NULL);
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
  char *progname = av[0];
  int status;
  int seq_start = 0;
  stat_info *sinfo, *shead = NULL, *slast = NULL;
  int statusoff=0;

  /*
   * Set up error logging
   */
    if (log_init(progname)) {
        log_syserr("Couldn't initialize logging module");
        exit(1);
    }

  const char* pqfname = getQueuePath();

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
          if (log_set_destination(optarg)) {
              log_syserr("Couldn't set logging destination to \"%s\"",
                      optarg);
              exit(1);
          }
	  break;
	case 'q':
	  pqfname = optarg;
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

    setQueuePath(pqfname);

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
  if ((status = pq_open (pqfname, PQ_DEFAULT, &pq)))
    {
      if (status > 0) {
          log_add_syserr("\"%s\" failed", pqfname);
          log_flush_error();
      }
      else {
          log_error_q("\"%s\" failed: %s", pqfname, "Internal error");
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

	log_notice_q ("open and memorymap %s\0", filename);

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
	    log_notice_q ("%ld bytes memory mapped\0", (long) statb.st_size);

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
			log_error_q ("could not compute MD5\0");
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
			    log_add_syserr("could not set timestamp");
                            log_flush_error();
			  }
			/*
			 * Insert the product
			 */
			status = pq_insert (pq, &prod);
			log_info_q ("%d %s\0", status, prod.info.ident);
                
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
		    log_error_q ("truncated grib file at: %d", prod.info.seqno);
		    GRIBDONE = 1;
		    break;
		  case -7:
		    log_error_q ("End sequence 7777 not found where expected: %d",
			    prod.info.seqno);
		    griboff += griblen;
		    log_error_q("resume looking at %d\0",griboff);
		    break;
		  default:
		    log_error_q ("unknown error %d\0", status);
		    griboff += griblen;
		    if (griboff >= statb.st_size)
		      GRIBDONE = 1;
		    break;
		  }

		if (griboff >= statb.st_size)
		  GRIBDONE = 1;
	      }


	    log_notice_q ("munmap\0");
	    (void) munmap ((void *)prodmmap, statb.st_size);

	    if ( stat_size != 0 )
	    /*
	     * Add a status message to product queue
	     */
	      {
		char *statusmess;
		log_notice_q("stats_size %ld %ld\0",stat_size,sinfo_cnt);

		statusmess = calloc((30 * sinfo_cnt) + stat_size +
                        strlen(filename) + 128, sizeof(char));
                if(statusmess == NULL) 
		  {
              	    log_syserr("could not malloc status message %ld\0",
              	            stat_size);
           	  }
           	else
		  {
		    char tmpprod[512];
                    sinfo = shead; slast = NULL;

                    status = set_timestamp(&prod.info.arrival);
                    /* ctime ends with \n\0" */
                    sprintf(statusmess,"%s complete (%ld bytes) at %sInserted %ld of %ld\n",
                       filename,(long)statb.st_size, ctime(&prod.info.arrival.tv_sec),
                       insert_sum,(long)statb.st_size);

                    while(sinfo != NULL) 
		      {
                        sprintf(tmpprod,"%3d %5d %8d %s\n",sinfo->insertstatus,
                            sinfo->seqno,sinfo->prodsz,sinfo->prodname);
                        strcat(statusmess,tmpprod);

                        slast = sinfo;
                 	sinfo = sinfo->next;

                        free(slast->prodname);
                    	free(slast);

                      }

		    shead = NULL;

                    sprintf(tmpprod,".status.%s %06d",filename, prod.info.seqno);
                    prod.info.ident = tmpprod;
                    prod.data = statusmess;
                    prod.info.sz = strlen(statusmess);
                    status = mm_md5(md5ctxp, prod.data, prod.info.sz, prod.info.signature);
                    status = set_timestamp(&prod.info.arrival);
                    status = pq_insert(pq, &prod);
                    if(log_is_enabled_info)
                        log_info_q("%s", s_prod_info(NULL, 0, &prod.info,
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
