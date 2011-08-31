/*
 *   Copyright 2004, University Corporation for Atmospheric Research
 *   See COPYRIGHT file for copying and redistribution conditions.
 */
#include <stdio.h>
#include <string.h>
#include <noaaportLog.h>
#include "nport.h"

int wmo_header(char *prod_name, size_t prod_size, char *wmohead, char *wmometa, int *metaoff);
void redbook_header(char *buf, int nbytes, char *tstr);

int readpsh(char *buf,psh_struct *psh)
{
int iret=0;
unsigned char b1,b2,b3,b4,val;
unsigned long lval;

psh->hasccb = 0;
psh->ccbmode = 0;
psh->ccbsubmode = 0;
psh->metadata[0] = '\0';
psh->metaoff = -1;

b1 = (unsigned char)buf[0];
b2 = (unsigned char)buf[1];
b3 = (unsigned char)buf[2];
b4 = (unsigned char)buf[3];
lval = (b3 << 8) + b4;   /*psh len */

psh->onum = b1;
psh->otype = b2;
psh->olen = lval;

val = (unsigned char)buf[4];
psh->version = val;

val = (unsigned char)buf[5];
psh->hflag = val;

b1 = (unsigned char)buf[6];
b2 = (unsigned char)buf[7];
lval = (b1 << 8) + b2;
psh->psdl = lval;

b1 = (unsigned char)buf[8];
b2 = (unsigned char)buf[9];
lval = (b1 << 8) + b2;
psh->bytes_per_record = lval;

val = (unsigned char)buf[10];
psh->ptype = val;

val = (unsigned char)buf[11];
psh->pcat = val;

b1 = (unsigned char)buf[12];
b2 = (unsigned char)buf[13];
lval = (b1 << 8) + b2;
psh->pcode = lval;

b1 = (unsigned char)buf[14];
b2 = (unsigned char)buf[15];
lval = (b1 << 8) + b2; 
psh->frags = lval;

b1 = (unsigned char)buf[16];
b2 = (unsigned char)buf[17];
lval = (b1 << 8) + b2;
psh->nhoff = lval;

b1 = (unsigned char)buf[19];
psh->source = b1;

b1 = (unsigned char)buf[20];
b2 = (unsigned char)buf[21];
b3 = (unsigned char)buf[22];
b4 = (unsigned char)buf[23];
lval = (((((b1 << 8) + b2) << 8) + b3) << 8) + b4;
psh->seqno = lval;

b1 = (unsigned char)buf[24];
b2 = (unsigned char)buf[25];
b3 = (unsigned char)buf[26];
b4 = (unsigned char)buf[27];
lval = (((((b1 << 8) + b2) << 8) + b3) << 8) + b4;
psh->rectime = lval;

b1 = (unsigned char)buf[28];
b2 = (unsigned char)buf[29];
b3 = (unsigned char)buf[30];
b4 = (unsigned char)buf[31];
lval = (((((b1 << 8) + b2) << 8) + b3) << 8) + b4;
psh->transtime = lval;

b1 = (unsigned char)buf[32];
b2 = (unsigned char)buf[33];
lval = (b1 << 8) + b2;
psh->runid = lval;

b1 = (unsigned char)buf[34];
b2 = (unsigned char)buf[35];
lval = (b1 << 8) + b2;
psh->origrunid = lval;

return(iret);
}

