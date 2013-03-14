/**
 *   Copyright 2013, University Corporation for Atmospheric Research
 *   All rights reserved
 *   <p>
 *   See file COPYRIGHT in the top-level source-directory for copying and
 *   redistribution conditions.
 *   <p>
 *   This file converts a NOAPORT SBN data-product into an LDM data-product
 *   and inserts the result into the LDM product-queue.
 */
#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "nport.h"

#include "ldm.h"
#include "ldmProductQueue.h"
#include "md5.h"
#include "inetutil.h"
#include "log.h"

datastore *dataheap=NULL;
int nextfrag = 0, MAXFRAGS=1000;

void ds_init(int nfrags)
{
if ( dataheap == NULL ) dataheap = (datastore *)malloc(MAXFRAGS * sizeof(datastore));

if ( nfrags >= MAXFRAGS )
   {
   MAXFRAGS = nfrags + 1;
   unotice("increasing fragheap to %d\0",MAXFRAGS);
   dataheap = (datastore *)realloc ( dataheap, MAXFRAGS * sizeof(datastore));
   }

nextfrag = 0;

}

datastore *ds_alloc()
{
int thisfrag;

if ( dataheap == NULL ) 
   ds_init ( MAXFRAGS );

if ( nextfrag >= MAXFRAGS )
   {
   MAXFRAGS += 1000;
   uerror("Error in number of frags, increasing fragheap to %d\0",MAXFRAGS);
   dataheap = (datastore *)realloc ( dataheap, MAXFRAGS * sizeof(datastore));
   }

thisfrag = nextfrag;
nextfrag++;
return(&dataheap[thisfrag]);

}

void
ds_free ()
{
nextfrag = 0;
}

int
prod_isascii (char *pname, char *prod, size_t psize)
{
  int i = 1;

/* assume prod[0] is CTRL-A and prod[psize-1] is CTRL-C */
/* within product, allow RS, HT, CR, NL, \0 */
/* add a little leeway....accept ETX in last 8 bytes since some products do */
/* this is only to keep HDS FOS category. Would rather use NOAAPORT types (SRC) */
  while (i < psize - 1)
    {
      if ((prod[i] < ' ') || (prod[i] > 127))
	{
	  if ((prod[i] == 3) && (i < (psize - 9)))
	    return (0);
	  if ((prod[i] != 0) && (prod[i] != 30) && (prod[i] != '\t') &&
	          (prod[i] != '\n') && (prod[i] != '\r'))
	    return (0);
	}
      i++;
      /* lets only do first 100 and last 100 bytes for the check */
      if ((i > 100) && (i < (psize - 101)))
	i = psize - 101;
    }

  return (1);
}

