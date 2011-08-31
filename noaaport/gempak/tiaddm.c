#include <stdio.h>

void ti_addm ( int *idtarr, int *imin, int *outarr, int *iret)
{
int iyear, imonth, iday, ihour, iminut;
int jmin, jtmp, jhr, jday;
int k, nday;

*iret = 0;
if ( *imin < 0 ) 
   {
   *iret = -16;
   return;
   }

iyear = idtarr[0];
imonth = idtarr[1];
iday = idtarr[2];
ihour = idtarr[3];
iminut = idtarr[4];

if ( ( imonth < 1 ) || ( imonth > 12 ) )
   {
   *iret = -8;
   return;
   }

if ( iyear < 0 )
   {
   *iret = -7;
   return;
   }

ti_daym ( iyear, imonth, &nday);

if ( ( iday < 1 ) || ( iday > nday ) )
   {
   *iret = -9;
   return;
   }

jmin = *imin % 60;
jtmp = *imin / 60;
jhr = jtmp % 24;
jday = jtmp / 24;

iminut = iminut + jmin;
if ( iminut > 59 )
   {
   iminut = iminut - 60;
   jhr++;
   }

ihour = ihour + jhr;
if ( ihour > 23 )
   {
   ihour = ihour - 24;
   jday++;
   }

for ( k = 0; k < jday; k++ )
   {
   ti_daym ( iyear, imonth, &nday);
   if ( iday == nday )
      {
      imonth = imonth + 1;
      if ( imonth == 13 )
         {
         iyear++;
         imonth = 1;
         }
      ti_daym ( iyear, imonth, &nday);
      iday = 1;
      }
   else
      iday++;
   }

if ( idtarr[0] < 100 ) iyear = iyear % 100; /* set to 2 digit if was orig */

outarr[0] = iyear;
outarr[1] = imonth;
outarr[2] = iday;
outarr[3] = ihour;
outarr[4] = iminut;


}
