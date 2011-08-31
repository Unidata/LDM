/*
 *   Copyright 2004, University Corporation for Atmospheric Research
 *   See COPYRIGHT file for copying and redistribution conditions.
 */
#include <stdio.h>
#include "nport.h"
#include "noaaportLog.h"

int readsbn(char *buf, sbn_struct *sbn)
{
unsigned char b1,b2,b3,b4,val;
unsigned long lval;
unsigned long csum=0;
int i;


b1 = (unsigned char)buf[14];
b2 = (unsigned char)buf[15];
lval = (b1 << 8) + b2;
/* validate the checksum */
for(i=0;i<14;i++) csum = csum + (unsigned char)buf[i];

if(csum != lval) 
   {
   nplError("SBN checksum invalid %u %u",csum,lval);
   return(-1);
   }
else
   sbn->checksum = lval;

b1 = buf[0]; 
if(b1 != 255)
   {
   for(i=0;i<32;i++)
      nplInfo("look val %d %u",i,buf[i]);
   return(-1);
   }

val = (unsigned char)buf[2];
b1 = (val >> 4);
b2 = (val & 15);
sbn->version = b1;
sbn->len = (int)b2 * 4;

val = (unsigned char)buf[4];
sbn->command = val;
switch ( sbn->command )
   {
   case 3: /* product format data transfer */
   case 5: /* Synchonize timing */
   case 10:/* Test message */
	break;
   default:
	nplError ( "Invalid SBN command %d", sbn->command );
	return(-1);
   }

val = (unsigned char)buf[5];
sbn->datastream = val;

b1 = (unsigned char)buf[8];
b2 = (unsigned char)buf[9];
b3 = (unsigned char)buf[10];
b4 = (unsigned char)buf[11];
lval = (((((b1 << 8) + b2) << 8) + b3) << 8) + b4;
sbn->seqno = lval;

b1 = (unsigned char)buf[12];
b2 = (unsigned char)buf[13];
lval = (b1 << 8) + b2;
sbn->runno = lval;

return(0);
}
