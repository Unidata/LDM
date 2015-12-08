/*
 *   Copyright 2011, University Corporation for Atmospheric Research
 *
 *   See file COPYRIGHT in the top-level source-directory for copying and
 *   redistribution conditions.
 */
#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "gribinsert.h"
#include "mylog.h"

void search_for_grib(data,filelen,offset)
unsigned char *data;
off_t filelen;
off_t *offset;
{
int FOUND=0;
off_t pos;

pos = *offset;
while((FOUND == 0)&&(pos < (filelen-3)))
   {
   if(memcmp((char *)(data + pos),"GRIB",4) == 0)
      FOUND = 1;
   else
      pos++;
   }

if ( FOUND )
   *offset = pos;
else
   *offset = filelen;
}

int get_grib_info(unsigned char *data, off_t filelen, off_t *off, size_t *len, int *gversion)
{
int i, ioff;
size_t griblen=0;
unsigned int b1,b2,b3,b4;

*len = 0;

if(*off > filelen - 3) 
    return(-1);

if(memcmp((char *)(data + *off),"GRIB",4) != 0)
   search_for_grib(data,filelen,off);

if (*off > filelen - 3)
   return(-1);

if(memcmp((char *)(data + *off),"GRIB",4) == 0)
   {
   /*
    * get version number
    */
   *gversion = (int) data[ (int)(*off)+7];
   udebug("GRIB version %d\0",*gversion);
   switch( *gversion )
      {
      case 0:
      case 1:
      		b1 = (unsigned int) data[ (int)(*off)+4];
      		b2 = (unsigned int) data[ (int)(*off)+5];
      		b3 = (unsigned int) data[ (int)(*off)+6];
      		griblen = (((b1 << 8) + b2) << 8) + b3;
		udebug("grib1 length %u\0",griblen);
		break;
      case 2:
      		b1 = (unsigned int) data[ (int)(*off)+12];
      		b2 = (unsigned int) data[ (int)(*off)+13];
      		b3 = (unsigned int) data[ (int)(*off)+14];
      		b4 = (unsigned int) data[ (int)(*off)+15];
      		griblen = (((((b1 << 8) + b2) << 8) + b3 ) << 8 ) + b4;
		udebug("grib2 length %u\0",griblen);
		break;
      default:
		uerror("Unknown GRIB version %d\0",*gversion);
		return(-7);
      }

   ioff = (int)(*off) + (int)(griblen);
   if((*off + griblen) > filelen)
      return(-2);

   if( memcmp( data + *off +  griblen - 4, "7777" ,4) == 0)
      *len = griblen;
   }

if(*len == 0)
   {
   if ( griblen < 4 )
      {
      uinfo("GRIB/7777 short [Invalid GRIB]");
      *len = 4;
      }
   else
      {
      *len = griblen;
      udebug ( "look vals %u %u %u %u     %u %u %u %u     %u %u %u %u",
	   data [*off], data [*off+1], data [*off + 2], data [*off+3],
	  data [ ioff - 4], 
	  data [ ioff - 3], 
	  data [ ioff - 2], 
	  data [ ioff - 1], 
	  data [ ioff + 0], 
	  data [ ioff + 1], 
	  data [ ioff + 2], 
	  data [ ioff + 3] );
      }
   uerror("7777 not found %u\0",griblen);
   return(-7);
   }
else
   return(0);

}