int readccb(char *buf,ccb_struct *ccb, psh_struct *psh, int blen)
{
int iret=0;
unsigned char b1,b2;
char wmohead[256];
int wmolen, metaoff=-1;
char redbook_title[45];
char wmometa[256];

redbook_title[0] = '\0';
wmometa[0] = '\0';

b1 = (unsigned char)buf[0];
b2 = (unsigned char)buf[1];

ccb->b1 = b1;
ccb->len = 2 * (int)(((b1 & 63) << 8) + b2);

if ( ccb->len > blen ) /* A rouge product missing its CCB, reported to NWS DM 3/10/05 by Chiz */
   {
   nplError ("invalid ccb length = %d %d %d, blen %d\n",ccb->len,b1,b2,blen);

   /* try a failsafe header, otherwise use our own! */
   wmolen = 0;
   while ( ( (int)buf[wmolen] >= 32) && ( wmolen < 256 ) ) wmolen++;
   if ( wmolen > 0 )
      strncat(psh->pname,buf,wmolen);
   else
      sprintf(psh->pname,"Unidentifiable product\0");
   ccb->len = 0;
   return(-1);
   }


b1 = (unsigned char)buf[10];
b2 = (unsigned char)buf[11];
psh->ccbmode = b1;
psh->ccbsubmode = b2;
nplDebug("ccb mode %d ccb submode %d\0",psh->ccbmode,psh->ccbsubmode);
psh->hasccb = 1;

b1 = (unsigned char)buf[12];
b2 = (unsigned char)buf[13];
ccb->user1 = b1;
ccb->user2 = b1;
nplDebug("ccb user1 %d ccb user2 %d\0",ccb->user1,ccb->user2);
psh->ccbdtype[0] = '\0';     /* Initialize ccbdtype...eventually used to identify data type */

/* see if this looks like a WMO header, if so canonicalize */
if(wmo_header(buf+ccb->len,blen - ccb->len,wmohead,wmometa,&metaoff)==0)
   {
   if(strlen(wmohead) > 0)
      {
      strcat(psh->pname,wmohead); 
      if (metaoff > 0) psh->metaoff = metaoff;
      }
   else
      {
      wmolen = 0;
      while ( ( (int)buf[wmolen + ccb->len] >= 32) && ( wmolen < 256 ) )
         wmolen++;

      strncat(psh->pname,buf+ccb->len,wmolen);
      }
   }
else
   {
   wmolen = 0;
   while ( ( (int)buf[wmolen + ccb->len] >= 32) && ( wmolen < 256 ) )
      wmolen++;
   if ( wmolen > 0 )
      strncat(psh->pname,buf+ccb->len,wmolen);
   else
      sprintf(psh->pname,"Unidentifiable product\0");

   nplNotice("Non-wmo product type %s ccbmode %d ccbsubmode %d\0",
	psh->pname, psh->ccbmode, psh->ccbsubmode);
   }


/* for now, since NEXRAD is not identified as a separate PTYPE, use WMO header check */
if((psh->ccbmode == 2)&&(psh->ccbsubmode == 0)) 
   {
   if((memcmp(buf+ccb->len,"SDUS5",5) == 0)||
      (memcmp(buf+ccb->len,"SDUS2",5) == 0)||
      (memcmp(buf+ccb->len,"SDUS3",5) == 0)||
      (memcmp(buf+ccb->len,"SDUS7",5) == 0)||
      (memcmp(buf+ccb->len,"SDUS8",5) == 0) )
      {
      /* uncompressed nids check */
      if(ccb->user1 != 'F') sprintf(psh->ccbdtype,"nids/\0");
      psh->pcat = 99;
      }
   }
/*
unfortunately, NWS isn't using CCB mode for all product types
else if((psh->pcat == 4)&&(psh->ccbmode == 2)&&(psh->ccbsubmode == 1))
   sprintf(psh->ccbdtype,"grib/\0");
else if(psh->pcat == 5)
   sprintf(psh->ccbdtype,"bufr/\0");
*/
else if (psh->pcat == 2)
   {
   /* see if this is recognizeable as a redbook graphic */
   redbook_header(buf+ccb->len,blen - ccb->len, redbook_title);
   if(strlen(redbook_title) > 0)
      sprintf(psh->ccbdtype,"redbook %d_%d/\0", ccb->user1, ccb->user2);
   else
      sprintf(psh->ccbdtype,"graph %d_%d/\0",  ccb->user1, ccb->user2);
   }



/* create metadata */
sprintf(psh->metadata," !\0");

if ( psh->ccbdtype[0] != '\0')
   strcat ( psh->metadata, psh->ccbdtype);

if ( redbook_title[0] != '\0' )
   strcat( psh->metadata, redbook_title);

if ( wmometa[0] != '\0' )
   strcat( psh->metadata, wmometa);

if ( psh->metadata[2] == '\0' ) psh->metadata[0] = '\0';


/* restore meta data to PROD NAME
strcat(psh->pname,psh->metadata); */

return(iret);
}