void
process_prod(
    prodstore                   nprod,
    char*                       PROD_NAME,
    char*                       memheap,
    size_t                      heapsize,
    MD5_CTX*                    md5try,
    LdmProductQueue* const      lpq,            /**< Pointer to LDM
                                                 *   product-queue object */
    psh_struct*                 psh,
    sbn_struct*                 sbn)
{
  int status;
  product prod;

  static char myname[HOSTNAMESIZE];
  static char _nohead[] = "_NOHEAD";
  static feedtypet feedtext = IDS | DDPLUS;
  static feedtypet feednonascii = HDS;
  static feedtypet feedgraphic = HDS;
  static feedtypet feedpoint = HDS;
  static feedtypet feedgrid = HDS;
/*
static feedtypet feedtext = NTEXT;
static feedtypet feedgraphic = NGRAPH;
static feedtypet feedpoint = NPOINT;
*/
  static feedtypet feedngrid = NGRID;

  static feedtypet feedother = NOTHER;
  static feedtypet feednids = NEXRAD;
  static feedtypet feedtype = NIMAGE;
  char PRODID[1024];

  if ( ( strcmp(psh->metadata, " !grib2/") == 0 ) && 
       ( psh->metaoff > 0 ) && ( psh->metaoff < ( heapsize - 16 ) ) )
    {
    char *cpos;
    size_t lengrib;
    unsigned char b1, b2, b3, b4;
    static char GRIBstr[]="GRIB";

    cpos = &memheap[psh->metaoff];

    if ( memcmp ( cpos, GRIBstr, (size_t)4 ) == 0)
       {
       b1 = (unsigned char) cpos[12];
       b2 = (unsigned char) cpos[13];
       b3 = (unsigned char) cpos[14];
       b4 = (unsigned char) cpos[15];
       lengrib = (((((b1 << 8) + b2) << 8) + b3 ) << 8 ) + b4;
       grib2name(cpos, lengrib, PROD_NAME, &psh->metadata[2]);
       udebug("%d PRODname %s meta %s",psh->metaoff,PROD_NAME,psh->metadata); 
       }
    }

  sprintf (PRODID, "%s%s\0", PROD_NAME, psh->metadata);

  (void) strcpy (myname, ghostname ());

  prod.info.origin = myname;

  if (sbn->datastream == 6)	/* dvbs broadcast */
    {
      switch (psh->ptype)
	{
	case 1:
	case 2:
	case 3:
	  prod.info.feedtype = feedtype;
	  break;
	case 4:
	  prod.info.feedtype = feedngrid;
	  /*switch ( psh->pcat )
	     {
	     case 4:
	     prod.info.feedtype = feedngrid;
	     break;
	     case 5:
	     prod.info.feedtype = feedpoint;
	     break;
	     default:
	     prod.info.feedtype = feedother;
	     } */
	  break;
	default:
	  prod.info.feedtype = feedother;
	}
    }
  else if (psh->ptype < 4)
    prod.info.feedtype = feedtype;
  else
    {
      /* generally left with NWSTG data */
      switch (psh->pcat)
	{
	case 1:
	case 7:
	  prod.info.feedtype = feedtext;
	  break;
	case 101:
	case 107:
	  prod.info.feedtype = feednonascii;
	  break;
	case 2:
	  prod.info.feedtype = feedgraphic;
	  break;
	case 4:
	  prod.info.feedtype = feedgrid;
	  break;
	case 5:
	  prod.info.feedtype = feedpoint;
	  break;
	case 99:
	  prod.info.feedtype = feednids;
	  break;
	default:
	  prod.info.feedtype = feedother;
	}
    }
  prod.info.seqno = nprod.seqno;
  prod.data = memheap;
  prod.info.sz = heapsize;
  prod.info.ident = PRODID;

  if (prod.info.sz <= 0)
    {
      uerror ("heapsize is invalid %ld for prod %s\0", prod.info.sz,
	      prod.info.ident);
      return;
    }


  MD5Final (prod.info.signature, md5try);
  uinfo ("md5 checksum final\0");

  if (strlen (prod.info.ident) == 0)
    {
      prod.info.ident = _nohead;
      unotice ("strange header %s (%d) size %d %d\0", prod.info.ident,
	       psh->ptype, prod.info.sz, prod.info.seqno);
    }

  status = set_timestamp (&prod.info.arrival);
  uinfo ("timestamp %ld\0", prod.info.arrival);

  status = lpqInsert(lpq, &prod);
  if (status == 0) {
      unotice ("%s inserted [cat %d type %d ccb %d/%d seq %d size %d]\0",
	       prod.info.ident, psh->pcat, psh->ptype, psh->ccbmode,
	       psh->ccbsubmode, prod.info.seqno, prod.info.sz);
      return;
  }
  else if (3 == status) {
    unotice ("%s already in queue [%d]\0", prod.info.ident, prod.info.seqno);
  }
  else {
    uerror ("pqinsert failed [%d] %s\0", status, prod.info.ident);
  }


  return;
}


size_t
prodalloc (long int nfrags, long int dbsize, char **heap)
{
  size_t bsize;
  char *newheap;
  static size_t largestsiz = 0;

  if (nfrags == 0)
    nfrags = 1;
  bsize = (nfrags * dbsize) + 32;

  udebug ("heap allocate %ld  [%ld %ld] bytes\0", bsize, nfrags, dbsize);
  if (*heap == NULL)
    {
      newheap = (char *) malloc (bsize);
      largestsiz = bsize;
      udebug ("malloc new\0");
    }
  else
    {
      if (bsize > largestsiz)
	{
	  newheap = (char *) realloc (*heap, bsize);
	  largestsiz = bsize;
	  udebug ("remalloc\0");
	}
      else
	newheap = *heap;
    }

  *heap = newheap;

  return (bsize);
}
