/*
 *   Copyright 2014, University Corporation for Atmospheric Research
 *   See COPYRIGHT file for copying and redistribution conditions.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "nport.h"
#include "log.h"

int npunz (char *zstr, int *lenout, int *ioff);

const char *platform_id(unsigned char satid);
const char *channel_id(unsigned char channel);
const char *sector_id(unsigned char sector);

#define MAX_BLOCK       6000
uLong  uncomprLen = MAX_BLOCK;
Byte   uncompr[MAX_BLOCK];

int readpdb(char *buf, psh_struct *psh, pdb_struct *pdb, int zflag, int bufsz )
{
   int           iret=0;
   unsigned char b1,b2;
   unsigned long lval;
   int           wmocnt, i, ioff;
   char*         wbuf;
   char          res_string[8];
   char          ldmname[256] = {0};

   wmocnt = 0;
   memset(psh->pname,0,sizeof(psh->pname));
   memset(ldmname,0,sizeof(ldmname));

   while((wmocnt<512)&&(buf[wmocnt] != '\n')) {
      if ((unsigned char)buf[wmocnt] >= 32) {
         if((unsigned char)buf[wmocnt] > 32)
            strncat(psh->pname,buf+wmocnt,1);
         else
            strcat(psh->pname," "); // Guaranteed safe because 512 < sizeof(psh->pname)
      }
      wmocnt++;
   }
   if (wmocnt > 0)
      log_info_q("%s %d\0",psh->pname, bufsz);

   if ( zflag ) {
      log_debug("compressed file %d\0",wmocnt+1);
      if( npunz ( buf + wmocnt + 1, &i, &ioff ) != 0) {
         pdb->platform = 0;
         pdb->channel = 0;
         pdb->year = 0; pdb->month = 0; pdb->day = 0;
         pdb->hour = 0; pdb->minute = 0;
         pdb->sector = 0;
         pdb->res = 0;
         snprintf(ldmname, sizeof(ldmname), "satz/ch%d/%s/%s/%04d%02d%02d %02d%02d/%s/%dkm/ %s",
                 psh->ptype,
                 (char *)platform_id((unsigned char)pdb->platform),
                 (char *)channel_id((unsigned char)pdb->channel),
                 pdb->year,pdb->month,pdb->day,pdb->hour,pdb->minute,
                 (char *)sector_id((unsigned char)pdb->sector),
                 pdb->res,
                 psh->pname);
         ldmname[sizeof(ldmname)-1] = 0;
         strcpy(psh->pname, ldmname);
         pdb->len = -1;
         return ( -1 );
      }
      else {
         wbuf = (char *)(uncompr + ioff);
      }
   }
   else {
      wbuf = buf + wmocnt + 1;
   }

   b1 = (unsigned char)wbuf[0];
   pdb->source = b1;

   b1 = (unsigned char)wbuf[1];
   pdb->platform = b1;

   b1 = (unsigned char)wbuf[2];
   pdb->sector = b1;

   b1 = (unsigned char)wbuf[3];
   pdb->channel = b1;

   b1 = (unsigned char)wbuf[4];
   b2 = (unsigned char)wbuf[5];
   lval = (b1 << 8) + b2;
   pdb->nrec = lval;

   b1 = (unsigned char)wbuf[6];
   b2 = (unsigned char)wbuf[7];
   lval = (b1 << 8) + b2;
   pdb->recsize = lval;


   b1 = (unsigned char)wbuf[8];
   pdb->year = b1;
   if(pdb->year > 70) {
      pdb->year += 1900;
   }
   else {
      pdb->year += 2000;
   }

   b1 = (unsigned char)wbuf[9];
   pdb->month = b1;
   b1 = (unsigned char)wbuf[10];
   pdb->day = b1;
   b1 = (unsigned char)wbuf[11];
   pdb->hour = b1;
   b1 = (unsigned char)wbuf[12];
   pdb->minute = b1;
   b1 = (unsigned char)wbuf[13];
   pdb->second = b1;
   b1 = (unsigned char)wbuf[14];
   pdb->sechunds = b1;

   log_debug("look time %04d%02d%02d %02d%02d %02d.%02d\0",
      pdb->year,pdb->month,pdb->day,pdb->hour,pdb->minute,pdb->second,pdb->sechunds);

   b1 = (unsigned char)wbuf[16];
   b2 = (unsigned char)wbuf[17];
   lval = (b1 << 8) + b2;
   pdb->nx = lval;
   if(pdb->nx < 1)
      iret = -1;
   b1 = (unsigned char)wbuf[18];
   b2 = (unsigned char)wbuf[19];
   lval = (b1 << 8) + b2;
   pdb->ny = lval;
   if(pdb->ny < 1)
      iret = -1;

   pdb->res = (unsigned char)wbuf[41];
   memset(res_string,0,sizeof(res_string));

   /*
   sprintf(res_string," %dkm\0",pdb->res);
   strcat(psh->pname,res_string);
   */

   if ( !zflag )
      /* set Octet 43 to "128" since we will encode the data block with png */
      wbuf[42] = 128;

   snprintf(ldmname, sizeof(ldmname)-1, "%s/ch%d/%s/%s/%04d%02d%02d %02d%02d/%s/%dkm/ %s",
		 zflag ? "satz" : "sat",
         psh->ptype,
         (char *)platform_id((unsigned char)pdb->platform),
         (char *)channel_id((unsigned char)pdb->channel),
         pdb->year,pdb->month,pdb->day,pdb->hour,pdb->minute,
         (char *)sector_id((unsigned char)pdb->sector),
         pdb->res,
         psh->pname);
   strcpy(psh->pname,ldmname);

   pdb->len = 512 + wmocnt;

   return(iret);
}


#define CHECK_ERR(err, msg) { \
    if (err != Z_OK) { \
        log_error_q("%s error: %d\0", msg, err); \
    } \
}

int npunz (char *zstr, int *lenout, int *ioff)
{
    /*
     * ZLIB decompression variables.
     */
    z_stream        d_stream = {}; // Initializes to zero
    uLong           lentot=0;

    int nbytes=540, err;
    int i;

    *lenout = 0;

    d_stream.zalloc = (alloc_func) 0;
    d_stream.zfree  = (free_func) 0;
    d_stream.opaque = (voidpf) 0;

    err = inflateInit ( &d_stream );
    CHECK_ERR ( err, "inflateInit" );

    d_stream.next_in   = (Byte *)(zstr + lentot);
    d_stream.avail_in  = nbytes - lentot;
    d_stream.next_out  = uncompr + *lenout;
    d_stream.avail_out = (uInt)uncomprLen - *lenout;

    err = inflate ( &d_stream, Z_NO_FLUSH );

    if (err != Z_STREAM_END) {
        CHECK_ERR(err, "large inflate");
        (void)inflateEnd(&d_stream);
        return(-1);
    }

    *lenout = *lenout + d_stream.total_out;

    err = inflateEnd ( &d_stream );
    CHECK_ERR ( err, "inflateEnd" );

    /*
     * jump past the internal WMO header
     */
    i = 0;
    while  ((uncompr[i] != '\n') && (i < *lenout))
        i++;

    if ( i == *lenout )
        return( -1 );
    else
        *ioff = i + 1;

    return(0);
} /*end */
